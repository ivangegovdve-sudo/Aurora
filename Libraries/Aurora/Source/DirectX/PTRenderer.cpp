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

#include "PTRenderer.h"

#include "AssetManager.h"
#include "CompiledShaders/Accumulation.hlsl.h"
#include "CompiledShaders/PostProcessing.hlsl.h"
#include "CompiledShaders/TemporalResolve.hlsl.h"
#include "MemoryPool.h"
#include "PTDevice.h"
#include "PTEnvironment.h"
#include "PTGeometry.h"
#include "PTGroundPlane.h"
#include "PTImage.h"
#include "PTLight.h"
#include "PTMaterial.h"
#include "PTScene.h"
#include "PTShaderLibrary.h"

#if ENABLE_MATERIALX
#include "MaterialX/MaterialGenerator.h"
#endif

// Include the denoiser.
#if defined(ENABLE_DENOISER)
#include "Denoiser.h"
#endif

// Development flag to dump materialX documents to disk.
// NOTE: This should never be enabled in committed code; it is only for local development.
#define AU_DEV_DUMP_PROCESSED_MATERIALX_DOCUMENTS 0

// Development flag to turn of exception catching during the render loop.
// NOTE: This should never be 0 in committed code; it is only for local development.
#define AU_DEV_CATCH_EXCEPTIONS_DURING_RENDERING 1

BEGIN_AURORA

// Renderer descriptor heap layout. Keep offsets synchronized with shader registers and root
// signature UAV ranges. Append new buffers to avoid shifting existing pass slots.
static const uint32_t kOutputDescriptorCount    = 4;
static const uint32_t kDenoisingDescriptorCount = 8;
static const int kUpscaledDescriptorOffset = kOutputDescriptorCount + kDenoisingDescriptorCount;
static const int kValidationDescriptorOffset = kUpscaledDescriptorOffset + 1;
static const int kTAAHistoryDescriptorOffset = kValidationDescriptorOffset + 1;
static const int kDepthNDCDisplayDescriptorOffset = kTAAHistoryDescriptorOffset + 2;
static const int kDemodDescriptorOffset = kDepthNDCDisplayDescriptorOffset + 1;
static const int kGuideNormalRoughnessDescriptorOffset = kDemodDescriptorOffset + 2;
static const uint32_t kDescriptorCount                 = kGuideNormalRoughnessDescriptorOffset + 1;

// PostProcessing's UAV table spans the whole heap from slot 0; Accumulation's starts at slot 1 and
// is one shorter.
static_assert(kDescriptorCount == 20,
    "Descriptor heap layout changed; update numDescriptors in PostProcessing.hlsl (this "
    "count) and Accumulation.hlsl (this count minus one).");

// The offsets of output-related descriptors.
static const int kFinalDescriptorOffset        = 0;
static const int kAccumulationDescriptorOffset = 1;
static const int kDirectDescriptorOffset       = 2;

#if AU_DEV_PERFORMANCE_LOGGING
// Number of GPU timestamp slots: start, afterRays, afterNRD, afterAccum, afterUpscale,
// afterPostProc.
static const uint32_t kTimestampSlots = 6;
#endif

static float halton(uint32_t index, uint32_t base)
{
    float result = 0.0f;
    float scale  = 1.0f / static_cast<float>(base);
    while (index > 0)
    {
        result += scale * static_cast<float>(index % base);
        index /= base;
        scale /= static_cast<float>(base);
    }
    return result;
}

// Bias-for-variance controls, applied only while the denoising AOVs are produced: no denoiser can
// remove a firefly from an unclamped near-specular chain at 1 spp, while the progressive path
// averages them out and must not pay the bias. kIndirectBounceClampScale is relative to
// maxLuminance, and 0.03 matches NRD's guidance that a sample's energy increase stay under 20-30x.
static constexpr float kPathRegularizationStrength = 0.65f;
static constexpr float kIndirectBounceClampScale   = 0.03f;

#if AU_DEV_PERFORMANCE_LOGGING
// Wall-clock time per phase of renderInternal(), averaged over frames and reported alongside the
// GPU timestamps. Sequential marks rather than scoped timers: a mark records the time since the
// previous one, so the deltas sum to the frame time and unattributed time cannot hide. Phases are
// held in call order rather than a map, so the report reads in the order the frame runs.
namespace
{
struct CPUPhaseTimers
{
    vector<pair<const char*, double>> totals;
    std::chrono::steady_clock::time_point lastMark;
    uint32_t frames = 0;

    void mark(const char* name)
    {
        auto const now                                          = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> const elapsed = now - lastMark;
        lastMark                                                = now;

        for (auto& entry : totals)
        {
            if (entry.first == name)
            {
                entry.second += elapsed.count();
                return;
            }
        }
        totals.emplace_back(name, elapsed.count());
    }

    void report()
    {
        if (frames == 0)
            return;
        string line;
        for (auto const& [name, total] : totals)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "%s=%.2fms  ", name, total / frames);
            line += buffer;
        }
        AU_INFO("[CPU over %u frames] %s", frames, line.c_str());
    }
};
CPUPhaseTimers gCPUPhases;
} // namespace
#define AU_CPU_MARK(name) gCPUPhases.mark(name)
#define AU_CPU_MARK_RESET() gCPUPhases.lastMark = std::chrono::steady_clock::now()
#else
#define AU_CPU_MARK(name)
#define AU_CPU_MARK_RESET()
#endif // AU_DEV_PERFORMANCE_LOGGING

// The length of the jitter sequence, or 0 for "do not repeat". A finite sequence is a vendor
// upscaler requirement: reconstructing N display pixels from one render pixel needs the render
// samples to cover all N sub-pixel positions periodically, hence 8 * (display / render)^2, matching
// FSR's own helper. It must NOT be applied to Aurora's own temporal resolve, where at ratio 1.0 the
// formula gives 8 and the resolve settles into an eight-frame limit cycle instead of converging.
static uint32_t getTemporalJitterPhaseCount(bool isUpscalerActive, float upscaleRatio)
{
    if (!isUpscalerActive)
        return 0;

    float const count = 8.0f * upscaleRatio * upscaleRatio;
    return static_cast<uint32_t>(glm::clamp(count, 8.0f, 256.0f));
}

static vec2 getTemporalJitter(uint32_t frameIndex, uint32_t phaseCount)
{
    // Center the Halton sequence in the [-0.5, 0.5] pixel range expected by NRD / temporal
    // upscalers.
    uint32_t const index = (phaseCount > 0u ? frameIndex % phaseCount : frameIndex) + 1u;
    return vec2(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);
}

PTRenderer::PTRenderer(uint32_t taskCount) : RendererBase(taskCount)
{
    // Initialize a new device. If no suitable device can be created, return immediately.
    _isValid = initDevice();
    if (!_isValid)
    {
        return;
    }

    // Initialize the command list, which includes creating the command queue and a command
    // allocator for each simultaneously active task.
    initCommandList();

    // Initialize a per-frame data buffer.
    initFrameData();

    // Initialize the shader table.
    initRayGenShaderTable();

    // Initialize the accumulation and post-processing compute shaders.
    initAccumulation();
    initPostProcessing();
    initTemporalResolve();
    initTimestamps();

    // Initialize the scratch buffer and vertex buffer pools. The function to create scratch buffers
    // uses unordered access, as required for BLAS/TLAS scratch buffers.
    _pScratchBufferCache = make_unique<ScratchBufferPool>(_taskCount, [&](size_t size) {
        return createBuffer(size, "Scratch Buffer Pool", D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    });
    _pVertexBufferPool   = make_unique<VertexBufferPool>(
        [&](size_t size) { return createTransferBuffer(size, "VertexBufferPool"); });
}

PTRenderer::~PTRenderer()
{
    // Do nothing if the renderer is not valid, i.e. construction had failed.
    if (!_isValid)
    {
        return;
    }

    // Wait for the GPU to complete any pending work, so that resources are not released while they
    // are still being used.
    waitForTask();
}

// NOTE: In the createXXX() functions below, the new object has a raw pointer to the renderer, and
// must not exist beyond the lifetime of the renderer. This could be enforced with weak_ptr<>, but
// that is cumbersome.

IWindowPtr PTRenderer::createWindow(WindowHandle handle, uint32_t width, uint32_t height)
{
    // Create and return a new window object.
    return make_shared<PTWindow>(this, handle, width, height);
}

IRenderBufferPtr PTRenderer::createRenderBuffer(int width, int height, ImageFormat imageFormat)
{
    // Create and return a new render buffer object.
    return make_shared<PTRenderBuffer>(this, width, height, imageFormat);
}

IImagePtr PTRenderer::createImagePointer(const IImage::InitData& initData)
{
    // Create a return a new image object.
    return make_shared<PTImage>(this, initData);
}

ISamplerPtr PTRenderer::createSamplerPointer(const Properties& props)
{
    // Create a return a new sampler object.
    return make_shared<PTSampler>(this, props);
}

IMaterialPtr PTRenderer::createMaterialPointer(
    const string& materialType, const string& document, const string& name)
{
    return dxScene()->createMaterialPointer(materialType, document, name);
}

IScenePtr PTRenderer::createScene()
{
    // Create and return a new scene object.
    return make_shared<PTScene>(this, kDescriptorCount);
}

IEnvironmentPtr PTRenderer::createEnvironmentPointer()
{
    // Create and return a new environment object.
    return make_shared<PTEnvironment>(this);
}

IGeometryPtr PTRenderer::createGeometryPointer(const GeometryDescriptor& desc, const string& name)
{
    // Create and return a new environment object.
    return make_shared<PTGeometry>(this, name, desc);
}

IGroundPlanePtr PTRenderer::createGroundPlanePointer()
{
    // Create and return a new ground plane object.
    return make_shared<PTGroundPlane>(this);
}

void PTRenderer::setScene(const IScenePtr& pScene)
{
    // Wait for the GPU to complete any pending work, so that scene resources are not released
    // while they are still being used.
    if (_pScene)
    {
        waitForTask();
    }

    // Assign the new scene.
    _pScene = dynamic_pointer_cast<SceneBase>(pScene);

    // Scene's default resources cannot be created until the scene is attached to a renderer.
    _pScene->createDefaultResources();
}

void PTRenderer::setTargets(const TargetAssignments& targetAssignments)
{
    // TODO: Validate that all targets have the same dimensions. Will involve adding a dimensions
    // accessor to ITarget.
    AU_ASSERT(targetAssignments.size() > 0, "There must be at least one target assignment.");

    // Get the target assigned to the final AOV (required).
    auto targetIt = targetAssignments.find(AOV::kFinal);
    AU_ASSERT(targetIt != targetAssignments.end(), "A target must be assigned to the Final AOV.");
    _pTargetFinal = dynamic_pointer_cast<PTTarget>(targetIt->second);

    // Get the target assigned to the NDC depth AOV (optional).
    targetIt         = targetAssignments.find(AOV::kDepthNDC);
    _pTargetDepthNDC = targetIt != targetAssignments.end()
        ? dynamic_pointer_cast<PTTarget>(targetIt->second)
        : nullptr;
}

void PTRenderer::render(uint32_t sampleStart, uint32_t sampleCount)
{
    // This function is a major entry point, so issue a fatal error if the renderer is not valid. It
    // will not be valid if initialization failed, or if there was a fatal error during rendering.
    // NOTE: The error handling further below is not sufficient to handle this because attempting to
    // use an invalid renderer could result in a memory access violation, which is not covered by
    // the try / catch block. Handling that requires platform-specific support, which is not worth
    // doing here.
    if (!_isValid)
    {
        AU_FAIL("Attempting to render with an invalid renderer.");
    }

    // Perform rendering. This includes exception handling to catch any fatal errors that may occur
    // in rendering.
    //
    // Specifically, this is meant to catch timeout detection and recovery (TDR) events, when a
    // single operation takes longer than two seconds (by default). This can happen with path
    // tracing on a slow GPU, or with large output dimensions, or with a complex scene. When this
    // happens, the device is removed ("lost") and resources must be recreated. However, Aurora does
    // not retain enough information for such a recovery, so this is treated as a fatal
    // (catastrophic) error and AU_FAIL is used to indicate this.
    //
    // By default, AU_FAIL will abort the application, so if the application wants to attempt
    // recovery, it must set a callback on the Log interface. Note that the Aurora renderer cannot
    // be reused after a failure, so it should be released and recreated.
#if AU_DEV_CATCH_EXCEPTIONS_DURING_RENDERING
    try
    {

#endif
        renderInternal(sampleStart, sampleCount);

#if AU_DEV_CATCH_EXCEPTIONS_DURING_RENDERING
    }
    catch (...)
    {
        _isValid = false;
        AU_FAIL("Rendering has failed, likely due to an operation taking too long to complete.");
    }
#endif
}

void PTRenderer::waitForTask()
{
    // NOTE: This causes the CPU to wait for all pending GPU work to finish. This is generally used
    // to avoid resource contention. However, this can affect performance should be avoided if
    // possible.

    // Set an event to be triggered when the fence reaches the *previous* task number, and wait
    // until that event is triggered. This means the the work for the previous task is done.
    // NOTE: The very first task is skipped, as there is no prior task to wait for. Also the fence
    // value must start at one, as zero is not valid, so one (1) is added here.
    if (_taskNumber > 0)
    {
        uint64_t fenceValue = _taskNumber - 1;
        checkHR(_pTaskFence->SetEventOnCompletion(fenceValue + 1, _hTaskEvent));
        ::WaitForSingleObject(_hTaskEvent, INFINITE);
    }

#if AU_DEV_PERFORMANCE_LOGGING
    // Log per-stage GPU timings every 60 frames via the timestamp readback buffer.
    // Log every call (waitForTask fires ~once per kDenoisingSamples frames in interactive mode).
    ++_gpuTimingFrameNum;
    if (_pTimestampHeap && _pTimestampReadback && _timestampFrequency > 0)
    {
        uint64_t* pTs     = nullptr;
        D3D12_RANGE range = { 0, kTimestampSlots * sizeof(uint64_t) };
        if (SUCCEEDED(_pTimestampReadback->Map(0, &range, reinterpret_cast<void**>(&pTs))))
        {
            auto ms = [&](uint32_t a, uint32_t b) -> float {
                return (pTs[b] > pTs[a]) ? static_cast<float>(pTs[b] - pTs[a]) * 1000.0f /
                        static_cast<float>(_timestampFrequency)
                                         : 0.0f;
            };
            // Skip log if any slot has a bogus value (e.g. uninitialized query heap on first
            // frame). A valid single frame never exceeds 1 s on any current GPU.
            float total = ms(0, 5);
            float accum = ms(2, 3);
            if (total > 0.0f && total < 1000.0f && accum < 1000.0f)
            {
                AU_INFO(
                    "[GPU] Rays=%.2fms  NRD=%.2fms  Accum=%.2fms  Upscale=%.2fms"
                    "  PostProc=%.2fms  Total=%.2fms",
                    ms(0, 1), ms(1, 2), accum, ms(3, 4), ms(4, 5), total);
            }
            D3D12_RANGE noWrite = { 0, 0 };
            _pTimestampReadback->Unmap(0, &noWrite);
        }
    }
#endif
}

const vector<string>& PTRenderer::builtInMaterials()
{
    return shaderLibrary().builtInMaterials();
}

PTShaderLibrary& PTRenderer::shaderLibrary()
{
    return dxScene()->shaderLibrary();
}

ID3D12ResourcePtr PTRenderer::createBuffer(size_t size, const string& name,
    D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    // Specify the heap properties and buffer description.
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size, flags);

    // Create a committed resource of the specified size.
    ID3D12ResourcePtr pResource;
    checkHR(_pDXDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, state, nullptr, IID_PPV_ARGS(&pResource)));

    // Set the resource name.
    checkHR(pResource->SetName(Foundation::s2w(name).c_str()));

    return pResource;
}

