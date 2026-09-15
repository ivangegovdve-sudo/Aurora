// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "pch.h"

#include "Upscaler.h"

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>

BEGIN_AURORA

namespace
{

// ---- NGX subsystem lifetime ------------------------------------------------------------------
//
// NVSDK_NGX_D3D12_Init loads the driver-side NGX runtime and DLSS snippet DLLs and does the
// capability handshake: expensive, and per DEVICE rather than per feature. Reference-counted here
// instead of tied to Upscaler's lifetime, so a mode change or resize recreates only the much
// cheaper feature handle.
int g_ngxRefCount                 = 0;
ID3D12Device5* g_pNGXDevice       = nullptr;
NVSDK_NGX_Parameter* g_pNGXParams = nullptr;

// NVIDIA's guidance is Init_with_ProjectID with a GUID and engine type when the application has no
// NVIDIA-issued numeric application ID.
const char* const kNGXProjectId     = "b3f1e4a2-6c58-4d21-9a77-0f5c2d8e41b6";
const char* const kNGXEngineVersion = "1.0";

// Initializes NGX if not already up and takes a reference. Returns false if NGX is unavailable, in
// which case no reference is taken.
bool acquireNGX(ID3D12Device5* pDevice)
{
    if (g_ngxRefCount > 0)
    {
        // NGX is per-device. Aurora only ever has one, but fail cleanly rather than silently use
        // the wrong one.
        if (g_pNGXDevice != pDevice)
        {
            AU_WARN("NGX is already initialized for a different D3D12 device; DLSS unavailable.");
            return false;
        }
        g_ngxRefCount++;
        return true;
    }

    // L"." puts NGX's log and data files next to the executable.
    NVSDK_NGX_Result res = NVSDK_NGX_D3D12_Init_with_ProjectID(
        kNGXProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kNGXEngineVersion, L".", pDevice);
    if (NVSDK_NGX_FAILED(res))
    {
        return false;
    }

    res = NVSDK_NGX_D3D12_GetCapabilityParameters(&g_pNGXParams);
    if (res == NVSDK_NGX_Result_FAIL_OutOfDate)
    {
        // Driver too old for the capability API (445 or older per nvsdk_ngx.h). DLSS itself may
        // still work, so fall back to a plain parameter map: feature creation only needs somewhere
        // to put parameters. It carries no capability entries, so querySupport() reports the mode
        // unsupported - honest, since nothing can confirm otherwise.
        res = NVSDK_NGX_D3D12_AllocateParameters(&g_pNGXParams);
    }
    if (NVSDK_NGX_FAILED(res))
    {
        NVSDK_NGX_D3D12_Shutdown1(pDevice);
        g_pNGXParams = nullptr;
        return false;
    }

    g_pNGXDevice  = pDevice;
    g_ngxRefCount = 1;
    return true;
}

// Releases a reference, shutting NGX down when the last one goes away.
void releaseNGX()
{
    if (g_ngxRefCount == 0)
    {
        return;
    }
    if (--g_ngxRefCount > 0)
    {
        return;
    }

    if (g_pNGXParams)
    {
        NVSDK_NGX_D3D12_DestroyParameters(g_pNGXParams);
        g_pNGXParams = nullptr;
    }
    if (g_pNGXDevice)
    {
        NVSDK_NGX_D3D12_Shutdown1(g_pNGXDevice);
        g_pNGXDevice = nullptr;
    }
}

// Submits a one-shot command list and blocks until the GPU is done. NGX feature creation records
// driver-side setup work that must complete before the feature can be evaluated.
void executeAndWait(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue,
    ID3D12GraphicsCommandList* pCommandList)
{
    pCommandList->Close();
    ID3D12CommandList* pLists[] = { pCommandList };
    pCommandQueue->ExecuteCommandLists(1, pLists);

    ComPtr<ID3D12Fence> pFence;
    if (FAILED(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence))))
    {
        return;
    }
    HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent)
    {
        return;
    }
    pCommandQueue->Signal(pFence.Get(), 1);
    pFence->SetEventOnCompletion(1, hEvent);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
}

// Maps a per-dimension upscale ratio to the closest DLSS performance-quality preset. DLSS derives
// internal behaviour (including default render presets) from this, so it must describe the ratio
// actually in use rather than being hard-coded.
NVSDK_NGX_PerfQuality_Value perfQualityForScale(float scale)
{
    if (scale <= 1.05f)
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    if (scale < 1.6f)
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    if (scale < 1.85f)
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    if (scale < 2.5f)
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
}

