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

#include "AssetManager.h"
#include "RendererBase.h"
#include "SceneBase.h"

BEGIN_AURORA

int upscalerModeFromName(const string& name)
{
    if (name == Names::UpscalerModes::kNone)
        return kUpscalerModeNone;
    if (name == Names::UpscalerModes::kDLSS)
        return kUpscalerModeDLSS;
    if (name == Names::UpscalerModes::kFSR)
        return kUpscalerModeFSR;
    if (name == Names::UpscalerModes::kDLSSRayReconstruction)
        return kUpscalerModeDLSSRayReconstruction;

    // Warn once per distinct name.
    static set<string> warnedNames;
    if (warnedNames.insert(name).second)
    {
        AU_WARN("Unknown upscaler mode \"%s\"; rendering without an upscaler. Expected one of "
                "\"%s\", \"%s\", \"%s\", \"%s\".",
            name.c_str(), Names::UpscalerModes::kNone.c_str(),
            Names::UpscalerModes::kDLSS.c_str(), Names::UpscalerModes::kFSR.c_str(),
            Names::UpscalerModes::kDLSSRayReconstruction.c_str());
    }

    return kUpscalerModeNone;
}

// Create or get the property set (options) for the renderer.
static PropertySetPtr gpPropertySet;
static PropertySetPtr propertySet()
{
    if (gpPropertySet)
    {
        return gpPropertySet;
    }

    gpPropertySet = make_shared<PropertySet>();

    // Add the renderer options to the property set.
    gpPropertySet->add(kLabelIsResetHistoryEnabled, false);
    gpPropertySet->add(kLabelIsDenoisingEnabled, false);
    gpPropertySet->add(kLabelIsDiffuseOnlyEnabled, false);
    gpPropertySet->add(kLabelDebugMode, 0);
    gpPropertySet->add(kLabelMaxLuminance, 1000.0f);
    gpPropertySet->add(kLabelTraceDepth, 5);
    gpPropertySet->add(kLabelIsToneMappingEnabled, false);
#if defined(__APPLE__)
    gpPropertySet->add(kLabelIsGammaCorrectionEnabled, false);
#else
    gpPropertySet->add(kLabelIsGammaCorrectionEnabled, true);
#endif
    gpPropertySet->add(kLabelIsAlphaEnabled, false);
    gpPropertySet->add(kLabelBrightness, vec3(1.0f, 1.0f, 1.0f));
    gpPropertySet->add(kLabelUnits, string("centimeter"));
    gpPropertySet->add(kLabelImportanceSamplingMode, kImportanceSamplingModeMIS);
    gpPropertySet->add(kLabelIsFlipImageYEnabled, true);
    gpPropertySet->add(kLabelIsReferenceBSDFEnabled, false);
    gpPropertySet->add(kLabelIsForceOpaqueShadowsEnabled, false);
    gpPropertySet->add(kLabelUpscalerMode, Names::UpscalerModes::kNone);
    gpPropertySet->add(kLabelIsTemporalResolveEnabled, true);
    gpPropertySet->add(kLabelUpscalerQuality, 0); // Native / DLAA
    gpPropertySet->add(kLabelIsRussianRouletteEnabled, true);
    gpPropertySet->add(kLabelRussianRouletteStartDepth, 3);
    // Default the MDL generator option to match the build-time toggle:
    // when Aurora is compiled with ENABLE_MDL=1 the new MDL pipeline is
    // the active default; when ENABLE_MDL=0 it is unconditionally off.
#if ENABLE_MATERIALX && ENABLE_MDL
    gpPropertySet->add(kLabelOptionUseMDLMaterialGenerator, true);
#else
    gpPropertySet->add(kLabelOptionUseMDLMaterialGenerator, false);
#endif

    return gpPropertySet;
}

RendererBase::RendererBase(uint32_t taskCount) : FixedValues(propertySet()), _taskCount(taskCount)
{
    // Initialize the asset manager.
    _pAssetMgr = make_unique<AssetManager>();
    _pAssetMgr->enableVerticalFlipOnImageLoad(_values.asBoolean(kLabelIsFlipImageYEnabled));

#if ENABLE_MATERIALX && ENABLE_MDL
    // Initialize the MDL SDK.
    _pMdlSdk = make_shared<MdlSdk>();
#endif

    assert(taskCount > 0);
}

RendererBase::~RendererBase()
{
    // Explicitly release the scene before the MDL SDK so that all
    // handles are released before shutting down the SDK.
    _pScene.reset();
}

void RendererBase::setOptions(const Properties& options)
{
    propertiesToValues(options, *this);
}

