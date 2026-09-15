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

#include "MaterialGenerator.h"

#include "MaterialBase.h"

namespace Aurora
{
namespace MaterialXCodeGen
{



MaterialGenerator::MaterialGenerator(const string& mtlxFolder)
{
    // Create code generator.
    _pCodeGenerator = make_unique<MaterialXCodeGen::BSDFCodeGenerator>(mtlxFolder);

    // Map of MaterialX output parameters to Aurora material property.
    // Used by PTRenderer::generateMaterialX.
    // clang-format off
    // Maps MaterialX BSDF output param names → Aurora Material struct field names.
    // The BSDFCodeGenerator processes the adsk compound's interface inputs (outer node)
    // and outputs them as setup function OUT-params keyed by compound input name.
    // Standard_surface inputs that are constant inside the compound are not output here;
    // they are handled separately via standardSurfaceConstants / standardSurfaceBoolConstants.
    _bsdfInputParamMapping =
    {
        // Standard surface inputs (used when material is a plain standard_surface).
        { "base",                             "base"                              },
        { "base_color",                       "baseColor"                         },
        { "diffuse_roughness",                "diffuseRoughness"                  },
        { "metalness",                        "metalness"                         },
        { "specular",                         "specular"                          },
        { "specular_color",                   "specularColor"                     },
        { "specular_roughness",               "specularRoughness"                 },
        { "specular_anisotropy",              "specularAnisotropy"                },
        { "specular_IOR",                     "specularIOR"                       },
        { "specular_rotation",                "specularRotation"                  },
        { "transmission",                     "transmission"                      },
        { "transmission_color",               "transmissionColor"                 },
        { "subsurface",                       "subsurface"                        },
        { "subsurface_color",                 "subsurfaceColor"                   },
        { "subsurface_scale",                 "subsurfaceScale"                   },
        { "subsurface_radius",                "subsurfaceRadius"                  },
        { "sheen",                            "sheen"                             },
        { "sheen_color",                      "sheenColor"                        },
        { "sheen_roughness",                  "sheenRoughness"                    },
        { "coat",                             "coat"                              },
        { "coat_color",                       "coatColor"                         },
        { "coat_roughness",                   "coatRoughness"                     },
        { "coat_anisotropy",                  "coatAnisotropy"                    },
        { "coat_rotation",                    "coatRotation"                      },
        { "coat_IOR",                         "coatIOR"                           },
        { "coat_affect_color",                "coatAffectColor"                   },
        { "coat_affect_roughness",            "coatAffectRoughness"               },
        { "emission",                         "emission"                          },
        { "emission_color",                   "emissionColor"                     },
        { "opacity",                          "opacity"                           },
        { "thin_walled",                      "thinWalled"                        },
        { "normal",                           "normal"                            },
        { "base_color_image_scale",           "baseColorTexTransform.scale"       },
        { "base_color_image_offset",          "baseColorTexTransform.offset"      },
        { "base_color_image_rotation",        "baseColorTexTransform.rotation"    },
        { "emission_color_image_scale",       "emissionColorTexTransform.scale"   },
        { "emission_color_image_offset",      "emissionColorTexTransform.offset"  },
        { "emission_color_image_rotation",    "emissionColorTexTransform.rotation"},
        { "opacity_image_scale",              "opacityTexTransform.scale"         },
        { "opacity_image_offset",             "opacityTexTransform.offset"        },
        { "opacity_image_rotation",           "opacityTexTransform.rotation"      },
        { "normal_image_scale",               "normalTexTransform.scale"          },
        { "normal_image_offset",              "normalTexTransform.offset"         },
        { "normal_image_rotation",            "normalTexTransform.rotation"       },
        { "specular_roughness_image_scale",   "specularRoughnessTexTransform.scale"    },
        { "specular_roughness_image_offset",  "specularRoughnessTexTransform.offset"   },
        { "specular_roughness_image_rotation","specularRoughnessTexTransform.rotation" },
        // adsk compound interface inputs (processed via BSDFCodeGenerator's outer-compound path).
        // These map the adsk-specific parameter names to Aurora Material struct fields.
        { "layered_diffuse",              "baseColor"         },
        { "layered_roughness",            "specularRoughness" },
        { "layered_fraction",             "coat"              },
        { "layered_anisotropy",           "specularAnisotropy"},
        { "layered_rotation",             "specularRotation"  },
        { "layered_normal",               "normal"            },
        { "layered_bottom_f0",            "specularColor"     },
        { "opaque_albedo",                "baseColor"         },
        { "metal_f0",                     "baseColor"         },
        { "surface_roughness",            "specularRoughness" },
        { "surface_anisotropy",           "specularAnisotropy"},
        { "surface_rotation",             "specularRotation"  },
        { "surface_albedo",               "coatColor"         },
        { "surface_normal",               "normal"            },
        { "surface_cutout",               "opacity"           },
        { "transparent_ior",              "specularIOR"       },
        { "transparent_color",            "transmissionColor" },
        { "glazing_transmission_color",   "transmissionColor" },
        { "glazing_transmission_roughness","specularRoughness"},
        { "glazing_f0",                   "specularColor"     },
        { "opaque_luminance_modifier",    "emissionColor"     },
        { "opaque_mfp_modifier",          "subsurfaceColor"   },
    };
    // clang-format on
}

shared_ptr<MaterialDefinition> MaterialGenerator::generate(
    const string& document, const string& materialName, const string& baseDirectory)
{
    shared_ptr<MaterialDefinition> pDef;

    // First check if the material has already been generated and cached.
    auto cacheKey = DefinitionKey(document, materialName, baseDirectory);
    auto mtliter  = _definitions.find(cacheKey);
    if (mtliter != _definitions.end())
    {
        pDef = mtliter->second.lock();
        if (pDef)
            return pDef;
        _definitions.erase(mtliter);
    }

    // Currently every material has its own definitions.
    _pCodeGenerator->clearDefinitions();

    // Create code generator result struct.
    MaterialXCodeGen::BSDFCodeGenerator::Result res;

    // Strings for generated HLSL and entry point.
    string generatedMtlxSetupFunction;

    // The hardcoded inputs are added manually to the generated HLSL and passed to the
    // code-generated set up function.  This is filled in by the inputMapper lambda when the code is
    // generated.
    set<string> hardcodedInputs;

    // Create set of supported BSDF inputs.
    set<string> supportedBSDFInputs;
    for (auto iter : _bsdfInputParamMapping)
    {
        supportedBSDFInputs.insert(iter.first);
    }

    // Run the code generator overriding MaterialX document name, so that regardless of the name in
    // the document, the generated HLSL is based on a hardcoded name string for caching purposes.
    // NOTE: This will immediate run the code generator and invoke the inputMapper and outputMapper
    // function populate hardcodedInputs and set modifiedNormal.
    if (!_pCodeGenerator->generate(
            document, &res, supportedBSDFInputs, "MaterialXDocument", materialName, baseDirectory))
    {
        // Fail if code generation fails.
        // TODO: Proper error handling here.
        AU_ERROR("Failed to generate MaterialX code.");
        return nullptr;
    }

    // Create set of the generated BSDF inputs.
    set<string> bsdfInputs;
    for (size_t i = 0; i < res.bsdfInputs.size(); i++)
    {
        bsdfInputs.insert(res.bsdfInputs[i].name);
    }

    // Create set of the textures used by the generated setup function.
    map<string, size_t> textures;
    for (size_t i = 0; i < res.textures.size(); i++)
    {
        textures[res.textures[i].image] = i;
    }

    // Normal modified if any BSDF input maps to the "normal" Aurora field.
    // Covers both standard_surface ("normal") and adsk compound names ("surface_normal",
    // "layered_normal") since all map to "normal" in _bsdfInputParamMapping.
    bool modifiedNormal = false;
    for (const auto& bi : res.bsdfInputs)
    {
        auto it = _bsdfInputParamMapping.find(bi.name);
        if (it != _bsdfInputParamMapping.end() && it->second == "normal")
        {
            modifiedNormal = true;
            break;
        }
    }

    // Create unique shader name from the code generator hash.
    string shaderName = "MaterialX_" + Foundation::sHash(res.functionHash);

    string functionInterface = "Material evaluateMaterial_" + shaderName +
        "(ShadingData shading, int headerOffset, inout float3 "
        "materialNormal, out bool "
        "isGeneratedNormal)";

    // Append the material accessor functions used to read material properties from
    // ByteAddressBuffer.
    UniformBuffer mtlConstantsBuffer(res.materialProperties, res.materialPropertyDefaults);
    generatedMtlxSetupFunction +=
        "#pragma warning (disable : 30081) // Implicit conversion from 'int' to 'bool'\n\n";
    generatedMtlxSetupFunction += "struct " + res.materialStructName + "\n";
    generatedMtlxSetupFunction += mtlConstantsBuffer.generateHLSLStruct();
    generatedMtlxSetupFunction += ";\n\n";
    generatedMtlxSetupFunction +=
        mtlConstantsBuffer.generateByteAddressBufferAccessors(res.materialStructName + "_");
    generatedMtlxSetupFunction += "\n";
    generatedMtlxSetupFunction += res.materialSetupCode;

    // Create a wrapper function that is called by the ray hit entry point to initialize material.
    generatedMtlxSetupFunction += "\nexport " + functionInterface + " {\n";
    generatedMtlxSetupFunction += "\tint offset = headerOffset + kMaterialHeaderSize;\n";

    // Fill in the global vertexData struct (used by generated code to access vertex attributes).
    generatedMtlxSetupFunction += "\tvertexData = shading;\n";

    // Fill in the isGeneratedNormal output (set to true if the code-generated setup function
    // generated a normal)
    generatedMtlxSetupFunction += "\tisGeneratedNormal =" + to_string(modifiedNormal) + ";\n";

    // Reset material to default values.
    generatedMtlxSetupFunction += "\tMaterial material = defaultMaterial();\n";

    // Create sampler structs for each of the textures in the materialX material.
    vector<string> samplerNames;
    for (size_t i = 0; i < res.textureDefaults.size(); i++)
    {
        string samplerName = "sampler" + to_string(i);
        // Create sampler from Nth texture and sampler.
        generatedMtlxSetupFunction += "\tsampler2D " + samplerName +
            " = createSampler2D( materialTexture(headerOffset, " + to_string(i) +
            "), materialSampler(headerOffset, " + to_string(i) + ") );\n";
        samplerNames.push_back(samplerName);
    }

    // Use the define DISTANCE_UNIT which is set in the shader library options.
    generatedMtlxSetupFunction += "\tint distanceUnit = DISTANCE_UNIT;\n";

    // Create temporary material struct
    generatedMtlxSetupFunction += "\t" + res.materialStructName + " setupMaterialStruct;\n";

    // Fill struct using the byte address buffer accessors.
    for (size_t i = 0; i < res.materialProperties.size(); i++)
    {
        generatedMtlxSetupFunction += "\tsetupMaterialStruct." +
            res.materialProperties[i].variableName + " = " + res.materialStructName + "_" +
            res.materialProperties[i].variableName + "(gGlobalMaterialConstants, offset);\n";
        ;
    }

    // Add code to call the generate setup function.
    generatedMtlxSetupFunction += "\t" + res.setupFunctionName + "(\n";

    // First argument is material struct.
    generatedMtlxSetupFunction += "setupMaterialStruct";

    // Add code for all the texture arguments.
    for (size_t i = 0; i < samplerNames.size(); i++)
    {
        // Add texture name.
        generatedMtlxSetupFunction += ",\n";

        // Add texture name.
        generatedMtlxSetupFunction += samplerNames[i];
    }

    // Add distance unit parameter, if used.
    if (res.hasUnits)
    {
        // Add texture name.
        generatedMtlxSetupFunction += ",\n";

        // Add distance unit variable.
        generatedMtlxSetupFunction += "distanceUnit";
    }

    // Add the BSDF inputs that will be output from setup function.
    for (size_t i = 0; i < res.bsdfInputs.size(); i++)
    {
        generatedMtlxSetupFunction += ",\n";
        {
            string mappedBSDFInput = _bsdfInputParamMapping[res.bsdfInputs[i].name];
            AU_ASSERT(
                !mappedBSDFInput.empty(), "Invalid BSDF input:%s", res.bsdfInputs[i].name.c_str());
            if (mappedBSDFInput == "normal")
            {
                // All inputs that map to "normal" (standard_surface "normal",
                // adsk "surface_normal", "layered_normal") feed the materialNormal out-param.
                generatedMtlxSetupFunction += "\t\tmaterialNormal";
            }
            else
            {
                generatedMtlxSetupFunction += "\t\tmaterial." + mappedBSDFInput;
            }
        }
    }

    // Finish function call.
    generatedMtlxSetupFunction += ");\n";

    // Mirror baseColor into metalColor — Aurora-specific field not in standard_surface.
    // metalness is now a regular BSDF OUT param (constant or variable), so checking
    // bsdfInputs is sufficient to know whether this material is metallic.
    if (bsdfInputs.count("metalness"))
        generatedMtlxSetupFunction += "material.metalColor = material.baseColor;\n";

    // For compounds that gate emission/subsurface on runtime bool properties (e.g.
    // adsk:opaque's opaque_emission / opaque_translucency), the compound's internal
    // ifequal computation is not reachable through the outer interface, so it is not
    // produced by the setup function.  Detect by looking for those properties in the
    // registered material property list and emit the gating logic directly.
    string emissionBoolVar, luminanceFloatVar, translucencyVar;
    for (size_t pi = 0; pi < res.materialProperties.size(); pi++)
    {
        const string& name = res.materialProperties[pi].name;
        if (name == "opaque_emission")        emissionBoolVar  = res.materialProperties[pi].variableName;
        else if (name == "opaque_luminance")  luminanceFloatVar = res.materialProperties[pi].variableName;
        else if (name == "opaque_translucency") translucencyVar = res.materialProperties[pi].variableName;
    }
    if (!emissionBoolVar.empty() && !luminanceFloatVar.empty())
        generatedMtlxSetupFunction +=
            "if (setupMaterialStruct." + emissionBoolVar + " && setupMaterialStruct." +
            luminanceFloatVar + " > 0.0f)\n    material.emission = 1.0f;\n";
    if (!translucencyVar.empty())
        generatedMtlxSetupFunction +=
            "if (setupMaterialStruct." + translucencyVar + ")\n    material.subsurface = 1.0f;\n";

    // Return the generated material struct.
    generatedMtlxSetupFunction += "\treturn material;\n";

    // Finish setup wrapper function.
    generatedMtlxSetupFunction += "}\n";

    // Output the shader name
    MaterialShaderSource source(shaderName, generatedMtlxSetupFunction);

    // Generate the function definitions used by the MaterialX code.
    string definitionGLSL;
    _pCodeGenerator->generateDefinitions(&definitionGLSL);

    // Add generated Slang definitions from the MaterialX code generator.
    source.definitions = "#include \"MaterialXCommon.slang\"\n";
    source.definitions +=
        "#pragma warning (disable : 30056) // Non-short-circuiting `?:` operator is deprecated\n\n";
    source.definitions += definitionGLSL;
    source.definitions += "#include \"GlobalPipelineState.slang\"\n";
    source.definitions += "#include \"GlobalBufferAccessors.slang\"\n";
    source.definitions += "#include \"Material.slang\"\n";

    source.setupFunctionDeclaration = functionInterface;

    // Create the default values object from the generated properties and textures.
    MaterialDefaultValues defaults(
        res.materialProperties, res.materialPropertyDefaults, res.textureDefaults);

    // Opaque if no opacity/transmission output exists — either as a variable interface input
    // or as a compound constant (now both surface as regular BSDF outputs in bsdfInputs).
    bool isOpaque = bsdfInputs.find("opacity") == bsdfInputs.end() &&
        bsdfInputs.find("transmission") == bsdfInputs.end() &&
        bsdfInputs.find("transparent_color") == bsdfInputs.end() &&
        bsdfInputs.find("glazing_transmission_color") == bsdfInputs.end();

    // Create update function which just sets opacity flag based on MaterialX inputs.
    function<void(MaterialBase&)> updateFunc = [isOpaque](MaterialBase& mtl) {
        mtl.setIsOpaque(isOpaque);
    };

    // Create the material definition.
    pDef = make_shared<MaterialDefinition>(source, defaults, updateFunc, isOpaque);

    // Set in the cache.
    _definitions[cacheKey] = pDef;

    return pDef;
}

} // namespace MaterialXCodeGen
} // namespace Aurora
