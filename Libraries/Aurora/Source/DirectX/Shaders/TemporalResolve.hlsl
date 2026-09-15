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

// A motion-vector reprojecting temporal anti-aliasing resolve.
//
// The ray generator jitters the camera by a sub-pixel Halton offset whenever a temporal stage is
// downstream. DLSS and FSR resolve that jitter themselves; RELAX does not, filtering only the
// indirect lobes. This pass resolves it in that case - without it the whole image shifts by up to
// half a pixel every frame - and stabilizes the direct channel, which otherwise reaches the screen
// at 1 spp unfiltered. Skipped when an upscaler is active, since that needs the raw jittered signal.
//
// Three choices avoid the usual TAA smear, in rough order of impact:
//   1. Catmull-Rom bicubic history resampling. Reprojection resamples every frame, so bilinear
//      smoothing compounds geometrically; the negative lobes preserve detail.
//   2. Feedback weight scales with on-screen velocity: shimmer is imperceptible during fast motion
//      but blur is not, so fast motion leans on the current frame.
//   3. History is clipped toward the current colour against a variance-derived box. A per-channel
//      min/max box over a high-contrast neighbourhood rejects almost nothing.

#include "Colors.hlsli"

// Declare the root signature for the compute shader.
// NOTE: The descriptor table mirrors PostProcessing.hlsl's register numbering (starting at the
// final texture, heap index 0) so the two shaders share one set of slot indices.
#define ROOT_SIGNATURE                                                                             \
    "RootFlags(0),"                                                                                \
    "DescriptorTable(UAV(u0, numDescriptors = 16, flags = DESCRIPTORS_VOLATILE)), "                \
    "RootConstants(b0, num32BitConstants = 5)"

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
RWTexture2D<float4> gValidation : register(u13);
// The two temporal-resolve history buffers, ping-ponged by gSettings.historyIndex.
//
// Two are needed, not one: the neighbourhood clip below reads gAccumulation at c +/- 1, so this
// pass cannot write gAccumulation, and it samples history at a reprojected (fractional) location,
// so it cannot write the texture it reads either. Both keep permanent descriptors and a root
// constant selects their roles, because overwriting a descriptor the GPU may still be reading
// would force a waitForTask() every frame.
RWTexture2D<float4> gTAAHistoryA : register(u14);
RWTexture2D<float4> gTAAHistoryB : register(u15);

// Layout of temporal resolve properties.
struct TemporalResolve
{
    // 0 => write A / read B, 1 => write B / read A.
    uint historyIndex;

    // Non-zero to discard history entirely for this frame.
    uint isReset;

    // Current-frame weight when the camera is effectively still.
    float blendAlphaMin;

    // Current-frame weight at or above speedReference.
    float blendAlphaMax;

    // On-screen speed, in pixels per frame, at which blendAlphaMax is reached.
    float speedReference;
};

ConstantBuffer<TemporalResolve> gSettings : register(b0);

// Reads the history texture selected by gSettings.historyIndex.
float3 loadHistory(int2 c)
{
    return gSettings.historyIndex == 0 ? gTAAHistoryB[c].rgb : gTAAHistoryA[c].rgb;
}

// Resamples history at a fractional pixel position with a Catmull-Rom bicubic.
//
// UAVs cannot be sampled, so this is a direct separable 4x4 evaluation. More expensive than
// bilinear, but bilinear reapplies to the history buffer every frame forever, so its smoothing
// compounds and is by far the dominant cause of TAA blur.
float3 sampleHistoryCatmullRom(float2 pos, int2 maxCoord)
{
    // Position relative to the texel centre grid.
    float2 samplePos = pos - 0.5f;
    float2 texelBase = floor(samplePos);
    float2 f         = samplePos - texelBase;
    float2 f2        = f * f;
    float2 f3        = f2 * f;

    // Catmull-Rom (cardinal spline, a = -0.5) weights for the four taps around the sample.
    float2 w0 = -0.5f * f3 + f2 - 0.5f * f;
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w2 = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
    float2 w3 = 0.5f * f3 - 0.5f * f2;

    float wx[4] = { w0.x, w1.x, w2.x, w3.x };
    float wy[4] = { w0.y, w1.y, w2.y, w3.y };

    float3 result = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int j = 0; j < 4; j++)
    {
        [unroll]
        for (int i = 0; i < 4; i++)
        {
            int2 tap = int2(texelBase) + int2(i - 1, j - 1);
            tap      = clamp(tap, int2(0, 0), maxCoord);
            result += loadHistory(tap) * (wx[i] * wy[j]);
        }
    }

    // Catmull-Rom's negative lobes can undershoot below zero on a high-contrast edge; HDR radiance
    // is non-negative, and the caller's clip step assumes it.
    return max(result, float3(0.0f, 0.0f, 0.0f));
}

