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

#include "Colors.hlsli"

// Declare the root signature for the compute shader.
// NOTE: DESCRIPTORS_VOLATILE is specified for root signature 1.0 behavior, where descriptors do not
// have to be initialized in advance.
#define ROOT_SIGNATURE                                                                             \
    "RootFlags(0),"                                                                                \
    "DescriptorTable(UAV(u0, numDescriptors = 20, flags = DESCRIPTORS_VOLATILE)), "                \
    "RootConstants(b0, num32BitConstants = 15)"

// Debug display modes.
#define kDebugModeOff 0
#define kDebugModeErrors 1
#define kDebugModeDepthView 2
#define kDebugModeNormal 3
#define kDebugModeBaseColor 4
#define kDebugModeRoughness 5
#define kDebugModeMetalness 6
#define kDebugModeDiffuse 7
#define kDebugModeDiffuseHitDist 8
#define kDebugModeGlossy 9
#define kDebugModeGlossyHitDist 10
#define kDebugModeValidation 11

// Source (input) and destination (output) textures.
// NOTE: Must match the C++ descriptor heap and Accumulation.hlsl (offset by one).
// Missing slots shift all later registers.
RWTexture2D<float4> gFinal : register(u0);
RWTexture2D<float4> gAccumulation : register(u1);
RWTexture2D<float4> gResult : register(u2);
RWTexture2D<float> gDepthNDC : register(u3);
RWTexture2D<float> gDepthView : register(u4);
RWTexture2D<float4> gNormalRoughness : register(u5);
RWTexture2D<float4> gBaseColorMetalness : register(u6);
RWTexture2D<float4> gDiffuse : register(u7);
RWTexture2D<float4> gGlossy : register(u8);
RWTexture2D<float4> gMotionVectors : register(u9);
RWTexture2D<float4> gDiffuseDenoised : register(u10);
RWTexture2D<float4> gGlossyDenoised : register(u11);
RWTexture2D<float4> gUpscaled : register(u12);
RWTexture2D<float4> gValidation : register(u13); // NRD's own debug overlay
// Temporal resolve history; source selected by temporalResolveSource.
RWTexture2D<float4> gTAAHistoryA : register(u14);
RWTexture2D<float4> gTAAHistoryB : register(u15);
// Display-resolution copy of gDepthNDC for the client-bound depth AOV.
RWTexture2D<float> gDepthNDCDisplay : register(u16);
// Unit-length world normal with LINEAR roughness in .w, written by the ray generator. The normal
// and roughness debug views read this rather than gNormalRoughness, which is in whatever encoding
// NRD was compiled for. Registers u17 and u18 are reserved for two material demodulation factors.
RWTexture2D<float4> gGuideNormalRoughness : register(u19);

// Layout of post-processing properties.
struct PostProcessing
{
    float3 brightness;
    int debugMode;
    float2 range;
    int isDenoisingEnabled;
    int isToneMappingEnabled;
    int isGammaCorrectionEnabled;
    int isAlphaEnabled;
    int upscalerMode; // 0=off, 1=DLSS, 2=FSR, 3=DLSS-RR; when non-zero, read from gUpscaled
    // 0 = temporal resolve did not run, 1 = read gTAAHistoryA, 2 = read gTAAHistoryB.
    int temporalResolveSource;
    // The path tracing resolution, which is below the dispatch (display) resolution whenever
    // a vendor upscaler is reconstructing. Every texture this shader reads EXCEPT gFinal and
    // gUpscaled lives at this resolution.
    int renderWidth;
    int renderHeight;
    // Non-zero when a depth AOV target is bound and gDepthNDCDisplay must be written.
    int writeDisplayDepth;
};

// Constant buffer of post-processing values.
ConstantBuffer<PostProcessing> gSettings : register(b0);

// Returns the beauty image for this frame from whichever stage produced it last: the vendor
// upscaler, the temporal resolve pass, or plain accumulation.
//
// Only the upscaler writes at display resolution; everything else is at render resolution, so
// this takes both coordinates rather than assuming they are the same.
float4 loadBeauty(uint2 displayCoords, uint2 renderCoords)
{
    if (gSettings.upscalerMode != 0)
    {
        return gUpscaled[displayCoords];
    }
    if (gSettings.temporalResolveSource == 1)
    {
        return gTAAHistoryA[renderCoords];
    }
    if (gSettings.temporalResolveSource == 2)
    {
        return gTAAHistoryB[renderCoords];
    }
    return gAccumulation[renderCoords];
}

// Normalizes the specified view depth value to the scene range (front to back).
float normalizeDepthView(float depthView)
{
    return (depthView - gSettings.range.x) / (gSettings.range.y - gSettings.range.x);
}

