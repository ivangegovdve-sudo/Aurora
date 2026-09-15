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
#pragma once

#include "BSDFCodeGenerator.h"
#include "MaterialBase.h"

BEGIN_AURORA

struct MaterialShaderSource;

namespace MaterialXCodeGen
{

class MaterialGenerator
{
public:
    MaterialGenerator(const string& mtlxFolder);

    /// Generate shader code for the named material inside the MaterialX document.
    /// If materialName is empty or not found, the first renderable material is used.
    /// baseDirectory is the folder the document was loaded from, used to resolve its relative
    /// texture references; it is empty for a document supplied as an in-memory string.
    shared_ptr<MaterialDefinition> generate(
        const string& document, const string& materialName = "", const string& baseDirectory = "");

    // Get the code generator used to generate material shader code.
    BSDFCodeGenerator& codeGenerator() { return *_pCodeGenerator; }

private:
    // Code generator used to generate MaterialX files.
    unique_ptr<MaterialXCodeGen::BSDFCodeGenerator> _pCodeGenerator;

    // Mapping from a MaterialX output property to a Standard Surface property.
    map<string, string> _bsdfInputParamMapping;

    // Cache keyed on (document, materialName, baseDirectory).
    using DefinitionKey = std::tuple<string, string, string>;
    struct KeyHash
    {
        size_t operator()(const DefinitionKey& k) const noexcept
        {
            size_t h = std::hash<string> {}(std::get<0>(k));
            // Boost-style hash combine.
            h ^= std::hash<string> {}(std::get<1>(k)) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<string> {}(std::get<2>(k)) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    unordered_map<DefinitionKey, weak_ptr<MaterialDefinition>, KeyHash> _definitions;
};

} // namespace MaterialXCodeGen

END_AURORA