TransferBuffer PTRenderer::createTransferBuffer(size_t sz, const string& name,
    D3D12_RESOURCE_FLAGS gpuBufferFlags, D3D12_RESOURCE_STATES gpuBufferState,
    D3D12_RESOURCE_STATES gpuBufferFinalState)
{
    TransferBuffer buffer;
    // Create the upload buffer in the UPLOAD heap (which will mean it is in CPU memory not VRAM).
    buffer.pUploadBuffer = createBuffer(sz, name + ":Upload");
    // Create the GPU buffer in the DEFAULT heap, with the flags and state provided (these will
    // default to D3D12_RESOURCE_FLAG_NONE and D3D12_RESOURCE_STATE_COPY_DEST).
    buffer.pGPUBuffer =
        createBuffer(sz, name + ":GPU", D3D12_HEAP_TYPE_DEFAULT, gpuBufferFlags, gpuBufferState);
    // Set the size.
    buffer.size = sz;
    // Set the renderer to this (will call transferBufferUpdated from unmap.)
    buffer.pRenderer = this;
    // Set the final state for the buffer, which it will transition to after uploading.
    buffer.finalState = gpuBufferFinalState;
    return buffer;
}

ID3D12ResourcePtr PTRenderer::createTexture(uvec2 dimensions, DXGI_FORMAT format,
    const string& name, bool isUnorderedAccess, bool shareable)
{
    return createTexture(uvec3(dimensions, 0), format, name, isUnorderedAccess, shareable);
}

ID3D12ResourcePtr PTRenderer::createTexture(uvec3 dimensions, DXGI_FORMAT format,
    const string& name, bool isUnorderedAccess, bool shareable)
{
    // Prepare a texture description.
    D3D12_RESOURCE_FLAGS resourceFlag =
        isUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    CD3DX12_RESOURCE_DESC texDesc = dimensions.z > 0
        ? CD3DX12_RESOURCE_DESC::Tex3D(
              format, dimensions.x, dimensions.y, uint16_t(dimensions.z), 1, resourceFlag)
        : CD3DX12_RESOURCE_DESC::Tex2D(
              format, dimensions.x, dimensions.y, 1, 1, 1, 0, resourceFlag);

    // Set the initial resource state to "copy" or unordered access, because that is what the render
    // process expects as the state.
    D3D12_RESOURCE_STATES resourceState =
        isUnorderedAccess ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST;

    // Set the appropriate flags for sharing the texture across devices, if requested.
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
    if (shareable)
    {
        heapFlags = D3D12_HEAP_FLAG_SHARED;
        texDesc.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    }

    // Create a resource using the description and flags.
    ID3D12ResourcePtr pResource;
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    checkHR(_pDXDevice->CreateCommittedResource(
        &heapProps, heapFlags, &texDesc, resourceState, nullptr, IID_PPV_ARGS(&pResource)));

    // Set the resource name.
    checkHR(pResource->SetName(Foundation::s2w(name).c_str()));

    return pResource;
}

D3D12_GPU_VIRTUAL_ADDRESS PTRenderer::getScratchBuffer(size_t size)
{
    return _pScratchBufferCache->get(size);
}

void PTRenderer::transferBufferUpdated(const TransferBuffer& buffer)
{
    AU_ASSERT(!buffer.isMapped(), "Buffer is mapped");

    // Get the mapped range for buffer (set the end to buffer size, in the case where end==0.)
    size_t beginMap = buffer.mappedRange.Begin;
    size_t endMap   = buffer.mappedRange.End == 0 ? buffer.size : buffer.mappedRange.End;

    // See if this buffer already exists in pending list.
    auto iter = _pendingTransferBuffers.find(buffer.pGPUBuffer.Get());
    if (iter != _pendingTransferBuffers.end())
    {
        // Just update the mapped range in the existing pending buffer.
        iter->second.mappedRange.Begin = std::min(iter->second.mappedRange.Begin, beginMap);
        iter->second.mappedRange.End   = std::max(iter->second.mappedRange.End, endMap);
        return;
    }

    // Add the buffer in the pending list, updating the end range if needed.
    _pendingTransferBuffers[buffer.pGPUBuffer.Get()]                 = buffer;
    _pendingTransferBuffers[buffer.pGPUBuffer.Get()].mappedRange.End = endMap;
}

void PTRenderer::getVertexBuffer(VertexBuffer& vertexBuffer, void* pData, size_t size)
{
    _pVertexBufferPool->get(vertexBuffer, pData, size);
}

void PTRenderer::flushVertexBufferPool()
{
    _pVertexBufferPool->flush();
}

void PTRenderer::deleteUploadedTransferBuffers()
{
    // Do nothing if no buffers to delete.
    if (_transferBuffersToDelete.empty())
        return;

    // Clear the list, so any upload or GPU buffer without a reference to it will be deleted.
    _transferBuffersToDelete.clear();
}

void PTRenderer::uploadTransferBuffers()
{
    // If there are no transfer buffers pending, do nothing.
    if (_pendingTransferBuffers.empty())
        return;

    // Begin a command list.
    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

    // Iterate through all pending buffers.
    for (auto iter = _pendingTransferBuffers.begin(); iter != _pendingTransferBuffers.end(); iter++)
    {
        // Get pending buffer.
        auto& buffer = iter->second;
        AU_ASSERT(!buffer.isMapped(), "Buffer was not unmapped");

        // Calculate the byte count from the mapped range (which will be the maximum range mapped
        // this frame.)
        size_t bytesToCopy = buffer.mappedRange.End - buffer.mappedRange.Begin;

        // If this buffer was previously uploaded we need transition back to
        // D3D12_RESOURCE_STATE_COPY_DEST before copying.
        if (buffer.wasUploaded)
            addTransitionBarrier(
                buffer.pGPUBuffer.Get(), buffer.finalState, D3D12_RESOURCE_STATE_COPY_DEST);

        // Submit buffer copy command for mapped range from the upload buffer to the GPU buffer.
        pCommandList->CopyBufferRegion(buffer.pGPUBuffer.Get(), buffer.mappedRange.Begin,
            buffer.pUploadBuffer.Get(), buffer.mappedRange.Begin, bytesToCopy);

        // Transition the buffer to its final state.
        // Note in practice the Nvidia driver seems to do this implicitly without any problems,
        // though the spec says this explicit transition is required.
        addTransitionBarrier(
            buffer.pGPUBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, buffer.finalState);

        // Set the uploaded flag.
        buffer.wasUploaded = true;

        // Add the buffer to the list to be deleted (if nothing has kept a reference to it) next
        // frame.
        _transferBuffersToDelete[iter->first] = iter->second;
    }

    // Submit the command list.
    submitCommandList();

    // Clear the pending list.
    _pendingTransferBuffers.clear();
}

ID3D12CommandAllocator* PTRenderer::getCommandAllocator()
{
    return _commandAllocators[_taskIndex].Get();
}

ID3D12GraphicsCommandList4* PTRenderer::beginCommandList()
{
    assert(!_isCommandListOpen);
    _isCommandListOpen = true;

    // Reset the command list using the current command allocator.
    // NOTE: It is safe to do this even if commands that were created with the command list are
    // still being executed. It is the command *allocator* that can't be reset that way.
    checkHR(_pCommandList->Reset(_commandAllocators[_taskIndex].Get(), nullptr));

    return _pCommandList.Get();
}

void PTRenderer::submitCommandList()
{
    assert(_isCommandListOpen);
    _isCommandListOpen = false;

    // Close the command list and execute it on the command queue.
    checkHR(_pCommandList->Close());
    ID3D12CommandList* pCommandList = _pCommandList.Get();
    _pCommandQueue->ExecuteCommandLists(1, &pCommandList); // no HRESULT
}

void PTRenderer::addTransitionBarrier(
    ID3D12Resource* pResource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
    assert(_isCommandListOpen);

    // Insert a resource barrier into the command list to transition the resource from its previous
    // state to a new one.
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(pResource, stateBefore, stateAfter);
    _pCommandList->ResourceBarrier(1, &barrier); // no HRESULT
}

void PTRenderer::addUAVBarrier(ID3D12Resource* pResource)
{
    assert(_isCommandListOpen);

    // Insert a resource barrier to ensure all UAV reads / writes are completed.
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(pResource);
    _pCommandList->ResourceBarrier(1, &barrier); // no HRESULT
}

