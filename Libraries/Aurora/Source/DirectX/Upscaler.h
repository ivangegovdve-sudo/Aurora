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

#include "PTDevice.h"
#include "RendererBase.h"

BEGIN_AURORA

// Wraps NVIDIA DLSS (Super Resolution and Ray Reconstruction) and AMD FidelityFX Super Resolution.
// Renders at renderDimensions() and reconstructs to displayDimensions(); equal dimensions
// degenerate to pure anti-aliasing (DLAA / FSR native-AA) with no performance benefit.
class Upscaler
{
public:
    enum class Mode
    {
        // Upscaler disabled.
        Off = kUpscalerModeNone,
        // NVIDIA DLSS 4 Super Resolution (requires an NVIDIA GPU and the NGX runtime).
        DLSS4 = kUpscalerModeDLSS,
        // AMD FidelityFX Super Resolution.
        FSR = kUpscalerModeFSR,
        // NVIDIA DLSS Ray Reconstruction: denoise + upscale + AA in one network.
        DLSS_RR = kUpscalerModeDLSSRayReconstruction,
    };

    // Matches kLabelUpscalerQuality values in RendererBase.h. Ratios are the per-dimension scale
    // factors both vendors document for these names, so one set covers DLSS and FSR alike.
    enum class Quality
    {
        Native      = 0, // 1.0x  - anti-aliasing only (DLAA / FSR native-AA). No speed benefit.
        Quality     = 1, // 1.5x
        Balanced    = 2, // 1.7x
        Performance = 3, // 2.0x
    };

    struct Textures
    {
        // Required by every mode. Inputs are at render resolution, output at display resolution.
        ID3D12Resource* pColor         = nullptr; // HDR radiance input.
        ID3D12Resource* pOutput        = nullptr; // Upscaled HDR output.
        ID3D12Resource* pDepth         = nullptr; // NDC depth [0,1], non-reversed.
        ID3D12Resource* pMotionVectors = nullptr; // Screen-space motion, in render pixels.

        // Guide buffers required by DLSS Ray Reconstruction only.
        ID3D12Resource* pDiffuseAlbedo   = nullptr;
        ID3D12Resource* pSpecularAlbedo  = nullptr;
        ID3D12Resource* pNormalRoughness = nullptr; // Roughness packed in .w.
        ID3D12Resource* pDiffuseHitDist  = nullptr;
        ID3D12Resource* pSpecularHitDist = nullptr;
    };

    // Per-frame non-texture inputs.
    struct FrameState
    {
        bool isReset        = false;
        vec2 cameraJitter   = vec2(0.0f); // Render-resolution pixels, in [-0.5, 0.5].
        float timeDeltaMs   = 0.0f;
        float cameraNear    = 0.1f;
        float cameraFar     = 1000.0f;
        float cameraFovY    = 0.7854f; // Radians.
        float metersPerUnit = 0.01f;   // Scene unit -> metre conversion.
    };

    // Whether a mode is usable on this device, per the vendor runtime rather than the GPU vendor
    // ID. Expensive on the first DLSS call (initializes NGX); PTRenderer caches the answer.
    static IRenderer::UpscalerSupport querySupport(PTDevice& device, Mode mode);

    // The per-dimension scale factor for a quality preset (>= 1.0). Fixed vendor-published ratios
    // rather than an NGX query, which would need NGX initialized before the render resolution is
    // known - and that must be known before any resource is created. Both APIs accept an arbitrary
    // in-range render size, so the two are equivalent in practice.
    static float qualityScale(Quality quality);

    // Throws std::exception on initialization failure. "requestedMode" is retained separately from
    // the mode actually initialized, so a caller comparing its request against mode() does not see
    // a permanent mismatch (and rebuild every frame) when DLSS falls back to FSR.
    Upscaler(ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, Mode requestedMode,
        const uvec2& renderDimensions, const uvec2& displayDimensions);
    ~Upscaler();

    void upscale(ID3D12GraphicsCommandList4* pCommandList, const Textures& textures,
        const FrameState& state);

    // The mode asked for, which may differ from the one actually running.
    Mode requestedMode() const { return _requestedMode; }
    Mode mode() const { return _mode; }
    const uvec2& renderDimensions() const { return _renderDimensions; }
    const uvec2& displayDimensions() const { return _displayDimensions; }
    bool isInitialized() const { return _isInitialized; }

    // Whether this instance can serve the request without being rebuilt.
    bool matches(
        Mode requestedMode, const uvec2& renderDimensions, const uvec2& displayDimensions) const
    {
        return _requestedMode == requestedMode && _renderDimensions == renderDimensions &&
            _displayDimensions == displayDimensions;
    }

private:
    void initDLSS(
        ID3D12Device5* pDevice, ID3D12CommandQueue* pCommandQueue, bool isRayReconstruction);
    void initFSR(ID3D12Device5* pDevice);
    void destroyDLSS();
    void destroyFSR();

    Mode _requestedMode = Mode::Off;
    Mode _mode          = Mode::Off;
    uvec2 _renderDimensions;
    uvec2 _displayDimensions;
    bool _isInitialized = false;

    // Vendor state.
    struct NGXState;
    unique_ptr<NGXState> _pNGX;
    struct FSRState;
    unique_ptr<FSRState> _pFSR;
};

END_AURORA
