
// Extensions required (should be in prefix)
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require

// Buffer types for vertex data.
// NOTE: A vec3 array in a PhysicalStorageBuffer gets stride 16 under Vulkan's relaxed layout
// rules, but the vertex buffers are tightly packed at stride 12. Declare them as float arrays
// (stride 4) and reconstruct vec3/vec2 in the accessors below.
layout(buffer_reference, std430, buffer_reference_align=4, scalar) buffer Indices     { uint i[]; };
layout(buffer_reference, std430, buffer_reference_align=4, scalar) buffer Positions   { float v[]; };
layout(buffer_reference, std430, buffer_reference_align=4, scalar) buffer Normals     { float n[]; };
layout(buffer_reference, std430, buffer_reference_align=4, scalar) buffer Tangents    { float tn[]; };
layout(buffer_reference, std430, buffer_reference_align=4, scalar) buffer TexCoords   { float t[]; };

// Array of textures and samplers for all instances.
layout(binding = 6) uniform sampler2D textureSamplers[];

// Function prototypes for accessors defined below. Required because transpiled code calls
// these functions before their definitions appear. getMaterial_0 is omitted since it's
// defined after the MaterialConstants_0 struct.
uvec3 getIndicesForTriangle_0(int triangleIndex);
vec3  getPositionForVertex_0(int vertexIndex);
vec3  getNormalForVertex_0(int vertexIndex);
vec3  getTangentForVertex_0(int vertexIndex);
vec2  getTexCoordForVertex_0(int vertexIndex);
bool  instanceHasNormals_0();
bool  instanceHasTangents_0();
bool  instanceHasTexCoords_0();
bool  instanceIsOpaque_0();
int   getInstanceBufferOffset_0();
vec4  sampleBaseColorTexture_0(int mtlOffset, vec2 uv, float level);
vec4  sampleSpecularRoughnessTexture_0(int mtlOffset, vec2 uv, float level);
vec4  sampleEmissionColorTexture_0(int mtlOffset, vec2 uv, float level);
vec4  sampleNormalTexture_0(int mtlOffset, vec2 uv, float level);
vec4  sampleOpacityTexture_0(int mtlOffset, vec2 uv, float level);

// Split marker: code above is self-contained, code below requires MaterialConstants_0.
#define AURORA_SPLIT_REQUIRES_TRANSPILED_TYPES

// Buffer type for material.
layout(buffer_reference, std430, scalar) buffer Materials   { MaterialConstants_0 m[]; };

// Shader record for hit shaders. Must match HitGroupShaderRecord struct in HGIScene.h.
layout(shaderRecordEXT, std430) buffer InstanceShaderRecord
{
    // Geometry data.
    Indices indices;
    Positions positions;
    Normals normals;
    Tangents tangents;
    TexCoords texcoords;

    // Material data.
    Materials material;

    // Index into texture sampler array for material's textures.
    int baseColorTextureIndex;
    int specularRoughnessTextureIndex;
    int normalTextureIndex;
    int opacityTextureIndex;
    int emissionTextureIndex;

    // Geometry flags.
    uint hasNormals;
    uint hasTangents;
    uint hasTexCoords;
    uint isOpaque;
    int instanceBufferOffset;
} instance;


// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
uvec3 getIndicesForTriangle_0(int triangleIndex) {
    uint v0 = instance.indices.i[triangleIndex*3+0];
    uint v1 = instance.indices.i[triangleIndex*3+1];
    uint v2 = instance.indices.i[triangleIndex*3+2];

    return uvec3(v0,v1,v2);
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
vec3 getPositionForVertex_0(int vertexIndex) {
    int base = vertexIndex * 3;
    return vec3(instance.positions.v[base], instance.positions.v[base + 1], instance.positions.v[base + 2]);
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
vec3 getNormalForVertex_0(int vertexIndex) {
    int base = vertexIndex * 3;
    return vec3(instance.normals.n[base], instance.normals.n[base + 1], instance.normals.n[base + 2]);
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
vec3 getTangentForVertex_0(int vertexIndex) {
    int base = vertexIndex * 3;
    return vec3(instance.tangents.tn[base], instance.tangents.tn[base + 1], instance.tangents.tn[base + 2]);
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
vec2 getTexCoordForVertex_0(int vertexIndex) {
    int base = vertexIndex * 2;
    return vec2(instance.texcoords.t[base], instance.texcoords.t[base + 1]);
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
bool instanceHasNormals_0() {
    return instance.hasNormals!=0;
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
bool instanceHasTangents_0() {
    return instance.hasTangents!=0;
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
bool instanceHasTexCoords_0() {
    return instance.hasTexCoords!=0;
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
bool instanceIsOpaque_0() {
    return instance.isOpaque!=0;
}

// Implementation for forward declared geometry accessor function in PathTracingCommon.slang.
int getInstanceBufferOffset_0() {
    return instance.instanceBufferOffset;
}

// Implementation for forward declared material accessor function in Material.hlsli.
MaterialConstants_0 getMaterial_0() {
    return instance.material.m[0];
}

// Implementation for forward declared texture sample function in Material.hlsli.
vec4 sampleBaseColorTexture_0(int /*mtlOffset*/, vec2 uv, float level) {
    return texture(textureSamplers[nonuniformEXT(instance.baseColorTextureIndex)], uv);
}

// Implementation for forward declared texture sample function in Material.hlsli.
vec4 sampleSpecularRoughnessTexture_0(int /*mtlOffset*/, vec2 uv, float level) {
    return texture(textureSamplers[nonuniformEXT(instance.specularRoughnessTextureIndex)], uv);
}

// Implementation for forward declared texture sample function in Material.hlsli.
vec4 sampleEmissionColorTexture_0(int /*mtlOffset*/, vec2 uv, float level) {
    // A material with no emission texture leaves this at kInvalidTextureIndex (-1).
    if (instance.emissionTextureIndex < 0)
        return vec4(0.0);
    return texture(textureSamplers[nonuniformEXT(instance.emissionTextureIndex)], uv);
}

// Implementation for forward declared texture sample function in Material.hlsli.
vec4 sampleNormalTexture_0(int /*mtlOffset*/, vec2 uv, float level) {
    return texture(textureSamplers[nonuniformEXT(instance.normalTextureIndex)], uv);
}

// Implementation for forward declared texture sample function in Material.hlsli.
vec4 sampleOpacityTexture_0(int /*mtlOffset*/, vec2 uv, float level) {
    return texture(textureSamplers[nonuniformEXT(instance.opacityTextureIndex)], uv);
}