void PTRenderer::completeTask()
{
    assert(!_isCommandListOpen);

    // NOTE: A "task" manages simultaneous access to limited resources. Specifically this applies
    // to the following resources:
    // - The command allocators: these must be reset regularly to avoid excessive memory
    //   consumption, so they are reset (arbitrarily) after each "task" of work. They can't be
    //   reset while they have commands that have been queued or are executing.
    // - The per-frame constant buffer: this is a single buffer partitioned into copies of data for
    //   each possible task queued for rendering. These are updated by the CPU, but this must not
    //   be done while the GPU might be reading the data.
    //
    // There are three relevant members:
    // - _taskCount: The number of simultaneously active tasks (default 3). Making this larger
    //   increases latency, but smaller may reduce performance.
    // - _taskIndex: Which of the available task "slots" is currently being used for new work, as a
    //   value between zero and _taskCount.
    // - _taskNumber: The current overall task number, a value that increments by one for each
    //   completed task. This does not necessarily correspond to "frames."

    // Have the command queue update the fence to the current task number plus one, when all pending
    // command lists have been processed.
    // NOTE: Fence value is always taskNumber+1 as zero is not a valid fence value.
    checkHR(_pCommandQueue->Signal(_pTaskFence.Get(), (_taskNumber + 1)));

    // Increment the task number.
    _taskNumber++;

    // Wait for the work for the task that last used the data for the current task index. For
    // example, if there are three active tasks, and this is overall task #4, then we need to make
    // sure that the work for task #1 (three tasks ago) is complete. This is "triple buffering."
    // NOTE: The fence value must start at one, as zero is not valid, so one (1) is added here.
    if (_taskNumber > _taskCount)
    {
        uint64_t fenceValue = _taskNumber - _taskCount;
        checkHR(_pTaskFence->SetEventOnCompletion(fenceValue + 1, _hTaskEvent));
        ::WaitForSingleObject(_hTaskEvent, INFINITE);
    }

    // Determine the current task index, which repeats: 0, 1, 2, 0, 1, etc. Reset the corresponding
    // command allocator, and reset the command list using that command allocator.
    _taskIndex = _taskNumber % _taskCount;
    checkHR(_commandAllocators[_taskIndex]->Reset());

    // Update the scratch buffer cache with the new task index.
    _pScratchBufferCache->update(_taskIndex);
}

PTSamplerPtr PTRenderer::defaultSampler()
{
    // Create a default sampler if it does not already exist.
    // NOTE: The object has a raw pointer to the renderer, and must not exist beyond the lifetime of
    // the renderer. This could be enforced with weak_ptr<>, but that is cumbersome.
    if (!_pDefaultSampler)
    {
        _pDefaultSampler = dynamic_pointer_cast<PTSampler>(createSamplerPointer({}));
    }

    // Return the existing default sampler.
    return _pDefaultSampler;
}

PTGroundPlanePtr PTRenderer::defaultGroundPlane()
{
    // Create a default ground plane if it does not already exist.
    // NOTE: The object has a raw pointer to the renderer, and must not exist beyond the lifetime of
    // the renderer. This could be enforced with weak_ptr<>, but that is cumbersome.
    if (!_pDefaultGroundPlane)
    {
        _pDefaultGroundPlane = dynamic_pointer_cast<PTGroundPlane>(createGroundPlanePointer());

        // Set the default ground plane as disabled, so that setting a null ground plane on a scene
        // will remove (i.e. not render) the ground plane.
        _pDefaultGroundPlane->values().setBoolean("enabled", false);
    }

    return _pDefaultGroundPlane;
}

bool PTRenderer::initDevice()
{
    // Create a device with ray tracing support.
    _pDevice = PTDevice::create(PTDevice::Features::kRayTracing);
    if (!_pDevice)
    {
        return false;
    }

    // Retain the DirectX device and factory objects.
    _pDXDevice  = _pDevice->device();
    _pDXFactory = _pDevice->factory();

    // Store the descriptor heap handle increment size for future use.
    _handleIncrementSize =
        _pDXDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return true;
}

void PTRenderer::initCommandList()
{
    // Create a command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    checkHR(_pDXDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_pCommandQueue)));

    // Create a command allocator for each active task, as a command allocator is needed for each
    // possible command list that is queued at once.
    _commandAllocators.resize(_taskCount);
    for (uint32_t i = 0; i < _taskCount; i++)
    {
        checkHR(_pDXDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&(_commandAllocators[i]))));
    }

    // Create a command list. Only a single command list is required for each thread, just one here.
    // NOTE: It is initialized with the first command allocator but will be reset with the command
    // allocator for the current active task index.
    checkHR(_pDXDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        _commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&_pCommandList)));
    _pCommandList->Close();

    // Create a fence and event for detecting when a command list is complete.
    checkHR(_pDXDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_pTaskFence)));
    _hTaskEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void PTRenderer::initFrameData()
{
    // Create a buffer to store frame data for each buffer. A single buffer can be used for this,
    // as long as each section of the buffer is not written to while it is being used by the GPU.
    size_t frameDataBufferSize = _FRAME_DATA_SIZE * _taskCount;
    _frameDataBuffer           = createTransferBuffer(frameDataBufferSize, "FrameData");
}

void PTRenderer::initRayGenShaderTable()
{
    // Compute the stride of each record in the shader table. The stride includes the shader
    // identifier and its parameters. Also compute the total size of the table.
    size_t rayGenShaderRecordStride = SHADER_ID_SIZE;
    rayGenShaderRecordStride += SHADER_RECORD_DESCRIPTOR_SIZE; // for the descriptor table
    rayGenShaderRecordStride = ALIGNED_SIZE(rayGenShaderRecordStride, SHADER_RECORD_ALIGNMENT);
    _rayGenShaderTableSize   = rayGenShaderRecordStride; // just one shader record

    // Create a transfer buffer for the shader table.
    _rayGenShaderTable = createTransferBuffer(_rayGenShaderTableSize, "RayGenShaderTable");
}

void PTRenderer::updateRayGenShaderTable()
{
    // Map the ray gen shader table.
    uint8_t* pShaderTableMappedData = _rayGenShaderTable.map();

    // Write the shader identifier for the ray gen shader.
    ::memcpy_s(pShaderTableMappedData, _rayGenShaderTableSize,
        shaderLibrary().getShaderID(PTShaderLibrary::kRayGenEntryPointName), SHADER_ID_SIZE);

    // Unmap the shader table buffer.
    _rayGenShaderTable.unmap();
}

void PTRenderer::initAccumulation()
{
    // Get the byte code for the compute shader.
    D3D12_SHADER_BYTECODE shaderByteCode =
        CD3DX12_SHADER_BYTECODE(g_pAccumulationShader, _countof(g_pAccumulationShader));

    // Create a root signature from the definition in the byte code.
    checkHR(_pDXDevice->CreateRootSignature(0, g_pAccumulationShader,
        _countof(g_pAccumulationShader), IID_PPV_ARGS(&_pAccumulationRootSignature)));

    // Prepare compute pipeline state with the compute shader.
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.CS                                = shaderByteCode;
    checkHR(
        _pDXDevice->CreateComputePipelineState(&desc, IID_PPV_ARGS(&_pAccumulationPipelineState)));
}

void PTRenderer::initTemporalResolve()
{
    // Get the byte code for the compute shader.
    D3D12_SHADER_BYTECODE shaderByteCode =
        CD3DX12_SHADER_BYTECODE(g_pTemporalResolveShader, _countof(g_pTemporalResolveShader));

    // Create a root signature from the definition in the byte code.
    checkHR(_pDXDevice->CreateRootSignature(0, g_pTemporalResolveShader,
        _countof(g_pTemporalResolveShader), IID_PPV_ARGS(&_pTemporalResolveRootSignature)));

    // Prepare compute pipeline state with the compute shader.
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.CS                                = shaderByteCode;
    checkHR(_pDXDevice->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(&_pTemporalResolvePipelineState)));
}

void PTRenderer::initPostProcessing()
{
    // Get the byte code for the compute shader.
    D3D12_SHADER_BYTECODE shaderByteCode =
        CD3DX12_SHADER_BYTECODE(g_pPostProcessingShader, _countof(g_pPostProcessingShader));

    // Create a root signature from the definition in the byte code.
    checkHR(_pDXDevice->CreateRootSignature(0, g_pPostProcessingShader,
        _countof(g_pPostProcessingShader), IID_PPV_ARGS(&_pPostProcessingRootSignature)));

    // Prepare compute pipeline state with the compute shader.
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.CS                                = shaderByteCode;
    checkHR(_pDXDevice->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(&_pPostProcessingPipelineState)));
}

void PTRenderer::initTimestamps()
{
#if AU_DEV_PERFORMANCE_LOGGING
    // Create a D3D12 timestamp query heap for per-stage GPU timing (Perf-D).
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count                 = kTimestampSlots;
    heapDesc.NodeMask              = 0;
    if (FAILED(_pDXDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&_pTimestampHeap))))
        return; // GPU doesn't support timestamp queries -- timing silently disabled.

    _pTimestampReadback = createBuffer(kTimestampSlots * sizeof(uint64_t), "TimestampReadback",
        D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    // Frequency is needed to convert tick deltas to milliseconds.
    if (FAILED(_pCommandQueue->GetTimestampFrequency(&_timestampFrequency)))
        _timestampFrequency = 0;
#endif
}

