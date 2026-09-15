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

#include "Denoiser.h"

// The NRD integration implementation, not just its declarations.
#include <NRDIntegration.hpp>

BEGIN_AURORA

// Frame time NRD is seeded with before any has been measured.
static constexpr float kInitialFrameTimeMs = 1000.0f / 60.0f;

// A fatal error handler that simply throws an exception.
// NOTE: If execution is allowed to continue, the behavior that follows the error is undefined, so
// this new handler throws an exception. Specifically NRD may cause a memory access violation, which
// requires cumbersome platform-specific exception handling; it is better to throw a standard
// exception immediately.
static void AbortExecution(void* /*userArg*/)
{
    throw HRException(E_FAIL);
}

// Calls RecreateD3D12 under SEH, converting NRI/NRD failures into Result::FAILURE. Handles
// hardware exceptions and the C++ exception thrown by AbortExecution (SEH code 0xE06D7363).
//
// Must be a free function: MSVC rejects __try in functions with destructible C++ locals.
static nrd::Result NRDRecreateD3D12Guarded(nrd::Integration& nrd,
    const nrd::IntegrationCreationDesc& integrationDesc,
    const nrd::InstanceCreationDesc& instanceDesc,
    const nri::DeviceCreationD3D12Desc& deviceCreationDesc)
{
    __try
    {
        return nrd.RecreateD3D12(integrationDesc, instanceDesc, deviceCreationDesc);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nrd::Result::FAILURE;
    }
}