// A compute shader that applies post-processing to the source texture.
// NOTE: The indicated thread group dimensions provide good occupancy for the current code, and
// must match the values at the Dispatch() call.
[RootSignature(ROOT_SIGNATURE)]
[numthreads(16, 8, 1)]
void PostProcessing(uint3 threadID : SV_DispatchThreadID)
{
    // Skip any shader invocation where the thread ID is outside the screen dimensions, as the
    // shader will be invoked with more threads than pixels when the dimension are not evenly
    // divided by the thread group dimensions.
    // This pass runs at DISPLAY resolution, so the extent must come from the final texture rather
    // than from gAccumulation, which is at render resolution.
    uint2 screenDims;
    gFinal.GetDimensions(screenDims.x, screenDims.y);
    if (any(threadID.xy >= screenDims))
    {
        return;
    }

    // Get the screen coordinates (2D) from the thread ID.
    float2 coords = threadID.xy;

    // The matching coordinate in the render-resolution buffers. Every texture below except
    // gFinal and gUpscaled is at render resolution, so all of the debug AOV reads use this.
    // Zero means the path tracer rendered at the display resolution; see
    // RendererBase::updatePostProcessingGPUStruct.
    uint2 renderDims = gSettings.renderWidth > 0
        ? uint2(gSettings.renderWidth, gSettings.renderHeight)
        : screenDims;
    uint2 rc          = min(uint2(coords * float2(renderDims) / float2(screenDims)),
        renderDims - uint2(1, 1));

    // Use the appropriate texture for output if a debug mode is enabled.
    float4 beauty                 = loadBeauty(threadID.xy, rc);
    float3 color                  = 0.0f;
    float alpha                   = beauty.a;
    bool isDenoisingEnabled       = gSettings.isDenoisingEnabled;
    bool isGammaCorrectionEnabled = gSettings.isGammaCorrectionEnabled;
    switch (gSettings.debugMode)
    {
    // Output: whichever stage produced this frame's beauty image -- the vendor upscaler, the
    // temporal resolve pass, or raw accumulation. See loadBeauty().
    case kDebugModeOff:
    case kDebugModeErrors:
        color = beauty.rgb;
        break;

    // View depth. Normalize the R channel of the view depth texture (grayscale).
    // NOTE: gDepthView holds SIGNED right-handed view Z, negative in front of the camera, while
    // gSettings.range is the positive [near, far] extent of the scene bounding box; take the
    // magnitude so the two conventions agree. Sky pixels carry the FrameData::skyViewZ sentinel,
    // which is beyond the far extent and so normalizes above 1, i.e. saturated white.
    case kDebugModeDepthView:
        color                    = normalizeDepthView(abs(gDepthView[rc].r)).rrr;
        isGammaCorrectionEnabled = false;
        break;

    // Normal, remapped from [-1, 1] to [0, 1] for display.
    //
    // Deliberately NOT gNormalRoughness: under NRD_NORMAL_ENCODING = R10_G10_B10_A2_UNORM that is an
    // opaque packing of the normal and the roughness together, so displaying it raw shows an
    // encoding artefact and the picture changes whenever NRD is rebuilt with other settings.
    case kDebugModeNormal:
        color                    = gGuideNormalRoughness[rc].xyz * 0.5f + 0.5f;
        isGammaCorrectionEnabled = false;
        break;

    // Base color. Use the RGB channels of the base-color-metalness texture.
    case kDebugModeBaseColor:
        color = gBaseColorMetalness[rc].rgb;
        break;

    // Roughness. Duplicate the A channel of the normal-roughness texture.
    // Linear roughness, from the plain guide for the same reason as the normal above: the alpha of
    // gNormalRoughness is a material ID under the current encoding, and sqrt(roughness) under the
    // other one.
    case kDebugModeRoughness:
        color                    = gGuideNormalRoughness[rc].www;
        isGammaCorrectionEnabled = false;
        break;

    // Metalness. Duplicate the A channel of the base-color-metalness texture.
    case kDebugModeMetalness:
        color                    = gBaseColorMetalness[rc].aaa;
        isGammaCorrectionEnabled = false;
        break;

    // Diffuse. Use the RGB channels of the diffuse texture.
    case kDebugModeDiffuse:
        color = (isDenoisingEnabled ? gDiffuseDenoised[rc] : gDiffuse[rc]).rgb;
        break;

    // Diffuse Hit Distance. Duplicate the A channel of the diffuse texture.
    case kDebugModeDiffuseHitDist:
        color = (isDenoisingEnabled ? gDiffuseDenoised[rc] : gDiffuse[rc]).aaa;
        isGammaCorrectionEnabled = false;
        break;

    // Glossy. Use the RGB channels of the glossy texture.
    case kDebugModeGlossy:
        color = (isDenoisingEnabled ? gGlossyDenoised[rc] : gGlossy[rc]).rgb;
        break;

    // Glossy Hit Distance. Duplicate the A channel of the glossy texture.
    case kDebugModeGlossyHitDist:
        color = (isDenoisingEnabled ? gGlossyDenoised[rc] : gGlossy[rc]).aaa;
        isGammaCorrectionEnabled = false;
        break;

    // NRD's own debug overlay: pre-tonemapped, display-ready colors, shown as-is with no further
    // brightness/tonemap/gamma processing. See NRD's README "VALIDATION LAYER" section for the
    // viewport grid it renders.
    case kDebugModeValidation:
        color = gValidation[rc].rgb;
        isGammaCorrectionEnabled = false;
        break;
    }

    // Perform post-processing for any output based on the accumulation (beauty) texture.
    if (gSettings.debugMode <= kDebugModeErrors)
    {
        // Apply brightness.
        color *= gSettings.brightness;

        // Apply ACES tone mapping or simple saturation.
        if (gSettings.isToneMappingEnabled)
        {
            color = toneMapACES(color);
        }
    }

    // Apply gamma correction.
    // NOTE: Gamma correction must be performed here as UAV textures don't support sRGB write.
    if (isGammaCorrectionEnabled)
    {
        color = linearTosRGB(saturate(color));
    }

    // Write to the final texture, optionally with alpha.
    gFinal[coords] = float4(color, gSettings.isAlphaEnabled ? alpha : 1.0f);

    // Point-upsample the depth AOV to display resolution for a client-bound depth target.
    if (gSettings.writeDisplayDepth != 0)
    {
        gDepthNDCDisplay[coords] = gDepthNDC[rc];
    }
}