// Clips "history" toward "current" until it lies inside the axis-aligned box [boxMin, boxMax].
//
// Preferred over a per-channel clamp: clamping moves each channel independently and so can shift
// hue, whereas clipping walks the single line between the two colors and only ever returns a blend
// of two colors that were both really present.
float3 clipToBox(float3 history, float3 current, float3 boxMin, float3 boxMax)
{
    float3 direction = history - current;
    float3 magnitude = abs(direction);

    // Distance, along the line, at which each axis leaves the box. A near-zero component cannot
    // leave the box, so give it an effectively infinite distance rather than dividing by zero.
    // NOTE: HLSL 2021 rejects a vector condition in a ternary ("condition for short-circuiting
    // ternary operator must be scalar"), so these use select().
    float3 limit = select(direction > 0.0f, boxMax - current, current - boxMin);
    float3 t     = select(magnitude > 1e-6f, limit / max(magnitude, 1e-6f), float3(1.0f, 1.0f, 1.0f));

    float scale = saturate(min(min(t.x, t.y), t.z));
    return current + direction * scale;
}

// Resolves the per-frame camera jitter into a stable image. The thread group dimensions give good
// occupancy for the current code, and MUST match the values at the Dispatch() call.
[RootSignature(ROOT_SIGNATURE)]
[numthreads(16, 8, 1)]
void TemporalResolve(uint3 threadID : SV_DispatchThreadID)
{
    uint2 dims;
    gAccumulation.GetDimensions(dims.x, dims.y);
    if (any(threadID.xy >= dims))
    {
        return;
    }

    int2 c         = int2(threadID.xy);
    int2 maxCoord  = int2(dims) - 1;
    float4 current = gAccumulation[c];

    // Gather 3x3 neighbourhood statistics in YCoCg, which gives luminance and the two chroma axes
    // independent bounds - markedly more stable than bounding three correlated RGB channels.
    float3 currentYCoCg = RGBtoYCoCg(current.rgb);
    float3 moment1      = float3(0.0f, 0.0f, 0.0f);
    float3 moment2      = float3(0.0f, 0.0f, 0.0f);
    float3 neighbourMin = float3(1e30f, 1e30f, 1e30f);
    float3 neighbourMax = float3(-1e30f, -1e30f, -1e30f);
    [unroll]
    for (int j = -1; j <= 1; j++)
    {
        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            int2 n           = clamp(c + int2(i, j), int2(0, 0), maxCoord);
            float3 neighbour = (i == 0 && j == 0) ? currentYCoCg : RGBtoYCoCg(gAccumulation[n].rgb);
            moment1 += neighbour;
            moment2 += neighbour * neighbour;
            neighbourMin = min(neighbourMin, neighbour);
            neighbourMax = max(neighbourMax, neighbour);
        }
    }

    // Variance-derived box, intersected with the true min/max. A raw min/max box over a
    // high-contrast neighbourhood is so wide that it rejects essentially no stale history, which is
    // exactly when ghosting appears; one standard deviation is the usual compromise between
    // rejecting ghosts and not clipping away genuine detail.
    const float kVarianceGamma = 1.0f;
    float3 mean                = moment1 / 9.0f;
    float3 sigma               = sqrt(max(moment2 / 9.0f - mean * mean, float3(0.0f, 0.0f, 0.0f)));
    float3 boxMin              = max(mean - kVarianceGamma * sigma, neighbourMin);
    float3 boxMax              = min(mean + kVarianceGamma * sigma, neighbourMax);

    // Keep the current sample inside the box, or clipToBox has no valid interior point to clip
    // toward.
    boxMin = min(boxMin, currentYCoCg);
    boxMax = max(boxMax, currentYCoCg);

    // Reproject. Motion vectors are 2.5D: XY is the screen-space pixel delta "previous minus
    // current", and Z is the change in linear view depth (see HitShaderEntryPoints.slang).
    float4 motion  = gMotionVectors[c];
    float2 prevPos = float2(c) + 0.5f + motion.xy;

    bool isValid = gSettings.isReset == 0;
    isValid      = isValid && all(prevPos >= 0.0f) && all(prevPos < float2(dims));

    // Reject history across a depth discontinuity, using the motion vector's Z rather than a second
    // depth buffer read. mv.z is |prevZ| - |curZ|, so a large relative change means this pixel's
    // surface is not the one that was here last frame.
    float viewZ = abs(gDepthView[c].r);
    isValid     = isValid && (abs(motion.z) < 0.05f * max(viewZ, 1e-4f));

    float3 history      = sampleHistoryCatmullRom(prevPos, maxCoord);
    float3 historyYCoCg = clipToBox(RGBtoYCoCg(history), currentYCoCg, boxMin, boxMax);
    history             = YCoCgtoRGB(historyYCoCg);

    // Guard against a NaN/Inf history entry poisoning the buffer permanently.
    // NOTE: any(a || b) is rejected under HLSL 2021 ("operands for short-circuiting logical binary
    // operator must be scalar"), so reduce each vector to a scalar first.
    isValid = isValid && !(any(isnan(history)) || any(isinf(history)));

    // Scale the feedback weight with on-screen speed: history is worth leaning on when the camera
    // is nearly still (where shimmer is what the eye notices) and much less so during fast motion
    // (where accumulated resampling blur is what the eye notices).
    float speed  = length(motion.xy);
    float alpha  = lerp(gSettings.blendAlphaMin, gSettings.blendAlphaMax,
        saturate(speed / max(gSettings.speedReference, 1e-4f)));
    alpha        = isValid ? saturate(alpha) : 1.0f;

    float4 result = float4(lerp(history, current.rgb, alpha), current.a);

    if (gSettings.historyIndex == 0)
    {
        gTAAHistoryA[c] = result;
    }
    else
    {
        gTAAHistoryB[c] = result;
    }
}