void PTRenderer::renderInternal(uint32_t sampleStart, uint32_t sampleCount)
{
    AU_CPU_MARK_RESET();
    assert(_pScene && _pTargetFinal);
    assert(sampleCount > 0);

    // If non-zero instance count, then ensure we have valid bounds.
    AU_ASSERT(!dxScene()->instanceCount() || dxScene()->bounds().isValid(),
        "Scene bounds are not valid. Were valid bounds set with IScene::setBounds() ?");

    // Suppress D3D12 ERROR-level break-on-severity while denoising: NRD's compute commands trigger
    // validation errors expected for its usage patterns, and the resulting DebugBreak reaches
    // render()'s handler as a fatal "rendering has failed". CORRUPTION-level breaks stay active.
    // TODO: narrow this to the specific D3D12_MESSAGE_IDs NRD provokes, via an info-queue filter.
    ComPtr<ID3D12InfoQueue> pInfoQueueFrame;
    bool hadFrameBreakOnError = false;
    if (isDenoisingRequested() &&
        SUCCEEDED(_pDXDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueueFrame))))
    {
        hadFrameBreakOnError = pInfoQueueFrame->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR);
        if (hadFrameBreakOnError)
            pInfoQueueFrame->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    }

    // Call preUpdate function (will create any resources before the scene and shaders are rebuilt).
    dxScene()->preUpdate();
    AU_CPU_MARK("preUpdate");

    // Have any options changed?
    if (_bIsDirty)
    {
        string unit = _values.asString(kLabelUnits);
        dxScene()->setUnit(unit);

        // Set the default BSDF (if true will use Reference, if false will use Standard Surface.)
        shaderLibrary().setOption(
            "USE_REFERENCE_BSDF", _values.asBoolean(kLabelIsReferenceBSDFEnabled));

        // Clear the dirty flag.
        _bIsDirty = false;
    }

    // Update the scene.
    dxScene()->update();
    AU_CPU_MARK("sceneUpdate");

    // Rebuild shader library if needed.
    if (shaderLibrary().rebuildRequired())
    {
        // If shader rebuild required, wait for GPU to be idle, then rebuild.
        waitForTask();
        int globalTextureCount, globalTexture3DCount, globalSamplerCount;
        dxScene()->computeMaterialTextureCount(
            globalTextureCount, globalTexture3DCount, globalSamplerCount);
        shaderLibrary().rebuild(globalTextureCount, globalTexture3DCount, globalSamplerCount);

        // Update the ray gen shader table after rebuild.
        updateRayGenShaderTable();

        // Clear the scene's shader data, to ensure that is rebuilt too.
        dxScene()->clearShaderData();
    }

    AU_CPU_MARK("shaderRebuild");

    // Update the scene resources.
    dxScene()->updateResources();
    AU_CPU_MARK("sceneResources");

    // Update the resources associated with the scene, output (targets), and denoising.
    // NOTE: These updates are very fast if nothing relevant has changed since the last render.
    _isDimensionsChanged = _outputDimensions != _pTargetFinal->dimensions();
    _outputDimensions    = _pTargetFinal->dimensions();

    // Derive the path tracing resolution, before any resource is created and independently of
    // anything the update functions below compute.
    {
        float const scale = effectiveRenderScale();
        uvec2 const newRenderDimensions =
            glm::max(uvec2(1u), uvec2(glm::round(vec2(_outputDimensions) / scale)));
        _isRenderDimensionsChanged = newRenderDimensions != _renderDimensions;
        _renderDimensions          = newRenderDimensions;
    }
    _isDescriptorHeapChanged = _pDescriptorHeap.Get() != dxScene()->descriptorHeap();
    _pDescriptorHeap         = dxScene()->descriptorHeap();
    _pSamplerDescriptorHeap  = dxScene()->samplerDescriptorHeap();
    updateSceneResources();
    updateOutputResources();
    updateDenoisingResources();
    AU_CPU_MARK("rendererResources");
    _isDimensionsChanged       = false;
    _isRenderDimensionsChanged = false;
    _isDescriptorHeapChanged   = false;

    // Report the implied denoising when super resolution is requested.
    if (!_warnedUpscalerWithoutDenoiser && !_values.asBoolean(kLabelIsDenoisingEnabled) &&
        isDenoisingRequested())
    {
        AU_WARN(
            "Super Resolution requires a denoised input, so denoising is running even though "
            "isDenoisingEnabled is false. Use upscalerMode = \"DLSSRayReconstruction\" for an "
            "upscaler that denoises for itself.");
        _warnedUpscalerWithoutDenoiser = true;
    }

    // Reset every temporal history when the configuration behind it changes, so the user only has
    // to request a reset for what the renderer cannot see, such as a camera cut or a new scene. The
    // zero-initialized default differs from any real configuration, so frame one starts clean.
    TemporalConfig const temporalConfig = { upscalerMode(), _values.asInt(kLabelUpscalerQuality),
        _values.asBoolean(kLabelIsTemporalResolveEnabled), isDenoisingRequested(),
        _outputDimensions };
    if (temporalConfig != _temporalConfig)
    {
        setBoolean(kLabelIsResetHistoryEnabled, true);
        _temporalConfig = temporalConfig;
    }

    // Capture the one-shot history-reset request and clear it. Must happen AFTER
    // updateDenoisingResources(), which sets this option when (re-)initializing the denoiser;
    // capturing earlier would defer the reset by a frame.
    bool isResetHistoryEnabled = _values.asBoolean(kLabelIsResetHistoryEnabled);
    if (isResetHistoryEnabled)
        setBoolean(kLabelIsResetHistoryEnabled, false);

    // Advance the seed each frame during temporal accumulation to avoid repeating the same
    // sequence; hold it fixed otherwise, since a moving seed with no temporal stage just looks
    // like crawling static. Never wrap it on a short period -- it seeds PCG2D's state directly, so
    // frames one period apart would be bit-identical.
    uint32_t seedOffset = 0;
    if (isDenoisingRequested() || isUpscalerRequested())
    {
        // Increment the stored seed offset when rendering has been restarted.
        if (sampleStart == 0)
        {
            ++_seedOffset;
        }

        // Use the stored seed offset, instead of the default zero.
        seedOffset = _seedOffset;
    }

    // Temporal stages require one sample per render(): their frame index advances once per call.
    // Additional loop iterations would reuse jitter, matrices, and motion vectors.
    if (isDenoisingRequested() || isUpscalerRequested())
    {
        sampleCount = 1;
    }

    // Prepare a ray dispatch description, and perform a ray tracing dispatch for each requested
    // sample, starting at the sample start index.
    // NOTE: While it is possible to render multiple samples in a single dispatch instead of using a
    // loop (as done here), that is actually much slower in practice.
    D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};
    prepareRayDispatch(dispatchRaysDesc);
    AU_CPU_MARK("prepareDispatch");
    for (uint32_t i = 0; i < sampleCount; i++)
    {
        // Update the per-frame data. There is one copy of the data per task, and a task is
        // completed at the end of this loop, so the data update must be performed here.
        updateFrameData(isResetHistoryEnabled);
        AU_CPU_MARK("frameData");

        // Perform ray tracing for the current sample.
        submitRayDispatch(dispatchRaysDesc, sampleStart + i, seedOffset);
        AU_CPU_MARK("rayDispatch");

        // Perform denoising of the diffuse / glossy radiance results.
        // NOTE: This does nothing if denoising is not enabled.
        submitDenoising(isResetHistoryEnabled);
        AU_CPU_MARK("denoising");

        // Perform accumulation using the generated AOVs, some of which may have been denoised.
        // NOTE: This should not use the sample offset, as it needs to know how far along the
        // accumulation has proceeded.
        submitAccumulation(sampleStart + i);

        // Resolve the sub-pixel camera jitter into a stable image.
        // NOTE: This does nothing unless denoising is on and no vendor upscaler is active.
        submitTemporalResolve(isResetHistoryEnabled);

        // Perform upscaling as a full-resolution quality pass (upscaler).
        // NOTE: Must happen after accumulation so the upscaler reads the current frame.
        submitUpscaling(isResetHistoryEnabled);
        AU_CPU_MARK("accumulateResolveUpscale");

        // Tell the NEXT iteration (and the next frame) whether NRD is producing output, so the ray
        // gen shader writes direct-only exactly when accumulation will composite NRD's lobes.
        _nrdAvailableLastFrame = _nrdSubmittedThisFrame;

        // Complete the task here, because the denoiser itself only supports the same number of
        // tasks and uses one for each sample counter increment.
        completeTask();
        AU_CPU_MARK("completeTask");
    }

    // Perform post-processing and copy the results to the targets.
    submitPostProcessing();
    AU_CPU_MARK("postProcessing");

    // Present the targets.
    // NOTE: If the target has a swap chain, the following applies to avoid errors:
    // - WRONGSWAPCHAINBUFFERREFERENCE: Present must be done *after* the previous command list is
    //   submitted to avoid this immediate error.
    // - OBJECT_DELETED_WHILE_STILL_IN_USE: Present must be done *before* ending the task, to avoid
    //   this error on swap chain resize or application shutdown. Specifically, a command queue
    //   Signal() call must be made after presenting. The reason for this is unclear, but it has to
    //   do with the swap chain interacting with the command queue it was created with.
    _pTargetFinal->present();
    if (_pTargetDepthNDC)
    {
        _pTargetDepthNDC->present();
    }

    // Complete the task, so that rendering is ready for the next task.
    completeTask();
    AU_CPU_MARK("presentAndComplete");

#if AU_DEV_PERFORMANCE_LOGGING
    if (++gCPUPhases.frames % 20 == 0)
    {
        gCPUPhases.report();
    }
#endif

    // Retain this frame's camera as the next frame's "previous", for reprojection. With no temporal
    // stage there is nothing to reproject, so the jitter and frame index are cleared instead.
    _cameraViewPrevFrame = _cameraView;
    _cameraProjPrevFrame = _cameraProj;
    _cameraJitterPrev    = _cameraJitter;
    if (isDenoisingRequested() || isUpscalerRequested())
    {
        _hasPreviousTemporalFrame = true;
        _temporalFrameIndex++;
    }
    else
    {
        _cameraJitter             = vec2(0.0f);
        _cameraJitterPrev         = vec2(0.0f);
        _hasPreviousTemporalFrame = false;
        _temporalFrameIndex       = 0;
    }

    // Restore D3D12 ERROR break-on-severity now that all NRD GPU work is queued.
    if (pInfoQueueFrame && hadFrameBreakOnError)
        pInfoQueueFrame->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

    // Clear the history reset flag.
    // NOTE: This is done as a convenience for the client, as resetting history is almost always
    // intended only for one frame.
    _values.setValue(kLabelIsResetHistoryEnabled, false);
}

void PTRenderer::updateFrameData(bool isRestartRequested)
{
    // Call base-class update function to get GPU frame data.
    updateFrameDataGPUStruct();

    // NRD feeds accumulation and the upscaler; Ray Reconstruction replaces NRD.
    bool const userWantsDenoising   = isDenoisingRequested();
    bool const wantsNRDThisFrame    = userWantsDenoising && !isRayReconstructionActive();
    bool const wantsTemporalHistory = userWantsDenoising || isUpscalerRequested();

    // renderInternal() already consumed the one-shot reset flag.
    bool const resetHistory = isRestartRequested || !_hasPreviousTemporalFrame;

    // Apply sub-pixel camera jitter ONLY when something downstream resolves it. Denoising alone is
    // not enough: it filters only the indirect lobes, so an unresolved jitter shakes the whole
    // image by up to half a pixel every frame. When false, computeCameraRay still moves the ray
    // within the pixel using white noise, and NRD is told the jitter is zero -- reporting the true
    // jitter there measured 23% worse frame-to-frame stability.
    bool const wantsJitter = hasTemporalResolve();
    uint32_t const jitterPhaseCount =
        getTemporalJitterPhaseCount(isUpscalerRequested(), effectiveRenderScale());
    vec2 const currentJitter =
        wantsJitter ? getTemporalJitter(_temporalFrameIndex, jitterPhaseCount) : vec2(0.0f);

    // Frame period, start-of-frame to start-of-frame. NRD derives its accumulation length from this
    // and DLSS/FSR are given it directly, so it must be stamped here rather than at the end of
    // renderInternal(), which would measure the caller-side gap and pin this to its 1 ms floor.
    auto const now    = std::chrono::steady_clock::now();
    float timeDeltaMs = kDefaultFrameTimeMs;
    if (_hasPreviousTemporalFrame)
    {
        timeDeltaMs = std::chrono::duration<float, std::milli>(now - _lastFrameUpdateTime).count();
        timeDeltaMs = glm::clamp(timeDeltaMs, 1.0f, 1000.0f);
    }
    _lastFrameUpdateTime = now;

    bool const canUseDenoisedHistoryThisFrame = !resetHistory && _nrdAvailableLastFrame;
    _frameData.isDenoisingEnabled = (wantsNRDThisFrame && canUseDenoisedHistoryThisFrame) ? 1 : 0;
    _frameData.isDepthNDCEnabled  = (_pTargetDepthNDC || wantsTemporalHistory) ? 1 : 0;
    bool const denoisingAOVs      = isDenoisingAOVsEnabled();
    _frameData.isDenoisingAOVsEnabled = denoisingAOVs ? 1 : 0;

    // Opt into the biased firefly controls only while denoising. RendererBase leaves them
    // off so the progressive path converges to the unbiased image.
    _frameData.isPathRegularizationEnabled = denoisingAOVs ? 1 : 0;
    _frameData.pathRegularizationStrength  = denoisingAOVs ? kPathRegularizationStrength : 0.0f;
    _frameData.indirectBounceClampScale    = denoisingAOVs ? kIndirectBounceClampScale : 0.0f;
    _frameData.cameraViewProjPrev          = (!resetHistory && _hasPreviousTemporalFrame)
                 ? (_cameraProjPrevFrame * _cameraViewPrevFrame)
                 : _frameData.cameraViewProj;
    _frameData.cameraInvViewPrev           = (!resetHistory && _hasPreviousTemporalFrame)
                  ? transpose(inverse(_cameraViewPrevFrame))
                  : _frameData.cameraInvView;
    _frameData.cameraJitter                = currentJitter;
    _frameData.cameraJitterPrev =
        (!resetHistory && _hasPreviousTemporalFrame) ? _cameraJitter : currentJitter;
    _frameData.timeDeltaMs             = timeDeltaMs;
    _frameData.isTemporalJitterEnabled = wantsJitter ? 1 : 0;
    _cameraJitter                      = currentJitter;

    // Copy frame data to the frame data buffer, at the buffer data location for the current task
    // index. The updated section of the buffer must be set on the pipeline later with
    // SetComputeRootConstantBufferView().
    uint8_t* pFrameDataBufferMappedData = nullptr;
    size_t start                        = _FRAME_DATA_SIZE * _taskIndex;
    size_t end                          = start + _FRAME_DATA_SIZE;
    pFrameDataBufferMappedData          = _frameDataBuffer.map(end, start);
    ::memcpy_s(
        pFrameDataBufferMappedData + start, _FRAME_DATA_SIZE, &_frameData, sizeof(_frameData));
    _frameDataBuffer.unmap();

    // Upload the transfer buffers after changing the frame data, so the new frame data is available
    // on the GPU while rendering frame.
    uploadTransferBuffers();
}

void PTRenderer::updateSceneResources()
{
    // Nothing to do here. Resource changes are handled by the scene.
}

