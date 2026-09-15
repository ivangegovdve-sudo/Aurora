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

#ifndef __MATERIALX_COMMON__
#define __MATERIALX_COMMON__

static ShadingData vertexData;

// --------------------------------------------------------------------------
// GLSL compatibility layer.
// The MaterialX Slang generator inherits from GlslShaderGenerator and uses
// GLSL-backed implementations for many nodes (image sampling, ramp, split,
// rotate, etc.).  Those implementations reference GLSL built-in names and
// types that Slang does not recognise natively.
// --------------------------------------------------------------------------

// GLSL built-in function → Slang equivalent.
#define mix lerp

// GLSL vector type → Slang equivalent.
// These are required by inlined GLSL node implementations (mx_image_*.glsl,
// mx_rotate_vector2.glsl, mx_ramplr_*.glsl, mx_splitlr_*.glsl, …).
// uvec3 is needed by noise implementations (mx_gradient_vec3, mx_hash_vec3).
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define uvec2 uint2
#define uvec3 uint3
#define uvec4 uint4

// --------------------------------------------------------------------------
// Texture infrastructure — from stdlib/genslang/lib/mx_texture.slang.
// --------------------------------------------------------------------------
struct SamplerTexture2D
{
    Texture2D tex;
    SamplerState sampler;
};

float4 textureLod(SamplerTexture2D t, float2 uv, float lod)
{
    return t.tex.SampleLevel(t.sampler, uv, lod);
}

float4 texture(SamplerTexture2D t, float2 uv)
{
    // Use SampleLevel(lod=0) — Sample opcode is invalid in ray tracing shaders.
    return t.tex.SampleLevel(t.sampler, uv, 0.0f);
}

float4 textureGrad(SamplerTexture2D t, float2 uv, float2 ddx_, float2 ddy_)
{
    return t.tex.SampleGrad(t.sampler, uv, ddx_, ddy_);
}

int2 textureSize(SamplerTexture2D t, int mipLevel)
{
    uint width, height, levels;
    t.tex.GetDimensions(mipLevel, width, height, levels);
    return int2(width, height);
}

// sampler2D is the GLSL name used by GLSL-backed node implementations and
// by the generated wrapper code.  Alias it to the Slang type above.
typealias sampler2D = SamplerTexture2D;

// Helper called by the generated evaluateMaterial wrapper to create samplers.
sampler2D createSampler2D(Texture2D<float4> textureObject, SamplerState s)
{
    sampler2D txt;
    txt.tex     = textureObject;
    txt.sampler = s;
    return txt;
}

// --------------------------------------------------------------------------
// Math helpers — from stdlib/genslang/lib/mx_math.slang.
// --------------------------------------------------------------------------
#define mx_inversesqrt rsqrt
#define mx_sin sin
#define mx_cos cos
#define mx_tan tan
#define mx_asin asin
#define mx_acos acos
#define mx_radians radians
#define mx_float_bits_to_int asint

// GLSL matrices are column-major while Slang is row-major, so we reverse
// the multiplication order to match the MaterialX spec (see mx_math.slang).
float2 mx_matrix_mul(float2 v, float2x2 m) { return mul(m, v); }
float3 mx_matrix_mul(float3 v, float3x3 m) { return mul(m, v); }
float4 mx_matrix_mul(float4 v, float4x4 m) { return mul(m, v); }
float2 mx_matrix_mul(float2x2 m, float2 v) { return mul(v, m); }
float3 mx_matrix_mul(float3x3 m, float3 v) { return mul(v, m); }
float4 mx_matrix_mul(float4x4 m, float4 v) { return mul(v, m); }
float2x2 mx_matrix_mul(float2x2 m1, float2x2 m2) { return mul(m2, m1); }
float3x3 mx_matrix_mul(float3x3 m1, float3x3 m2) { return mul(m2, m1); }
float4x4 mx_matrix_mul(float4x4 m1, float4x4 m2) { return mul(m2, m1); }

float  mx_square(float  x) { return x * x; }
float2 mx_square(float2 x) { return x * x; }
float3 mx_square(float3 x) { return x * x; }

