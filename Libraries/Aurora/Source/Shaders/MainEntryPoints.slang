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

// Prefix containing the common code used by all material types.
#include "Options.slang"
#include "HitShaderEntryPoints.slang"
#include "DefaultMaterial.slang"
#if ENABLE_NRD
#include "NRD.slang"
#endif

// =================================================================================================
// Ray Gen Shader
// =================================================================================================
// HgiVulkan can only have one main entry point for the shader source.
// To share this file between different renderers, we leverage preprocessors and
// enable one shader stage at a time.
// TODO: Consider to use separate shader files for each shader stage.
#ifndef DISABLE_HGI_SHADER_STAGE_RAY_GEN

// Results of the path tracing sample.
struct Output
{
    // Depth in Normalized Device Coordinates, for entire path.
    float depthNDC;
    // Normal of first collision in path.
    float3 normal;
    // Material metalness of first collision in path.
    float metalness;
    // Material roughness of first collision in path.
    float roughness;
    // Material base color of first collision in path.
    float3 baseColor;
    // Depth in Normalized Device Coordinates, for entire path.
    float depthView;
    // Direct lighting contribution for path.
    float3 direct;
    // Indirect lighting contribution for path (split into diffuse and glossy.)
    IndirectOutput indirect;
    // Alpha for path (0.0 only if all ray segments were transparent)
    float alpha;

    [mutating]
    void clear()
    {
        indirect.clear();
        depthNDC = 1.0f;
        normal = -1.0f;
        metalness = 0.1f;
        roughness = 0.1f;
        baseColor = BLACK;
        depthView = -INFINITY;
        direct = 0.0f;
        alpha = 0.0;
    }
}