void PTRenderer::updateOutputResources()
{
    // NOTE: If the client switches between several resident targets with different dimensions (e.g.
    // several viewports), creating new output textures every time may become a performance
    // bottleneck. This can be resolved by having a small cache of output textures with recently
    // used dimensions.
    //
    // Only single textures are required (not one per active task), as the renderer does not employ
    // multiple command queues that could operate simultaneously, and the CPU does not read or write
    // these textures.

    // Get the final target image format, converted to a DXGI format.
    DXGI_FORMAT newFormat = PTImage::getDXFormat(_pTargetFinal->format());

    // Create new required textures if the output dimensions or image format have changed. The final
    // (tone-mapped) texture lives at DISPLAY resolution, as it is copied straight into the swap
    // chain; everything the path tracer writes lives at RENDER resolution.
    bool isTexturesUpdated = false;
    if (_isDimensionsChanged || _finalFormat != newFormat)
    {
        // If there is an existing final texture, flush the renderer to make sure the related
        // textures are no longer being used by the pipeline.
        if (_pTexFinal)
        {
            waitForTask();
        }

        // Create the final texture with the final target format (usually integer SDR).
        // NOTE: A texture with unordered access can't have an "_SRGB" format, so gamma correction
        // must be performed in the post-processing step.
        _finalFormat      = newFormat;
        _pTexFinal        = createTexture(_outputDimensions, _finalFormat, "Output", true);
        isTexturesUpdated = true;
    }

    if (_isRenderDimensionsChanged)
    {
        if (_pTexAccumulation)
        {
            waitForTask();
        }

        // The accumulation and direct textures use a floating-point (HDR) format, with unordered
        // access.
        const DXGI_FORMAT kHDRFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        _pTexAccumulation = createTexture(_renderDimensions, kHDRFormat, "HDR Accumulation", true);
        _pTexDirect       = createTexture(_renderDimensions, kHDRFormat, "HDR Direct", true);

        // Ping-ponged history for the temporal resolve pass. Half-float is plenty: the default
        // maxLuminance clamp is 1000 and fp16 reaches 65504. Allocated unconditionally, so that
        // toggling the resolve on does not trigger a mid-session GPU flush and reallocation.
        _pTexTAAHistory[0] =
            createTexture(_renderDimensions, DXGI_FORMAT_R16G16B16A16_FLOAT, "TAA History A", true);
        _pTexTAAHistory[1] =
            createTexture(_renderDimensions, DXGI_FORMAT_R16G16B16A16_FLOAT, "TAA History B", true);
        _taaHistoryIndex  = 0;
        _hasTAAHistory    = false;
        isTexturesUpdated = true;
    }

    bool const wantsInternalDepthNDC = isDenoisingRequested() || isUpscalerRequested();
    bool const needsDepthTexture     = static_cast<bool>(_pTargetDepthNDC) || wantsInternalDepthNDC;

    // Create (or clear) the NDC depth texture if the render dimensions have changed or a temporal
    // consumer (target / denoiser / upscaler) has toggled the requirement.
    if (_isRenderDimensionsChanged || needsDepthTexture != static_cast<bool>(_pTexDepthNDC))
    {
        // If there is an existing NDC depth texture, flush the renderer to make sure the texture is
        // no longer being used by the pipeline.
        if (_pTexDepthNDC)
        {
            waitForTask();
        }

        // ALWAYS single-channel R32_FLOAT, to match the "RWTexture2D<float> gDepthNDC" every shader
        // that binds it declares: createUAV() derives the UAV format from the resource, so deriving
        // this one from the bound depth target instead would bind a four-channel UAV to a
        // single-channel declaration. Clients must request ImageFormat::Float_R for a depth target.
        if (needsDepthTexture)
        {
            _pTexDepthNDC =
                createTexture(_renderDimensions, DXGI_FORMAT_R32_FLOAT, "NDC Depth", true);
        }
        else
        {
            _pTexDepthNDC.Reset();
        }
        isTexturesUpdated = true;
    }

    // The display-resolution depth copy exists only to feed a client-bound depth AOV target. Point
    // sampling is the correct upsample for it, so post-processing writes it directly.
    bool const needsDepthDisplayTexture = static_cast<bool>(_pTargetDepthNDC);
    if (_isDimensionsChanged || needsDepthDisplayTexture != static_cast<bool>(_pTexDepthNDCDisplay))
    {
        if (_pTexDepthNDCDisplay)
        {
            waitForTask();
        }
        if (needsDepthDisplayTexture)
        {
            _pTexDepthNDCDisplay = createTexture(
                _outputDimensions, DXGI_FORMAT_R32_FLOAT, "NDC Depth (display)", true);
        }
        else
        {
            _pTexDepthNDCDisplay.Reset();
        }
        isTexturesUpdated = true;
    }

    // If textures have been updated, or the descriptor heap has changed, create UAVs for the
    // textures.
    if (isTexturesUpdated || _isDescriptorHeapChanged)
    {
        // Flush the renderer to make sure the (unchanged) descriptor heap is not being used.
        if (!_isDescriptorHeapChanged)
        {
            waitForTask();
        }

        // Create UAVs for the textures as the first entries in the descriptor heap.
        // The order they are written to the descriptor heap must match the ray gen root descriptor
        // defined in PTShaderLibrary::initRootSignatures and the AOV RWTexture2D defined in
        // MainEntryPoints.slang.
        // TODO: These do not seem to match the values in the shader, why is that?
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        createUAV(_pTexFinal.Get(), handle);
        createUAV(_pTexAccumulation.Get(), handle);
        createUAV(_pTexDirect.Get(), handle);
        createUAV(_pTexDepthNDC.Get(), handle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE taaHandle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), kTAAHistoryDescriptorOffset,
            _handleIncrementSize);
        createUAV(_pTexTAAHistory[0].Get(), taaHandle);
        createUAV(_pTexTAAHistory[1].Get(), taaHandle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE depthDisplayHandle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            kDepthNDCDisplayDescriptorOffset, _handleIncrementSize);
        createUAV(_pTexDepthNDCDisplay.Get(), depthDisplayHandle);
    }
}

void PTRenderer::updateDenoisingResources()
{
    // Create the temporal guide textures when first needed (denoising, upscaling or a debug AOV),
    // then keep them alive across a toggle-off: recreating them costs a GPU flush plus a
    // reallocation.
    bool isTexturesUpdated = false;
    bool needsTextures     = isDenoisingAOVsEnabled() || _pDenoisingTexDepthView != nullptr;
    if (needsTextures)
    {
        // Recreate textures only when dimensions change or they don't exist yet.
        if (_isRenderDimensionsChanged || !_pDenoisingTexDepthView)
        {
            isTexturesUpdated = true;

            // Flush in-flight GPU work before releasing existing resources.
            if (_pDenoisingTexDepthView)
            {
                waitForTask();
                _pDenoisingTexDepthView.Reset();
                _pDenoisingTexNormalRoughness.Reset();
                _pDenoisingTexBaseColorMetalness.Reset();
                _pDenoisingTexDiffuse.Reset();
                _pDenoisingTexGlossy.Reset();
                _pDenoisingTexDiffuseOut.Reset();
                _pDenoisingTexGlossyOut.Reset();
                _pDenoisingTexMotionVectors.Reset();
                _pTexValidation.Reset();
                _pTexDemodDiffuse.Reset();
                _pTexDemodSpecular.Reset();
                _pTexGuideNormalRoughness.Reset();
            }

            // Define resource formats for denoising textures.
            const DXGI_FORMAT kDepthViewFormat          = DXGI_FORMAT_R32_FLOAT;
            // Must match NRD_NORMAL_ENCODING in Scripts/installExternals.py.
            const DXGI_FORMAT kNormalRoughnessFormat    = DXGI_FORMAT_R10G10B10A2_UNORM;
            const DXGI_FORMAT kBaseColorMetalnessFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            const DXGI_FORMAT kDiffuseFormat            = DXGI_FORMAT_R16G16B16A16_FLOAT;
            const DXGI_FORMAT kGlossyFormat             = DXGI_FORMAT_R16G16B16A16_FLOAT;

            // Create the denoising textures, each with unordered access.
            const uvec2& dims       = _renderDimensions;
            _pDenoisingTexDepthView = createTexture(dims, kDepthViewFormat, "DepthView", true);
            _pDenoisingTexNormalRoughness =
                createTexture(dims, kNormalRoughnessFormat, "Normal / Roughness", true);
            _pDenoisingTexBaseColorMetalness =
                createTexture(dims, kBaseColorMetalnessFormat, "Base Color / Metalness", true);
            _pDenoisingTexDiffuse    = createTexture(dims, kDiffuseFormat, "Diffuse Input", true);
            _pDenoisingTexGlossy     = createTexture(dims, kGlossyFormat, "Glossy Input", true);
            _pDenoisingTexDiffuseOut = createTexture(dims, kDiffuseFormat, "Diffuse Output", true);
            _pDenoisingTexGlossyOut  = createTexture(dims, kGlossyFormat, "Glossy Output", true);
            // Shared 2.5D motion vectors:
            // - XY are screen-space motion in pixel units
            // - Z is (viewZPrev - viewZ) in view-space units
            // NRD converts XY to UV-space via motionVectorScale and consumes Z directly; FSR/DLSS
            // use only XY.
            _pDenoisingTexMotionVectors =
                createTexture(dims, DXGI_FORMAT_R16G16B16A16_FLOAT, "Motion Vectors", true);
            // NRD's own debug overlay, which requires an RGBA8+ output.
            _pTexValidation =
                createTexture(dims, DXGI_FORMAT_R8G8B8A8_UNORM, "NRD Validation", true);
            // Float rather than 8-bit UNORM: these are divided out of the radiance and multiplied
            // back in, and NRD_MaterialFactors floors them at 0.02, only 5 codes into an 8-bit
            // range.
            _pTexDemodDiffuse =
                createTexture(dims, DXGI_FORMAT_R16G16B16A16_FLOAT, "Demodulation Diffuse", true);
            _pTexDemodSpecular =
                createTexture(dims, DXGI_FORMAT_R16G16B16A16_FLOAT, "Demodulation Specular", true);
            // Signed normal components need a signed format, unlike NRD's UNORM guide buffer.
            _pTexGuideNormalRoughness = createTexture(
                dims, DXGI_FORMAT_R16G16B16A16_FLOAT, "Guide Normal / Roughness", true);
        }
    }

    // If textures have been updated, or the descriptor heap has changed, create UAVs for the
    // textures.
    if ((isTexturesUpdated || _isDescriptorHeapChanged) && _pDenoisingTexDepthView)
    {
        // Flush the renderer to make sure the (unchanged) descriptor heap is not being used.
        if (!_isDescriptorHeapChanged)
        {
            waitForTask();
        }

        // Create UAVs for the denoising textures, immediately after the output-related ones, in the
        // order the descriptor heap layout at the top of this file requires.
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            kOutputDescriptorCount, _handleIncrementSize);
        createUAV(_pDenoisingTexDepthView.Get(), handle);
        createUAV(_pDenoisingTexNormalRoughness.Get(), handle);
        createUAV(_pDenoisingTexBaseColorMetalness.Get(), handle);
        createUAV(_pDenoisingTexDiffuse.Get(), handle);
        createUAV(_pDenoisingTexGlossy.Get(), handle);
        createUAV(_pDenoisingTexMotionVectors.Get(), handle);
        createUAV(_pDenoisingTexDiffuseOut.Get(), handle);
        createUAV(_pDenoisingTexGlossyOut.Get(), handle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE validationHandle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), kValidationDescriptorOffset,
            _handleIncrementSize);
        createUAV(_pTexValidation.Get(), validationHandle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE demodHandle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), kDemodDescriptorOffset,
            _handleIncrementSize);
        createUAV(_pTexDemodDiffuse.Get(), demodHandle);
        createUAV(_pTexDemodSpecular.Get(), demodHandle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE guideNormalHandle(
            _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            kGuideNormalRoughnessDescriptorOffset, _handleIncrementSize);
        createUAV(_pTexGuideNormalRoughness.Get(), guideNormalHandle);
    }