// Mode is nested in Upscaler; alias it so the helpers below can name its enumerators.
using Mode = Upscaler::Mode;

// Human-readable name for a mode (e.g. "DLSS4", "FSR", "DLSS-RR", "Off").
const char* modeName(Mode mode)
{
    switch (mode)
    {
    case Mode::DLSS4:
        return "DLSS4";
    case Mode::FSR:
        return "FSR";
    case Mode::DLSS_RR:
        return "DLSS-RR";
    default:
        return "Off";
    }
}
} // anonymous namespace

// Pimpl definitions live here so unique_ptr<T> can generate correct destructors.

struct Upscaler::NGXState
{
    NVSDK_NGX_Handle* pFeature = nullptr;
    bool isRayReconstruction   = false;
};

struct Upscaler::FSRState
{
    ffxContext context = nullptr;
};

// ---- Capability query ----

namespace
{
// Reads one NGX feature's availability from the capability parameter map. A failed Get means "not
// available" rather than an error: NGX returns FAIL_UnsupportedParameter for features a driver does
// not know about, and on a driver too old for the capability API the map carries none of these.
IRenderer::UpscalerSupport readNGXFeatureSupport(NVSDK_NGX_Parameter* pParams,
    const char* availableName, const char* needsDriverName, const char* majorName,
    const char* minorName)
{
    IRenderer::UpscalerSupport support;
    if (!pParams)
    {
        return support;
    }

    int available = 0;
    if (NVSDK_NGX_SUCCEED(pParams->Get(availableName, &available)) && available != 0)
    {
        support.isSupported = true;
        return support;
    }

    // Not available. Distinguish a merely-too-old driver, since the user can act on that.
    int needsDriver = 0;
    if (NVSDK_NGX_SUCCEED(pParams->Get(needsDriverName, &needsDriver)) && needsDriver != 0)
    {
        support.needsDriverUpdate = true;
        unsigned int major        = 0;
        unsigned int minor        = 0;
        pParams->Get(majorName, &major);
        pParams->Get(minorName, &minor);
        support.minDriverVersionMajor = major;
        support.minDriverVersionMinor = minor;
    }
    return support;
}
} // namespace

IRenderer::UpscalerSupport Upscaler::querySupport(PTDevice& device, Mode mode)
{
    if (mode == Mode::Off)
    {
        return { true, false, 0, 0 };
    }

    if (mode == Mode::DLSS4 || mode == Mode::DLSS_RR)
    {
        // NGX only runs on NVIDIA hardware; initializing it elsewhere is pointless work.
        if (device.vendor() != PTDevice::Vendor::kNVIDIA)
        {
            return {};
        }

        // The capability map only exists while NGX is initialized, so acquire it for the query.
        // The reference count leaves a live upscaler unaffected; if nothing else holds NGX it is
        // shut down again on release.
        if (!acquireNGX(device.device()))
        {
            return {};
        }

        IRenderer::UpscalerSupport support = mode == Mode::DLSS4
            ? readNGXFeatureSupport(g_pNGXParams, NVSDK_NGX_Parameter_SuperSampling_Available,
                  NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
                  NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
                  NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor)
            : readNGXFeatureSupport(g_pNGXParams,
                  NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,
                  NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver,
                  NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMajor,
                  NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMinor);

        releaseNGX();
        return support;
    }

    if (mode == Mode::FSR)
    {
        // How many upscaler provider versions the loader offers for this device. With the count
        // zeroed and both output arrays null, ffxQuery only reports the count, needing no context
        // and allocating nothing. Zero versions or any failure means the loader is absent or does
        // not support this device - also what FSRStubs.cpp produces in a build without the signed
        // FSR binaries.
        uint64_t versionCount = 0;

        ffxQueryDescGetVersions versions {};
        versions.header.type    = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device         = device.device();
        versions.outputCount    = &versionCount;

        ffxReturnCode_t const result = ffxQuery(nullptr, &versions.header);
        return { result == FFX_API_RETURN_OK && versionCount > 0, false, 0, 0 };
    }

    return {};
}

float Upscaler::qualityScale(Quality quality)
{
    switch (quality)
    {
    case Quality::Quality:
        return 1.5f;
    case Quality::Balanced:
        return 1.7f;
    case Quality::Performance:
        return 2.0f;
    default:
        return 1.0f;
    }
}

// ---- Construction / destruction ----