// The ray generation shader, iteratively shades the entire ray path.
[shader("raygeneration")]
void RayGenShader()
{
    // Get the max trace depth from frame settings.
    int maxTraceDepth = gFrameData.traceDepth;

    // Get the dispatch dimensions (screen size), dispatch index (screen coordinates), and sample
    // index.
    uint2 screenSize = DispatchRaysDimensions().xy;
    uint2 screenCoords = DispatchRaysIndex().xy;
    // Mix in the per-frame seed offset. With a temporal stage the sample index is 0 every frame, so
    // without this every frame would draw the identical random sequence and RELAX would read the
    // same bright paths as a stable signal. The offset is 0 when no temporal stage is active.
    uint sampleIndex = gSampleData.sampleIndex + gSampleData.seedOffset;

    InstanceRayPayload rayPayload;
    rayPayload.rng             = initRand(sampleIndex, screenSize, screenCoords);
    rayPayload.totalDistance   = 0.0f;
    rayPayload.rayContribution = float3(1.0f, 1.0f, 1.0f);
    rayPayload.packedData      = 0;
    rayPayload.rayType         = CAMERA_RAY;
    rayPayload.misLobeID       = NO_SECONDARY_RAY;
    rayPayload.outputAlpha     = 0.0f;
    rayPayload.isPrimaryRay    = true;
    rayPayload.isInside        = false;

    // Compute a camera ray (origin and direction) for the current screen coordinates. This applies
    // a random offset to support antialiasing and depth of field.
    float3 rayOrigin;
    float3 rayDirection;
    computeCameraRay(screenCoords, screenSize, gFrameData.cameraInvView, gFrameData.viewSize,
        gFrameData.isOrthoProjection, gFrameData.focalDistance, gFrameData.lensRadius,
        gFrameData.cameraJitter, gFrameData.isTemporalJitterEnabled, rayPayload.rng, rayOrigin,
        rayDirection);
#if DIRECTX
    // Saved before the loop overwrites them. The primary leg's hit distance is recovered from the
    // depth-0 viewZ below, and gDepthView will be overwritten by secondary shaders later.
    float3 primaryRayDir    = rayDirection;
    float savedPrimaryViewZ = -INFINITY;
#endif // DIRECTX

    // The results of the tracing the entire ray path through the scene.
    Output output;
    output.clear();

#if DIRECTX
    if (gFrameData.isDenoisingAOVsEnabled)
        gMotionVectors[screenCoords] = float4(0.0f);
#endif // DIRECTX

    // Prepare the environment data structure.
    Environment environment = prepareEnvironmentValues();

    // Iterate through every segment in the ray path, until maximum depth is reached or path is ended.
    for (int rayDepth = 0; rayDepth <= maxTraceDepth && !rayPayload.pathEnded; rayDepth++)
    {
        // Have we exceeded ray depth for this path?
        if (rayDepth == maxTraceDepth)
        {
            // If this ray collides with nothing, should the background or lighting environment be shaded?
            bool shadeBackgroundOnMiss = rayPayload.isPrimaryRay
                || rayPayload.rayType == TRANSMISSION_LOBE
                || rayPayload.rayType == GROUND_PLANE_REFLECTION_RAY;

            // If we have reached the end of this path, evaluate the environment and terminate the path.
            rayPayload.direct = evaluateEnvironment(environment, rayDirection, shadeBackgroundOnMiss);
            rayPayload.direct *= rayPayload.rayContribution;
            rayPayload.pathEnded = true;
        }
        else
        {
            // The minimum T for this ray segment is zero for the initial camera ray otherwise use
            // a small epsilon to avoid self-intersection.
            float minT = rayDepth == 0 ? 0.0f : M_RAY_TMIN;

            // Trace an instance ray that will return the instance shading result from the closest hit, if any.
            rayPayload.rayDepth = rayDepth;
            traceInstanceRay(gScene, rayOrigin, rayDirection, minT, rayPayload);

            rayOrigin = rayPayload.nextRayOrigin;
            rayDirection = rayPayload.nextRayDirection;
            output.alpha = rayPayload.outputAlpha;

            if (rayPayload.skipContribution)
            {
                rayPayload.skipContribution = false;
                continue;
            }
        }

        // How the result is accumulated depends on whether this ray is a primary ray.
        if (rayDepth == 0)
        {
#if DIRECTX
            // Capture viewZ written by the depth-0 hit or miss shader before a secondary ray
            // overwrites it. Only read it when the AOVs are enabled: gDepthView is not
            // allocated otherwise, and its descriptor slot is left uninitialized.
            if (gFrameData.isDenoisingAOVsEnabled)
            {
                savedPrimaryViewZ = gDepthView[screenCoords].r;
            }
#endif // DIRECTX
            // The hit and miss shaders guard their AOV writes on isPrimaryRay, which
            // traceInstanceRay never clears, so clear it here to stop secondary rays overwriting
            // gDepthView and the other guides.
            rayPayload.isPrimaryRay = false;

            // Deliberately NOT soft-clamped like the secondary bounce below: that clamp compensates
            // for probabilistic lobe selection, and this MIS result already divides by real PDFs,
            // so clamping it would discard genuine energy from bright reflections.
            output.direct = rayPayload.direct;

            // Route the environment-MIS result to its lobe's channel, but do NOT let it set that
            // channel's hit distance: environment lighting has none, and NRD requires the in-lobe
            // first bounce (recorded below) instead, or exactly 0 for a lobe that was never
            // sampled -- the marker AREA_3X3 reconstructs from.
            if (rayPayload.misLobeID == DIFFUSE_LOBE)
            {
                output.indirect.diffuse = rayPayload.indirect;
            }
            else
            {
                output.indirect.glossy = rayPayload.indirect;
            }
        }
        else
        {
            // If this secondary ray compute the total radiance from all the lighting components.
            float3 radiance = rayPayload.direct + rayPayload.indirect;

            // Soft-clamp this bounce's radiance before it is added to the running indirect total
            // and carried into further bounces, catching a spike where it originates rather than
            // after it has compounded. Biased, so the renderer only enables it for a denoiser; a
            // scale of zero means off, not "clamp to black".
            if (gFrameData.indirectBounceClampScale > 0.0f)
            {
                adjustRadiance(
                    gFrameData.maxLuminance * gFrameData.indirectBounceClampScale, false, radiance);
            }

            // Hit distance is an NRD input only. Recover the primary leg from the viewZ saved at
            // depth 0 -- Aurora stores signed view Z, so primaryHitT = -viewZ / dot(dir, camFwd).
            // A secondary ray that missed does not increment totalDistance, so a non-positive
            // result means it reached the environment.
            float hitDist = 0.0f;
#if DIRECTX
            if (gFrameData.isDenoisingAOVsEnabled)
            {
                float3 camFwd       = -gFrameData.cameraInvView[2].xyz;
                float primaryHitT   = -savedPrimaryViewZ / dot(primaryRayDir, camFwd);
                float secondaryHitT = rayPayload.totalDistance - primaryHitT;
                hitDist             = (secondaryHitT > 0.0f) ? secondaryHitT : INFINITY;
            }
#endif // DIRECTX

            // Accumulate the total radiance into the diffuse or glossy component, based on the lobe
            // this ray segment originated from.
            //
            // Only the FIRST bounce sets the hit distance. NRD takes the first bounce's hitT as is,
            // but requires later bounces to be adjusted for curvature and lobe energy dissipation
            // on the application side, which Aurora does not do.
            if (rayPayload.rayType == DIFFUSE_LOBE)
            {
                output.indirect.diffuse += radiance;
                if (rayDepth == 1)
                    output.indirect.diffuseHitDist = hitDist;
            }
            else
            {
                output.indirect.glossy += radiance;
                if (rayDepth == 1)
                    output.indirect.glossyHitDist = hitDist;
            }
        }

        // Set the ray type for the next segment.
        rayPayload.rayType = rayPayload.nextRayType;
    }

    float3 finalRadiance = output.direct + output.indirect.diffuse + output.indirect.glossy;
    float4 result = float4(finalRadiance, output.alpha);

    // Adjust the radiance of the sample, e.g. to perform corrections.
    adjustRadiance(gFrameData.maxLuminance, gFrameData.isDisplayErrorsEnabled, result.rgb);

    // Clamp each radiance lobe before storing it in the NRD input AOVs: RELAX sanitizes NaN and
    // Inf itself, but this keeps very bright fireflies out of its temporal history. The ceiling
    // must be maxLuminance, the same one the non-denoised path uses just above, or a legitimately
    // bright signal reads dimmer with denoising on than off.
    if (gFrameData.isDenoisingEnabled)
    {
        adjustRadiance(gFrameData.maxLuminance, false, output.direct);
        adjustRadiance(gFrameData.maxLuminance, false, output.indirect.diffuse);
        adjustRadiance(gFrameData.maxLuminance, false, output.indirect.glossy);
    }

    // Store the result in the "result" output texture. If denoising is enabled, only the "direct"
    // (non-denoised) shading is included in the result texture, as the rest of shading is stored in
    // the denoising AOVs below.
    result.rgb = gFrameData.isDenoisingEnabled ? output.direct : result.rgb;
    gResult[screenCoords] = result;

    // XXX: Vulkan does not support denoising.
#if DIRECTX
    if (gFrameData.isDenoisingAOVsEnabled)
    {
        // Material demodulation, which NRD requires: it denoises radiance, not irradiance, so the
        // BRDF must be applied after it runs. Accumulation.hlsl multiplies the same factors back
        // in. Read back from the guide AOVs rather than recomputed, so both halves of the round
        // trip and DLSS Ray Reconstruction see identical values.
        float4 guide             = gGuideNormalRoughness[screenCoords];
        float4 baseColorMetalness = gBaseColorMetalness[screenCoords];
        float3 albedo            = baseColorMetalness.rgb * (1.0f - baseColorMetalness.a);
        float3 Rf0               = lerp(0.04f.xxx, baseColorMetalness.rgb, baseColorMetalness.a);

        float3 diffFactor;
        float3 specFactor;
#if ENABLE_NRD
        NRD_MaterialFactors(guide.xyz, -primaryRayDir, albedo, Rf0, guide.w, diffFactor, specFactor);
#else
        // Same shape as NRD_MaterialFactors, floored the same way, so the non-NRD build stays
        // self-consistent through the same round trip.
        diffFactor = lerp(0.02f.xxx, 1.0f.xxx, albedo);
        specFactor = lerp(0.02f.xxx, 1.0f.xxx, Rf0 * lerp(0.1f, 1.0f, guide.w));
#endif
        gDemodDiffuse[screenCoords]  = float4(diffFactor, 1.0f);
        gDemodSpecular[screenCoords] = float4(specFactor, 1.0f);

        output.indirect.diffuse /= diffFactor;
        output.indirect.glossy  /= specFactor;

        // Prepare the diffuse and glossy radiance and hit distances for NRD.
#if ENABLE_NRD
        // RELAX takes the raw world-space hit distance. Three states, set by IndirectOutput.clear()
        // and the depth >= 1 code above:
        //   -1.0  lobe not sampled -> 0.0, the marker AREA_3X3 reconstructs from.
        //   INF   secondary ray reached the environment -> kEnvironmentHitDist.
        //   > 0   secondary ray hit geometry -> the distance as is.
        //
        // The environment case must NOT collapse to 0.0, which is the "not sampled" marker: RELAX
        // drives specular virtual-motion reprojection from hit distance, so 0.0 would claim the
        // reflection sits on the surface. The sentinel is beyond any plausible scene extent but
        // inside fp16, since these AOVs are R16G16B16A16_FLOAT.
        static const float kEnvironmentHitDist = 32768.0f;
        float diffuseRawHitDist = isinf(output.indirect.diffuseHitDist)
            ? kEnvironmentHitDist
            : (output.indirect.diffuseHitDist > 0.0f ? output.indirect.diffuseHitDist : 0.0f);
        float glossyRawHitDist  = isinf(output.indirect.glossyHitDist)
            ? kEnvironmentHitDist
            : (output.indirect.glossyHitDist  > 0.0f ? output.indirect.glossyHitDist  : 0.0f);
        float4 diffusePacked = RELAX_FrontEnd_PackRadianceAndHitDist(output.indirect.diffuse, diffuseRawHitDist, true);
        float4 glossyPacked  = RELAX_FrontEnd_PackRadianceAndHitDist(output.indirect.glossy,  glossyRawHitDist,  true);
#else
        float diffuseHitDist = saturate(output.indirect.diffuseHitDist / gFrameData.sceneSize);
        float glossyHitDist  = saturate(output.indirect.glossyHitDist  / gFrameData.sceneSize);
        float4 diffusePacked = float4(output.indirect.diffuse, diffuseHitDist);
        float4 glossyPacked  = float4(output.indirect.glossy, glossyHitDist);
#endif

        // Store the data for the denoising AOVs.
        gDiffuse[screenCoords] = diffusePacked;
        gGlossy[screenCoords] = glossyPacked;
    }
#endif // DIRECTX
}
#endif // DISABLE_HGI_SHADER_STAGE_RAY_GEN