#if defined(ENABLE_DENOISER)
    // Create the denoiser once when first enabled, then keep it alive across a toggle-off for the
    // same reason as the textures above. submitDenoising() checks the option at runtime.
    bool needsDenoiser = isDenoisingRequested() || _pDenoiser != nullptr;
    if (needsDenoiser && _pDenoisingTexDepthView)
    {
        // Create a new denoiser if needed.
        bool isNewDenoiser = false;
        if (!_pDenoiser)
        {
            _pDenoiser = make_unique<Denoiser>(_pDXDevice.Get(), _pCommandQueue.Get(), _taskCount);
            isNewDenoiser = true;
        }

        // (Re-)initialize when the denoiser is new, the dimensions changed, or a prior initialize()
        // failed -- the last case retries each frame until it succeeds.
        if (isNewDenoiser || _isRenderDimensionsChanged || !_pDenoiser->isInitialized())
        {
            // clang-format off
            Denoiser::Textures textures =
            {
                _pDenoisingTexDepthView.Get(),
                _pDenoisingTexNormalRoughness.Get(),
                _pDenoisingTexDiffuse.Get(),
                _pDenoisingTexGlossy.Get(),
                _pDenoisingTexDiffuseOut.Get(),
                _pDenoisingTexGlossyOut.Get(),
                _pDenoisingTexMotionVectors.Get(),
                _pTexValidation.Get()
            };
            // clang-format on
            _pDenoiser->initialize(_renderDimensions, textures);
            // Force a one-shot history clear, so denoise() seeds its previous-frame camera matrices
            // before the first reprojection; stale identity matrices produce NaN.
            if (_pDenoiser->isInitialized())
                _values.setValue(kLabelIsResetHistoryEnabled, true);
        }
    }

#endif // ENABLE_DENOISER

    // Create or destroy the upscaler based on the current upscalerMode property.
    updateUpscalerResources();
}

void PTRenderer::updateUpscalerResources()
{
#if defined(ENABLE_UPSCALER)
    auto const mode = static_cast<Upscaler::Mode>(upscalerMode());

    if (mode != Upscaler::Mode::Off)
    {
        // Compare against the REQUESTED mode, not the live one: the constructor falls back from
        // DLSS to FSR when NGX is unavailable, so the resulting mode would mismatch every frame.
        bool const isCompatible =
            _pUpscaler && _pUpscaler->matches(mode, _renderDimensions, _outputDimensions);

        // A previous failure is latched so a throwing construction is not retried every frame for
        // the same configuration.
        bool const isKnownBad = _upscalerFailedMode == mode &&
            _upscalerFailedRenderDimensions == _renderDimensions &&
            _upscalerFailedDisplayDimensions == _outputDimensions;

        if (!isCompatible && !isKnownBad)
        {
            if (_pUpscaler)
                waitForTask();

            try
            {
                _pUpscaler = make_unique<Upscaler>(_pDXDevice.Get(), _pCommandQueue.Get(), mode,
                    _renderDimensions, _outputDimensions);
                _upscalerFailedMode = Upscaler::Mode::Off;

                // The upscaler's output is the one place in the post-ray-tracing chain that is at
                // display rather than render resolution.
                _pUpscaledTex = createTexture(_outputDimensions, DXGI_FORMAT_R16G16B16A16_FLOAT,
                    "Upscaled", /*isUnorderedAccess=*/true);

                // Expose the upscaled texture to the PostProcessing shader.
                CD3DX12_CPU_DESCRIPTOR_HANDLE h(
                    _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                    kUpscaledDescriptorOffset, _handleIncrementSize);
                createUAV(_pUpscaledTex.Get(), h);
            }
            catch (const exception& e)
            {
                AU_ERROR("Upscaler initialization failed: %s. Upscaling disabled.", e.what());
                _pUpscaler.reset();
                _pUpscaledTex.Reset();
                _upscalerFailedMode              = mode;
                _upscalerFailedRenderDimensions  = _renderDimensions;
                _upscalerFailedDisplayDimensions = _outputDimensions;
                CD3DX12_CPU_DESCRIPTOR_HANDLE h(
                    _pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                    kUpscaledDescriptorOffset, _handleIncrementSize);
                createUAV(nullptr, h);
            }
        }
        else if (_pUpscaledTex && _isDimensionsChanged)
        {
            // The display resolution moved but the upscaler is still compatible, so just resize its
            // output.
            waitForTask();
            _pUpscaledTex = createTexture(_outputDimensions, DXGI_FORMAT_R16G16B16A16_FLOAT,
                "Upscaled", /*isUnorderedAccess=*/true);
            CD3DX12_CPU_DESCRIPTOR_HANDLE h(_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                kUpscaledDescriptorOffset, _handleIncrementSize);
            createUAV(_pUpscaledTex.Get(), h);
        }
    }
    // Mode::Off deliberately does nothing: the upscaler and its NGX/FSR runtime state are kept
    // alive across a toggle to Off, so re-enabling does not pay for a rebuild.
#endif // ENABLE_UPSCALER
}

void PTRenderer::prepareRayDispatch(D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc)
{
    // Prepare a ray dispatch description, including the dimensions...
    // NOTE: this is the RENDER resolution, below the display resolution whenever a vendor upscaler
    // runs at a non-Native quality preset.
    dispatchRaysDesc.Width  = _renderDimensions.x;
    dispatchRaysDesc.Height = _renderDimensions.y;
    dispatchRaysDesc.Depth  = 1;

    // ... the shader table for the ray generation shader...
    D3D12_GPU_VIRTUAL_ADDRESS address = _rayGenShaderTable.pGPUBuffer->GetGPUVirtualAddress();
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = address;
    dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes  = _rayGenShaderTableSize;

    // ... the shader table for the miss shader(s)...
    size_t missShaderRecordStride  = 0;
    uint32_t missShaderRecordCount = 0;
    ID3D12ResourcePtr pMissShaderTable =
        dxScene()->getMissShaderTable(missShaderRecordStride, missShaderRecordCount);
    dispatchRaysDesc.MissShaderTable.StartAddress  = pMissShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.MissShaderTable.StrideInBytes = missShaderRecordStride;
    dispatchRaysDesc.MissShaderTable.SizeInBytes   = missShaderRecordStride * missShaderRecordCount;

    // ... the shader table for the hit group(s).
    // NOTE: If there are no instances, the hit group shader table will be nullptr.
    size_t hitGroupShaderRecordStride  = 0;
    uint32_t hitGroupShaderRecordCount = 0;
    ID3D12ResourcePtr pHitGroupShaderTable =
        dxScene()->getHitGroupShaderTable(hitGroupShaderRecordStride, hitGroupShaderRecordCount);
    dispatchRaysDesc.HitGroupTable.StartAddress =
        pHitGroupShaderTable ? pHitGroupShaderTable->GetGPUVirtualAddress() : 0;
    dispatchRaysDesc.HitGroupTable.StrideInBytes = hitGroupShaderRecordStride;
    dispatchRaysDesc.HitGroupTable.SizeInBytes =
        hitGroupShaderRecordStride * hitGroupShaderRecordCount;
}

void PTRenderer::submitRayDispatch(
    const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc, uint32_t sampleIndex, uint32_t seedOffset)
{
    // Begin a command list.
    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 0: start-of-frame timestamp (before any GPU work for this frame).
    if (_pTimestampHeap)
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
#endif

    // Prepare the descriptor heaps for CBV/SRV/UAV and for samplers.
    ID3D12DescriptorHeap* pDescriptorHeaps[] = { _pDescriptorHeap.Get(),
        _pSamplerDescriptorHeap.Get() };

    // Prepare the root signature, and pipeline state.
    pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
    pCommandList->SetComputeRootSignature(shaderLibrary().globalRootSignature().Get());
    pCommandList->SetPipelineState1(shaderLibrary().pipelineState().Get());

    // Set the global root signature arguments.
    // Must match the root signature defined in PTShaderLibrary::initRootSignatures and the GPU
    // version in GlobalRootSignature.slang.
    {
        // 0) The acceleration structure.
        D3D12_GPU_VIRTUAL_ADDRESS accelStructureAddress =
            dxScene()->accelerationStructure()->GetGPUVirtualAddress();
        pCommandList->SetComputeRootShaderResourceView(0, accelStructureAddress);

        // 1) The sample index, as a root constant.
        SampleData sampleData = { sampleIndex, seedOffset };
        pCommandList->SetComputeRoot32BitConstants(1, 2, &sampleData, 0);

        // 2) The frame data constant buffer, for the current task index.
        D3D12_GPU_VIRTUAL_ADDRESS frameDataBufferAddress =
            _frameDataBuffer.pGPUBuffer->GetGPUVirtualAddress() + _FRAME_DATA_SIZE * _taskIndex;
        pCommandList->SetComputeRootConstantBufferView(2, frameDataBufferAddress);

        // 3) The environment data constant buffer.
        ID3D12Resource* pEnvironmentDataBuffer = dxScene()->environment()->buffer();
        D3D12_GPU_VIRTUAL_ADDRESS environmentDataBufferAddress =
            pEnvironmentDataBuffer->GetGPUVirtualAddress();
        pCommandList->SetComputeRootConstantBufferView(3, environmentDataBufferAddress);

        // 4) The environment alias map structured buffer.
        // NOTE: Structured buffers must be specified with an SRV, not a CBV.
        ID3D12Resource* pEnvironmentAliasMapBuffer = dxScene()->environment()->aliasMap();
        D3D12_GPU_VIRTUAL_ADDRESS environmentAliasMapBufferAddress =
            pEnvironmentAliasMapBuffer ? pEnvironmentAliasMapBuffer->GetGPUVirtualAddress() : 0;
        pCommandList->SetComputeRootShaderResourceView(4, environmentAliasMapBufferAddress);

        // 5) The environment texture descriptor table.
        // NOTE: This starts right after the renderer's descriptors, as defined by the scene.
        CD3DX12_GPU_DESCRIPTOR_HANDLE handle(
            _pDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        handle.Offset(kDescriptorCount, _handleIncrementSize);
        pCommandList->SetComputeRootDescriptorTable(5, handle);

        // 6) The ground plane data constant buffer.
        ID3D12Resource* pGroundPlaneDataBuffer = dxScene()->groundPlane()->buffer();
        D3D12_GPU_VIRTUAL_ADDRESS groundPlaneDataBufferAddress =
            pGroundPlaneDataBuffer->GetGPUVirtualAddress();
        pCommandList->SetComputeRootConstantBufferView(6, groundPlaneDataBufferAddress);

        // 7) The null acceleration structure (used for layer material shading).
        D3D12_GPU_VIRTUAL_ADDRESS nullAccelStructAddress = 0;
        pCommandList->SetComputeRootShaderResourceView(7, nullAccelStructAddress);

        // 8) The global material buffer
        pCommandList->SetComputeRootShaderResourceView(
            8, dxScene()->globalMaterialBuffer().pGPUBuffer->GetGPUVirtualAddress());

        // 9) The global instance buffer
        pCommandList->SetComputeRootShaderResourceView(
            9, dxScene()->globalInstanceBuffer().pGPUBuffer->GetGPUVirtualAddress());

        // 10) The layer geometry buffer
        pCommandList->SetComputeRootShaderResourceView(
            10, dxScene()->layerGeometryBuffer().pGPUBuffer->GetGPUVirtualAddress());

        // 11) The transform matrix buffer
        pCommandList->SetComputeRootShaderResourceView(
            11, dxScene()->transformMatrixBuffer().pGPUBuffer->GetGPUVirtualAddress());

        // 12) The global material texture array
        handle.Offset(dxScene()->environment()->descriptorCount(), _handleIncrementSize);
        pCommandList->SetComputeRootDescriptorTable(12, handle);

        // 12) The global material 3D texture array
        if (dxScene()->numActiveMaterialTextures3D() > 0)
        {
            handle.Offset(dxScene()->numActiveMaterialTextures(), _handleIncrementSize);
            pCommandList->SetComputeRootDescriptorTable(13, handle);
        }

        // 14) The global material sampler array
        CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(
            _pSamplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        pCommandList->SetComputeRootDescriptorTable(14, samplerHandle);

        // 15) AOV output images
        CD3DX12_GPU_DESCRIPTOR_HANDLE handle2(
            _pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), kDirectDescriptorOffset,
            _handleIncrementSize);
        pCommandList->SetComputeRootDescriptorTable(15, handle2);

        // 16) The guide block: the two material demodulation factors and the plain normal /
        // roughness. Its own table, as the slots immediately after the AOV range are already spoken
        // for by the denoiser's outputs.
        CD3DX12_GPU_DESCRIPTOR_HANDLE guideHandle(
            _pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), kDemodDescriptorOffset,
            _handleIncrementSize);
        pCommandList->SetComputeRootDescriptorTable(16, guideHandle);
    }

    // Launch the ray generation shader with the dispatch, which performs path tracing.
    // NOTE: Rendering performance is somewhat choppy if you try to perform multiple dispatches
    // in a single command list. For that reason, there is a separate command list for each path
    // tracing sample (dispatch). That means each command list has mostly redundant setup (above),
    // but that is still a better runtime experience.
    // EXCEPTION: while denoising, the list is left open for NRD to record into and
    // submitDenoising() submits it.
    pCommandList->DispatchRays(&dispatchRaysDesc);

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 1: end of DispatchRays (ray tracing complete).
    // When denoising is off, also record slot 2 so NRD reads as 0 ms in the log.
    if (_pTimestampHeap)
    {
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        if (!isDenoisingRequested())
            pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
    }
#endif

    if (!isDenoisingRequested())
        submitCommandList();
    // else: leave command list open for NRD to record into it
}

