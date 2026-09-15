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

#include "Loaders.h"
#include "SceneContents.h"

#include "Aurora/Foundation/Geometry.h"
#include <glm/gtx/quaternion.hpp>

#include <fstream>
#include <regex>

// Build a 4×4 world-space transform from a glTF node (matrix or TRS).
static glm::mat4 getNodeTransform(const tinygltf::Node& node)
{
    if (!node.matrix.empty())
        return glm::make_mat4(node.matrix.data());

    glm::vec3 translation(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale(1.0f);

    if (!node.translation.empty())
        translation = glm::vec3(static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2]));

    if (!node.rotation.empty())
        rotation = glm::quat(static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]));

    if (!node.scale.empty())
        scale = glm::vec3(static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2]));

    return glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) *
        glm::scale(glm::mat4(1.0f), scale);
}

// Recursively collect camera nodes into sceneContents.
// TODO: propagate parentTransform so nested camera nodes get the correct world-space view matrix.
static void loadCameras(
    tinygltf::Model& model, SceneContents& sceneContents, std::vector<int>& nodes)
{
    for (int nodeIdx : nodes)
    {
        tinygltf::Node& node = model.nodes[nodeIdx];
        if (node.camera != -1)
        {
            SceneCamera sceneCamera;
            sceneCamera.name       = node.name;
            sceneCamera.viewMatrix = glm::inverse(getNodeTransform(node));

            tinygltf::Camera& camera = model.cameras[node.camera];
            if (camera.type == "perspective")
            {
                sceneCamera.cameraType                      = SCENE_CAMERA_TYPE_PERSPECTIVE;
                sceneCamera.perspectiveProperties.yfov      = static_cast<float>(camera.perspective.yfov);
                sceneCamera.perspectiveProperties.aspectRatio =
                    static_cast<float>(camera.perspective.aspectRatio);
                sceneCamera.perspectiveProperties.znear = static_cast<float>(camera.perspective.znear);
                sceneCamera.perspectiveProperties.zfar  = static_cast<float>(camera.perspective.zfar);
            }
            else if (camera.type == "orthographic")
            {
                sceneCamera.cameraType                       = SCENE_CAMERA_TYPE_ORTHOGRAPHIC;
                sceneCamera.orthographicProperties.znear = static_cast<float>(camera.orthographic.znear);
                sceneCamera.orthographicProperties.zfar  = static_cast<float>(camera.orthographic.zfar);
                sceneCamera.orthographicProperties.xmag  = static_cast<float>(camera.orthographic.xmag);
                sceneCamera.orthographicProperties.ymag  = static_cast<float>(camera.orthographic.ymag);
            }
            sceneContents.cameras.push_back(sceneCamera);
        }
        loadCameras(model, sceneContents, node.children);
    }
}

// Decode a glTF accessor into a flat float array.
// Only FLOAT component type is supported; other types (e.g. normalized BYTE/SHORT) would require
// de-normalisation and are not used by Aurora's loaders today.
static std::vector<float> decodeFloatAccessor(const tinygltf::Model& model, int accessorIdx)
{
    if (accessorIdx < 0)
        return {};
    const tinygltf::Accessor&   acc = model.accessors[accessorIdx];
    const tinygltf::BufferView& bv  = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer&     buf = model.buffers[bv.buffer];

    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        AU_WARN("Skipping accessor with unsupported component type %d (only FLOAT is supported).",
            acc.componentType);
        return {};
    }

    int componentCount = tinygltf::GetNumComponentsInType(acc.type);
    std::vector<float> out;
    out.reserve(acc.count * componentCount);

    size_t stride = bv.byteStride ? bv.byteStride : static_cast<size_t>(componentCount) * sizeof(float);
    const uint8_t* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset;

    for (size_t i = 0; i < acc.count; ++i, ptr += stride)
    {
        const float* row = reinterpret_cast<const float*>(ptr);
        for (int c = 0; c < componentCount; ++c)
            out.push_back(row[c]);
    }
    return out;
}