Upscaler::Upscaler(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, Mode requestedMode,
    const uvec2& renderDimensions, const uvec2& displayDimensions) :
    _requestedMode(requestedMode),
    _mode(requestedMode),
    _renderDimensions(renderDimensions),
    _displayDimensions(displayDimensions)
{
    AU_INFO("Upscaler: initializing %s, rendering %ux%u -> %ux%u.", modeName(requestedMode),
        renderDimensions.x, renderDimensions.y, displayDimensions.x, displayDimensions.y);

    if (_mode == Mode::DLSS4 || _mode == Mode::DLSS_RR)
    {
        try
        {
            initDLSS(pDevice, pCommandQueue, _mode == Mode::DLSS_RR);
        }
        catch (const std::exception& e)
        {
            // Fall back to FSR, which works on any GPU with the FidelityFX SDK. _requestedMode is
            // deliberately left alone: the renderer compares against it to decide whether to
            // rebuild, so mutating it would make every frame see a mismatch and pay a full NGX init
            // plus a synchronous GPU stall.
            AU_WARN("DLSS init failed (%s); falling back to FSR.", e.what());
            _mode = Mode::FSR;
            initFSR(pDevice); // throws on FSR failure - propagates to caller
        }
    }
    else if (_mode == Mode::FSR)
    {
        initFSR(pDevice); // throws on failure
    }
}

Upscaler::~Upscaler()
{
    if (_pNGX)
        destroyDLSS();
    if (_pFSR)
        destroyFSR();
}

// ---- Per-frame upscale ----