void PTRenderer::submitDenoising(bool isRestartRequested)
{
#if defined(ENABLE_DENOISER)
    // Skip when NRD is unavailable/off or DLSS Ray Reconstruction consumes undenoised lobes.
    // Must match updateFrameData()'s wantsNRDThisFrame to avoid incorrect radiance output.
    // submitRayDispatch() left the command list open, so this path must submit it.
    if (!_pDenoiser || !_pDenoiser->isInitialized() || !isDenoisingRequested() ||
        isRayReconstructionActive())
    {
        if (_isCommandListOpen)
            submitCommandList();
        return;
    }

    // NRD must record into the SAME command list as the preceding DispatchRays: its barriers are
    // validated against the per-list resource state view, which does not reflect UAV writes made in
    // a different list, and a split produces D3D12 structural errors.
    assert(_isCommandListOpen);

    // Prepare denoiser state using the MAIN (already-open) command list.
    Denoiser::DenoiserState denoiserState;
    denoiserState.isRestart         = isRestartRequested;
    denoiserState.taskIndex         = _taskIndex;
    denoiserState.cameraView        = _cameraView;
    denoiserState.cameraProj        = _cameraProj;
    denoiserState.cameraJitter      = _frameData.cameraJitter;
    denoiserState.cameraJitterPrev  = _frameData.cameraJitterPrev;
    denoiserState.timeDeltaMs       = _frameData.timeDeltaMs;
    // The validation overlay is an extra full-screen pass. Turn it off by default.
    denoiserState.enableValidation  = _values.asInt(kLabelDebugMode) == kDebugModeValidation;
    denoiserState.denoisingRange    = denoisingRange();
    denoiserState.pCommandList      = _pCommandList.Get();
    denoiserState.pCommandAllocator = _commandAllocators[_taskIndex].Get();

    // Returns false if NRD recorded nothing (e.g. the first-frame warmup), in which case the frame
    // must not count as a successful submission or accumulation would composite unwritten outputs.
    bool const nrdRan = _pDenoiser->denoise(denoiserState);

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 2: end of NRD denoising stage.
    if (_pTimestampHeap)
        _pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
#endif

    // Submit regardless: even on the warmup frame the list still holds the DispatchRays.
    submitCommandList();

    _nrdSubmittedThisFrame = nrdRan;
#else
    // No denoiser in this build; the composite falls back to the raw path traced result.
    (void)isRestartRequested;
    if (_isCommandListOpen)
    {
        submitCommandList();
    }
    _nrdSubmittedThisFrame = false;
#endif // ENABLE_DENOISER
}

#if defined(ENABLE_UPSCALER)
// Derives the camera near plane, far plane and vertical field of view from a projection matrix,
// which FSR needs for its disocclusion and lock heuristics. Aurora builds its projection with
// glm::perspective, i.e. right-handed with clip depth in [-1, 1], for which:
//     proj[2][2] = -(far + near) / (far - near)
//     proj[3][2] = -2 * far * near / (far - near)
//     proj[1][1] = 1 / tan(fovY / 2)
static void decomposeProjection(const mat4& proj, float& nearPlane, float& farPlane, float& fovY)
{
    bool const isOrtho = proj[3][3] == 1.0f;
    if (isOrtho || proj[1][1] == 0.0f)
    {
        // An orthographic projection has no field of view and no perspective near/far in the sense
        // FSR means, so supply harmless finite values.
        nearPlane = 0.1f;
        farPlane  = 1000.0f;
        fovY      = 0.7854f; // 45 degrees
        return;
    }

    float const denomNear = proj[2][2] - 1.0f;
    float const denomFar  = proj[2][2] + 1.0f;
    nearPlane             = denomNear != 0.0f ? proj[3][2] / denomNear : 0.1f;
    farPlane              = denomFar != 0.0f ? proj[3][2] / denomFar : 1000.0f;
    fovY                  = 2.0f * std::atan(1.0f / proj[1][1]);

    // Guard against a degenerate or reversed decomposition reaching the vendor SDK.
    nearPlane = glm::max(nearPlane, 1e-6f);
    farPlane  = glm::max(farPlane, nearPlane * 1.0001f);
    fovY      = glm::clamp(fovY, 1e-3f, 3.1f);
}

// Converts one of Aurora's supported distance-unit names to metres. FSR uses this to interpret
// view-space distances physically.
static float metersPerSceneUnit(const string& unit)
{
    if (unit == "nanometer")
        return 1e-9f;
    if (unit == "micron" || unit == "micrometer")
        return 1e-6f;
    if (unit == "millimeter")
        return 1e-3f;
    if (unit == "centimeter")
        return 1e-2f;
    if (unit == "kilometer")
        return 1e3f;
    if (unit == "inch")
        return 0.0254f;
    if (unit == "foot")
        return 0.3048f;
    if (unit == "yard")
        return 0.9144f;
    if (unit == "mile")
        return 1609.344f;
    return 1.0f; // "meter", and anything unrecognized
}

void PTRenderer::submitUpscaling(bool isRestartRequested)
{
    _didUpscaleThisFrame = false;

    if (!_pUpscaler || !_pUpscaler->isInitialized())
        return;

    // _pUpscaler is kept alive across upscalerMode toggling to Off.
    if (!isUpscalerRequested())
        return;

    // DLSS and FSR require depth and motion vectors for temporal reprojection.
    if (!_pTexDepthNDC || !_pDenoisingTexMotionVectors)
        return;

    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

    // Ensure every input the upscaler reads is complete.
    addUAVBarrier(_pTexAccumulation.Get());
    addUAVBarrier(_pTexDepthNDC.Get());
    addUAVBarrier(_pDenoisingTexMotionVectors.Get());

    Upscaler::Textures textures;
    textures.pColor         = _pTexAccumulation.Get();
    textures.pOutput        = _pUpscaledTex.Get();
    textures.pDepth         = _pTexDepthNDC.Get();
    textures.pMotionVectors = _pDenoisingTexMotionVectors.Get();
    // Ray Reconstruction gets the plain unit-length normal rather than NRD's encoded one, and the
    // same demodulation factors NRD is given: it performs the same demodulation internally.
    textures.pNormalRoughness = _pTexGuideNormalRoughness.Get();
    textures.pDiffuseAlbedo   = _pTexDemodDiffuse.Get();
    textures.pSpecularAlbedo  = _pTexDemodSpecular.Get();
    textures.pDiffuseHitDist  = _pDenoisingTexDiffuse.Get();
    textures.pSpecularHitDist = _pDenoisingTexGlossy.Get();

    Upscaler::FrameState state;
    state.isReset      = isRestartRequested;
    state.cameraJitter = _frameData.cameraJitter;
    state.timeDeltaMs  = _frameData.timeDeltaMs;
    decomposeProjection(_cameraProj, state.cameraNear, state.cameraFar, state.cameraFovY);
    state.metersPerUnit = metersPerSceneUnit(_values.asString(kLabelUnits));

    _pUpscaler->upscale(pCommandList.Get(), textures, state);
    _didUpscaleThisFrame = true;

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 4: end of upscaling stage.
    if (_pTimestampHeap)
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
#endif

    submitCommandList();
}
#else
void PTRenderer::submitUpscaling(bool /*isRestart*/) {}
#endif // ENABLE_UPSCALER

bool PTRenderer::updateAccumulationGPUStruct(uint32_t sampleIndex, Accumulation* pStaging)
{
    Accumulation settings;
    // Prepare accumulation settings.
    settings.sampleIndex = sampleIndex;
    // Composite NRD's output only when the ray generator wrote the direct-only path AND NRD
    // actually produced output, so a freshly reset denoiser is never composited.
    settings.isDenoisedCompositeEnabled =
        (_frameData.isDenoisingEnabled != 0 && _nrdSubmittedThisFrame) ? 1 : 0;
    // Deliberately broader than the composite flag: a vendor upscaler is a temporal reconstructor
    // even on a frame where NRD did not run, and must be handed the raw per-frame signal.
    settings.isTemporalResolveDownstream =
        (settings.isDenoisedCompositeEnabled != 0 || isUpscalerRequested()) ? 1 : 0;
    settings.skyViewZ = _frameData.skyViewZ;
    // Must match the ray generator's demodulation condition exactly, or the composite is left
    // scaled by the material factors (or their reciprocal).
    settings.remodulateMaterials = (isDenoisingAOVsEnabled() && _pTexDemodDiffuse) ? 1 : 0;

    // If there are no changes compared local CPU copy, then do nothing and return false.
    if (memcmp(&_accumData, &settings, sizeof(Accumulation)) == 0)
        return false; // No changes.

    // Update local CPU copy.
    _accumData = settings;

    // Update staging buffer, if one provided.
    if (pStaging)
        *pStaging = settings;

    return true;
}

void PTRenderer::submitAccumulation(uint32_t sampleIndex)
{
    // Prepare accumulation settings.
    updateAccumulationGPUStruct(sampleIndex);

    // Begin a command list.
    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

    // Add a UAV barrier so that the direct texture can't be used until the previous dispatch is
    // complete.
    addUAVBarrier(_pTexDirect.Get());

    // Prepare the pipeline for accumulation: descriptor heap, root signature, and pipeline
    // state.
    pCommandList->SetDescriptorHeaps(1, _pDescriptorHeap.GetAddressOf());
    pCommandList->SetComputeRootSignature(_pAccumulationRootSignature.Get());
    pCommandList->SetPipelineState(_pAccumulationPipelineState.Get());

    // Set the root signature arguments for accumulation: the descriptor table and the accumulation
    // settings. The descriptor table must start with the accumulation texture.
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
        kAccumulationDescriptorOffset, _handleIncrementSize);
    pCommandList->SetComputeRootDescriptorTable(0, handle);
    pCommandList->SetComputeRoot32BitConstants(1, sizeof(Accumulation) / 4, &_accumData, 0);

    // Dispatch the accumulation shader, which performs (optional) deferred shading and merges new
    // path tracing samples with the previous results.
    // NOTE: The dispatch is performed with thread group dimensions that provide good occupancy for
    // the current compute shader code. This must match the values in the compute shader.
    constexpr uvec2 kThreadGroupCount(16, 8);
    pCommandList->Dispatch((_renderDimensions.x + kThreadGroupCount.x - 1) / kThreadGroupCount.x,
        (_renderDimensions.y + kThreadGroupCount.y - 1) / kThreadGroupCount.y, 1);

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 3: end of accumulation stage.
    if (_pTimestampHeap)
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
#endif

    // Submit the command list.
    submitCommandList();
}

