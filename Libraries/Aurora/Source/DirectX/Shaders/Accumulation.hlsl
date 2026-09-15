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

// Declare the root signature for the compute shader.
// NOTE: DESCRIPTORS_VOLATILE is specified for root signature 1.0 behavior, where descriptors do not
// have to be initialized in advance.
#define ROOT_SIGNATURE                                                                             \
    "RootFlags(0),"                                                                                \
    "DescriptorTable(UAV(u0, numDescriptors = 19, flags = DESCRIPTORS_VOLATILE)), "                \
    "RootConstants(b0, num32BitConstants = 5)"

// Source (input) and destination (output) textures.
RWTexture2D<float4> gAccumulation : register(u0);
RWTexture2D<float4> gResult : register(u1);
RWTexture2D<float> gDepthNDC : register(u2);
RWTexture2D<float> gDepthView : register(u3);
RWTexture2D<float4> gNormalRoughness : register(u4);
RWTexture2D<float4> gBaseColorMetalness : register(u5);
RWTexture2D<float4> gDiffuse : register(u6);
RWTexture2D<float4> gGlossy : register(u7);
RWTexture2D<float4> gMotionVectors : register(u8);
RWTexture2D<float4> gDiffuseDenoised : register(u9);
RWTexture2D<float4> gGlossyDenoised : register(u10);
// The material demodulation factors the ray generator divided out of the NRD input lobes, to be
// multiplied back in below. This table starts at the accumulation texture (heap index 1), so
// register uN is heap index N + 1; these sit at the end of the heap, past the descriptors only
// post-processing uses. See kDemodDescriptorOffset. Read-only in this pass.
RWTexture2D<float4> gDemodDiffuse : register(u16);
RWTexture2D<float4> gDemodSpecular : register(u17);
// Unit-length world normal with .w storing linear roughness. Written by the ray generator.
// NRD's guide cannot serve this purpose because of its descriptor layout.
// Read-only in this pass.
RWTexture2D<float4> gGuideNormalRoughness : register(u18);

// Layout of accumulation properties.
struct Accumulation
{
    uint sampleIndex;

    // Composite NRD's denoised diffuse/glossy outputs on top of the direct channel.
    bool isDenoisedCompositeEnabled;

    // A temporal stage downstream of this one owns frame-to-frame blending. Broader than the flag
    // above: an upscaler is a temporal stage even on a frame where NRD did not run.
    bool isTemporalResolveDownstream;

    // Sentinel view-Z written for sky / miss pixels; see FrameData::skyViewZ.
    float skyViewZ;

    // Reverse the material demodulation the ray generator applied to the NRD input lobes.
    bool remodulateMaterials;
};

// Constant buffer of accumulation values.
ConstantBuffer<Accumulation> gSettings : register(b0);

// A compute shader that accumulates path tracing results, optionally with denoising.
// NOTE: The indicated thread group dimensions provide good occupancy for the current code, and
// must match the values at the Dispatch() call.
[RootSignature(ROOT_SIGNATURE)]
[numthreads(16, 8, 1)]
void Accumulation(uint3 threadID : SV_DispatchThreadID)
{
    // Skip any shader invocation where the thread ID is outside the screen dimensions, as the
    // shader will be invoked with more threads than pixels when the dimension are not evenly
    // divided by the thread group dimensions.
    uint2 screenDims;
    gAccumulation.GetDimensions(screenDims.x, screenDims.y);
    if (any(threadID.xy >= screenDims))
    {
        return;
    }

    // Get the screen coordinates (2D) from the thread ID, and the color / alpha from the result of
    // the most recent sample. Treat the result as the "extra" shading value, optionally used below.
    float2 screenCoords = threadID.xy;
    float4 result       = gResult[screenCoords];
    float3 extra        = result.rgb;

    // Combine data from textures if denoising is enabled. Otherwise the "result" value has the
    // complete path tracing output.
    if (gSettings.isDenoisedCompositeEnabled)
    {
        // A "hit factor" of 0 for a background sample and 1 for a surface sample. Background is
        // detected against the sky sentinel the miss shader writes (see FrameData::skyViewZ), which
        // is a large finite negative value rather than -INFINITY, because an infinity in the view-Z
        // guide is a NaN hazard inside NRD. Real geometry (|viewZ| <= denoisingRange) and the
        // sentinel (twice that) sit comfortably either side of this midpoint.
        float hitFactor = gDepthView[screenCoords].r > (gSettings.skyViewZ * 0.75f) ? 1.0f : 0.0f;

        // RELAX outputs linear RGB directly, so no inverse colour transform is needed here.
        float3 denoisedDiffuse = gDiffuseDenoised[screenCoords].rgb * hitFactor;
        float3 denoisedGlossy  = gGlossyDenoised[screenCoords].rgb * hitFactor;

        // Re-modulate: put back the material the ray generator divided out before NRD saw the
        // signal. The same factors from the same buffers, so the round trip is lossless wherever
        // the denoiser is a no-op. See MainEntryPoints.slang.
        if (gSettings.remodulateMaterials)
        {
            denoisedDiffuse *= gDemodDiffuse[screenCoords].rgb;
            denoisedGlossy  *= gDemodSpecular[screenCoords].rgb;
        }

        // Combine the following:
        // - Extra: direct shading (not denoised). Stored in gResult by the ray gen shader.
        // - The denoised diffuse radiance.
        // - The denoised glossy radiance.
        result.rgb = extra + denoisedDiffuse + denoisedGlossy;
    }

    // If the sample index is greater than zero, blend the new result color with the previous
    // accumulation color.
    uint sampleIndex = gSettings.sampleIndex;
    if (sampleIndex > 0)
    {
        // Get the previous result. If it has an infinity component, then it represents an error
        // pixel, and it should remain unmodified. Otherwise blend it with the new result.
        float4 prevResult = gAccumulation[screenCoords];
        if (any(isinf(prevResult)))
        {
            result = prevResult;
        }
        else
        {
            // Compute a blend factor (between the previous and new result) based on the sample
            // index, with the new result having less influence with an increasing sample index,
            // e.g. with sample index #4 (the 5th sample), the final result is 4/5 of the previous
            // (accumulated) result and 1/5 of the new result. When a temporal stage downstream of
            // this one owns frame-to-frame blending, t = 1.0 bypasses the progressive average
            // entirely: a second accumulation layer in series compounds latency, and a temporal
            // upscaler's heuristics expect the raw per-frame signal, not a pre-averaged one.
            float t = gSettings.isTemporalResolveDownstream ? 1.0f : 1.0f / (sampleIndex + 1);

            // Blend between the previous result and the new result using the factor.
            result = lerp(prevResult, result, t);
        }
    }

    // Write to the output (accumulation) buffer.
    gAccumulation[screenCoords] = result;
}