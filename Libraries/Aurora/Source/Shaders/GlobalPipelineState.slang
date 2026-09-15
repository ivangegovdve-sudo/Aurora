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
#ifndef GLOBALPIPELINESTATE_H
#define GLOBALPIPELINESTATE_H

#include "Geometry.slang"
#include "Frame.slang"

// =================================================================================================
// Structure defintions, used in global pipeline state
// =================================================================================================

// Layout of environment constant properties.
struct EnvironmentConstants
{
    float3 lightTop;
    float _padding1;
    float3 lightBottom;
    float lightTexLuminanceIntegral;
    float4x4 lightTransform;
    float4x4 lightTransformInv;
    float3 backgroundTop;
    float _padding3;
    float3 backgroundBottom;
    float _padding4;
    float4x4 backgroundTransform;
    bool backgroundUseScreen;
    bool hasLightTex;
    bool hasBackgroundTex;
};

// Layout of an alias map entry.
// Must match CPU struct Entry in AliasMap.h.
// NOTE: Padding to 16 byte (float4) alignment is added for best performance.
struct AliasEntry
{
    uint alias;
    float probability;
    float pdf;
    float _padding1;
};

// Layout of ground plane properties.
// Must match CPU struct GroundPlaneData in PTGroundPlane.h.
struct GroundPlane
{
    bool enabled;
    float3 position;
    float3 normal;
    float3 tangent;
    float3 bitangent;
    float shadowOpacity;
    float3 shadowColor;
    float reflectionOpacity;
    float3 reflectionColor;
    float reflectionRoughness;
};

// =================================================================================================
// Global Variables - For All Shaders
// Must match global root signature defined defined in PTShaderLibrary::initRootSignatures and data 
// setup in PTRenderer::submitRayDispatch.
// =================================================================================================

// The top-level acceleration structure with the scene contents.
[[vk::binding(0)]] RaytracingAccelerationStructure gScene : register(t0);

// Constant buffers of sample and per-frame values.
[[vk::binding(2)]] ConstantBuffer<SampleData> gSampleData : register(b0);
[[vk::binding(1)]] ConstantBuffer<FrameData> gFrameData : register(b1);

// Environment data.
[[vk::binding(3)]] ConstantBuffer<EnvironmentConstants> gEnvironmentConstants : register(b2);
// XXX: Vulkan does not support ground plane.
// gGroundPlane and gNullScene have no vk::binding and are never bound by the HGI backend.
// gEnvironmentAliasMap is bound on both backends for importance sampling.
[[vk::binding(4)]] StructuredBuffer<AliasEntry> gEnvironmentAliasMap : register(t1);
[[vk::binding(9)]] Texture2D<float4> gEnvironmentLightTexture : register(t2);
[[vk::binding(8)]] Texture2D<float4> gEnvironmentBackgroundTexture : register(t3);
#if DIRECTX
ConstantBuffer<GroundPlane> gGroundPlane : register(b3);
RaytracingAccelerationStructure gNullScene : register(t4);
#endif

#if DIRECTX
// Material textures for all the scene materials.  Looked up using indices stored in material header.
SamplerState gSamplerArray[] : register(s0);
#else
// The global sampler state, used by default for texture sampling.
[[vk::binding(7)]] SamplerState gDefaultSampler : register(s0);
#endif

// Output and AOV textures.
// Must match root signature defined in PTShaderLibrary::initRootSignatures and the descriptors
// uploaded to the heap in PTRenderer::updateOutputResources and PTRenderer::updateDenoisingResources.
[[vk::binding(5)]] RWTexture2D<float4> gResult : register(u0);
// XXX: Vulkan does not support denoising.
#if DIRECTX
RWTexture2D<float> gDepthNDC : register(u1);
RWTexture2D<float> gDepthView : register(u2);
RWTexture2D<float4> gNormalRoughness : register(u3);
RWTexture2D<float4> gBaseColorMetalness : register(u4);
RWTexture2D<float4> gDiffuse : register(u5);
RWTexture2D<float4> gGlossy : register(u6);
RWTexture2D<float4> gMotionVectors : register(u7);
// The guide block for NRD.
RWTexture2D<float4> gDemodDiffuse : register(u8);
RWTexture2D<float4> gDemodSpecular : register(u9);
RWTexture2D<float4> gGuideNormalRoughness : register(u10);

// Projects world position to screen space and returns previous-minus-current pixel delta for motion vectors.
// Used by closest-hit, miss, and ground plane shaders for NRD, DLSS, and FSR reprojection.
float2 computeScreenMotion(float4x4 currentViewProj, float4x4 prevViewProj, float4 worldPos)
{
    float4 currentClip = mul(currentViewProj, worldPos);
    float4 prevClip    = mul(prevViewProj, worldPos);

    // Guard perspective divide: w=0 on camera plane, or when ray is parallel to near plane.
    float2 currentNDC = currentClip.xy / max(abs(currentClip.w), 1e-6f);
    float2 prevNDC    = prevClip.xy / max(abs(prevClip.w), 1e-6f);

    // NDC to pixels. Y is flipped because NDC +Y is up while pixel +Y is down.
    float2 screenSize    = float2(DispatchRaysDimensions().xy);
    float2 currentScreen = (float2(currentNDC.x, -currentNDC.y) * 0.5f + 0.5f) * screenSize;
    float2 prevScreen    = (float2(prevNDC.x, -prevNDC.y) * 0.5f + 0.5f) * screenSize;

    return prevScreen - currentScreen;
}

// View-space depth: negative in front of camera, not Euclidean distance.
float computeViewZ(float4x4 invView, float3 worldPos)
{
    // invView is transposed, so its rows are right / up / front / eye position.
    float3 cameraForward = -invView[2].xyz;

    return -dot(worldPos - invView[3].xyz, cameraForward);
}

// 2.5D motion vector: XY = screen-space delta, Z = view depth change (current - prev).
// NRD expects "previous minus current" with positive values, while the current view Z is negative
// in front of camera, so currentViewZ - prevViewZ yields |prevZ| - |currentZ|.
float4 computeMotionVector(float3 worldPos, float currentViewZ)
{
    float prevViewZ = computeViewZ(gFrameData.cameraInvViewPrev, worldPos);
    float2 motionXY = computeScreenMotion(
        gFrameData.cameraViewProj, gFrameData.cameraViewProjPrev, float4(worldPos, 1.0f));

    return float4(motionXY, currentViewZ - prevViewZ, 0.0f);
}

// Environment motion vector: ray direction as homogeneous vector (w=0) captures camera rotation
// while ignoring translation. Zero would indicate static background.
float4 computeSkyMotionVector(float3 rayDirection)
{
    float2 motionXY = computeScreenMotion(
        gFrameData.cameraViewProj, gFrameData.cameraViewProjPrev, float4(rayDirection, 0.0f));

    return float4(motionXY, 0.0f, 0.0f);
}
#endif

#endif // GLOBALPIPELINESTATE_H
