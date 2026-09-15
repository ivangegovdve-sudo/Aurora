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

#include "AssetManager.h"
#include "Properties.h"
#include "SceneBase.h"

#if ENABLE_MATERIALX && ENABLE_MDL
#include "MdlSdk.h"
#endif

BEGIN_AURORA

class SceneBase;

// Property names as constants.
static const string kLabelIsResetHistoryEnabled       = "isResetHistoryEnabled";
static const string kLabelIsDenoisingEnabled          = "isDenoisingEnabled";
static const string kLabelIsDiffuseOnlyEnabled        = "isDiffuseOnlyEnabled";
static const string kLabelDebugMode                   = "debugMode";
static const string kLabelMaxLuminance                = "maxLuminance";
static const string kLabelTraceDepth                  = "traceDepth";
static const string kLabelIsToneMappingEnabled        = "isToneMappingEnabled";
static const string kLabelIsGammaCorrectionEnabled    = "isGammaCorrectionEnabled";
static const string kLabelIsAlphaEnabled              = "alphaEnabled";
static const string kLabelBrightness                  = "brightness";
static const string kLabelUnits                       = "units";
static const string kLabelImportanceSamplingMode      = "importanceSamplingMode";
static const string kLabelIsFlipImageYEnabled         = "isFlipImageYEnabled";
static const string kLabelIsReferenceBSDFEnabled      = "isReferenceBSDFEnabled";
static const string kLabelIsForceOpaqueShadowsEnabled = "isForceOpaqueShadowsEnabled";
static const string kLabelIsRussianRouletteEnabled    = "isRussianRouletteEnabled";
static const string kLabelRussianRouletteStartDepth   = "russianRouletteStartDepth";
static const string kLabelUpscalerMode                = "upscalerMode";
static const string kLabelUpscalerQuality             = "upscalerQuality";
// Motion-vector temporal resolve uses sub-pixel camera jitter for anti-aliasing.
// Skipped when DLSS or FSR is active.
static const string kLabelIsTemporalResolveEnabled = "isTemporalResolveEnabled";
// When true, MaterialX materials are compiled via the NVIDIA MDL SDK (MaterialX -> MDL -> HLSL).
// When false (default) the existing MaterialX-via-Slang generator is used. Only honored on builds
// where ENABLE_MDL=1 was set at compile time AND when before initiating the scene; ignored
// otherwise.
static const string kLabelOptionUseMDLMaterialGenerator = "useMDLMaterialGenerator";

// The debug modes include:
// - 0 Output (accumulation)
// - 1 Output with Errors
// - 2 View Depth
// - 3 Normal
// - 4 Base Color
// - 5 Roughness
// - 6 Metalness
// - 7 Diffuse
// - 8 Diffuse Hit Distance
// - 9 Glossy
// - 10 Glossy Hit Distance
// - 11 NRD Validation overlay
static const int kDebugModeErrors = 1;
// NRD's validation overlay: per-pixel accumulated frame counts and reprojection quality.
static const int kDebugModeValidation = 11;
static const int kMaxDebugMode        = kDebugModeValidation;

// Frame time assumed before any has been measured. Temporal denoisers and upscalers
// need a plausible number on their very first frame; 60 fps is the conventional guess.
static constexpr float kDefaultFrameTimeMs = 1000.0f / 60.0f;

// Importance sampling mode options as constants.
static const int kImportanceSamplingModeBSDF        = 0;
static const int kImportanceSamplingModeEnvironment = 1;
static const int kImportanceSamplingModeMIS         = 2;

// Upscaler mode values used by the PostProcessing GPU struct and Upscaler::Mode.
// Maps the client-facing Names::UpscalerModes strings to those values.
static const int kUpscalerModeNone                  = 0;
static const int kUpscalerModeDLSS                  = 1;
static const int kUpscalerModeFSR                   = 2;
static const int kUpscalerModeDLSSRayReconstruction = 3;

// Converts an upscaler mode name to its integer value. Treat unknown names as kUpscalerModeNone.
int upscalerModeFromName(const string& name);

// A base class for implementations of IRenderer.
class RendererBase : public IRenderer, public FixedValues
{
public:
    /*** Lifetime Management ***/

    RendererBase(uint32_t activeTaskCount);