// Decode a glTF index accessor into a uint32 array (handles BYTE / SHORT / INT).
static std::vector<uint32_t> decodeIndexAccessor(const tinygltf::Model& model, int accessorIdx)
{
    if (accessorIdx < 0)
        return {};
    const tinygltf::Accessor&   acc = model.accessors[accessorIdx];
    const tinygltf::BufferView& bv  = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer&     buf = model.buffers[bv.buffer];

    // glTF spec §3.6.2.4: index accessors must have byteStride == 0 (tightly packed), but handle
    // non-zero stride defensively by deriving element size from the component type.
    size_t componentSize = 0;
    switch (acc.componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  componentSize = 1; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: componentSize = 2; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   componentSize = 4; break;
    default:
        AU_WARN("Skipping index accessor with unsupported component type %d.", acc.componentType);
        return {};
    }
    size_t stride = bv.byteStride ? bv.byteStride : componentSize;

    const uint8_t* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset;
    std::vector<uint32_t> out;
    out.reserve(acc.count);

    for (size_t i = 0; i < acc.count; ++i)
    {
        const uint8_t* elem = ptr + i * stride;
        uint32_t idx = 0;
        switch (acc.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            idx = static_cast<uint32_t>(*elem);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            idx = static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(elem));
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            idx = *reinterpret_cast<const uint32_t*>(elem);
            break;
        default:
            break;
        }
        out.push_back(idx);
    }
    return out;
}