float3 mx_srgb_encode(float3 color)
{
    bool3 isAbove = (color > float3(0.0031308));
    float3 linSeg = color * 12.92;
    float3 powSeg = 1.055 * pow(max(color, float3(0.0)), float3(1.0 / 2.4)) - 0.055;
    return select(isAbove, powSeg, linSeg);
}

float  mx_mod(float  a, float  b) { return (a - b * floor(a / b)); }
float2 mx_mod(float2 a, float2 b) { return (a - b * floor(a / b)); }
float3 mx_mod(float3 a, float3 b) { return (a - b * floor(a / b)); }
float4 mx_mod(float4 a, float4 b) { return (a - b * floor(a / b)); }
float2 mx_mod(float2 a, float  b) { return (a - b * floor(a / b)); }
float3 mx_mod(float3 a, float  b) { return (a - b * floor(a / b)); }
float4 mx_mod(float4 a, float  b) { return (a - b * floor(a / b)); }

float3x3 mx_inverse(float3x3 m)
{
    float one_over_det = 1.f / determinant(m);
    float3x3 result;
    result[0][0] = +(m[1][1]*m[2][2]-m[1][2]*m[2][1])*one_over_det;
    result[0][1] = -(m[0][1]*m[2][2]-m[0][2]*m[2][1])*one_over_det;
    result[0][2] = +(m[0][1]*m[1][2]-m[0][2]*m[1][1])*one_over_det;
    result[1][0] = -(m[1][0]*m[2][2]-m[1][2]*m[2][0])*one_over_det;
    result[1][1] = +(m[0][0]*m[2][2]-m[0][2]*m[2][0])*one_over_det;
    result[1][2] = -(m[0][0]*m[1][2]-m[0][2]*m[1][0])*one_over_det;
    result[2][0] = +(m[1][0]*m[2][1]-m[1][1]*m[2][0])*one_over_det;
    result[2][1] = -(m[0][0]*m[2][1]-m[0][1]*m[2][0])*one_over_det;
    result[2][2] = +(m[0][0]*m[1][1]-m[0][1]*m[1][0])*one_over_det;
    return result;
}

float  mx_atan(float y_over_x)           { return atan(y_over_x); }
float  mx_atan(float y, float x)         { return atan2(y, x); }
float2 mx_atan(float2 y, float2 x)      { return atan2(y, x); }
float3 mx_atan(float3 y, float3 x)      { return atan2(y, x); }
float4 mx_atan(float4 y, float4 x)      { return atan2(y, x); }

// --------------------------------------------------------------------------
// mx_transform_uv — Slang-native replacement for stdlib/genglsl/lib/mx_transform_uv.glsl.
// BSDFCodeGenerator maps the "lib/$fileTransformUv" include to this definition
// (defined here instead of the GLSL file so we avoid the vec2 dependency).
// --------------------------------------------------------------------------
float2 mx_transform_uv(float2 uv, float2 uv_scale, float2 uv_offset)
{
    uv = uv * uv_scale + uv_offset;
    return uv;
}

// --------------------------------------------------------------------------
// dFdx / dFdy stubs — ray tracing has no screen-space derivatives.
// All scalar and vector overloads are needed: adsk:bitmap uses float2,
// adsk:height_map (bump) uses float and float3.
// --------------------------------------------------------------------------
float  dFdx(float  val) { return 1.0 / 256.0; }
float  dFdy(float  val) { return 1.0 / 256.0; }
float2 dFdx(float2 val) { return float2(1.0 / 256.0, 1.0 / 256.0); }
float2 dFdy(float2 val) { return float2(1.0 / 256.0, 1.0 / 256.0); }
float3 dFdx(float3 val) { return float3(1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0); }
float3 dFdy(float3 val) { return float3(1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0); }

// --------------------------------------------------------------------------
// MaterialX constants used by GLSL-backed node implementations (e.g. adsk:height_map).
// These are defined in stdlib/genglsl/lib/mx_math.glsl but not emitted into
// the Slang path for GLSL-source node implementations.
// --------------------------------------------------------------------------
#ifndef M_FLOAT_EPS
#define M_FLOAT_EPS 1e-8
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TWO_PI
#define M_TWO_PI 6.28318530717958647692
#endif

#endif // __MATERIALX_COMMON__