    /*** IRenderer Functions ***/

    void setOptions(const Properties& option) override;
    IValues& options() override { return *this; };
    void setCamera(const mat4& view, const mat4& projection, float focalDistance = 1.0f,
        float lensRadius = 0.0f) override;
    void setCamera(
        const float* view, const float* proj, float focalDistance, float lensRadius) override;
    void addMdlSearchPath(const std::string& path) override;
    void setLoadResourceFunction(LoadResourceFunction func) override;

    /*** Functions ***/

    bool isValid() { return _isValid; }
    void valuesToProperties(const FixedValueSet& values, Properties& props) const;
    void propertiesToValues(const Properties& properties, IValues& values);

    unique_ptr<AssetManager>& assetManager() { return _pAssetMgr; }
#if ENABLE_MATERIALX && ENABLE_MDL
    shared_ptr<MdlSdk>& mdlSdk() { return _pMdlSdk; }
#endif

// TODO: Destruction via shared_ptr is not safe, we should have some kind of kill list system, but
// can't seem to get it to work.
#if 0
    void destroyImage(IImagePtr& pImg)
    {
        _imageDestroyList.push_back(pImg);
        pImg.reset();
    }
#endif

    // The max trace depth, for recursion in ray tracing.
    static const int kMaxTraceDepth;

protected:
    virtual ~RendererBase(); // hidden destructor

    // Layout of per-frame parameters.
    // Must match the GPU version Frame.slang.
    struct FrameData
    {
        // The view-projection matrix.
        mat4 cameraViewProj;

        // The previous frame's view-projection matrix.
        mat4 cameraViewProjPrev;

        // The inverse view matrix, also transposed. The *rows* must have the desired vectors:
        // right, up, front, and eye position. HLSL array access with [] returns rows, not columns,
        // hence the need for the matrix to be supplied transposed.
        mat4 cameraInvView;

        // The previous frame's inverse view matrix, also transposed.
        mat4 cameraInvViewPrev;

        // The dimensions of the view (in world units) at a distance of 1.0 from the camera, which
        // is useful to build ray directions.
        vec2 viewSize;

        // Deterministic per-frame camera jitter, in pixel units.
        vec2 cameraJitter = vec2(0.0f);

        // Previous frame's deterministic camera jitter, in pixel units.
        vec2 cameraJitterPrev = vec2(0.0f);

        // Whether the camera is using an orthographic projection. Otherwise a perspective
        // projection is assumed.
        int isOrthoProjection = 0;

        // The distance from the camera for sharpest focus, for depth of field.
        float focalDistance = 0.f;

        // The diameter of the lens for depth of field. If this is zero, there is no depth of field,
        // i.e. pinhole camera.
        float lensRadius = 0.f;

        // Time delta between frames in milliseconds.
        float timeDeltaMs = kDefaultFrameTimeMs;

        // Whether deterministic temporal jitter should be used instead of random AA jitter.
        int isTemporalJitterEnabled = 0;

        // The size of the scene, specifically the maximum distance between any two points in the
        // scene.
        float sceneSize = 0.f;

        // Whether shadow evaluation should treat all objects as opaque, as a performance
        // optimization.
        int isForceOpaqueShadowsEnabled = 0;

        // Whether to write the NDC depth result to an output texture.
        int isDepthNDCEnabled = 0;

        // Whether to render the diffuse material component only.
        int isDiffuseOnlyEnabled = 0;

        // Whether to display shading errors as bright colored samples.
        int isDisplayErrorsEnabled = 0;

        // Whether denoising is enabled, which affects how path tracing is performed.
        int isDenoisingEnabled = 0;

        // Whether to write the AOV data required for denoising.
        int isDenoisingAOVsEnabled = 0;

        // The maximum recursion level (or path length) when tracing rays.
        int traceDepth = 0;

        // The maximum luminance for path tracing samples, for simple firefly clamping.
        float maxLuminance = 0.f;

        // Whether Russian Roulette path termination is enabled.
        int isRussianRouletteEnabled = 0;

        // The bounce depth (0-based) at which Russian Roulette termination begins being applied.
        int russianRouletteStartDepth = 0;