void RendererBase::setCamera(
    const mat4& view, const mat4& projection, float focalDistance, float lensRadius)
{
    assert(focalDistance > 0.0f && lensRadius >= 0.0f);

    _cameraView    = view;
    _cameraProj    = projection;
    _focalDistance = focalDistance;
    _lensRadius    = lensRadius;
}

void RendererBase::setCamera(
    const float* view, const float* proj, float focalDistance, float lensRadius)
{
    assert(focalDistance > 0.0f && lensRadius >= 0.0f);

    _cameraView    = make_mat4(view);
    _cameraProj    = make_mat4(proj);
    _focalDistance = focalDistance;
    _lensRadius    = lensRadius;
}

void RendererBase::addMdlSearchPath(const std::string& path)
{
#if ENABLE_MATERIALX && ENABLE_MDL
    _pMdlSdk->mdlConfig()->add_mdl_path(path.c_str());
#else
    (void)path;
#endif
}

void RendererBase::setLoadResourceFunction(LoadResourceFunction func)
{
    // Implemented here rather than per backend, so no backend can forget it. The asset manager it
    // forwards to is owned by RendererBase.
    _pAssetMgr->setLoadResourceFunction(func);
}

// Note that this handles strings differently than the implementation in SceneBase.
void RendererBase::propertiesToValues(const Properties& properties, IValues& values)
{
    for (auto& property : properties)
    {
        switch (property.second.type)
        {
        default:
        case PropertyValue::Type::Undefined:
            values.clearValue(property.first);
            break;

        case PropertyValue::Type::Bool:
            values.setBoolean(property.first, property.second._bool);
            break;

        case PropertyValue::Type::Int:
            values.setInt(property.first, property.second._int);
            break;

        case PropertyValue::Type::Float:
            values.setFloat(property.first, property.second._float);
            break;

        case PropertyValue::Type::Float2:
            AU_WARN("Cannot convert Float2 property.");
            break;

        case PropertyValue::Type::Float3:
            values.setFloat3(property.first, value_ptr(property.second._float3));
            break;

        case PropertyValue::Type::Float4:
            AU_WARN("Cannot convert Float4 property.");
            break;

        case PropertyValue::Type::String:
            values.setString(property.first, property.second._string);
            break;

        case PropertyValue::Type::Matrix4:
            values.setMatrix(property.first, value_ptr(property.second._matrix4));
            break;
        }
    }
}

float RendererBase::denoisingRange() const
{
    // Based on the scene's farthest view-space extent, with 10% headroom.
    // Clamp the result to a useful range.
    static constexpr float kRangeHeadroom = 1.1f;
    static constexpr float kMinRange      = 1.0f;
    static constexpr float kMaxRange      = 5.0e8f;

    const Foundation::BoundingBox& bounds = _pScene->bounds();
    if (!bounds.isValid())
    {
        return kMinRange;
    }

    Foundation::BoundingBox viewBox = bounds.transform(_cameraView);
    float const maxViewDepth        = -viewBox.min().z;
    return glm::clamp(maxViewDepth * kRangeHeadroom, kMinRange, kMaxRange);
}

