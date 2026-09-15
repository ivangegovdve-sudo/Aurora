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
#pragma once

#include <chrono>

#include "PTEnvironment.h"
#include "PTGeometry.h"
#include "PTGroundPlane.h"
#include "PTMaterial.h"
#include "PTSampler.h"
#include "PTScene.h"
#include "PTShaderLibrary.h"
#include "PTTarget.h"
#include "RendererBase.h"

// Include the denoiser if it is enabled.
#if defined(ENABLE_DENOISER)
#include "Denoiser.h"
#endif

// Include the upscaler if it is enabled.
#if defined(ENABLE_UPSCALER)
#include "Upscaler.h"
#endif

// Set to 1 for per-pass GPU timestamps and a per-phase CPU breakdown of renderInternal().
#define AU_DEV_PERFORMANCE_LOGGING 0

BEGIN_AURORA

// Forward references.
class AssetManager;
class PTDevice;
class MaterialShader;
class PTShaderLibrary;
class ScratchBufferPool;
class VertexBufferPool;
struct TransferBuffer;

// An path tracing (PT) implementation for IRenderer.
class PTRenderer : public RendererBase
{
public:
    /*** Types ***/

    template <typename DataType>
    using FillDataFunction = function<void(DataType&)>;

    /*** Lifetime Management ***/

    /// Path tracing renderer constructor.
    ///
    /// \param activeTaskCount Maximum number of tasks active at once.
    PTRenderer(uint32_t activeTaskCount);
    ~PTRenderer();

    /*** IRenderer Functions ***/

    IWindowPtr createWindow(WindowHandle handle, uint32_t width, uint32_t height) override;
    IRenderBufferPtr createRenderBuffer(int width, int height, ImageFormat imageFormat) override;
    IImagePtr createImagePointer(const IImage::InitData& initData) override;
    ISamplerPtr createSamplerPointer(const Properties& props) override;
    IMaterialPtr createMaterialPointer(const string& materialType = Names::MaterialTypes::kBuiltIn,
        const string& document = "Default", const string& name = "") override;
    IScenePtr createScene() override;
    IEnvironmentPtr createEnvironmentPointer() override;
    IGeometryPtr createGeometryPointer(
        const GeometryDescriptor& desc, const string& name = "") override;
    IGroundPlanePtr createGroundPlanePointer() override;
    IRenderer::Backend backend() const override { return IRenderer::Backend::DirectX; }
    IRenderer::UpscalerSupport upscalerSupport(const string& mode) override;
    void setScene(const IScenePtr& pScene) override;
    void setTargets(const TargetAssignments& targetAssignments) override;
    void render(uint32_t sampleStart, uint32_t sampleCount) override;
    void waitForTask() override;
    const vector<string>& builtInMaterials() override;

    /*** Functions ***/