// Load a single glTF node (and its children) into the Aurora scene.
// parentTransform: accumulated world-space transform from ancestor nodes.
// materialPaths:   per-material-index Aurora paths (fallback when no node extension is present).
static void loadNode(const tinygltf::Model& model, int nodeIdx,
    const glm::mat4& parentTransform, Aurora::IScene* pScene,
    SceneContents& sceneContents, const std::vector<Aurora::Path>& materialPaths,
    const std::string& filePath, int& geomCounter)
{
    const tinygltf::Node& node = model.nodes[nodeIdx];
    glm::mat4 localTransform   = parentTransform * getNodeTransform(node);

    if (node.mesh >= 0)
    {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx)
        {
            const tinygltf::Primitive& prim = mesh.primitives[primIdx];
            if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            // --- Decode geometry ---
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
                continue;

            std::vector<float> positions = decodeFloatAccessor(model, posIt->second);
            if (positions.empty())
                continue;

            std::vector<float> normals;
            auto nrmIt = prim.attributes.find("NORMAL");
            if (nrmIt != prim.attributes.end())
                normals = decodeFloatAccessor(model, nrmIt->second);

            std::vector<float> texCoords;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
                texCoords = decodeFloatAccessor(model, uvIt->second);

            std::vector<uint32_t> indices = decodeIndexAccessor(model, prim.indices);
            if (indices.empty())
            {
                // Non-indexed mesh: generate sequential indices.
                uint32_t vCount = static_cast<uint32_t>(positions.size() / 3);
                indices.resize(vCount);
                for (uint32_t i = 0; i < vCount; ++i) indices[i] = i;
            }

            uint32_t vertexCount = static_cast<uint32_t>(positions.size()) / 3;
            uint32_t indexCount  = static_cast<uint32_t>(indices.size());

            // Generate flat normals when the mesh provides none.
            if (normals.empty())
            {
                normals.resize(positions.size(), 0.0f);
                Foundation::calculateNormals(vertexCount, positions.data(),
                    indexCount / 3, indices.data(), normals.data());
            }

            std::string geomPath = filePath + ":gltfGeom:" + to_string(geomCounter);
            std::string instPath = filePath + ":gltfInst:" + to_string(geomCounter);
            ++geomCounter;

            SceneGeometryData& geomData = sceneContents.addGeometry(geomPath);
            geomData.positions          = std::move(positions);
            geomData.normals            = std::move(normals);
            geomData.texCoords          = std::move(texCoords);
            geomData.indices            = std::move(indices);

            Aurora::GeometryDescriptor& geomDesc = geomData.descriptor;
            geomDesc.type                        = Aurora::PrimitiveType::Triangles;
            geomDesc.vertexDesc.attributes[Aurora::Names::VertexAttributes::kPosition] =
                Aurora::AttributeFormat::Float3;
            geomDesc.vertexDesc.attributes[Aurora::Names::VertexAttributes::kNormal] =
                Aurora::AttributeFormat::Float3;
            if (!geomData.texCoords.empty())
                geomDesc.vertexDesc.attributes[Aurora::Names::VertexAttributes::kTexCoord0] =
                    Aurora::AttributeFormat::Float2;
            geomDesc.vertexDesc.count = vertexCount;
            geomDesc.indexCount       = indexCount;

            geomDesc.getAttributeData = [geomPath, &sceneContents](
                                            Aurora::AttributeDataMap& buffers, size_t, size_t,
                                            size_t, size_t) {
                SceneGeometryData& gd = sceneContents.geometry[geomPath];
                buffers[Aurora::Names::VertexAttributes::kPosition].address = gd.positions.data();
                buffers[Aurora::Names::VertexAttributes::kPosition].size =
                    gd.positions.size() * sizeof(float);
                buffers[Aurora::Names::VertexAttributes::kPosition].stride = sizeof(vec3);

                buffers[Aurora::Names::VertexAttributes::kNormal].address = gd.normals.data();
                buffers[Aurora::Names::VertexAttributes::kNormal].size =
                    gd.normals.size() * sizeof(float);
                buffers[Aurora::Names::VertexAttributes::kNormal].stride = sizeof(vec3);

                if (!gd.texCoords.empty())
                {
                    buffers[Aurora::Names::VertexAttributes::kTexCoord0].address =
                        gd.texCoords.data();
                    buffers[Aurora::Names::VertexAttributes::kTexCoord0].size =
                        gd.texCoords.size() * sizeof(float);
                    buffers[Aurora::Names::VertexAttributes::kTexCoord0].stride = sizeof(vec2);
                }

                buffers[Aurora::Names::VertexAttributes::kIndices].address = gd.indices.data();
                buffers[Aurora::Names::VertexAttributes::kIndices].size =
                    gd.indices.size() * sizeof(uint32_t);
                buffers[Aurora::Names::VertexAttributes::kIndices].stride = sizeof(uint32_t);
                return true;
            };

            pScene->setGeometryDescriptor(geomPath, geomDesc);

            // --- Material ---
            // Priority 1: node-level AURORA_materials_materialX extension.
            // Priority 2: primitive's glTF material index.
            Aurora::Path materialPath;

            auto nodeExtIt = node.extensions.find("AURORA_materials_materialX");
            if (nodeExtIt != node.extensions.end() && nodeExtIt->second.IsObject())
            {
                const tinygltf::Value& ext = nodeExtIt->second;
                if (ext.Has("materialXFile"))
                {
                    std::string mtlxRelPath = ext.Get("materialXFile").Get<std::string>();
                    auto gltfDir            = filesystem::path(filePath).parent_path();
                    std::string mtlxAbsPath = (gltfDir / mtlxRelPath).generic_string();

                    std::ifstream ifs(mtlxAbsPath);
                    if (ifs)
                    {
                        std::string mtlxContent((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());

                        // Prefix relative texture paths with the MTLX file's directory so
                        // Aurora's resource loader can find them regardless of working directory.
                        //
                        // The regex matches only <input> elements that carry type="filename"
                        // (via the lookahead), preventing accidental rewrites of non-filename
                        // value attributes that happen to end in an image extension.
                        //
                        // Group 1: everything up to and including value="
                        // Group 2: the relative path (ending with image extension)
                        // Replacement: group1 + absolute-dir + group2
                        // The closing quote of the value attribute is outside the match and
                        // is preserved automatically.
                        static const std::regex kRelativeImagePath(
                            R"((<input\b(?=[^>]*\btype="filename")[^>]*\bvalue=")((?![A-Za-z]:|/)[^"]+\.(jpg|jpeg|png|bmp|hdr|exr|tga|tiff)))");
                        std::string mtlxDir =
                            filesystem::path(mtlxAbsPath).parent_path().generic_string() + "/";
                        mtlxContent = std::regex_replace(
                            mtlxContent, kRelativeImagePath, "$1" + mtlxDir + "$2");

                        materialPath = "AURORA_mtlx:" + mtlxAbsPath;
                        pScene->setMaterialType(materialPath,
                            Aurora::Names::MaterialTypes::kMaterialX, mtlxContent);
                    }
                    else
                    {
                        AU_WARN("Could not open MaterialX file: %s", mtlxAbsPath.c_str());
                    }
                }
            }

            if (materialPath.empty() && prim.material >= 0 &&
                static_cast<size_t>(prim.material) < materialPaths.size())
            {
                materialPath = materialPaths[prim.material];
            }

            // --- Instance ---
            Aurora::Properties instProps = {
                { Aurora::Names::InstanceProperties::kTransform, localTransform }
            };
            if (!materialPath.empty())
                instProps[Aurora::Names::InstanceProperties::kMaterial] = materialPath;

            pScene->addInstance(instPath, geomPath, instProps);

            Aurora::InstanceDefinition instDef = { instPath, instProps };
            sceneContents.instances.push_back({ instDef, geomPath });
            sceneContents.vertexCount   += vertexCount;
            sceneContents.triangleCount += indexCount / 3;

            // Update scene bounds.
            for (size_t vi = 0; vi + 2 < geomData.positions.size(); vi += 3)
            {
                sceneContents.bounds.add(glm::vec3(localTransform *
                    glm::vec4(geomData.positions[vi], geomData.positions[vi + 1],
                        geomData.positions[vi + 2], 1.0f)));
            }
        }
    }

    for (int childIdx : node.children)
        loadNode(model, childIdx, localTransform, pScene, sceneContents, materialPaths,
            filePath, geomCounter);
}

// Loads a glTF file into the renderer and scene from the specified file path.
// Supports the AURORA_materials_materialX node extension for MaterialX material binding.
bool loadglTFFile(Aurora::IRenderer* /* pRenderer */, Aurora::IScene* pScene,
    const string& filePath, SceneContents& sceneContents)
{
    sceneContents.cameras.clear();

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    string warnings, errors;

    // Try binary first (fast magic-byte check); fall back to ASCII.
    bool result = loader.LoadBinaryFromFile(&model, &errors, &warnings, filePath);
    if (!result)
        result = loader.LoadASCIIFromFile(&model, &errors, &warnings, filePath);
    if (!result)
    {
        AU_ERROR("Failed to load glTF file: %s — %s", filePath.c_str(), errors.c_str());
        return false;
    }

    for (tinygltf::Scene& scene : model.scenes)
        loadCameras(model, sceneContents, scene.nodes);

    // Camera-only load path (pScene == nullptr).
    if (!pScene)
        return true;

    // Build Aurora materials for each glTF material (fallback when no node extension is present).
    std::vector<Aurora::Path> materialPaths;
    materialPaths.reserve(model.materials.size());
    for (size_t mi = 0; mi < model.materials.size(); ++mi)
    {
        const tinygltf::Material& mat = model.materials[mi];
        Aurora::Path matPath          = filePath + ":gltfMat:" + to_string(mi);

        const auto& pbr = mat.pbrMetallicRoughness;
        glm::vec3 baseColor(static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]));

        Aurora::Properties props = {
            { "base_color",         baseColor },
            { "metalness",          static_cast<float>(pbr.metallicFactor) },
            { "specular_roughness", static_cast<float>(pbr.roughnessFactor) },
        };
        pScene->setMaterialProperties(matPath, props);
        materialPaths.push_back(matPath);
    }

    // Recursively load all scene nodes.
    int geomCounter = 0;
    for (tinygltf::Scene& scene : model.scenes)
    {
        for (int nodeIdx : scene.nodes)
            loadNode(model, nodeIdx, glm::mat4(1.0f), pScene, sceneContents,
                materialPaths, filePath, geomCounter);
    }

    return true;
}