        // Whether path space roughness regularization is enabled for indirect (non-primary) rays,
        // to reduce fireflies from near-specular indirect bounce chains (mirrors, glass, etc.).
        int isPathRegularizationEnabled = 0;

        // The strength (0-1) of the path regularization roughness floor at deep bounces.
        float pathRegularizationStrength = 0.f;

        // Scale (relative to maxLuminance) used to soft-clamp each indirect bounce's radiance
        // before it accumulates further, to reduce compounding firefly energy.
        float indirectBounceClampScale = 0.f;

        // Finite view-space Z for sky/miss pixels, outside the denoiser range. RELAX
        // reconstructs neighbour positions from viewZ before masking them; infinity yields NaN.
        // Always assigned; see denoisingRange().
        float skyViewZ = 0.0f;

        // Pads "lights" onto the 16-byte boundary HLSL places it on.
        vec2 _padding1 = vec2(0.0f);

        // Current light data for scene (duplicated each frame in flight.)
        SceneBase::LightData lights;
    };
    // "lights" must land on a 16-byte boundary on both sides.
    static_assert(offsetof(FrameData, lights) % 16 == 0,
        "FrameData::lights must be 16-byte aligned to match Frame.slang; adjust _padding1.");

    // Post-processing settings GPU data.
    struct PostProcessing
    {
        vec3 brightness;
        int debugMode;
        vec2 range;
        int isDenoisingEnabled;
        int isToneMappingEnabled;
        int isGammaCorrectionEnabled;
        int isAlphaEnabled;
        int upscalerMode; // matches PostProcessing.hlsl and kLabelUpscalerMode
        // Which temporal-resolve history texture holds this frame's resolved image:
        // 0 = the pass did not run, 1 = gTAAHistoryA, 2 = gTAAHistoryB.
        // Scalar: HLSL cbuffer packing forbids vectors from crossing 16-byte boundaries.
        // Keep this layout matched with the shader; scalars never cross boundaries.
        int temporalResolveSource;
        // Resolution used by post-processing inputs, excluding final/upscaled textures.
        int renderWidth;
        int renderHeight;
        // Writes the display-resolution depth copy for depth AOV.
        int writeDisplayDepth;
    };
    // Must match PTRenderer's PostProcessing.hlsl num32BitConstants.
    // Mismatches cause D3D12 dispatch validation errors; update both together.
    static constexpr uint32_t kPostProcessingConstantCount = 15;
    static_assert(sizeof(PostProcessing) / sizeof(uint32_t) == kPostProcessingConstantCount,
        "PostProcessing size changed; update num32BitConstants in PostProcessing.hlsl.");

    // Sample settings GPU data.
    struct SampleData
    {
        // The sample index (iteration) for the frame, for progressive rendering.
        uint sampleIndex;

        // An offset to apply to the sample index for seeding a random number generator.
        uint seedOffset;
    };

    FrameData _frameData;
    SampleData _sampleData;
    PostProcessing _postProcessingData;

    bool updateFrameDataGPUStruct(FrameData* pStaging = nullptr);
    bool updatePostProcessingGPUStruct(PostProcessing* pStaging = nullptr);

    // Far scene extent in view space, with headroom. Denoisers use it to distinguish geometry
    // from background; backends must use this exact value because FrameData::skyViewZ derives
    // from it.
    float denoisingRange() const;

    /*** Protected Variables ***/

// TODO: Destruction via shared_ptr is not safe, we should have some kind of kill list system, but
// can't seem to get it to work.
#if 0
    vector<IImagePtr> _imageDestroyList;
#endif
    shared_ptr<SceneBase> _pScene;

    bool _isValid        = false;
    uint32_t _taskCount  = 0;
    uint32_t _taskIndex  = 0;
    uint64_t _taskNumber = 0;
    mat4 _cameraView;
    mat4 _cameraProj;
    float _focalDistance = 1.0f;
    float _lensRadius    = 0.0f;

    // Asset manager for loading external assets.
    unique_ptr<AssetManager> _pAssetMgr;

#if ENABLE_MATERIALX && ENABLE_MDL
    // NVIDIA MDL SDK. Used for generating BSDF code for MaterialX.
    shared_ptr<MdlSdk> _pMdlSdk;
#endif
};
MAKE_AURORA_PTR(RendererBase);

END_AURORA