    ID3D12Device5* dxDevice() const { return _pDXDevice.Get(); }
    IDXGIFactory4* dxFactory() const { return _pDXFactory.Get(); }
    ID3D12CommandQueue* commandQueue() const { return _pCommandQueue.Get(); }
    // Create a transfer buffer.  GPU buffer state defaults to D3D12_RESOURCE_STATE_COPY_DEST as it
    // is used as a target for a resource copy command. The final GPU buffer state defaults to
    // D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE.
    TransferBuffer createTransferBuffer(size_t sz, const string& name = "",
        D3D12_RESOURCE_FLAGS gpuBufferFlags       = D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATES gpuBufferState      = D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATES gpuBufferFinalState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12ResourcePtr createBuffer(size_t size, const string& name = "",
        D3D12_HEAP_TYPE heapType    = D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAGS flags  = D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ);
    template <typename DataType>
    void updateBuffer(TransferBuffer& bufferOut, FillDataFunction<DataType> fillDataFunction);
    ID3D12ResourcePtr createTexture(uvec2 dimensions, DXGI_FORMAT format, const string& name = "",
        bool isUnorderedAccess = false, bool shareable = false);
    ID3D12ResourcePtr createTexture(uvec3 dimensions, DXGI_FORMAT format, const string& name = "",
        bool isUnorderedAccess = false, bool shareable = false);
    D3D12_GPU_VIRTUAL_ADDRESS getScratchBuffer(size_t size);
    void getVertexBuffer(VertexBuffer& vertexBuffer, void* pData, size_t size);
    void transferBufferUpdated(const TransferBuffer& buffer);
    void flushVertexBufferPool();
    void uploadTransferBuffers();
    void deleteUploadedTransferBuffers();
    ID3D12CommandAllocator* getCommandAllocator();
    ID3D12GraphicsCommandList4* beginCommandList();
    void submitCommandList();
    void addTransitionBarrier(ID3D12Resource* pResource, D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter);
    void addUAVBarrier(ID3D12Resource* pResource);
    void completeTask();
    PTSamplerPtr defaultSampler();
    PTGroundPlanePtr defaultGroundPlane();

private:
    // Temporal resolve settings GPU data. Must match TemporalResolve.hlsl.
    struct TemporalResolveSettings
    {
        // 0 => write history A / read history B, 1 => the reverse.
        unsigned int historyIndex = 0;

        // Non-zero to discard history entirely for this frame.
        unsigned int isReset = 0;

        // Weight given to the current frame when the camera is effectively still.
        float blendAlphaMin = 0.0f;

        // Weight given to the current frame at or above speedReference pixels of motion per frame.
        float blendAlphaMax = 0.0f;

        // On-screen speed, in pixels per frame, at which blendAlphaMax is reached.
        float speedReference = 0.0f;
    };

    // The configuration every temporal history depends on. Internal processing requests a history
    // reset when it changes, so a client need not know which options invalidate what.
    struct TemporalConfig
    {
        int upscalerMode              = 0;
        int upscalerQuality           = 0;
        bool isTemporalResolveEnabled = false;
        bool isDenoisingEnabled       = false;
        uvec2 outputDimensions        = uvec2(0);

        bool operator==(const TemporalConfig& other) const
        {
            return upscalerMode == other.upscalerMode && upscalerQuality == other.upscalerQuality &&
                isTemporalResolveEnabled == other.isTemporalResolveEnabled &&
                isDenoisingEnabled == other.isDenoisingEnabled &&
                outputDimensions == other.outputDimensions;
        }
        bool operator!=(const TemporalConfig& other) const { return !(*this == other); }
    };

    // Accumulation settings GPU data. Must match Accumulation.hlsl.
    struct Accumulation
    {
        unsigned int sampleIndex = 0;

        // Composite NRD's denoised diffuse/glossy outputs on top of the direct channel.
        unsigned int isDenoisedCompositeEnabled = 0;

        // A downstream temporal stage owns frame-to-frame blending, so accumulation must pass the
        // current frame through untouched rather than adding a second average.
        unsigned int isTemporalResolveDownstream = 0;

        // Sentinel view-Z written for sky / miss pixels.
        float skyViewZ = 0.0f;

        // Reverse the material demodulation the ray generator applied to the NRD input lobes.
        unsigned int remodulateMaterials = 0;
    };

    // Both are uploaded with SetComputeRoot32BitConstants(sizeof(T)/4, ...), so their size must
    // equal "num32BitConstants" in the matching HLSL root signature.
    static_assert(sizeof(Accumulation) / sizeof(uint32_t) == 5,
        "Accumulation size changed; update num32BitConstants in Accumulation.hlsl.");
    static_assert(sizeof(TemporalResolveSettings) / sizeof(uint32_t) == 5,
        "TemporalResolveSettings size changed; update num32BitConstants in TemporalResolve.hlsl.");

    /*** Private Types ***/
    static const size_t _FRAME_DATA_SIZE =
        ALIGNED_SIZE(sizeof(FrameData), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    /*** Private Functions ***/

    bool initDevice();
    void initCommandList();
    void initFrameData();
    void initRayGenShaderTable();
    void initAccumulation();
    void initPostProcessing();
    void initTemporalResolve();
    void renderInternal(uint32_t sampleStart, uint32_t sampleCount);
    void updateRayGenShaderTable();
    void updateFrameData(bool isRestartRequested);
    void updateSceneResources();
    void updateOutputResources();
    void updateDenoisingResources();
    void updateUpscalerResources();
    bool updateAccumulationGPUStruct(uint32_t sampleIndex, Accumulation* pStaging = nullptr);
    void prepareRayDispatch(D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc);
    void submitRayDispatch(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc, uint32_t sampleIndex,
        uint32_t seedOffset);
    void submitDenoising(bool isRestart);
    void submitUpscaling(bool isRestart);
    void submitAccumulation(uint32_t sampleIndex);
    void submitTemporalResolve(bool isRestart);
    void submitPostProcessing();
    bool isTemporalResolveActive() const;
    bool isRayReconstructionActive() const;
    bool isDenoisingRequested() const;
    int upscalerMode() const;
    bool isUpscalerRequested() const { return upscalerMode() != 0; }
    float effectiveRenderScale() const;
    bool hasTemporalResolve() const;
    void initTimestamps();
    void createUAV(ID3D12Resource* pTexture, CD3DX12_CPU_DESCRIPTOR_HANDLE& handle);
    void copyTextureToTarget(ID3D12Resource* pTexture, PTTarget* pTarget);
    bool isDenoisingAOVsEnabled() const;
    PTScenePtr dxScene() { return static_pointer_cast<PTScene>(_pScene); }
    PTShaderLibrary& shaderLibrary();

    /*** Private Variables ***/

    unique_ptr<PTDevice> _pDevice;
    bool _isCommandListOpen = false;
    TransferBuffer _frameDataBuffer;
    PTEnvironmentPtr _pEnvironment;
    uvec2 _outputDimensions;
    bool _isDimensionsChanged = true;
    // Path tracer and denoiser resolution, can be different with a upscaler.
    uvec2 _renderDimensions;
    bool _isRenderDimensionsChanged = true;
    uint32_t _seedOffset      = 0;
    PTTargetPtr _pTargetFinal;
    PTTargetPtr _pTargetDepthNDC;
    bool _didWarnDepthTargetFormat = false;
    bool _isDescriptorHeapChanged = true;
    PTSamplerPtr _pDefaultSampler;
    PTGroundPlanePtr _pDefaultGroundPlane;
    Accumulation _accumData;

    /*** DirectX 12 Objects ***/

    ID3D12Device5Ptr _pDXDevice;
    IDXGIFactory4Ptr _pDXFactory;
    ID3D12CommandQueuePtr _pCommandQueue;
    map<ID3D12Resource*, TransferBuffer> _pendingTransferBuffers;
    map<ID3D12Resource*, TransferBuffer> _transferBuffersToDelete;
    vector<ID3D12CommandAllocatorPtr> _commandAllocators;
    ID3D12GraphicsCommandList4Ptr _pCommandList;
    ID3D12FencePtr _pTaskFence;
    HANDLE _hTaskEvent = nullptr;
    ID3D12PipelineStatePtr _pAccumulationPipelineState;
    ID3D12RootSignaturePtr _pAccumulationRootSignature;
    ID3D12PipelineStatePtr _pPostProcessingPipelineState;
    ID3D12RootSignaturePtr _pPostProcessingRootSignature;
    ID3D12PipelineStatePtr _pTemporalResolvePipelineState;
    ID3D12RootSignaturePtr _pTemporalResolveRootSignature;
    ID3D12DescriptorHeapPtr _pDescriptorHeap;
    ID3D12DescriptorHeapPtr _pSamplerDescriptorHeap;
    UINT _handleIncrementSize = 0;
    ID3D12ResourcePtr _pTexFinal;           // for tone-mapped final output (usually SDR)
    ID3D12ResourcePtr _pTexDepthNDC;        // for NDC depth output, at RENDER resolution
    ID3D12ResourcePtr _pTexDepthNDCDisplay; // display-resolution copy of the NDC depth target
    ID3D12ResourcePtr _pTexAccumulation;    // for accumulation (HDR)
    ID3D12ResourcePtr _pTexDirect;          // for path tracing or direct lighting (HDR)
    // Ping-ponged history for the temporal resolve pass (HDR).
    ID3D12ResourcePtr _pTexTAAHistory[2];
    DXGI_FORMAT _finalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    TransferBuffer _rayGenShaderTable;
    size_t _rayGenShaderTableSize = 0;
    unique_ptr<ScratchBufferPool> _pScratchBufferCache;
    unique_ptr<VertexBufferPool> _pVertexBufferPool;
    unordered_map<string, PTSamplerPtr> _mtlxSamplers;
#if AU_DEV_PERFORMANCE_LOGGING
    // GPU timestamp query infrastructure for per-stage performance logging.
    ComPtr<ID3D12QueryHeap> _pTimestampHeap;
    ID3D12ResourcePtr _pTimestampReadback;
    uint64_t _timestampFrequency = 0;
    uint32_t _gpuTimingFrameNum  = 0;
#endif

    /*** Denoising Variables ***/

#if defined(ENABLE_DENOISER)
    unique_ptr<Denoiser> _pDenoiser;
#endif
    ID3D12ResourcePtr _pDenoisingTexDepthView;
    ID3D12ResourcePtr _pDenoisingTexNormalRoughness;
    ID3D12ResourcePtr _pDenoisingTexBaseColorMetalness;
    ID3D12ResourcePtr _pDenoisingTexDiffuse;
    ID3D12ResourcePtr _pDenoisingTexGlossy;
    ID3D12ResourcePtr _pDenoisingTexDiffuseOut;
    ID3D12ResourcePtr _pDenoisingTexGlossyOut;
    ID3D12ResourcePtr _pDenoisingTexMotionVectors;
    ID3D12ResourcePtr _pTexValidation; // NRD's debug overlay output.
    // Denoising guide buffers.
    ID3D12ResourcePtr _pTexDemodDiffuse;
    ID3D12ResourcePtr _pTexDemodSpecular;
    ID3D12ResourcePtr _pTexGuideNormalRoughness;
    // Whether NRD submitted denoising commands this frame; drives the accumulation composite.
    bool _nrdSubmittedThisFrame = false;
    bool _nrdAvailableLastFrame = false;

    /*** Upscaler Variables ***/

    // Cache answers from Upscaler::querySupport.
    std::map<string, IRenderer::UpscalerSupport> _upscalerSupportCache;
    bool _warnedUpscalerWithoutDenoiser = false;

#if defined(ENABLE_UPSCALER)
    unique_ptr<Upscaler> _pUpscaler;
    ID3D12ResourcePtr _pUpscaledTex;   // HDR intermediate at display resolution
    bool _didUpscaleThisFrame = false; // true when DLSS/FSR ran and wrote gUpscaled
    // Last failed configuration; avoid retrying each frame.
    Upscaler::Mode _upscalerFailedMode     = Upscaler::Mode::Off;
    uvec2 _upscalerFailedRenderDimensions  = uvec2(0);
    uvec2 _upscalerFailedDisplayDimensions = uvec2(0);
#endif

    /*** Temporal Anti-Aliasing (TAA) Variables ***/

    TemporalConfig _temporalConfig;
    uint32_t _taaHistoryIndex = 0;
    bool _hasTAAHistory = false;
    bool _didTemporalResolveThisFrame = false;
    int _temporalResolveSource = 0;
    mat4 _cameraViewPrevFrame       = mat4(1.0f);
    mat4 _cameraProjPrevFrame       = mat4(1.0f);
    vec2 _cameraJitter              = vec2(0.0f);
    vec2 _cameraJitterPrev          = vec2(0.0f);
    bool _hasPreviousTemporalFrame  = false;
    uint32_t _temporalFrameIndex    = 0;
    std::chrono::steady_clock::time_point _lastFrameUpdateTime = std::chrono::steady_clock::now();
};

// Creates (if needed) and an updates a GPU constant buffer with data supplied by the caller.
template <typename DataType>
void PTRenderer::updateBuffer(TransferBuffer& buffer, FillDataFunction<DataType> fillDataFunction)
{
    // Create a transfer buffer for the data if it doesn't already exist.
    static const size_t BUFFER_SIZE = sizeof(DataType);
    if (!buffer.valid())
    {
        buffer = createTransferBuffer(
            BUFFER_SIZE, "updateBuffer:" + to_string(uint64(&fillDataFunction)));
    }

    // Fill the data using the callback function.
    DataType data;
    fillDataFunction(data);

    // Copy the data to the constant buffer.
    void* pMappedData = buffer.map();
    ::memcpy_s(pMappedData, BUFFER_SIZE, &data, BUFFER_SIZE);
    buffer.unmap();
}

MAKE_AURORA_PTR(PTRenderer);

END_AURORA
