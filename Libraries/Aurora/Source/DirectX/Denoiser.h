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

BEGIN_AURORA

// A class for simplifying the use of the NRD denoiser.
class Denoiser
{
public:
    /*** Types ***/

    // A set of input / output textures, as D3D12 resources.
    struct Textures
    {
        ID3D12Resource* pDepthView       = nullptr;
        ID3D12Resource* pNormalRoughness = nullptr;
        ID3D12Resource* pDiffuse         = nullptr;
        ID3D12Resource* pGlossy          = nullptr;
        ID3D12Resource* pDiffuseOut      = nullptr;
        ID3D12Resource* pGlossyOut       = nullptr;
        ID3D12Resource* pMotionVectors   = nullptr;
        // NRD's own debug overlay (RGBA8 or better), written when enableValidation is set.
        ID3D12Resource* pValidation = nullptr;
    };

    // Per-frame state needed to run the denoiser.
    struct DenoiserState
    {
        bool isRestart     = false;
        uint32_t taskIndex = 0;
        mat4 cameraView;
        mat4 cameraProj;
        vec2 cameraJitter     = vec2(0.0f);
        vec2 cameraJitterPrev = vec2(0.0f);
        float timeDeltaMs     = 0.0f;
        bool enableValidation = false;
        // The range within which a pixel counts as geometry rather than background.
        float denoisingRange = 1.0f;
        ID3D12GraphicsCommandList* pCommandList;
        ID3D12CommandAllocator* pCommandAllocator;
    };

    // The user-supplied resource types, i.e. everything up to and including OUT_VALIDATION. The
    // two entries excluded are NRD's own TRANSIENT_POOL and PERMANENT_POOL, which sit at the end
    // of the enum immediately before MAX_NUM.
    static constexpr uint32_t NRD_USER_POOL_SIZE = (uint32_t)nrd::ResourceType::MAX_NUM - 2;

    /*** Lifetime Management ***/

    Denoiser(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, uint32_t taskCount);
    ~Denoiser();

    /*** Functions ***/

    void initialize(const uvec2& dimensions, const Textures& textures);
    bool isInitialized() const { return _isInitialized; }
    // Returns true if NRD commands were actually recorded, false if the frame was skipped.
    bool denoise(const DenoiserState& state);

private:
    /*** Private Functions ***/

    void initSettings();
    void updateRelaxSettings(const DenoiserState& state);
    void setTextureEntry(nrd::ResourceType type, ID3D12Resource* pTexture);

    /*** Private Variables ***/

    bool _isInitialized = false;
    uvec2 _dimensions;
    uint32_t _taskCount                = 3;
    uint32_t _frameNumber              = 0;
    ID3D12Device5* _pD3DDevice         = nullptr;
    ID3D12CommandQueue* _pCommandQueue = nullptr;
    mat4 _cameraViewPrev;
    mat4 _cameraProjPrev;
    nrd::Integration _nrd;
    // D3D12 resource pointers indexed by nrd::ResourceType, populated by setTextureEntry() and
    // handed to DenoiseD3D12() each frame.
    array<ID3D12Resource*, NRD_USER_POOL_SIZE> _nrdTexturesD3D12 = {};
    nrd::CommonSettings _nrdSettings;
    nrd::RelaxSettings _relaxSettings;
    
    // NRI resources.
    nri::DeviceCreationD3D12Desc _nriDeviceCreationDesc = {};
    nri::QueueFamilyD3D12Desc _nriQueueFamily           = {};
};

END_AURORA