bool RendererBase::updateFrameDataGPUStruct(FrameData* pStaging)
{
    FrameData frameData;

    // Get the camera properties:
    // - The view-projection matrix.
    // - The inverse view matrix, transposed.
    // - The dimensions of the view (in world units) at a distance of 1.0 from the camera, derived
    //   from the [0,0] and [1,1] elements of the projection matrix.
    // - Whether the camera uses an orthographic projection, defined by the [3,3] element of the
    //   projection matrix.
    // - Focal distance and lens radius, for depth of field.
    frameData.cameraViewProj     = _cameraProj * _cameraView;
    frameData.cameraViewProjPrev = frameData.cameraViewProj;
    frameData.cameraInvView      = transpose(inverse(_cameraView));
    frameData.cameraInvViewPrev  = frameData.cameraInvView;
    frameData.viewSize           = vec2(2.0f / _cameraProj[0][0], 2.0f / _cameraProj[1][1]);
    frameData.cameraJitter       = vec2(0.0f);
    frameData.cameraJitterPrev   = vec2(0.0f);
    frameData.isOrthoProjection  = _cameraProj[3][3] == 1.0f;
    frameData.focalDistance      = _focalDistance;
    frameData.lensRadius         = _lensRadius;
    // A backend that measures frame time will overwrite this.
    frameData.timeDeltaMs             = kDefaultFrameTimeMs;
    frameData.isTemporalJitterEnabled = 0;

    // Get the scene size, specifically the maximum distance between any two points in the scene.
    // This is computed as the distance between the min / max corners of the bounding box.
    const Foundation::BoundingBox& bounds = _pScene->bounds();
    frameData.sceneSize                   = glm::length(bounds.max() - bounds.min());

    // Place sky / miss pixels beyond the denoising range so they classify as background.
    // Use a negative value for Aurora's signed right-handed view Z; NRD takes the absolute value.
    frameData.skyViewZ = -denoisingRange() * 2.0f;

    // Copy the current light buffer for the scene to this frame's light data.
    memcpy(&frameData.lights, &_pScene->lights(), sizeof(frameData.lights));

    int debugMode                = _values.asInt(kLabelDebugMode);
    int traceDepth               = _values.asInt(kLabelTraceDepth);
    traceDepth                   = glm::max(1, glm::min(kMaxTraceDepth, traceDepth));
    frameData.traceDepth         = traceDepth;
    frameData.isDenoisingEnabled = 0;
    frameData.isForceOpaqueShadowsEnabled =
        _values.asBoolean(kLabelIsForceOpaqueShadowsEnabled) ? 1 : 0;
    frameData.isDiffuseOnlyEnabled   = _values.asBoolean(kLabelIsDiffuseOnlyEnabled) ? 1 : 0;
    frameData.maxLuminance           = _values.asFloat(kLabelMaxLuminance);
    frameData.isDisplayErrorsEnabled = debugMode == kDebugModeErrors ? 1 : 0;
    frameData.isRussianRouletteEnabled = _values.asBoolean(kLabelIsRussianRouletteEnabled) ? 1 : 0;
    frameData.russianRouletteStartDepth =
        glm::max(0, _values.asInt(kLabelRussianRouletteStartDepth));

    // These biased variance-reduction settings are enabled only by denoising backends.
    // Keep the default unbiased; backends that use a denoiser will override these values.
    frameData.isPathRegularizationEnabled = 0;
    frameData.pathRegularizationStrength  = 0.0f;
    frameData.indirectBounceClampScale    = 0.0f;

    // If there are no changes compared local CPU copy, then do nothing and return false.
    if (memcmp(&_frameData, &frameData, sizeof(FrameData)) == 0)
        return false; // No changes.

    // Set the local copy of frame data to use for next comparison.
    _frameData = frameData;

    // If staging buffer pointer was passed in, copy to that.
    if (pStaging)
        *pStaging = frameData;

    return true;
}

bool RendererBase::updatePostProcessingGPUStruct(PostProcessing* pStaging)
{
    PostProcessing settings;

    // Compute the scene range, i.e. the near and far distance of the scene bounding box from the
    // current view. Since the view has a direction along the -Z axis, the range is determined as
    // [-maxZ, -minZ].
    Foundation::BoundingBox viewBox = _pScene->bounds().transform(_cameraView);
    vec2 sceneRange(-viewBox.max().z, -viewBox.min().z);

    // Prepare post-processing settings.
    int debugMode                     = _values.asInt(kLabelDebugMode);
    settings.brightness               = _values.asFloat3(kLabelBrightness);
    settings.debugMode                = glm::max(0, glm::min(debugMode, kMaxDebugMode));
    settings.range                    = sceneRange;
    settings.isDenoisingEnabled       = 0;
    settings.isToneMappingEnabled     = _values.asBoolean(kLabelIsToneMappingEnabled);
    settings.isGammaCorrectionEnabled = _values.asBoolean(kLabelIsGammaCorrectionEnabled);
    settings.isAlphaEnabled           = _values.asBoolean(kLabelIsAlphaEnabled);
    settings.upscalerMode             = upscalerModeFromName(_values.asString(kLabelUpscalerMode));

    // Set by the backend that owns temporal and upscaling passes.
    // Zero means no temporal history, render/display split, or depth copy.
    settings.temporalResolveSource = 0;
    settings.renderWidth           = 0;
    settings.renderHeight          = 0;
    settings.writeDisplayDepth     = 0;

    // If there are no changes compared local CPU copy, then do nothing and return false.
    if (memcmp(&_postProcessingData, &settings, sizeof(PostProcessing)) == 0)
        return false; // No changes.

    // Update local CPU copy.
    _postProcessingData = settings;

    // Update staging buffer, if one provided.
    if (pStaging)
        *pStaging = settings;

    return true;
}

// The maximum trace depth for recursion, set on the ray tracing pipeline and not to be exceeded.
const int RendererBase::kMaxTraceDepth = 10;

END_AURORA