// Wraps DenoiseD3D12 in the same SEH handler, for the same reasons.
static bool NRDDenoiseD3D12Guarded(nrd::Integration& nrd, const nrd::Identifier* denoisers,
    uint32_t count, nri::CommandBufferD3D12Desc& commandBufferDesc,
    nrd::ResourceSnapshot& resourceSnapshot)
{
    __try
    {
        nrd.DenoiseD3D12(denoisers, count, commandBufferDesc, resourceSnapshot);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// NRI v179 requires MessageCallback to be non-null in CallbackInterface. Log NRI messages.
static void NRIMessageCallback(nri::Message messageType, const char* file, uint32_t line,
    const char* message, void* /*userArg*/)
{
    if (messageType == nri::Message::ERROR)
        AU_ERROR("[NRI] %s (%s:%u)", message, file, line);
    else
        AU_INFO("[NRI] %s (%s:%u)", message, file, line);
}

Denoiser::Denoiser(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, uint32_t taskCount) :
    _taskCount(taskCount), _pD3DDevice(pDevice), _pCommandQueue(pCommandQueue)
{
    // Get information about the NRD library.
    const nrd::LibraryDesc* lib = nrd::GetLibraryDesc();
    AU_INFO("Using NVIDIA NRD %u.%u.%u.", lib->versionMajor, lib->versionMinor, lib->versionBuild);

    // Build the descriptor for initialize()'s RecreateD3D12(); it creates the internal NRI device.
    _nriQueueFamily             = {};
    _nriQueueFamily.d3d12Queues = &_pCommandQueue;
    _nriQueueFamily.queueNum    = 1;
    _nriQueueFamily.queueType   = nri::QueueType::GRAPHICS;

    _nriDeviceCreationDesc                                   = {};
    _nriDeviceCreationDesc.d3d12Device                       = pDevice;
    _nriDeviceCreationDesc.queueFamilies                     = &_nriQueueFamily;
    _nriDeviceCreationDesc.queueFamilyNum                    = 1;
    _nriDeviceCreationDesc.callbackInterface.MessageCallback = NRIMessageCallback;
    _nriDeviceCreationDesc.callbackInterface.AbortExecution  = AbortExecution;
    // Enhanced Barriers are required: NRI expresses NRD's transitions as access/layout/stage, which
    // does not map cleanly onto legacy D3D12_RESOURCE_STATES and produces structural validation
    // errors at Close().
    _nriDeviceCreationDesc.disableD3D12EnhancedBarriers = false;
    // Aurora does not need to initialize NVIDIA's API.
    _nriDeviceCreationDesc.disableNVAPIInitialization = true;

    // Initialize the denoiser settings.
    // NOTE: Some settings are constants, and others will be updated during rendering.
    initSettings();
}

Denoiser::~Denoiser() = default;

void Denoiser::initialize(const uvec2& dimensions, const Textures& textures)
{
    if (_isInitialized)
    {
        _nrd.Destroy();
        _isInitialized = false;
    }
    // Keep NRD's frame counter and warmup state synchronized.
    _frameNumber = 0;

    // Clear the D3D12 texture pointer table.
    _nrdTexturesD3D12.fill(nullptr);

    // Store the dimensions.
    _dimensions = dimensions;
    auto width  = static_cast<uint16_t>(_dimensions.x);
    auto height = static_cast<uint16_t>(_dimensions.y);

    // Specify the desired denoising methods.
    nrd::DenoiserDesc denoiserDescs[] = { { 0, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR } };

    // Create the NRD instance.
    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers                 = denoiserDescs;
    instanceCreationDesc.denoisersNum              = _countof(denoiserDescs);

    // Create the NRD Integration instance.
    nrd::IntegrationCreationDesc integrationCreationDesc = {};
    strncpy_s(integrationCreationDesc.name, "Aurora", sizeof(integrationCreationDesc.name) - 1);
    integrationCreationDesc.resourceWidth  = width;
    integrationCreationDesc.resourceHeight = height;
    // Match NRD's descriptor pool rotation to Aurora's pipeline depth, so a pool is only reused
    // once the GPU has finished the frame that last used it.
    integrationCreationDesc.queuedFrameNum = static_cast<uint8_t>(_taskCount);

    // RecreateD3D12 may trigger debug errors on older hardware. Suppress break-on-error and
    // restore it afterward.
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    bool hadBreakOnError = false;
    if (_pD3DDevice && SUCCEEDED(_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
    {
        hadBreakOnError = pInfoQueue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR);
        if (hadBreakOnError)
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    }

    // RecreateD3D12() is the D3D12-native integration path: it creates its own internal NRI device
    // from the descriptor and enables DenoiseD3D12() for this instance.
    nrd::Result const result = NRDRecreateD3D12Guarded(
        _nrd, integrationCreationDesc, instanceCreationDesc, _nriDeviceCreationDesc);

    if (pInfoQueue && hadBreakOnError)
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

    _isInitialized = result == nrd::Result::SUCCESS;
    if (!_isInitialized)
    {
        // Stop if recreation failed; NRD state may be invalid.
        // This also covers unsupported hardware.
        AU_WARN("NRD RecreateD3D12 failed (result=%d); denoising is disabled for this renderer.",
            static_cast<int>(result));
        return;
    }

    // DenoiseD3D12() takes raw ID3D12Resource pointers, indexed by nrd::ResourceType.
    // NOTE: The entries for shadow denoising (Sigma denoiser) are not needed here.
    setTextureEntry(nrd::ResourceType::IN_VIEWZ, textures.pDepthView);
    setTextureEntry(nrd::ResourceType::IN_NORMAL_ROUGHNESS, textures.pNormalRoughness);
    setTextureEntry(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, textures.pDiffuse);
    setTextureEntry(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, textures.pGlossy);
    setTextureEntry(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, textures.pDiffuseOut);
    setTextureEntry(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, textures.pGlossyOut);
    setTextureEntry(nrd::ResourceType::IN_MV, textures.pMotionVectors);
    setTextureEntry(nrd::ResourceType::OUT_VALIDATION, textures.pValidation);
}

bool Denoiser::denoise(const DenoiserState& state)
{
    // Initialize previous matrices for valid RELAX reprojection. Identity projection can produce
    // NaNs, especially at small viewZ.
    if (state.isRestart)
    {
        _cameraViewPrev = state.cameraView;
        _cameraProjPrev = state.cameraProj;
    }

    // Advance NRD's frame counter once per call, including warmup frames.
    _nrd.NewFrame();

    // Set common settings before warmup. NRD derives frustum and handedness from these matrices.
    // NOTE: worldToViewMatrix includes rotation and translation (camera position).
    ::memcpy(_nrdSettings.viewToClipMatrix, value_ptr(state.cameraProj), sizeof(mat4));
    ::memcpy(_nrdSettings.viewToClipMatrixPrev, value_ptr(_cameraProjPrev), sizeof(mat4));
    ::memcpy(_nrdSettings.worldToViewMatrix, value_ptr(state.cameraView), sizeof(mat4));
    ::memcpy(_nrdSettings.worldToViewMatrixPrev, value_ptr(_cameraViewPrev), sizeof(mat4));

    // Set resource sizes.
    _nrdSettings.resourceSize[0]     = static_cast<uint16_t>(_dimensions.x);
    _nrdSettings.resourceSize[1]     = static_cast<uint16_t>(_dimensions.y);
    _nrdSettings.resourceSizePrev[0] = static_cast<uint16_t>(_dimensions.x);
    _nrdSettings.resourceSizePrev[1] = static_cast<uint16_t>(_dimensions.y);
    _nrdSettings.rectSize[0]         = static_cast<uint16_t>(_dimensions.x);
    _nrdSettings.rectSize[1]         = static_cast<uint16_t>(_dimensions.y);
    _nrdSettings.rectSizePrev[0]     = static_cast<uint16_t>(_dimensions.x);
    _nrdSettings.rectSizePrev[1]     = static_cast<uint16_t>(_dimensions.y);

    _nrdSettings.denoisingRange = state.denoisingRange;
    _nrdSettings.frameIndex     = _frameNumber;

    // NRD's own debug overlay, written into OUT_VALIDATION. It is an extra full-screen pass writing
    // an extra full-screen target, and only readable through one debug mode.
    _nrdSettings.enableValidation = state.enableValidation;

    // NRD wants non-jittered 2.5D motion: XY reprojects current UV to previous UV, Z is the change
    // in linear view depth. Aurora keeps XY in pixel units so the buffer can be shared with
    // DLSS/FSR, hence the UV rescale here; Z is already in view-space units. Passing Z through
    // rather than letting NRD reconstruct it (scale 0.0) keeps the true jittered hit position.
    _nrdSettings.motionVectorScale[0] = 1.0f / static_cast<float>(_dimensions.x);
    _nrdSettings.motionVectorScale[1] = 1.0f / static_cast<float>(_dimensions.y);
    _nrdSettings.motionVectorScale[2] = 1.0f;

    // In pixels, not UV: NRD feeds the difference of these straight into a "(1 + jitterDelta) /
    // rectH" term. getTemporalJitter() produces exactly this raw [-0.5, 0.5] range, and it is what
    // DLSS and FSR are given too.
    _nrdSettings.cameraJitter[0]        = state.cameraJitter.x;
    _nrdSettings.cameraJitter[1]        = state.cameraJitter.y;
    _nrdSettings.cameraJitterPrev[0]    = state.cameraJitterPrev.x;
    _nrdSettings.cameraJitterPrev[1]    = state.cameraJitterPrev.y;
    _nrdSettings.timeDeltaBetweenFrames = state.timeDeltaMs;

    // Clear history via accumulation mode. Frame number must increment every frame for NRD.
    _nrdSettings.accumulationMode = state.isRestart ? nrd::AccumulationMode::CLEAR_AND_RESTART
                                                    : nrd::AccumulationMode::CONTINUE;

    _nrd.SetCommonSettings(_nrdSettings);

    // Skip the first dispatch. NRD's initial CLEAR_AND_RESTART layout fails D3D12 validation at
    // Close(). NewFrame() and SetCommonSettings() still advance NRD's frame counter.
    if (state.isRestart && _frameNumber == 0)
    {
        _frameNumber++;
        return false; // no GPU commands recorded
    }

    // Refresh only the per-frame parts of the RELAX settings; the rest live in initSettings().
    updateRelaxSettings(state);
    _nrd.SetDenoiserSettings(0, &_relaxSettings);

    // DenoiseD3D12() wraps raw resources internally; restoreInitialState restores UAV state.
    nrd::ResourceSnapshot resourceSnapshot = {};
    resourceSnapshot.restoreInitialState   = true;

    for (uint32_t i = 0; i < NRD_USER_POOL_SIZE; i++)
    {
        ID3D12Resource* pD3DResource = _nrdTexturesD3D12[i];
        if (pD3DResource)
        {
            nrd::Resource resource  = {};
            resource.d3d12.resource = pD3DResource;
            resource.state          = { nri::AccessBits::SHADER_RESOURCE_STORAGE,
                         nri::Layout::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER };
            resourceSnapshot.SetResource(static_cast<nrd::ResourceType>(i), resource);
        }
    }

    nri::CommandBufferD3D12Desc commandBufferDesc = { state.pCommandList, state.pCommandAllocator };

    // Denoise through the D3D12-native path, which is what makes the raw resource pointers above
    // valid; the external-NRI-device path produces structural errors at Close().
    nrd::Identifier denoisers[] = { 0 };
    if (!NRDDenoiseD3D12Guarded(_nrd, denoisers, 1, commandBufferDesc, resourceSnapshot))
    {
        AU_WARN("NRD DenoiseD3D12 failed; denoising is disabled for this renderer.");
        _isInitialized = false;
        return false;
    }

    // Increment the frame number and store the camera settings.
    _frameNumber++;
    _cameraViewPrev = state.cameraView;
    _cameraProjPrev = state.cameraProj;

    return true;
}

void Denoiser::initSettings()
{
    // Set values for common settings that won't change.
    // NOTE: The setting structure has reasonable defaults for some of these, but all of them are
    // set here in order to make them easy to experiment with.
    _nrdSettings.cameraJitter[0]            = 0.0f;
    _nrdSettings.cameraJitter[1]            = 0.0f;
    _nrdSettings.cameraJitterPrev[0]        = 0.0f;
    _nrdSettings.cameraJitterPrev[1]        = 0.0f;
    // These defaults are overwritten per frame once the output dimensions are known.
    // Aurora stores 2.5D motion with XY in pixel units and Z in view-space units; denoise()
    // converts XY to NRD's UV-space convention and leaves Z unchanged.
    _nrdSettings.motionVectorScale[0] = 1.0f;
    _nrdSettings.motionVectorScale[1] = 1.0f;
    _nrdSettings.motionVectorScale[2] = 1.0f;
    _nrdSettings.rectOrigin[0]        = 0;
    _nrdSettings.rectOrigin[1]        = 0;
    // Placeholder for the initial settings only; denoise() supplies the measured value.
    _nrdSettings.timeDeltaBetweenFrames = kInitialFrameTimeMs;

    // The middle of NRD's documented [0.01, 0.02] range. Widening it only trades history rejection
    // during a dolly for accepted history across real disocclusions, i.e. ghosting.
    _nrdSettings.disocclusionThreshold = 0.015f;
    _nrdSettings.splitScreen           = 0.0f;
    _nrdSettings.debug                 = 0.0f;
    _nrdSettings.enableValidation      = false;

    // ---- RELAX settings that do not vary frame to frame ----
    // RELAX rather than REBLUR: REBLUR sizes its kernel by normHitDist, so a background pixel next
    // to a foreground hit gets a large radius and bleeds across the edge. RELAX's wavelet passes
    // stop at luminance/normal discontinuities regardless of hit distance.
    _relaxSettings = nrd::RelaxSettings();

    // Each A-trous pass roughly doubles the filter's spatial reach, so this controls how FAR
    // blending spreads -- independent of the edge-stopping settings below, which control how much a
    // single pass tolerates. Below NRD's default of 5, so sparse bright samples inside a reflective
    // area are diluted less by their mostly-dark neighbours.
    _relaxSettings.atrousIterationNum = 4;

    // Aurora samples one lobe per pixel, leaving the other with hitDist = 0; AREA_3X3 lets RELAX
    // reconstruct the missing value from the neighbourhood. NRD's precondition for this is that the
    // lobe selection probability is clamped to [1/4, 3/4], which getGlossySelectionProbability()
    // enforces (StandardSurfaceBSDF.slang).
    _relaxSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;

    // NRD's own defaults. A higher phi blends more across luminance gradients, but cannot tell
    // genuine high-frequency lit detail from 1 spp noise, so looser values wash that detail out.
    _relaxSettings.diffusePhiLuminance  = 2.0f;
    _relaxSettings.specularPhiLuminance = 1.0f;

    // Strict edge stopping at silhouette contours (geometry boundaries, thin struts, etc.).
    // Defaults are 0.5 / 0.3; lower values make the A-trous filter stop more aggressively at
    // luminance/normal discontinuities, giving cleaner edges and fewer jaggy halos.
    _relaxSettings.luminanceEdgeStoppingRelaxation = 0.2f;
    _relaxSettings.normalEdgeStoppingRelaxation    = 0.1f;

    // Extra variance injected into the specular signal only where reprojection confidence is low,
    // so freshly-reset pixels lean on spatial neighbours rather than their own 1 spp sample.
    // Unlike specularPhiLuminance, converged pixels are unaffected.
    _relaxSettings.specularVarianceBoost = 3.0f;

    // RELAX's firefly suppression pass. A firefly is considerably worse for a downstream temporal
    // reconstructor than for a display, so keep this on.
    _relaxSettings.enableAntiFirefly = true;

    // Reconstructed frames after a history reset; NRD's documented maximum. This pass runs on
    // every disocclusion.
    _relaxSettings.historyFixFrameNum = 3;

    // NRD's default. Read only by the spatial passes, so it cannot affect whether history is
    // accepted; raising it merely opens the spatial normal-acceptance cone.
    _relaxSettings.lobeAngleFraction = 0.5f;

    // Standard deviation scale of the colour box used to clamp the slow history to the responsive
    // fast history. NRD's default.
    _relaxSettings.fastHistoryClampingSigmaScale = 2.0f;

    // NRD's defaults. Do not zero these on the grounds that Aurora resets on camera change: that
    // fires once, on the rising edge of motion, whereas antilag is the only thing catching
    // accumulated reprojection error DURING a multi-second orbit.
    _relaxSettings.antilagSettings.accelerationAmount = 0.3f;
    _relaxSettings.antilagSettings.resetAmount        = 0.5f;
}

void Denoiser::updateRelaxSettings(const DenoiserState& state)
{
    // Accumulation length, derived from elapsed time rather than fixed in frames, as NRD
    // recommends: a frame count means different things at different frame rates, so a fixed one
    // makes reprojection error take proportionally longer to decay as the frame rate drops. Above
    // NRD's 0.5 s default on purpose -- in design visualisation the camera is stationary far more
    // often than in a game, so a longer window buys real noise reduction on the still image.
    static constexpr float kAccumulationTime = 1.0f; // seconds

    // Floor at NRD's own defaults: a purely time-derived length collapses to ~8 frames of main and
    // 2 of fast history at low frame rates, where RELAX then injects so much of the raw 1 spp input
    // per frame that the result boils on a static camera. Flooring keeps this at least as good as
    // NRD out of the box and lets a high frame rate lengthen the window from there.
    static constexpr uint32_t kMinAccumulatedFrameNum     = 30; // nrd::RelaxSettings default
    static constexpr uint32_t kMinFastAccumulatedFrameNum = 6;  // nrd::RelaxSettings default

    const float fps = 1000.0f / glm::max(state.timeDeltaMs, 1.0f);
    const uint32_t maxAccumulatedFrameNum =
        glm::clamp(nrd::GetMaxAccumulatedFrameNum(kAccumulationTime, fps), kMinAccumulatedFrameNum,
            nrd::RELAX_MAX_HISTORY_FRAME_NUM);

    _relaxSettings.diffuseMaxAccumulatedFrameNum  = maxAccumulatedFrameNum;
    _relaxSettings.specularMaxAccumulatedFrameNum = maxAccumulatedFrameNum;

    // The fast history drives RELAX's history clamping, its primary anti-ghosting mechanism during
    // continuous motion. NRD documents it as "usually 5x-7x times shorter than the main history",
    // and that a fast history at or above the main one disables the mechanism entirely.
    const uint32_t maxFastAccumulatedFrameNum         = glm::clamp(maxAccumulatedFrameNum / 6u,
                kMinFastAccumulatedFrameNum, glm::max(maxAccumulatedFrameNum - 1u, 1u));
    _relaxSettings.diffuseMaxFastAccumulatedFrameNum  = maxFastAccumulatedFrameNum;
    _relaxSettings.specularMaxFastAccumulatedFrameNum = maxFastAccumulatedFrameNum;
}

void Denoiser::setTextureEntry(nrd::ResourceType type, ID3D12Resource* pTexture)
{
    _nrdTexturesD3D12[static_cast<uint32_t>(type)] = pTexture;
}

END_AURORA