bool saveglTFFile(Aurora::IRenderer* /* pRenderer */, Aurora::IScene* /* pScene */,
    const string& filePath, SceneContents& sceneContents)
{
    if (sceneContents.cameras.empty())
        return false;

    tinygltf::TinyGLTF writer;
    tinygltf::Model    model;
    tinygltf::Scene    scene;

    for (auto& camera : sceneContents.cameras)
    {
        tinygltf::Camera gltfCamera;
        gltfCamera.name = camera.name;

        if (camera.cameraType == SCENE_CAMERA_TYPE_PERSPECTIVE)
        {
            gltfCamera.type                    = "perspective";
            gltfCamera.perspective.yfov        = camera.perspectiveProperties.yfov;
            gltfCamera.perspective.aspectRatio = camera.perspectiveProperties.aspectRatio;
            gltfCamera.perspective.znear       = camera.perspectiveProperties.znear;
            gltfCamera.perspective.zfar        = camera.perspectiveProperties.zfar;
        }
        else
        {
            gltfCamera.type                      = "orthographic";
            gltfCamera.orthographic.znear        = camera.orthographicProperties.znear;
            gltfCamera.orthographic.zfar         = camera.orthographicProperties.zfar;
            gltfCamera.orthographic.xmag         = camera.orthographicProperties.xmag;
            gltfCamera.orthographic.ymag         = camera.orthographicProperties.ymag;
        }

        size_t cameraIndex = model.cameras.size();
        model.cameras.push_back(gltfCamera);

        tinygltf::Node cameraNode;
        cameraNode.name   = camera.name;
        cameraNode.camera = static_cast<int>(cameraIndex);

        // Flatten the column-major glm matrix into the glTF double array.
        glm::mat4 nodeMatrix = glm::inverse(camera.viewMatrix);
        const float* m       = glm::value_ptr(nodeMatrix);
        for (int i = 0; i < 16; ++i)
            cameraNode.matrix.push_back(static_cast<double>(m[i]));

        size_t nodeIndex = model.nodes.size();
        model.nodes.push_back(cameraNode);
        scene.nodes.push_back(static_cast<int>(nodeIndex));
    }

    model.scenes.push_back(scene);
    model.defaultScene = 0;

    return writer.WriteGltfSceneToFile(&model, filePath, false, false, true, false);
}