// =================================================================================================
// Shadow miss shader
// =================================================================================================
#ifndef DISABLE_HGI_SHADER_STAGE_MISS
// Shadow miss results sets the visibility to WHITE.
[shader("miss")]
void ShadowMissShader(inout ShadowRayPayload rayPayload)
{
    rayPayload.visibility = WHITE;
}
#endif // DISABLE_HGI_SHADER_STAGE_MISS

// =================================================================================================
// Instance miss shader
// =================================================================================================
#ifndef DISABLE_HGI_SHADER_STAGE_INSTANCE_MISS

[shader("miss")]
void InstanceMissShader(inout InstanceRayPayload rayPayload)
{
    float3 rayOrigin           = WorldRayOrigin();
    float3 rayDirection        = WorldRayDirection();
    Environment environment    = prepareEnvironmentValues();

    // Add the contribution of the ground plane, if enabled.
    bool absorbedByGroundPlane = evaluateGroundPlane(rayPayload, rayOrigin, rayDirection, environment);
    if (absorbedByGroundPlane)
        return;

    // If this ray collides with nothing, should the background or lighting environment be shaded?
    bool shadeBackgroundOnMiss = rayPayload.isPrimaryRay
        || rayPayload.rayType == TRANSMISSION_LOBE
        || rayPayload.rayType == GROUND_PLANE_REFLECTION_RAY;

    // No geometry was hit, shade the ray using the background color.
    // If needed otherwise do nothing, as environment lighting was calculated during MIS environment shading.
    if (shadeBackgroundOnMiss)
    {
        rayPayload.direct = evaluateEnvironment(environment, WorldRayDirection(), true);
        rayPayload.direct *= rayPayload.rayContribution;
    }

    // Is this a primary ray?
    if (rayPayload.isPrimaryRay)
    {
        // Prepare and store the data for the denoising AOVs, if enabled.
        // NOTE: Only the data for the last sample is stored, with the expectation that the sample count
        // will be one when these AOVs are enabled.
        // XXX: Vulkan does not support denoising.
#if DIRECTX
        uint2 screenCoords = DispatchRaysIndex().xy;

        if (gFrameData.isDenoisingAOVsEnabled)
        {
            // Store the data for the denoising AOVs.
            // A large FINITE sentinel, not -INFINITY: RELAX reconstructs a neighbour's world
            // position from its viewZ before masking the sample out, where an infinity yields
            // INF * 0 = NaN and contaminates the geometry pixels next to the sky. See
            // FrameData::skyViewZ in RendererBase.h.
            gDepthView[screenCoords] = gFrameData.skyViewZ;
            gNormalRoughness[screenCoords] = float4(0.0f, 0.0f, 0.0f, 0.1f);
            gBaseColorMetalness[screenCoords] = float4(0.0f, 0.0f, 0.0f, 0.1f);
            // Sky pixels have no surface, but the guide must still hold a well-formed unit normal:
            // Ray Reconstruction consumes it for every pixel. Face it back along the view ray.
            gGuideNormalRoughness[screenCoords] = float4(-rayDirection, 1.0f);

            // NRD treats sky as outside its denoising range, but DLSS and FSR reproject every
            // pixel including sky; see computeSkyMotionVector.
            gMotionVectors[screenCoords] = computeSkyMotionVector(rayDirection);
        }

        // Store the NDC depth value, if enabled. The value is only stored if the first sample was
        // computed (i.e. there is no previously stored value), or it is less than the previously stored
        // value.
        if (gFrameData.isDepthNDCEnabled)
        {
            // Set the output Normalized Device Coordinate (NDC) depth from hit position.
            float depthNDC = 1.0f;

            if (gSampleData.sampleIndex == 0 || depthNDC < gDepthNDC[screenCoords])
            {
                gDepthNDC[screenCoords] = depthNDC;
            }
        }
#endif // DIRECTX
    }

    // Terminate the path.
    rayPayload.pathEnded = true;
}
#endif // DISABLE_HGI_SHADER_STAGE_INSTANCE_MISS