void PTRenderer::submitTemporalResolve(bool isRestartRequested)
{
    _didTemporalResolveThisFrame = false;

    if (!isTemporalResolveActive() || !_pTexTAAHistory[0] || !_pDenoisingTexMotionVectors)
    {
        // The next enabled frame must start clean rather than blend against a stale texture.
        _hasTAAHistory = false;
        return;
    }

    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

    // Ensure the accumulation writes this pass reads are complete.
    addUAVBarrier(_pTexAccumulation.Get());

    TemporalResolveSettings settings = {};
    settings.historyIndex            = _taaHistoryIndex;
    settings.isReset                 = (isRestartRequested || !_hasTAAHistory) ? 1u : 0u;
    // Feedback weights, scaled with on-screen speed inside the shader. 1/16 is a conventional TAA
    // factor for a near-static camera: long enough to resolve the Halton jitter into stable edges,
    // short enough for the neighbourhood clip to react to real change. A single fixed weight
    // visibly softens the image during a camera orbit, because reprojection resamples the history
    // every frame and the smoothing compounds -- shimmer is what the eye notices when still and
    // blur is what it notices in motion, so the two ends want opposite weights.
    settings.blendAlphaMin = 1.0f / 16.0f;
    settings.blendAlphaMax = 0.6f;
    // Pixels of motion per frame at which the responsive weight is reached.
    settings.speedReference = 6.0f;

    pCommandList->SetDescriptorHeaps(1, _pDescriptorHeap.GetAddressOf());
    pCommandList->SetComputeRootSignature(_pTemporalResolveRootSignature.Get());
    pCommandList->SetPipelineState(_pTemporalResolvePipelineState.Get());

    // The descriptor table starts at the final texture, matching PostProcessing.hlsl's register
    // numbering so the two shaders share one set of slot indices.
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
        kFinalDescriptorOffset, _handleIncrementSize);
    pCommandList->SetComputeRootDescriptorTable(0, handle);
    pCommandList->SetComputeRoot32BitConstants(
        1, sizeof(TemporalResolveSettings) / 4, &settings, 0);

    constexpr uvec2 kThreadGroupCount(16, 8);
    pCommandList->Dispatch((_renderDimensions.x + kThreadGroupCount.x - 1) / kThreadGroupCount.x,
        (_renderDimensions.y + kThreadGroupCount.y - 1) / kThreadGroupCount.y, 1);

    submitCommandList();

    // Post-processing must read the texture just written, so record it before flipping. The shader
    // encodes this as 1 => history A, 2 => history B (0 means the pass did not run).
    _temporalResolveSource       = static_cast<int>(_taaHistoryIndex) + 1;
    _didTemporalResolveThisFrame = true;
    _hasTAAHistory               = true;
    _taaHistoryIndex ^= 1u;
}

void PTRenderer::submitPostProcessing()
{
    // Update the post processing GPU struct by calling base class function.
    updatePostProcessingGPUStruct();

    // Begin a command list.
    ID3D12GraphicsCommandList4Ptr pCommandList = beginCommandList();

    // Add a UAV barrier so that the accumulation texture can't be used until the previous dispatch
    // is complete.
    addUAVBarrier(_pTexAccumulation.Get());
    if (_didTemporalResolveThisFrame)
        addUAVBarrier(_pTexTAAHistory[_temporalResolveSource - 1].Get());
#if defined(ENABLE_UPSCALER)
    if (_didUpscaleThisFrame)
        addUAVBarrier(_pUpscaledTex.Get());
#endif

    // Prepare the pipeline for post-processing: descriptor heap, root signature, and pipeline
    // state.
    pCommandList->SetDescriptorHeaps(1, _pDescriptorHeap.GetAddressOf());
    pCommandList->SetComputeRootSignature(_pPostProcessingRootSignature.Get());
    pCommandList->SetPipelineState(_pPostProcessingPipelineState.Get());

    // Set the root signature arguments for post-processing: the descriptor table (at the start of
    // the heap) and the post-processing settings. The descriptor table must start with the final
    // texture.
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
        kFinalDescriptorOffset, _handleIncrementSize);
    pCommandList->SetComputeRootDescriptorTable(0, handle);
    // Correct the base-class settings for what this frame actually did: RendererBase only knows
    // what was requested. The denoised debug AOVs exist only where the denoiser ran, and the shader
    // must read gAccumulation rather than an unwritten gUpscaled when the upscaler was skipped.
    PostProcessing ppData        = _postProcessingData;
    ppData.temporalResolveSource = _didTemporalResolveThisFrame ? _temporalResolveSource : 0;
    ppData.isDenoisingEnabled    = (isDenoisingRequested() && !isRayReconstructionActive()) ? 1 : 0;
    ppData.renderWidth           = static_cast<int>(_renderDimensions.x);
    ppData.renderHeight          = static_cast<int>(_renderDimensions.y);
    ppData.writeDisplayDepth     = _pTexDepthNDCDisplay ? 1 : 0;
#if defined(ENABLE_UPSCALER)
    if (!_didUpscaleThisFrame)
        ppData.upscalerMode = 0;
#endif
    pCommandList->SetComputeRoot32BitConstants(1, sizeof(PostProcessing) / 4, &ppData, 0);

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 4: start of post-processing stage (= end of upscaling stage).
    // When upscaling ran, slot 4 was already recorded in submitUpscaling().
    // When not: record it here BEFORE the Dispatch so ms(3,4)=upscale=0ms.
    if (_pTimestampHeap)
    {
#if defined(ENABLE_UPSCALER)
        if (!_didUpscaleThisFrame)
            pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
#else  // ENABLE_UPSCALER
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
#endif // ENABLE_UPSCALER
    }
#endif // AU_DEV_PERFORMANCE_LOGGING

    // Dispatch the post-processing shader, which tone maps(as needed) the accumulation texture
    // (HDR) to the final texture (usually SDR).
    // NOTE: This should be done even if the settings mean no post-processing is performed as there
    // is still an implicit format conversion, from the accumulation texture (UAV) format to the
    // output texture format, e.g. floating-point to integer.
    // NOTE: The dispatch is performed with thread group dimensions that provide good occupancy for
    // the current compute shader code. This must match the values in the compute shader.
    constexpr uvec2 kThreadGroupCount(16, 8);
    pCommandList->Dispatch((_outputDimensions.x + kThreadGroupCount.x - 1) / kThreadGroupCount.x,
        (_outputDimensions.y + kThreadGroupCount.y - 1) / kThreadGroupCount.y, 1);

#if AU_DEV_PERFORMANCE_LOGGING
    // Slot 5: end of post-processing. Resolve all slots to the CPU-readable readback buffer.
    if (_pTimestampHeap && _pTimestampReadback)
    {
        pCommandList->EndQuery(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 5);
        pCommandList->ResolveQueryData(_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
            kTimestampSlots, _pTimestampReadback.Get(), 0);
    }
#endif

    // Copy the output textures to their associated targets, if any.
    copyTextureToTarget(_pTexFinal.Get(), _pTargetFinal.get());
    if (_pTargetDepthNDC && _pTexDepthNDCDisplay)
    {
        // From the display-resolution copy post-processing just wrote, not the render-resolution
        // buffer: CopyTextureRegion needs matching dimensions as well as matching formats.
        if (PTImage::getDXFormat(_pTargetDepthNDC->format()) == DXGI_FORMAT_R32_FLOAT)
        {
            copyTextureToTarget(_pTexDepthNDCDisplay.Get(), _pTargetDepthNDC.get());
        }
        else if (!_didWarnDepthTargetFormat)
        {
            // One-shot latch so a depth AOV target with an unusable format is reported once.
            _didWarnDepthTargetFormat = true;
            AU_WARN(
                "The depth (NDC) AOV target must use ImageFormat::Float_R; the target supplied "
                "uses a different format, so depth output is skipped.");
        }
    }

    // Submit the command list.
    submitCommandList();
}

void PTRenderer::createUAV(ID3D12Resource* pTexture, CD3DX12_CPU_DESCRIPTOR_HANDLE& handle)
{
    // For null textures: use an arbitrary format for the null descriptor.
    static D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescNull = {};
    uavDescNull.Format                                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDescNull.ViewDimension                           = D3D12_UAV_DIMENSION_TEXTURE2D;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D12_UNORDERED_ACCESS_VIEW_DESC* pUAVDesc;

    if (pTexture)
    {
        // Derive the format from the resource to satisfy D3D12 validation (Format=UNKNOWN is
        // rejected for typed resources by the debug layer).
        uavDesc.Format        = pTexture->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        pUAVDesc              = &uavDesc;
    }
    else
    {
        pUAVDesc = &uavDescNull;
    }

    // Create a valid or null descriptor for the texture, depending on whether the texture is
    // null. Then offset the handle by the increment, for creating a subsequent descriptor.
    _pDXDevice->CreateUnorderedAccessView(pTexture, nullptr, pUAVDesc, handle);
    handle.Offset(_handleIncrementSize);
}

void PTRenderer::copyTextureToTarget(ID3D12Resource* pTexture, PTTarget* pTarget)
{
    // Transition the final texture from writing (rendering to copying), then copy the final
    // texture to the target, and then transition back.
    addTransitionBarrier(
        pTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    pTarget->copyFromResource(pTexture, _pCommandList.Get());
    addTransitionBarrier(
        pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

bool PTRenderer::isTemporalResolveActive() const
{
    // Only meaningful when NRD is the last temporal stage. With no denoiser the renderer is in
    // progressive-refinement mode with stochastic AA jitter, which already resolves itself; with a
    // vendor upscaler the upscaler does this job and must be given the raw jittered signal.
    return _values.asBoolean(kLabelIsTemporalResolveEnabled) && isDenoisingRequested() &&
        !isUpscalerRequested();
}

bool PTRenderer::isRayReconstructionActive() const
{
#if defined(ENABLE_UPSCALER)
    if (upscalerMode() != static_cast<int>(Upscaler::Mode::DLSS_RR))
        return false;

    // Requested is not the same as running: Upscaler's constructor falls back to FSR when NGX is
    // unavailable, and construction can fail outright. In either case NRD must still run, or the
    // fallback would silently render undenoised.
    return _pUpscaler && _pUpscaler->isInitialized() &&
        _pUpscaler->mode() == Upscaler::Mode::DLSS_RR;
#else
    return false;
#endif
}

bool PTRenderer::isDenoisingRequested() const
{
#if defined(ENABLE_DENOISER)
    // DLSS and FSR Super Resolution are temporal reconstructors, not denoisers: on a raw 1 spp
    // image they reject history nearly everywhere and return the noise, measured 4.3x less stable
    // than with denoising on -- so they imply denoising. Ray Reconstruction is the exception: it
    // replaces the denoiser and wants the noise.
    if (isUpscalerRequested() && !isRayReconstructionActive())
    {
        return true;
    }

    return _values.asBoolean(kLabelIsDenoisingEnabled);
#else
    return false;
#endif // ENABLE_DENOISER
}

IRenderer::UpscalerSupport PTRenderer::upscalerSupport(const string& mode)
{
    auto const cached = _upscalerSupportCache.find(mode);
    if (cached != _upscalerSupportCache.end())
    {
        return cached->second;
    }

    // An unrecognized name is not a mode anything can run, report it unsupported.
    int const modeValue = upscalerModeFromName(mode);
    if (modeValue == kUpscalerModeNone && mode != Names::UpscalerModes::kNone)
    {
        _upscalerSupportCache[mode] = {};
        return {};
    }

#if defined(ENABLE_UPSCALER)
    IRenderer::UpscalerSupport const support =
        Upscaler::querySupport(*_pDevice, static_cast<Upscaler::Mode>(modeValue));
#else
    // Nothing but "no upscaling" can work when none is compiled in.
    IRenderer::UpscalerSupport const support = { modeValue == kUpscalerModeNone, false, 0, 0 };
#endif // ENABLE_UPSCALER

    _upscalerSupportCache[mode] = support;
    return support;
}

// The upscaler mode in effect, as an Upscaler::Mode value. Always Off without upscaler support.
int PTRenderer::upscalerMode() const
{
#if defined(ENABLE_UPSCALER)
    return upscalerModeFromName(_values.asString(kLabelUpscalerMode));
#else
    // Upscaler.h is not included in this configuration, so no Upscaler:: name may appear
    // outside a guard. Reporting Off here keeps every caller guard-free.
    return 0;
#endif // ENABLE_UPSCALER
}

// The per-dimension render scale in effect (always >= 1.0).
float PTRenderer::effectiveRenderScale() const
{
#if defined(ENABLE_UPSCALER)
    // Reducing the render resolution only makes sense when a vendor upscaler is there to
    // reconstruct it. Aurora's own temporal resolve is an anti-aliasing filter, not an
    // upscaler, so without one a reduced resolution would just be a smaller, stretched image.
    if (!isUpscalerRequested())
    {
        return 1.0f;
    }

    return Upscaler::qualityScale(
        static_cast<Upscaler::Quality>(_values.asInt(kLabelUpscalerQuality)));
#else
    return 1.0f;
#endif // ENABLE_UPSCALER
}

// Whether ANY stage downstream of ray generation resolves the sub-pixel camera jitter.
bool PTRenderer::hasTemporalResolve() const
{
    return isUpscalerRequested() || isTemporalResolveActive();
}

// The debug modes above kDebugModeErrors visualize one of the guide buffers directly.
bool PTRenderer::isDenoisingAOVsEnabled() const
{
    return isDenoisingRequested() || isUpscalerRequested() ||
        _values.asInt(kLabelDebugMode) > kDebugModeErrors;
}

END_AURORA