void Upscaler::upscale(
    ID3D12GraphicsCommandList4* pCommandList, const Textures& textures, const FrameState& state)
{
    if ((_mode == Mode::DLSS4 || _mode == Mode::DLSS_RR) && _pNGX && _pNGX->pFeature)
    {
        if (_pNGX->isRayReconstruction)
        {
            NVSDK_NGX_D3D12_DLSSD_Eval_Params eval = {};
            eval.pInColor                          = textures.pColor;
            eval.pInOutput                         = textures.pOutput;
            eval.pInDepth                          = textures.pDepth;
            eval.pInMotionVectors                  = textures.pMotionVectors;
            eval.pInDiffuseAlbedo                  = textures.pDiffuseAlbedo;
            eval.pInSpecularAlbedo                 = textures.pSpecularAlbedo;
            // Roughness travels in the normal buffer's .w, matching the Packed roughness mode set
            // at creation, so pInRoughness stays null.
            eval.pInNormals                = textures.pNormalRoughness;
            eval.pInDiffuseHitDistance     = textures.pDiffuseHitDist;
            eval.pInSpecularHitDistance    = textures.pSpecularHitDist;
            eval.InJitterOffsetX           = state.cameraJitter.x;
            eval.InJitterOffsetY           = state.cameraJitter.y;
            eval.InRenderSubrectDimensions = { _renderDimensions.x, _renderDimensions.y };
            eval.InReset                   = state.isReset ? 1 : 0;
            eval.InMVScaleX                = 1.0f;
            eval.InMVScaleY                = 1.0f;
            eval.InFrameTimeDeltaInMsec    = state.timeDeltaMs;

            NVSDK_NGX_Result res =
                NGX_D3D12_EVALUATE_DLSSD_EXT(pCommandList, _pNGX->pFeature, g_pNGXParams, &eval);
            if (NVSDK_NGX_FAILED(res))
                AU_ERROR("DLSS-RR evaluate failed: 0x%x", static_cast<unsigned>(res));
        }
        else
        {
            NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
            eval.Feature.pInColor                 = textures.pColor;
            eval.Feature.pInOutput                = textures.pOutput;
            eval.pInDepth                         = textures.pDepth;
            eval.pInMotionVectors                 = textures.pMotionVectors;
            eval.InJitterOffsetX                  = state.cameraJitter.x;
            eval.InJitterOffsetY                  = state.cameraJitter.y;
            eval.InRenderSubrectDimensions        = { _renderDimensions.x, _renderDimensions.y };
            eval.InReset                          = state.isReset ? 1 : 0;
            // No scaling: DLSS expects pixel-space motion vectors, which is what Aurora writes (see
            // HitShaderEntryPoints.slang), alongside the MVLowRes creation flag.
            eval.InMVScaleX = 1.0f;
            eval.InMVScaleY = 1.0f;
            // Used to judge object speed from motion vector magnitude and frame rate.
            eval.InFrameTimeDeltaInMsec = state.timeDeltaMs;

            NVSDK_NGX_Result res =
                NGX_D3D12_EVALUATE_DLSS_EXT(pCommandList, _pNGX->pFeature, g_pNGXParams, &eval);
            if (NVSDK_NGX_FAILED(res))
                AU_ERROR("DLSS evaluate failed: 0x%x", static_cast<unsigned>(res));
        }
    }
    else if (_mode == Mode::FSR && _pFSR)
    {
        ffxDispatchDescUpscale disp = {};
        disp.header.type            = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        disp.commandList            = pCommandList;
        // Tell FSR the current D3D12 resource state so it emits the right barriers. Every Aurora
        // texture created with unordered access stays in UNORDERED_ACCESS, depth included.
        disp.color =
            ffxApiGetResourceDX12(textures.pColor, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        disp.output =
            ffxApiGetResourceDX12(textures.pOutput, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        if (textures.pDepth)
            disp.depth =
                ffxApiGetResourceDX12(textures.pDepth, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        if (textures.pMotionVectors)
            disp.motionVectors = ffxApiGetResourceDX12(
                textures.pMotionVectors, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        disp.renderSize     = { _renderDimensions.x, _renderDimensions.y };
        disp.upscaleSize    = { _displayDimensions.x, _displayDimensions.y };
        disp.reset          = state.isReset;
        disp.frameTimeDelta = state.timeDeltaMs;
        disp.jitterOffset   = { state.cameraJitter.x, state.cameraJitter.y };
        disp.preExposure    = 1.0f;
        // Aurora's motion vectors are already in render-resolution pixels with FSR's expected
        // "previous minus current" direction, so no rescaling.
        disp.motionVectorScale = { 1.0f, 1.0f };
        // FSR reconstructs the previous frame's depth and derives its disocclusion and lock
        // heuristics from the camera frustum, so all four must be real values.
        disp.cameraNear              = state.cameraNear;
        disp.cameraFar               = state.cameraFar;
        disp.cameraFovAngleVertical  = state.cameraFovY;
        disp.viewSpaceToMetersFactor = state.metersPerUnit;
        ffxReturnCode_t err          = ffxDispatch(&_pFSR->context, &disp.header);
        if (err != FFX_API_RETURN_OK)
            AU_ERROR("FSR dispatch failed: %d", static_cast<int>(err));
    }
}

// ---- Private: DLSS init / destroy ----

void Upscaler::initDLSS(
    ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, bool isRayReconstruction)
{
    // 1. Take a reference on the shared, reference-counted NGX subsystem.
    if (!acquireNGX(pDevice))
    {
        throw std::runtime_error("NGX initialization failed");
    }

    _pNGX                      = make_unique<NGXState>();
    _pNGX->isRayReconstruction = isRayReconstruction;

    // 2. Feature creation records GPU work, so it needs a command list to submit and wait on.
    ComPtr<ID3D12CommandAllocator> pAlloc;
    ComPtr<ID3D12GraphicsCommandList> pCmd;
    pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    pDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc.Get(), nullptr, IID_PPV_ARGS(&pCmd));

    float const scale = _renderDimensions.x > 0
        ? static_cast<float>(_displayDimensions.x) / static_cast<float>(_renderDimensions.x)
        : 1.0f;

    // Flags shared by Super Resolution and Ray Reconstruction. AutoExposure is required alongside
    // IsHDR: IsHDR declares unbounded linear radiance and Aurora has no exposure buffer to give
    // instead. MVLowRes says motion vectors are at render resolution, which they are. MVJittered is
    // deliberately unset (Aurora's motion comes from unjittered matrices), as is DepthInverted
    // (gDepthNDC is (z/w + 1)/2, so 0 is near).
    int const featureFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    NVSDK_NGX_Result res = NVSDK_NGX_Result_Fail;
    if (isRayReconstruction)
    {
        NVSDK_NGX_DLSSD_Create_Params params = {};
        params.InWidth                       = _renderDimensions.x;
        params.InHeight                      = _renderDimensions.y;
        params.InTargetWidth                 = _displayDimensions.x;
        params.InTargetHeight                = _displayDimensions.y;
        params.InPerfQualityValue            = perfQualityForScale(scale);
        params.InFeatureCreateFlags          = featureFlags;
        // Ray Reconstruction replaces a hand-written denoiser entirely, so ask for the unified
        // model rather than the upscale-only path.
        params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        // Roughness is packed into the normal buffer's alpha
        // (NRD_FrontEnd_PackNormalAndRoughness); there is no separate roughness texture.
        params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
        // gDepthNDC is a hardware-style non-linear depth buffer.
        params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_HW;

        // No render-preset hint for Ray Reconstruction: the SDK defines the
        // NVSDK_NGX_RayReconstruction_Hint_Render_Preset_* enum but no matching parameter-name
        // macros, so there is no supported way to set one. The default is already the transformer
        // model.
        res = NGX_D3D12_CREATE_DLSSD_EXT(pCmd.Get(), 1, 1, &_pNGX->pFeature, g_pNGXParams, &params);
    }
    else
    {
        NVSDK_NGX_DLSS_Create_Params params = {};
        params.Feature.InWidth              = _renderDimensions.x;
        params.Feature.InHeight             = _renderDimensions.y;
        params.Feature.InTargetWidth        = _displayDimensions.x;
        params.Feature.InTargetHeight       = _displayDimensions.y;
        params.Feature.InPerfQualityValue   = perfQualityForScale(scale);
        params.InFeatureCreateFlags         = featureFlags;

        // Request DLSS 4's transformer model explicitly. Preset K is documented as the
        // transformer-based, best image quality preset; without this hint the driver picks
        // Preset_Default, and preset selection is what distinguishes DLSS 4 from the older CNN
        // model.
        NVSDK_NGX_Parameter_SetUI(g_pNGXParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
            NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(g_pNGXParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
            NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(g_pNGXParams,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
            NVSDK_NGX_DLSS_Hint_Render_Preset_K);
        NVSDK_NGX_Parameter_SetUI(g_pNGXParams,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
            NVSDK_NGX_DLSS_Hint_Render_Preset_K);

        res = NGX_D3D12_CREATE_DLSS_EXT(pCmd.Get(), 1, 1, &_pNGX->pFeature, g_pNGXParams, &params);
    }

    executeAndWait(pDevice, pCommandQueue, pCmd.Get());

    if (NVSDK_NGX_FAILED(res))
    {
        _pNGX.reset();
        releaseNGX();
        throw std::runtime_error(
            "NGX feature creation failed: 0x" + std::to_string(static_cast<unsigned>(res)));
    }

    AU_INFO("%s initialized: %ux%u -> %ux%u.", modeName(_mode), _renderDimensions.x,
        _renderDimensions.y, _displayDimensions.x, _displayDimensions.y);
    _isInitialized = true;
}

void Upscaler::destroyDLSS()
{
    if (_pNGX)
    {
        if (_pNGX->pFeature)
            NVSDK_NGX_D3D12_ReleaseFeature(_pNGX->pFeature);
        _pNGX.reset();
        // Only the feature is released here. The reference-counted NGX subsystem stays up as long
        // as any upscaler needs it, so a mode change or resize does not reload the driver runtime.
        releaseNGX();
    }
    _isInitialized = false;
}

// ---- Private: FSR init / destroy ----

void Upscaler::initFSR(ID3D12Device5* pDevice)
{
    _pFSR = make_unique<FSRState>();

    // Chain the DX12 backend descriptor, which provides the device, onto the upscale context one.
    ffxCreateBackendDX12Desc backendDesc = {};
    backendDesc.header.type              = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.header.pNext             = nullptr;
    backendDesc.device                   = pDevice;

    ffxCreateContextDescUpscale upscaleDesc = {};
    upscaleDesc.header.type                 = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.header.pNext                = &backendDesc.header;
    // AUTO_EXPOSURE pairs with HIGH_DYNAMIC_RANGE for the same reason DLSS needs it; see initDLSS.
    // DEPTH_INVERTED, DEPTH_INFINITE and MOTION_VECTORS_JITTER_CANCELLATION are deliberately unset,
    // for the same reasons as their DLSS equivalents.
    upscaleDesc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
#if defined(_DEBUG)
    upscaleDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#endif
    upscaleDesc.maxRenderSize  = { _renderDimensions.x, _renderDimensions.y };
    upscaleDesc.maxUpscaleSize = { _displayDimensions.x, _displayDimensions.y };
    upscaleDesc.fpMessage      = nullptr;

    ffxReturnCode_t err = ffxCreateContext(&_pFSR->context, &upscaleDesc.header, nullptr);
    if (err != FFX_API_RETURN_OK)
    {
        _pFSR.reset();
        throw std::runtime_error(
            "ffxCreateContext (FSR upscaler) failed: " + std::to_string(static_cast<int>(err)));
    }

    AU_INFO("FSR initialized: %ux%u -> %ux%u.", _renderDimensions.x, _renderDimensions.y,
        _displayDimensions.x, _displayDimensions.y);
    _isInitialized = true;
}

void Upscaler::destroyFSR()
{
    if (_pFSR && _pFSR->context)
        ffxDestroyContext(&_pFSR->context, nullptr);
    _pFSR.reset();
    _isInitialized = false;
}

END_AURORA
