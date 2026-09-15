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

#if !defined(DISABLE_UNIT_TESTS)

#include <AuroraTestHelpers.h>
#include <BaselineImageHelpers.h>
#include <TestHelpers.h>

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <streambuf>

using namespace Aurora;

namespace
{

class MaterialTest : public TestHelpers::FixtureBase
{
public:
    MaterialTest() {}

    void setupAssetPaths(const vector<string>& additionalPaths = {})
    {
        _paths = { "", dataPath() + "/Materials/", dataPath() + "/Textures/" };
        for (size_t i = 0; i < additionalPaths.size(); i++)
        {
            _paths.push_back(additionalPaths[i]);
        }
        for (const string& path : _paths)
        {
            defaultRenderer()->addMdlSearchPath(path);
        }
        // Setup the resource loading function to use asset search paths.
        auto loadResourceFunc = [this](const string& uri, vector<unsigned char>* pBufferOut,
                                    string* pFileNameOut) {
            // Iterate through all search paths.
            for (size_t i = 0; i < _paths.size(); i++)
            {
                // Prefix the current search path to URI.
                string currPath = _paths[i] + uri;
                ifstream is(currPath, ifstream::binary);

                // If file could not be opened, continue.
                if (!is)
                    continue;
                is.seekg(0, is.end);
                size_t length = is.tellg();
                is.seekg(0, is.beg);
                pBufferOut->resize(length);
                is.read((char*)&(*pBufferOut)[0], length);

                // Copy the URI to file name with no manipulation.
                *pFileNameOut = currPath;

                return true;
            }
            return false;
        };
        defaultRenderer()->setLoadResourceFunction(loadResourceFunc);
    }
    ~MaterialTest() {}

    // Test for the existence of the ADSK materialX libraries (in the working folder for the tests)
    bool adskMaterialXSupport() { return std::filesystem::exists("MaterialX/libraries/adsklib"); }

    // Load a MaterialX document and process file paths to correct locations for unit tests.
    string loadAndProcessMaterialXFile(const string& filename)
    {
        ifstream mtlXStream(filename);

        // Load source into string.
        string mtlXString((istreambuf_iterator<char>(mtlXStream)), istreambuf_iterator<char>());

        // Fail if file does not exist.
        if (mtlXString.empty())
            return "";

        // Replace the texture path
        string processedMtlXString =
            regex_replace(mtlXString, regex("\\.\\./Textures"), dataPath() + "\\Textures");

        return processedMtlXString;
    }

    vector<string> _paths;
};

// Test basic material functionality
TEST_P(MaterialTest, TestMaterialDefault)
{
    if (!backendSupported())
        return;

    // Create a pRenderer for this type
    IRenderer::Backend backend = rendererBackend();
    auto pRenderer             = createRenderer(backend);
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create a material
    IScenePtr pScene = pRenderer->createScene();
    pRenderer->setScene(pScene);

    Path testMaterial("testMaterial");
    pScene->setMaterialType(testMaterial);
    pScene->addPermanent(testMaterial);

    // Ensure the material properties can be set without error
    testFloatValue(*pScene, testMaterial, "base", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "base_color", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "specular_rotation", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "coat_IOR", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "transmission", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "diffuse_roughness", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "specular", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "specular_roughness", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "specular_anisotropy", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "specular_IOR", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "metalness", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "subsurface", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "subsurface_anisotropy", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "transmission_color", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "subsurface_scale", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "sheen", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "sheen_roughness", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloatValue(*pScene, testMaterial, "coat", true,
        " material param test" + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "specular_color", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "subsurface_color", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "subsurface_radius", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "sheen_color", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testFloat3Value(*pScene, testMaterial, "sheen_color", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");
    testBooleanValue(*pScene, testMaterial, "thin_walled", true,
        " material param test " + to_string(__LINE__) + " (" + rendererDescription() + ")");

    pScene->removePermanent(testMaterial);
}

// Convenience function to clear all material properties
static void clearAllProperties(Properties& props)
{
    props["base"].clear();
    props["base_color"].clear();
    props["diffuse_roughness"].clear();
    props["specular"].clear();
    props["specular_color"].clear();
    props["specular_roughness"].clear();
    props["specular_IOR"].clear();
    props["specular_anisotropy"].clear();
    props["specular_rotation"].clear();
    props["metalness"].clear();
    props["transmission"].clear();
    props["transmission_color"].clear();
    props["subsurface"].clear();
    props["subsurface_color"].clear();
    props["subsurface_radius"].clear();
    props["subsurface_scale"].clear();
    props["subsurface_anisotropy"].clear();
    props["sheen"].clear();
    props["sheen_color"].clear();
    props["sheen_roughness"].clear();
    props["thin_walled"].clear();
    props["coat"].clear();
    props["coat_color"].clear();
    props["coat_roughness"].clear();
    props["coat_anisotropy"].clear();
    props["coat_rotation"].clear();
    props["coat_IOR"].clear();
    props["coat_affect_color"].clear();
    props["coat_affect_roughness"].clear();
    props["opacity"].clear();

    props["base_color_image"].clear();
    props["specular_roughness_image"].clear();
    props["specular_color_image"].clear();
    props["coat_color_image"].clear();
    props["coat_roughness_image"].clear();
    props["emission_color_image"].clear();
    props["opacity_image"].clear();
    props["normal_image"].clear();
    props["displacement_image"].clear();
}

// Test material functionality using baseline image testing.
TEST_P(MaterialTest, TestMaterialClearMaterialProperty)
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create teapot geometry.
    Path geometry = createTeapotGeometry(*pScene);

    // Create a material (by setting default material type.)
    Path material("ClearMaterial");
    pScene->setMaterialType(material);

    Path inst("instance0");
    pScene->addInstance(inst, geometry, { { Names::InstanceProperties::kMaterial, material } });

    // Test color.
    vec3 bc0(1, 0, 0.25f);

    // Set the color property.
    Properties matProps;
    matProps["base_color"] = bc0;

    // Set the test color
    pScene->setMaterialProperties(material, matProps);

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_BeforeClear", "Materials");

    // Clear the color property.
    matProps["base_color"].clear();

    // Clear the test color
    pScene->setMaterialProperties(material, matProps);

    // Render the scene and check baseline image.
    // On rasterizer this will appear incorrect (the previous color will still be used).
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_AfterClear", "Materials");
}

// Convenience function to create a grid of teapots, with a single default material.
Paths createTeapotGrid(TestHelpers::FixtureBase& pFixture, uint32_t gridWidth, uint32_t gridHeight)
{
    auto pScene    = pFixture.defaultScene();
    auto pRenderer = pFixture.defaultRenderer();
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create shared geom for all instances.
    Path geometry = pFixture.createTeapotGeometry(*pScene);

    // Create grid parameters.
    const vec3 gridDims(gridWidth - 1, gridHeight - 1, 0.0f);
    const vec3 gridOrigin(0, 0, +17.0f);
    const vec3 gridGap(-3.1f, 3.1f, 0.0f); // Invert X so 0->1 is left to right
    const vec3 gridHalfSize = gridGap * gridDims * 0.5f;

    // All teapots created with the same default material.
    Path material("TeapotMaterial");
    pScene->setMaterialType(material);

    InstanceDefinitions instDefs;

    // Create a grid of teapots by adding add all the instances to the scene.
    // Material and transform variation is specified though a callback when the instances are
    // constructed.
    for (uint32_t index = 0; index < (gridWidth * gridHeight); index++)
    {
        size_t i     = index / gridWidth;
        size_t j     = index % gridHeight;
        vec3 gridPos = vec3(j, i, 0) * gridGap + gridOrigin - gridHalfSize;
        instDefs.push_back({ "instance" + to_string(index),
            { { Names::InstanceProperties::kTransform, translate(gridPos) },
                { Names::InstanceProperties::kMaterial, material } } });
    }

    return pScene->addInstances(geometry, instDefs);
}

// Test basic material properties using baseline image testing.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialBasicMaterialProperties) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialBasicMaterialProperties)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create a grid of teapot instances.
    const uint32_t gridWidth  = 5;
    const uint32_t gridHeight = 5;
    vector<Path> teapotGrid   = createTeapotGrid(*this, 5, 5);
    vector<Path> materialGrid(teapotGrid.size());

    // Test with reference BSDF and Standard Surface BSDF.
    for (int useReference = 0; useReference < 2; useReference++)
    {
        // Enabled reference BSDF via options.
        pRenderer->options().setBoolean("isReferenceBSDFEnabled", useReference);
        string mtlType = useReference ? "Reference" : "StandardSurface";

        // Create materials for each teapot.
        for (size_t i = 0; i < materialGrid.size(); i++)
        {
            Properties matProps;
            materialGrid[i] = "BasicMaterial" + mtlType + to_string(i);
            pScene->setMaterialType(materialGrid[i]);

            Properties instProps;
            instProps[Names::InstanceProperties::kMaterial] = materialGrid[i];
            pScene->setInstanceProperties(teapotGrid[i], instProps);
        }

        // Test colors.
        vec3 bc0(1, 0.4f, 0.25f);
        vec3 bc1(0.35f, 1, 0.3f);

        // Test base_color (X-axis) against base (Y-axis.)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Define the base_color color
                vec3 color             = mix(bc0, bc1, u);
                matProps["base_color"] = color;

                // Define the base scalar
                matProps["base"] = v * 1.2f;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_Base_" + mtlType, "Materials");

        // Test specular_color (X-axis) against specular (Y-axis)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];

                // Reset the old values
                Properties matProps;
                clearAllProperties(matProps);

                // Define the specular_color color
                vec3 color                 = mix(bc0, bc1, u);
                matProps["specular_color"] = color;

                // Define the specular scalar
                matProps["specular"] = v * 2.0f;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Specular_" + mtlType, "Materials");

        // Test diffuse_roughness (X-axis) against specular_roughness (Y-axis)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];

                // Reset the old values
                Properties matProps;
                clearAllProperties(matProps);

                // Zero metalness (setting for clarity, is actually the default)
                matProps["metalness"] = 0.0f;

                // Constant specular/base properties
                matProps["specular"]       = 0.5f;
                matProps["base"]           = 0.5f;
                matProps["specular_color"] = bc0;
                matProps["base_color"]     = bc1;

                // Vary diffuse and specular roughness with U and V values.
                matProps["diffuse_roughness"]  = u;
                matProps["specular_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Roughness_" + mtlType, "Materials");

        // Test diffuse_roughness (X-axis) against specular_roughness (Y-axis), with tiny metalness
        // value. This should be no different to zero but is very different.
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate from Y grid position.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.from X grid position.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];

                // Reset the old values
                Properties matProps;
                clearAllProperties(matProps);

                // Tiny metalness (should be close enough to zero to be indistinguishable)
                matProps["metalness"] = 0.001f;

                // Constant specular/base properties.
                matProps["specular"]       = 0.5f;
                matProps["base"]           = 0.5f;
                matProps["specular_color"] = bc0;
                matProps["base_color"]     = bc1;

                // Vary diffuse and specular roughness with U and V values.
                matProps["diffuse_roughness"]  = u;
                matProps["specular_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Roughness_TinyMetal_" + mtlType, "Materials");

        // Test metalness (X-axis) against specular_roughness (Y-axis)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate from Y grid position.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.from X grid position.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];

                // Reset the old values.
                Properties matProps;
                clearAllProperties(matProps);

                // Constant specular and base properties.
                matProps["specular"]          = 0.75f;
                matProps["base"]              = 0.25f;
                matProps["specular_color"]    = bc0;
                matProps["base_color"]        = bc1;
                matProps["diffuse_roughness"] = 0.5f;

                // Vary metalness and specular roughness with U and V values.
                matProps["metalness"]          = u;
                matProps["specular_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Metalness_" + mtlType, "Materials");
    }
}

// Set just the transmission value.
TEST_P(MaterialTest, TestMaterialSetTransmission)
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create teapot instance.
    Path geometry = createTeapotGeometry(*pScene);

    // Create the material.
    Path material("TransmissionMaterial");
    pScene->setMaterialProperties(material, { { "transmission", 0.5f } });

    // Create an instance with the material.
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(Path("instance0"), geometry, instProps));

    // Should render without crashing.
    ASSERT_NO_FATAL_FAILURE(pRenderer->render());

    // Wait for frame to avoid crash on exit
    pRenderer->waitForTask();
}

// Test emission properties.
TEST_P(MaterialTest, TestMaterialEmission)
{
    // Create the default scene and renderer.
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If the renderer is null, this renderer type is not supported, so skip the rest of the test.
    if (!pRenderer)
    {
        return;
    }
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create a material with a non-zero emission value and a specific emission color, along with
    // minimal diffuse / specular reflectance so that the emission is dominant.
    Path material("EmissionMaterial");
    pScene->setMaterialProperties(material,
        { { "emission", 1.0f }, { "emission_color", vec3(0.0f, 1.0f, 0.0f) }, { "base", 0.1f },
            { "specular", 0.1f } });

    // Create a teapot geometry object.
    Path geometry = createTeapotGeometry(*pScene);

    // Create an instance of the geometry, using the test material.
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(Path("instance0"), geometry, instProps));

    // Render and compare against the baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "Emission", "Materials");

    // Load a test image from disk as an Aurora image.
    const std::string imageFilePath = dataPath() + "/Textures/Mr._Smiley_Face.png";
    const Path imagePath            = loadImage(imageFilePath);

    // Set the image as the emission color image on the material.
    pScene->setMaterialProperties(material, { { "emission_color_image", imagePath } });

    // Render and compare against the baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "EmissionImage", "Materials");
}

// Test more advanced material properties using baseline image testing.
TEST_P(MaterialTest, TestMaterialAdvancedMaterialProperties)
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Create a grid of teapot instances.
    const uint32_t gridWidth  = 5;
    const uint32_t gridHeight = 5;
    Paths teapotGrid          = createTeapotGrid(*this, 5, 5);
    vector<Path> materialGrid(teapotGrid.size());
#if defined(__APPLE__)
    // Test with Standard Surface BSDF.
    for (int useReference = 0; useReference < 1; useReference++)
#else
    // Test with reference BSDF and Standard Surface BSDF.
    for (int useReference = 0; useReference < 2; useReference++)
#endif
    {
        // Enabled reference BSDF via options.
        pRenderer->options().setBoolean("isReferenceBSDFEnabled", useReference);
        string mtlType = useReference ? "Reference" : "StandardSurface";

        // Create materials for each teapot.
        for (size_t i = 0; i < materialGrid.size(); i++)
        {
            materialGrid[i] = "AdvancedMaterial" + mtlType + to_string(i);
            pScene->setMaterialType(materialGrid[i]);

            pScene->setInstanceProperties(
                teapotGrid[i], { { Names::InstanceProperties::kMaterial, materialGrid[i] } });
        }

        // Test colors.
        vec3 bc0(1, 0.4f, 0.25f);
        vec3 bc1(0.35f, 1, 0.3f);
        vec3 bc2(0.3f, 0.2f, 0.95f);
        vec3 bc3(0.7f, 0.8f, 0.05f);
        vec3 grey(0.25f, 0.25f, 0.25f);

        // Test subsurface color (X-axis) against subsurface radius (Y-axis)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.from Y grid position.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.from X grid position.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Reset the old values.
                clearAllProperties(matProps);

                // Constant base color.and sheen.
                matProps["base_color"] = grey;
                matProps["subsurface"] = 0.75f;

                // Vary subsurface color and subsurface radius with U and V values.
                vec3 color                    = mix(bc0, bc1, u);
                matProps["subsurface_color"]  = color;
                vec3 radius                   = mix(bc2, bc3, v);
                matProps["subsurface_radius"] = radius;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Subsurface_" + mtlType, "Materials");

        // Test sheen color (X-axis) against sheen roughness (Y-axis)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate from Y grid position.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.from X grid position.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Reset the old values.
                clearAllProperties(matProps);

                // Constant base color.and sheen.
                matProps["base_color"] = grey;
                matProps["sheen"]      = 1.0f;

                // Vary sheen color and sheen roughness with U and V values.
                vec3 color                  = mix(bc0, bc1, u);
                matProps["sheen_color"]     = color;
                matProps["sheen_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Sheen_" + mtlType, "Materials");

        // Test coat_color (X-axis) against coat_roughness (Y-axis.)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Reset the old values.
                clearAllProperties(matProps);

                // Constant coat, specular and base properties.
                matProps["coat"]     = 1.0f;
                matProps["coat_IOR"] = 0.5f;
                matProps["base"]     = 0.0f;
                matProps["specular"] = 0.0f;

                // Vary the coat_color color with U
                vec3 color             = mix(bc0, bc1, u);
                matProps["coat_color"] = color;

                // Vary the coat_roughness color with V
                matProps["coat_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        // TODO: The PT does not support this currently.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_Coat_" + mtlType, "Materials");

        // Test coat_affect_color (X-axis) against coat_affect_roughness (Y-axis.)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Reset the old values.
                clearAllProperties(matProps);

                // Constant coat, specular and base properties.
                matProps["coat"]       = 1.0f;
                matProps["coat_IOR"]   = 0.5f;
                matProps["base"]       = 0.0f;
                matProps["specular"]   = 0.0f;
                matProps["coat_color"] = bc0;

                // Vary the coat_affect_color color with U
                matProps["coat_affect_color"] = u;

                // Vary the coat_affect_roughness color with V
                matProps["coat_affect_roughness"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        // TODO: The PT does not support this currently.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_CoatAffect_" + mtlType, "Materials");

        // Test base_color (X-axis) against base (Y-axis.)
        for (uint32_t i = 0; i < gridHeight; i++)
        {
            // Calculate the V coordinate.
            float v = i / static_cast<float>(gridHeight - 1.0f);
            for (uint32_t j = 0; j < gridWidth; j++)
            {
                // Calculate the U coordinate.
                float u = j / static_cast<float>(gridWidth - 1.0f);

                // Get the material from the grid.
                Path material = materialGrid[i * gridWidth + j];
                Properties matProps;

                // Reset the old values.
                clearAllProperties(matProps);

                // Constant specular and base properties.
                matProps["base"]     = 0.75f;
                matProps["specular"] = 0.25f;

                // Vary the specular parameter with U.
                matProps["specular"] = u;

                // Vary the transmission parameter with V
                matProps["transmission"] = v;

                // Update the material.
                pScene->setMaterialProperties(material, matProps);
            }
        }

        // Render the scene and check baseline image.
        // TODO: PT does not support transmission currently.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_Transmission_" + mtlType, "Materials");
    }
}

// Test material type creation using MaterialX
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialTypes) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialTypes)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    setDefaultRendererPathTracingIterations(256);

    // Set an invalid material type.
    pScene->setMaterialType("BadMaterial", "NotAMaterialType", "");

    // Add permanent reference to force creation of material, which will trigger exception.
    ASSERT_THROW(pScene->addPermanent("BadMaterial"), TestHelpers::AuroraLoggerException);

    // We currently have one built-in.
    const vector<string> builtIns = pRenderer->builtInMaterials();
    ASSERT_EQ(builtIns.size(), 1);
    ASSERT_EQ(builtIns[0].compare("Default"), 0);

    // Set bad material built-in (on PT this will return default material type and print error.)
    pScene->setMaterialType(Path("nullMtl"), Names::MaterialTypes::kBuiltIn, "NotABuiltIn");

    // Add permanent reference to force creation of material (this will trigger error message).
    pScene->addPermanent("nullMtl");
    ASSERT_NE(lastLogMessage().find("Unknown built-in material type NotABuiltIn for material"),
        string::npos);

    // clang-format off
    // Test Material
    string testMtl =
        "<?xml version=\"1.0\" ?>\n"
        "<materialx version=\"1.37\">\n"
        "  <material name=\"Test_Material\">\n"
        "    <shaderref name=\"SR_Test1\" node=\"standard_surface\">\n"
        "      <bindinput name=\"base\" type=\"float\" value=\"1.0\" />\n"
        "      <bindinput name=\"base_color\" type=\"color3\" value=\"0.0, 1.0, 0.0\" />\n"
        "      <bindinput name=\"specular_color\" type=\"color3\" value=\"1.0, 1.0, 1.0\" />\n"
        "      <bindinput name=\"specular_roughness\" type=\"float\" value=\"0.3\" />\n"
        "    </shaderref>\n"
        "  </material>\n"
        "</materialx>";
    // clang-format on

    Path geometry = createTeapotGeometry(*pScene);

    // Create materialX material from document.
    Path mtl0("Mtl0");
    pScene->setMaterialType(mtl0, Names::MaterialTypes::kMaterialX, testMtl);

    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial]  = mtl0;
    instProps[Names::InstanceProperties::kTransform] = translate(vec3(-3, 0, 6));
    EXPECT_TRUE(pScene->addInstance(Path("I0"), geometry, instProps));

    // Create materialX material from path.
    Path mtl1("Mtl1");
    pScene->setMaterialType(
        mtl1, Names::MaterialTypes::kMaterialXPath, dataPath() + "/Materials/TestMaterial.mtlx");

    instProps[Names::InstanceProperties::kMaterial]  = mtl1;
    instProps[Names::InstanceProperties::kTransform] = translate(vec3(0, 0, 6));
    EXPECT_TRUE(pScene->addInstance(Path("I1"), geometry, instProps));

    // Create materialX material from path.
    Path mtl2("Mtl2");
    pScene->setMaterialType(mtl2, Names::MaterialTypes::kBuiltIn, "Default");

    instProps[Names::InstanceProperties::kMaterial]  = mtl1;
    instProps[Names::InstanceProperties::kTransform] = translate(vec3(3, 0, 6));
    EXPECT_TRUE(pScene->addInstance(Path("I2"), geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Test material creation using MaterialX
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    // Create a material with invalid document.
    pScene->setMaterialType("NullMaterial", Names::MaterialTypes::kMaterialX,
        "Not valid materialX! $#$(#*(%*#,.,.,.<><><>");

    // Error occurs when material resource is activated.
    pScene->addPermanent("NullMaterial");
    ASSERT_NE(lastLogMessage().find("Failed to generate MaterialX code."), string::npos)
        << lastLogMessage();

    // clang-format off
    // Test Material
    string testMtl =
        "<?xml version=\"1.0\" ?>\n"
        "<materialx version=\"1.37\">\n"
        "  <nodegraph name=\"NG_Test1\">\n"
        "    <position name=\"positionWorld\" type=\"vector3\" />\n"
        "    <constant name=\"constant_1\" type=\"color3\">\n"
        "      <input name=\"value\" type=\"color3\" value=\"1.0, 0.1, 0.0\" />\n"
        "    </constant>\n"
        "    <constant name=\"constant_2\" type=\"color3\">\n"
        "      <input name=\"value\" type=\"color3\" value=\"0.0, 0.0, 0.0\" />\n"
        "    </constant>\n"
        "    <constant name=\"mix_amount\" type=\"float\">\n"
        "      <input name=\"value\" type=\"float\" value=\"0.6\" />\n"
        "    </constant>\n"
        "    <mix name=\"mix1\" type=\"color3\">\n"
        "      <input name=\"mix\" type=\"float\" nodename=\"mix_amount\" />\n"
        "      <input name=\"bg\" type=\"color3\" nodename=\"constant_1\" />\n"
        "      <input name=\"fg\" type=\"color3\" nodename=\"constant_2\" />\n"
        "    </mix>\n"
        "    <output name=\"out\" type=\"color3\" nodename=\"mix1\" />\n"
        "  </nodegraph>\n"
        "\n"
        "  <material name=\"Test_Material\">\n"
        "    <shaderref name=\"SR_Test1\" node=\"standard_surface\">\n"
        "      <bindinput name=\"base\" type=\"float\" value=\"1.0\" />\n"
        "      <bindinput name=\"base_color\" type=\"color3\" nodegraph=\"NG_Test1\" output=\"out\" />\n"
        "      <bindinput name=\"specular_color\" type=\"color3\" value=\"1.0, 1.0, 1.0\" />\n"
        "      <bindinput name=\"specular_roughness\" type=\"float\" value=\"0.6\" />\n"
        "    </shaderref>\n"
        "  </material>\n"
        "</materialx>";
    // clang-format on

    // Create teapot instance.
    Path geometry = createTeapotGeometry(*pScene);
    Path testMaterial("TestMaterialX");
    pScene->setMaterialType(testMaterial, Names::MaterialTypes::kMaterialX, testMtl);

    Path instance("MaterialXInstance");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = testMaterial;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_FromString", "Materials");

    pScene->setMaterialProperties(testMaterial, { { "mix_amount/value", 0.9f } });

    pScene->setInstanceProperties(instance, instProps);

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_ParameterOverride", "Materials");
}

// Test material creation using MaterialX file dumped from HdAurora.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestHdAuroraMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestHdAuroraMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    if (!pRenderer)
        return;

    setDefaultRendererPathTracingIterations(256);

    setupAssetPaths();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    // Create teapot geom.
    Path geometry = createTeapotGeometry(*pScene);

    // Try loading a MtlX file dumped from HdAurora.
    Path material("HdAuroraMaterial");
    pScene->setMaterialType(material, Names::MaterialTypes::kMaterialXPath,
        dataPath() + "/Materials/HdAuroraTest.mtlx");

    // Add to scene.
    Path instance("HdAuroraInstance");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_HdAuroraMtlX", "Materials");
}

// Test material creation using MaterialX file dumped from HdAurora that has a texture.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestHdAuroraTextureMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestHdAuroraTextureMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    if (!pRenderer)
        return;

    setDefaultRendererPathTracingIterations(256);

    // NOTE: deliberately no setupAssetPaths() here. This document references its textures
    // relatively ("../Textures/..."), and passing it by path lets Aurora anchor those references
    // to the document's own folder, so no host supplied search path is needed.
    if (!pRenderer)
        return;

    // Create teapot geom.
    Path geometry = createTeapotGeometry(*pScene);

    // Try loading a MtlX file dumped from HdAurora.
    Path material("HdAuroraMaterial");
    pScene->setMaterialType(material, Names::MaterialTypes::kMaterialXPath,
        dataPath() + "/Materials/HdAuroraTextureTest.mtlx");

    // Add to scene.
    Path instance("HdAuroraInstance");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_HdAuroraMtlX", "Materials");
}

// Test different settings for isFlipImageYEnabled option.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialXFlipImageY) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialXFlipImageY)
#endif
{
    // This mtlx file requires support ADSK materialX support.
    if (!adskMaterialXSupport())
        return;

    for (int flipped = 0; flipped < 2; flipped++)
    {
        // Create the default scene (also creates renderer)
        auto pScene    = createDefaultScene();
        auto pRenderer = defaultRenderer();
        setDefaultRendererPathTracingIterations(256);

        // If pRenderer is null this renderer type not supported, skip rest of the test.
        if (!pRenderer)
            return;

        // Set the flip-Y option.
        Properties options = { { "isFlipImageYEnabled", bool(flipped != 0) } };
        pRenderer->setOptions(options);

        // Create teapot geom.
        Path geometry = createTeapotGeometry(*pScene);

        // Read the materialX document from file.
        string materialXFullPath = dataPath() + "/Materials/TestTexture.mtlx";

        // Load source into string.
        string processedMaterialXString = loadAndProcessMaterialXFile(materialXFullPath);
        // Fail if file does not exist.
        ASSERT_FALSE(processedMaterialXString.empty());

        // Create material with current flip-Y value.
        Path material("Texture");
        pScene->setMaterialType(
            material, Names::MaterialTypes::kMaterialX, processedMaterialXString);

        // Add to scene.
        Path instance(flipped ? "_Flipped" : "_NotFlipped");
        Properties instProps;
        instProps[Names::InstanceProperties::kMaterial] = material;
        EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

        // Render the scene and check baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + (flipped ? "_Flipped" : "_NotFlipped"), "Materials");
    }
}

// Test material creation using MaterialX
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestLotsOfMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestLotsOfMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    // clang-format off
    // Test Material
    string testMtl0 = R""""(
            <?xml version = "1.0" ?>
            <materialx version = "1.37">
               <material name="SS_Material">
                 <shaderref name="SS_ShaderRef1" node="standard_surface">
                  <bindinput name="base_color" type="color3" value="1,0,1" />
                  <bindinput name="specular_color" type="color3" value="0,1,0" />
                  <bindinput name="specular_roughness" type="float" value="0.8" />
                  <bindinput name="specular_IOR" type="float" value = "0.75" />
                  <bindinput name="emission_color" type="color3" value = "0.2,0.2,0.1" />
                  <bindinput name="coat" type="float" value = "0.75" />
                  <bindinput name="coat_roughness" type="float" value = "0.75" />
                </shaderref>
              </material>
            </materialx>
    )"""";

        string testMtl1 = R""""(
            <?xml version = "1.0" ?>
            <materialx version = "1.37">
               <material name="SS_Material">
                 <shaderref name="SS_ShaderRef1" node="standard_surface">
                  <bindinput name="base_color" type="color3" value="0,1,1" />
                  <bindinput name="specular_color" type="color3" value="0,1,0" />
                  <bindinput name="specular_IOR" type="float" value = "0.75" />
                  <bindinput name="emission_color" type="color3" value = "0.2,0.2,0.1" />
                  <bindinput name="coat" type="float" value = "0.75" />
                  <bindinput name="coat_roughness" type="float" value = "0.75" />
                </shaderref>
              </material>
            </materialx>
    )"""";

                string testMtl2 = R""""(
            <?xml version = "1.0" ?>
            <materialx version = "1.37">
               <material name="SS_Material">
                 <shaderref name="SS_ShaderRef1" node="standard_surface">
                  <bindinput name="base_color" type="color3" value="0,1,1" />
                  <bindinput name="specular_color" type="color3" value="1,0,0" />
                  <bindinput name="emission_color" type="color3" value = "0.2,0.2,0.1" />
                  <bindinput name="coat" type="float" value = "0.75" />
                  <bindinput name="coat_roughness" type="float" value = "0.75" />
                </shaderref>
              </material>
            </materialx>
    )"""";

    string testMtl3 = R""""(
            <?xml version = "1.0" ?>
            <materialx version = "1.37">
               <material name="SS_Material">
                 <shaderref name="SS_ShaderRef1" node="standard_surface">
                  <bindinput name="base_color" type="color3" value="0,1,0" />
                  <bindinput name="emission_color" type="color3" value = "0.2,0.2,0.1" />
                  <bindinput name="coat" type="float" value = "0.75" />
                  <bindinput name="coat_roughness" type="float" value = "0.75" />
                </shaderref>
              </material>
            </materialx>
    )"""";

    // clang-format on

    // Create teapot instance.
    Path geometry = createTeapotGeometry(*pScene);
    Path testMaterial0("TestMaterialX0");
    pScene->setMaterialType(testMaterial0, Names::MaterialTypes::kMaterialX, testMtl0);
    Path testMaterial1("TestMaterialX1");
    pScene->setMaterialType(testMaterial1, Names::MaterialTypes::kMaterialX, testMtl1);
    Path testMaterial2("TestMaterialX2");
    pScene->setMaterialType(testMaterial2, Names::MaterialTypes::kMaterialX, testMtl2);
    Path testMaterial3("TestMaterialX3");
    pScene->setMaterialType(testMaterial3, Names::MaterialTypes::kMaterialX, testMtl3);

    EXPECT_TRUE(pScene->addInstance("MaterialXInstance0", geometry,
        { { Names::InstanceProperties::kMaterial, testMaterial0 },
            { Names::InstanceProperties::kTransform, translate(vec3(+1.5, 0.0, 4.0)) } }));
    EXPECT_TRUE(pScene->addInstance("MaterialXInstance1", geometry,
        { { Names::InstanceProperties::kMaterial, testMaterial1 },
            { Names::InstanceProperties::kTransform, translate(vec3(-1.5, 0.0, 4.0)) } }));
    EXPECT_TRUE(pScene->addInstance("MaterialXInstance2", geometry,
        { { Names::InstanceProperties::kMaterial, testMaterial2 },
            { Names::InstanceProperties::kTransform, translate(vec3(+1.5, -2.0, 4.0)) } }));
    EXPECT_TRUE(pScene->addInstance("MaterialXInstance3", geometry,
        { { Names::InstanceProperties::kMaterial, testMaterial3 },
            { Names::InstanceProperties::kTransform, translate(vec3(-1.5, -2.0, 4.0)) } }));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Test different MtlX file that loads a BMP.
// Disabled as this testcase fails with error in MaterialGenerator::generate
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialXBMP) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialXBMP)
#endif
{
    // This mtlx file requires support ADSK materialX support.
    if (!adskMaterialXSupport())
        return;

    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    // Create teapot geom.
    Path geometry = createTeapotGeometry(*pScene);

    // Read the materialX document from file.
    string materialXFullPath = dataPath() + "/Materials/TestBMPTexture.mtlx";

    // Load and process the MaterialX document and ensure it loaded correctly.
    string processedMtlXString = loadAndProcessMaterialXFile(materialXFullPath);
    EXPECT_FALSE(processedMtlXString.empty());

    // Create material with current flip-Y value.
    Path material("Texture");
    pScene->setMaterialType(material, Names::MaterialTypes::kMaterialX, processedMtlXString);

    // Add to scene.
    Path instance("Mtl");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialXImageNode) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialXImageNode)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    // Create teapot geom.
    Path geometry = createTeapotGeometry(*pScene);

    // Read the materialX document from file.
    string materialXFullPath = dataPath() + "/Materials/TestImageNode.mtlx";

    // Load and process the MaterialX document and ensure it loaded correctly.
    string processedMtlXString = loadAndProcessMaterialXFile(materialXFullPath);
    EXPECT_FALSE(processedMtlXString.empty());

    // Create material with current flip-Y value.
    Path material("Texture");
    pScene->setMaterialType(material, Names::MaterialTypes::kMaterialX, processedMtlXString);

    // Add to scene.
    Path instance("Mtl");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

TEST_P(MaterialTest, TestMaterialTransparency)
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    // Constant colors.
    vec3 color0(0.5f, 1.0f, 0.3f);
    vec3 opacity0(0.1f, 0.25f, 0.1f);

    // Create geometry for teapot and plane geometry.
    Path planeGeom  = createPlaneGeometry(*pScene);
    Path teapotGeom = createTeapotGeometry(*pScene);

    // Create materials.
    Path transpMtl("TransparentMaterial");
    pScene->setMaterialProperties(transpMtl, {});
    Path opaqueMtl("OpaqueMaterial");
    pScene->setMaterialProperties(opaqueMtl, { { "base_color", color0 } });

    // Create opaque plane instance behind a transparent teapot instance.
    mat4 xform = translate(vec3(0, 0.5f, +2));
    Path planeInst("PlaneInstance");
    Properties planeInstProps;
    planeInstProps[Names::InstanceProperties::kMaterial]  = opaqueMtl;
    planeInstProps[Names::InstanceProperties::kTransform] = xform;
    EXPECT_TRUE(pScene->addInstance(planeInst, planeGeom, planeInstProps));

    Path teapotInst("TeapotInstance");
    Properties teapotInstProps;
    teapotInstProps[Names::InstanceProperties::kMaterial] = transpMtl;
    EXPECT_TRUE(pScene->addInstance(teapotInst, teapotGeom, teapotInstProps));

    // Render baseline image with transmission.
    pScene->setMaterialProperties(
        transpMtl, { { "transmission", 0.75f }, { "thin_walled", false } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "Transmission", "Materials");

    // Render baseline image with transmission and thin_walled flag set.
    pScene->setMaterialProperties(
        transpMtl, { { "transmission", 0.75f }, { "thin_walled", true } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "ThinWalled", "Materials");

    // Render baseline image with opacity, no transmission and thin_walled flag not set.
    pScene->setMaterialProperties(
        transpMtl, { { "transmission", 0.0f }, { "opacity", opacity0 }, { "thin_walled", false } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "Opacity", "Materials");
}

#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialShadowTransparency) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialShadowTransparency)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;
#if defined(__APPLE__)
    pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0, -.5f, 1)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));
    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat(
        Aurora::Names::LightProperties::kAngularDiameter, 0.04f);

    // Load pixels for test image file.
    const std::string txtName = dataPath() + "/Textures/Triangle.png";
    Path imagePath            = loadImage(txtName);

    // Constant colors.
    vec3 opacity0(0.5f, 0.5f, 0.3f);

    // Create geometry for teapot and plane geometry.
    Path planeGeom  = createPlaneGeometry(*pScene);
    Path teapotGeom = createTeapotGeometry(*pScene);

    // Create materials.
    Path transpMtl("TransparentMaterial");
    pScene->setMaterialProperties(transpMtl, {});
    Path opaqueMtl("OpaqueMaterial");
    pScene->setMaterialProperties(opaqueMtl, { { "base_color", vec3(1, 1, 1) } });

    // Create opaque plane instance behind a transparent teapot instance.
    mat4 xform = translate(vec3(0, 0.5f, +2)) * scale(vec3(5, 5, 5));
    Path planeInst("PlaneInstance");
    Properties planeInstProps;
    planeInstProps[Names::InstanceProperties::kMaterial]  = opaqueMtl;
    planeInstProps[Names::InstanceProperties::kTransform] = xform;
    EXPECT_TRUE(pScene->addInstance(planeInst, planeGeom, planeInstProps));

    Path teapotInst("TeapotInstance");
    Properties teapotInstProps;
    teapotInstProps[Names::InstanceProperties::kMaterial] = transpMtl;
    EXPECT_TRUE(pScene->addInstance(teapotInst, teapotGeom, teapotInstProps));

    // Render baseline image with transmission.
    pScene->setMaterialProperties(
        transpMtl, { { "transmission", 0.5f }, { "thin_walled", false } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "Transmission", "Materials");

    // Render baseline image with transmission and thin_walled flag set.
    pScene->setMaterialProperties(transpMtl, { { "opacity_image", imagePath } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "OpacityImage", "Materials");

    // Render baseline image with opacity, no transmission and thin_walled flag not set.
    pScene->setMaterialProperties(transpMtl,
        { { "transmission", 0.0f }, { "opacity_image", "" }, { "opacity", opacity0 },
            { "thin_walled", false } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "Opacity", "Materials");
}

// Fundamental MaterialX tests.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestBasicMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestBasicMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.25f, +1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // Create geometry.
    Path planePath  = createPlaneGeometry(*pScene);
    Path teapotPath = createTeapotGeometry(*pScene);

    // Add geometry with the materials.
    string imageMtlXPath        = dataPath() + "/Materials/ImageTest.mtlx";
    string processedMtlXString0 = loadAndProcessMaterialXFile(imageMtlXPath);
    EXPECT_FALSE(processedMtlXString0.empty());
    Path imageMaterialPath = "ImageMaterial";
    pScene->setMaterialType(
        imageMaterialPath, Names::MaterialTypes::kMaterialX, processedMtlXString0);

    Properties instProps;
    Path planeInst("PlaneInstance");
    mat4 scaleMtx                                    = scale(vec3(2, 2, 2));
    instProps[Names::InstanceProperties::kMaterial]  = imageMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = scaleMtx;
    EXPECT_TRUE(pScene->addInstance(planeInst, planePath, instProps));

    Path teapotInst("TeapotInstance");
    instProps[Names::InstanceProperties::kMaterial]  = imageMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = mat4();
    EXPECT_TRUE(pScene->addInstance(teapotInst, teapotPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_ImageTest", "Materials");

    // Test a few more MaterialX files.
    string inputsMtlXPath       = dataPath() + "/Materials/InputsTest.mtlx";
    string processedMtlXString1 = loadAndProcessMaterialXFile(inputsMtlXPath);
    EXPECT_FALSE(processedMtlXString1.empty());
    Path inputMaterialPath = "InputMaterial";
    pScene->setMaterialType(
        inputMaterialPath, Names::MaterialTypes::kMaterialX, processedMtlXString1);
    pScene->setInstanceProperties(
        teapotInst, { { Names::InstanceProperties::kMaterial, inputMaterialPath } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_InputsTest", "Materials");

    string StdsurfMtlXPath      = dataPath() + "/Materials/StdsurfTest.mtlx";
    string processedMtlXString3 = loadAndProcessMaterialXFile(StdsurfMtlXPath);
    EXPECT_FALSE(processedMtlXString3.empty());
    Path stdsurfPath = "StdsurfMaterial";
    pScene->setMaterialType(stdsurfPath, Names::MaterialTypes::kMaterialX, processedMtlXString3);
    pScene->setInstanceProperties(
        planeInst, { { Names::InstanceProperties::kMaterial, stdsurfPath } });
    pScene->setInstanceProperties(
        teapotInst, { { Names::InstanceProperties::kMaterial, stdsurfPath } });
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_StdsurfTest", "Materials");
}

// Pattern graph support enabled by native Slang codegen.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialXPatternGraph)
#else
TEST_P(MaterialTest, TestMaterialXPatternGraph)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.25f, +1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // Create geometry.
    Path planePath  = createPlaneGeometry(*pScene);
    Path teapotPath = createTeapotGeometry(*pScene);

    // Add geometry with the materials.
    string marblegraphMtlXPath = dataPath() + "/Materials/MarbleGraphTest.mtlx";
    string processedMtlXString = loadAndProcessMaterialXFile(marblegraphMtlXPath);
    EXPECT_FALSE(processedMtlXString.empty());
    Path marblegraphMaterialPath = "MarbleGraphMaterial";
    pScene->setMaterialType(
        marblegraphMaterialPath, Names::MaterialTypes::kMaterialX, processedMtlXString);

    Properties instProps;
    Path planeInst("PlaneInstance");
    mat4 scaleMtx                                    = scale(vec3(2, 2, 2));
    instProps[Names::InstanceProperties::kMaterial]  = marblegraphMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = scaleMtx;
    EXPECT_TRUE(pScene->addInstance(planeInst, planePath, instProps));

    Path teapotInst("TeapotInstance");
    instProps[Names::InstanceProperties::kMaterial]  = marblegraphMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = mat4();
    EXPECT_TRUE(pScene->addInstance(teapotInst, teapotPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Displacement shader enabled on non-Apple platforms.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMtlxDisplacementShader)
#else
TEST_P(MaterialTest, TestMtlxDisplacementShader)
#endif
{
    // Larger image so the material details are clearly visible.
    createDefaultRenderer(512, 512);

    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    // Max trace depth so reflections and displacement effects are fully captured.
    pRenderer->options().setInt("traceDepth", 10);
    setDefaultRendererPathTracingIterations(256);

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.25f, +1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // Create geometry.
    Path teapotPath = createTeapotGeometry(*pScene);

    // Add geometry with the brass + displacement material.
    string brassMtlXPath       = dataPath() + "/Materials/BrassTest.mtlx";
    string processedMtlXString = loadAndProcessMaterialXFile(brassMtlXPath);
    EXPECT_FALSE(processedMtlXString.empty());
    Path brassPath = "BrassMaterial";
    pScene->setMaterialType(brassPath, Names::MaterialTypes::kMaterialX, processedMtlXString);

    Properties instProps;
    Path teapotInst("TeapotInstance");
    instProps[Names::InstanceProperties::kMaterial]  = brassPath;
    instProps[Names::InstanceProperties::kTransform] = mat4();
    EXPECT_TRUE(pScene->addInstance(teapotInst, teapotPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Test MaterialX pattern graphs (checkerboard patterns via surface_unlit shader).
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestPatternGraphsMaterialX)
#else
TEST_P(MaterialTest, TestPatternGraphsMaterialX)
#endif
{
    // Larger image so the checker patterns are clearly visible.
    createDefaultRenderer(512, 512);

    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    if (!pRenderer)
        return;

    setDefaultRendererPathTracingIterations(64);

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 1.5f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.5f, 1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // PatternGraphs.mtlx defines two complete checker materials using a shared custom nodedef.
    string patternMtlXPath = dataPath() + "/Materials/PatternGraphs.mtlx";
    string patternMtlX     = loadAndProcessMaterialXFile(patternMtlXPath);
    EXPECT_FALSE(patternMtlX.empty()) << "Failed to load PatternGraphs.mtlx";
    if (patternMtlX.empty())
        return;

    // PatternMat1: fine 32×32 red/white checker on the plane.
    // PatternMat2: coarse 2×2 blue/white checker on the teapot.
    Path mat1("PatternMat1");
    pScene->setMaterialType(mat1, Names::MaterialTypes::kMaterialX, patternMtlX);
    Path mat2("PatternMat2");
    pScene->setMaterialType(mat2, Names::MaterialTypes::kMaterialX, patternMtlX);

    // Plane (scaled up so the 32×32 checker is legible) with the fine checker.
    Path planePath  = createPlaneGeometry(*pScene);
    Path teapotPath = createTeapotGeometry(*pScene);

    {
        Path inst("PlaneInst");
        Properties props;
        props[Names::InstanceProperties::kMaterial]  = mat1;
        props[Names::InstanceProperties::kTransform] = scale(vec3(3, 3, 3));
        EXPECT_TRUE(pScene->addInstance(inst, planePath, props));
    }
    // Teapot with the coarse checker.
    {
        Path inst("TeapotInst");
        Properties props;
        props[Names::InstanceProperties::kMaterial]  = mat2;
        props[Names::InstanceProperties::kTransform] = translate(vec3(0, 0, 0));
        EXPECT_TRUE(pScene->addInstance(inst, teapotPath, props));
    }

    // Render and compare against baseline.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Disabled as this testcase fails with error in MaterialGenerator::generate
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMtlXSamplers) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMtlXSamplers)
#endif
{
    // This mtlx file requires support ADSK materialX support.
    if (!adskMaterialXSupport())
        return;

    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    Properties options = { { "isFlipImageYEnabled", true } };
    pRenderer->setOptions(options);

    string materialLayer0XFullPath        = dataPath() + "/Materials/Decals/test_decal_mask.mtlx";
    string processedLayer0MaterialXString = loadAndProcessMaterialXFile(materialLayer0XFullPath);
    ASSERT_FALSE(processedLayer0MaterialXString.empty());

    Path kClampMaterialXPath("ClampMtlX");

    pScene->setMaterialType(
        kClampMaterialXPath, Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);

    // Create plane instance.
    Path geomPath = createPlaneGeometry(*pScene, vec2(2, 2), vec2(-0.5, -0.5));

    // Create instance with material.
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = kClampMaterialXPath;
    Path kInstance                                  = "SamplerInstance";
    EXPECT_TRUE(pScene->addInstance(kInstance, geomPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// MaterialX as layered materials
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialMaterialXLayers) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMaterialMaterialXLayers)
#endif
{
    // This mtlx file requires support ADSK materialX support.
    if (!adskMaterialXSupport())
        return;

    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    Properties options = { { "isFlipImageYEnabled", false } };
    pRenderer->setOptions(options);

    std::string proteinXFullPath        = dataPath() + "/Materials/FishScale.mtlx";
    string processedBaseMaterialXString = loadAndProcessMaterialXFile(proteinXFullPath);
    ASSERT_FALSE(processedBaseMaterialXString.empty());

    string materialLayer0XFullPath        = dataPath() + "/Materials/Decals/test_decal_mask.mtlx";
    string processedLayer0MaterialXString = loadAndProcessMaterialXFile(materialLayer0XFullPath);
    ASSERT_FALSE(processedLayer0MaterialXString.empty());

    string materialLayer1XFullPath = dataPath() + "/Materials/Decals/test_decal_transparent.mtlx";
    string processedLayer1MaterialXString = loadAndProcessMaterialXFile(materialLayer1XFullPath);
    ASSERT_FALSE(processedLayer1MaterialXString.empty());

    vector<Path> materialLayers = { "LayerMaterial0", "LayerMaterial1", "LayerMaterial1",
        "LayerMaterial0" };
    pScene->setMaterialType(
        materialLayers[0], Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);
    pScene->setMaterialType(
        materialLayers[1], Names::MaterialTypes::kMaterialX, processedLayer1MaterialXString);

    Path baseMaterial("BaseMaterial");
    pScene->setMaterialType(
        baseMaterial, Names::MaterialTypes::kMaterialX, processedBaseMaterialXString);

    int numLayers = 4;
    vector<string> geometryLayers;

    float scales[]      = { 1, 2, 1.5f, 0.4f };
    glm::vec2 origins[] = { glm::vec2(0.75f, 1.0f), glm::vec2(-0.25f, 1.2f), glm::vec2(0.25f, 0.6f),
        glm::vec2(-0.5f, 1.4f) };

    vector<vector<glm::vec2>> xformedUVs(numLayers);

    for (int layer = 0; layer < numLayers; layer++)
    {
        auto& uvs = xformedUVs[layer];
        uvs.resize(TestHelpers::TeapotModel::verticesCount());

        for (uint32_t i = 0; i < uvs.size(); i++)
        {
            uvs[i].x = *(TestHelpers::TeapotModel::vertices() + (i * 3 + 0));
            uvs[i].y = *(TestHelpers::TeapotModel::vertices() + (i * 3 + 1));
            uvs[i]   = origins[layer] - uvs[i];
            uvs[i] *= scales[layer];
        }

        const Path kDecalUVGeomPath = "DecalUVGeomPath" + to_string(layer);
        GeometryDescriptor geomDesc;
        geomDesc.type = PrimitiveType::Triangles;
        geomDesc.vertexDesc.attributes[Names::VertexAttributes::kTexCoord0] =
            AttributeFormat::Float2;
        geomDesc.vertexDesc.count = TestHelpers::TeapotModel::verticesCount();
        geomDesc.getAttributeData = [&xformedUVs, layer](AttributeDataMap& buffers,
                                        size_t firstVertex, size_t vertexCount, size_t firstIndex,
                                        size_t indexCount) {
            AU_ASSERT(firstVertex == 0, "Partial update not supported");
            AU_ASSERT(vertexCount == TestHelpers::TeapotModel::verticesCount(),
                "Partial update not supported");
            AU_ASSERT(firstIndex == 0, "Partial update not supported");
            AU_ASSERT(indexCount == 0, "Partial update not supported");
            buffers[Names::VertexAttributes::kTexCoord0].address = xformedUVs[layer].data();
            buffers[Names::VertexAttributes::kTexCoord0].size =
                TestHelpers::TeapotModel::verticesCount() * sizeof(vec2);
            buffers[Names::VertexAttributes::kTexCoord0].stride = sizeof(vec2);

            return true;
        };
        pScene->setGeometryDescriptor(kDecalUVGeomPath, geomDesc);
    }

    for (int layer = 0; layer < numLayers; layer++)
    {
        const Path kDecalUVGeomPath = "DecalUVGeomPath" + to_string(layer);
        geometryLayers.push_back(kDecalUVGeomPath);
    }

    // Create a teapot instance with a default material.
    Path geometry      = createTeapotGeometry(*pScene);
    Path planeGeometry = createPlaneGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance("TeapotInstance", geometry,
        { { Names::InstanceProperties::kMaterial, baseMaterial },
            { Names::InstanceProperties::kMaterialLayers, materialLayers },
            { Names::InstanceProperties::kGeometryLayers, geometryLayers },
            { Names::InstanceProperties::kTransform, glm::translate(glm::vec3(0, -0.5, -0.8)) } }));
    EXPECT_TRUE(pScene->addInstance(
        "Plane", planeGeometry, { { Names::InstanceProperties::kMaterial, baseMaterial } }));

    ASSERT_BASELINE_IMAGE_PASSES(currentTestName());

    // Remove teapot and replace with one with more layers.
    pScene->removeInstance("TeapotInstance");
    EXPECT_TRUE(pScene->addInstance("AnotherTeapotInstance", geometry,
        { { Names::InstanceProperties::kMaterial, baseMaterial },
            { Names::InstanceProperties::kMaterialLayers,
                vector<Path>({ materialLayers[3], materialLayers[2], materialLayers[1],
                    materialLayers[0], materialLayers[2], materialLayers[3] }) },
            { Names::InstanceProperties::kGeometryLayers,
                vector<Path>({ geometryLayers[0], geometryLayers[0], geometryLayers[1],
                    geometryLayers[0], geometryLayers[2], geometryLayers[3] }) },
            { Names::InstanceProperties::kTransform, glm::translate(glm::vec3(0, -0.5, -0.8)) } }));

    ASSERT_BASELINE_IMAGE_PASSES(currentTestName() + "_Removed");
}

// MaterialX as layered materials — enabled on non-Apple platforms.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMaterialMaterialXLayerTransforms)
#else
TEST_P(MaterialTest, TestMaterialMaterialXLayerTransforms)
#endif
{
    // This mtlx file requires support ADSK MaterialX support.
    if (!adskMaterialXSupport())
        return;

    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    Properties options = { { "isFlipImageYEnabled", false } };
    pRenderer->setOptions(options);

    std::string proteinXFullPath        = dataPath() + "/Materials/FishScale.mtlx";
    string processedBaseMaterialXString = loadAndProcessMaterialXFile(proteinXFullPath);
    ASSERT_FALSE(processedBaseMaterialXString.empty());

    string materialLayer0XFullPath        = dataPath() + "/Materials/Decals/test_decal_mask.mtlx";
    string processedLayer0MaterialXString = loadAndProcessMaterialXFile(materialLayer0XFullPath);
    ASSERT_FALSE(processedLayer0MaterialXString.empty());

    vector<Path> materialLayers = { "LayerMaterial0", "LayerMaterial1", "LayerMaterial2",
        "LayerMaterial3" };
    pScene->setMaterialType(
        materialLayers[0], Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);
    pScene->setMaterialProperties(materialLayers[0],
        {
            { "basecolor_bitmap/rotation_angle", 45.0f },
            { "basecolor_bitmap/uv_offset", glm::vec2(0, 0) },
            { "basecolor_bitmap/uv_scale", glm::vec2(2, 0.5) },
            { "opacity_bitmap/rotation_angle", 45.0f },
            { "opacity_bitmap/uv_offset", glm::vec2(0, 0) },
            { "opacity_bitmap/uv_scale", glm::vec2(2, 0.5) },
        });

    pScene->setMaterialType(
        materialLayers[1], Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);
    pScene->setMaterialProperties(materialLayers[1],
        {
            { "basecolor_bitmap/rotation_angle", -15.0f },
            { "basecolor_bitmap/uv_offset", glm::vec2(3.0, 0) },
            { "basecolor_bitmap/uv_scale", glm::vec2(4, 4) },
            { "opacity_bitmap/rotation_angle", -15.0f },
            { "opacity_bitmap/uv_offset", glm::vec2(3.0, 0) },
            { "opacity_bitmap/uv_scale", glm::vec2(4, 4) },
        });

    pScene->setMaterialType(
        materialLayers[2], Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);
    pScene->setMaterialProperties(materialLayers[2],
        {
            { "basecolor_bitmap/rotation_angle", 5.0f },
            { "basecolor_bitmap/uv_offset", glm::vec2(3.0, 2.0) },
            { "basecolor_bitmap/uv_scale", glm::vec2(4, 4) },
            { "opacity_bitmap/rotation_angle", 5.0f },
            { "opacity_bitmap/uv_offset", glm::vec2(3.0, 2.0) },
            { "opacity_bitmap/uv_scale", glm::vec2(4, 4) },
        });

    pScene->setMaterialType(
        materialLayers[3], Names::MaterialTypes::kMaterialX, processedLayer0MaterialXString);
    pScene->setMaterialProperties(materialLayers[3],
        {
            { "basecolor_bitmap/rotation_angle", -60.0f },
            { "basecolor_bitmap/uv_offset", glm::vec2(1.5, 0.0) },
            { "basecolor_bitmap/uv_scale", glm::vec2(0.5, 4) },
            { "opacity_bitmap/rotation_angle", -60.0f },
            { "opacity_bitmap/uv_offset", glm::vec2(1.5, 0.0) },
            { "opacity_bitmap/uv_scale", glm::vec2(0.5, 4) },
        });

    Path baseMaterial("BaseMaterial");
    pScene->setMaterialType(
        baseMaterial, Names::MaterialTypes::kMaterialX, processedBaseMaterialXString);

    int numGeomLayers = 1;
    vector<string> geometryLayers;

    float scales[]      = { 1, 2, 1.5f, 0.4f };
    glm::vec2 origins[] = { glm::vec2(0.75f, 1.0f), glm::vec2(-0.25f, 1.2f), glm::vec2(0.25f, 0.6f),
        glm::vec2(-0.5f, 1.4f) };

    vector<vector<glm::vec2>> xformedUVs(numGeomLayers);

    for (int layer = 0; layer < numGeomLayers; layer++)
    {
        auto& uvs = xformedUVs[layer];
        uvs.resize(TestHelpers::TeapotModel::verticesCount());

        for (uint32_t i = 0; i < uvs.size(); i++)
        {
            uvs[i].x = *(TestHelpers::TeapotModel::vertices() + (i * 3 + 0));
            uvs[i].y = *(TestHelpers::TeapotModel::vertices() + (i * 3 + 1));
            uvs[i]   = origins[layer] - uvs[i];
            uvs[i] *= scales[layer];
        }

        const Path kDecalUVGeomPath = "DecalUVGeomPath" + to_string(layer);
        GeometryDescriptor geomDesc;
        geomDesc.type = PrimitiveType::Triangles;
        geomDesc.vertexDesc.attributes[Names::VertexAttributes::kTexCoord0] =
            AttributeFormat::Float2;
        geomDesc.vertexDesc.count = TestHelpers::TeapotModel::verticesCount();
        geomDesc.getAttributeData = [&xformedUVs, layer](AttributeDataMap& buffers,
                                        size_t firstVertex, size_t vertexCount, size_t firstIndex,
                                        size_t indexCount) {
            AU_ASSERT(firstVertex == 0, "Partial update not supported");
            AU_ASSERT(vertexCount == TestHelpers::TeapotModel::verticesCount(),
                "Partial update not supported");
            AU_ASSERT(firstIndex == 0, "Partial update not supported");
            AU_ASSERT(indexCount == 0, "Partial update not supported");
            buffers[Names::VertexAttributes::kTexCoord0].address = xformedUVs[layer].data();
            buffers[Names::VertexAttributes::kTexCoord0].size =
                TestHelpers::TeapotModel::verticesCount() * sizeof(vec2);
            buffers[Names::VertexAttributes::kTexCoord0].stride = sizeof(vec2);

            return true;
        };
        pScene->setGeometryDescriptor(kDecalUVGeomPath, geomDesc);
    }

    for (size_t layer = 0; layer < materialLayers.size(); layer++)
    {
        const Path kDecalUVGeomPath = "DecalUVGeomPath0";
        geometryLayers.push_back(kDecalUVGeomPath);
    }

    // Create a teapot instance with a default material.
    Path geometry      = createTeapotGeometry(*pScene);
    Path planeGeometry = createPlaneGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance("TeapotInstance", geometry,
        { { Names::InstanceProperties::kMaterial, baseMaterial },
            { Names::InstanceProperties::kMaterialLayers, materialLayers },
            { Names::InstanceProperties::kGeometryLayers, geometryLayers },
            { Names::InstanceProperties::kTransform, glm::translate(glm::vec3(0, -0.5, -0.8)) } }));
    EXPECT_TRUE(pScene->addInstance(
        "Plane", planeGeometry, { { Names::InstanceProperties::kMaterial, baseMaterial } }));

    ASSERT_BASELINE_IMAGE_PASSES(currentTestName());
}

// Normal map image test.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestNormalMapMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestNormalMapMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.25f, +1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // Create geometry.
    Path planePath  = createPlaneGeometry(*pScene);
    Path teapotPath = createTeapotGeometry(*pScene);

    // Create material from mtlx document containing normal map.
    string materialXFullPath   = dataPath() + "/Materials/NormalMapExample.mtlx";
    string processedMtlXString = loadAndProcessMaterialXFile(materialXFullPath);
    EXPECT_FALSE(processedMtlXString.empty());
    const Path kMaterialPath = "NormalMaterial";
    pScene->setMaterialType(kMaterialPath, Names::MaterialTypes::kMaterialX, processedMtlXString);

    mat4 scaleMtx = scale(vec3(2, 2, 2));

    // Create geometry with the material.
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial]  = kMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = scaleMtx;
    EXPECT_TRUE(pScene->addInstance(nextPath(), planePath, instProps));

    instProps[Names::InstanceProperties::kMaterial]  = kMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = mat4();
    EXPECT_TRUE(pScene->addInstance(nextPath(), teapotPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

// Test object space normal in MaterialX.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestObjectSpaceMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestObjectSpaceMaterialX)
#endif
{
    // This mtlx file requires support ADSK MaterialX support.
    if (!adskMaterialXSupport())
        return;

    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();
    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setDefaultRendererPathTracingIterations(256);

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    // Create teapot geom.
    Path geometry = createTeapotGeometry(*pScene);

    // Try loading a MtlX file dumped from Oxide.
    Path material("ThreadMaterial");
    pScene->setMaterialType(
        material, Names::MaterialTypes::kMaterialXPath, dataPath() + "/Materials/TestThread.mtlx");

    // Add to scene.
    Path instance("ThreadInstance");
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial] = material;
    EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName() + "_ThreadMtlX", "Materials");
}

// Test Prism MaterialX materials — enabled on non-Apple platforms.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestPrismMaterialX)
#else
TEST_P(MaterialTest, TestPrismMaterialX)
#endif
{
    // This mtlx file requires support ADSK materialX support.
    if (!adskMaterialXSupport())
        return;

    // For now, only DXR supports the Prism materials.
    // TODO: Re-enable once Vulkan is back to working.
    if (!isDirectX())
        return;

    // Larger image so the material details are clearly visible.
    createDefaultRenderer(256, 256);

    // Fewer iterations than a large batch render; still gives stable per-material images.
    setDefaultRendererPathTracingIterations(128);

    // Path to the Prism MaterialX test files.
    // Material name to MTLX filename mapping, ordered as in aggregatescene.usda.
    // Format: { MaterialName, MtlxFilename }
    // Standard Prism files follow the "Prism_NNN.mtlx" convention; custom files use their full name.
    const vector<pair<string, string>> materialMappings = {
        { "ATypeBulbFrosted800lm",          "Prism_414.mtlx"     },
        { "Display7SegmentsLCDCyan",         "Prism_399.mtlx"     },
        { "MarbleTranslucent",               "Prism_397.mtlx"     },
        { "LeatherPerforatedYellow",         "Prism_360.mtlx"     },
        { "BrassOld",                        "Prism_334.mtlx"     },
        { "PlasticTexturedPolka",            "Prism_253.mtlx"     },
        { "CherryGlossy",                    "Prism_247.mtlx"     },
        { "SteelMeshWireSmall",              "Prism_227.mtlx"     },
        { "GlassFrostedMedium",              "Prism_207.mtlx"     },
        { "BaseMaterialGlazing",             "Prism_2023.mtlx"    },
        { "ClearSafetypatternCurves",        "Prism_2022.mtlx"    },
        { "ClearFrosted",                    "Prism_2016.mtlx"    },
        { "GreySinglePaneHighReflectivity",  "Prism_2013.mtlx"    },
        { "BronzeInsulating",                "Prism_2011.mtlx"    },
        { "BlueGreenSinglePane",             "Prism_2004.mtlx"    },
        { "ClearSinglePane",                 "Prism_2000.mtlx"    },
        { "WaterCalmSea",                    "Prism_198.mtlx"     },
        { "PolycarbonateBronze",             "Prism_171.mtlx"     },
        { "GlassClouds",                     "Prism_159.mtlx"     },
        { "GlassClear",                      "Prism_152.mtlx"     },
        { "GraniteRed",                      "Prism_134.mtlx"     },
        { "RubberWeathered",                 "Prism_132.mtlx"     },
        { "PlasticGlossyGrey",               "Prism_119.mtlx"     },
        { "PowderCoatRoughBlue",             "Prism_105.mtlx"     },
        { "StainlessDiamondPlateTreadBrite", "Prism_066.mtlx"     },
        { "CopperBrushedRadial",             "Prism_048.mtlx"     },
        { "BrassPolished",                   "Prism_042.mtlx"     },
        { "AluminumKnurled",                 "Prism_026.mtlx"     },
        { "PaintMetalFlakeRed",              "Prism_013.mtlx"     },
        { "PaintMetallicGreen",              "Prism_005.mtlx"     },
        { "CarbonFiberTwill",                "Prism_002.mtlx"     },
        { "WoodWalnut",                      "WoodWalnut.mtlx"    },
    };

    // Base path to the Prism test files in the Aurora test assets folder.
    const string prismBasePath = dataPath() + "/Materials/Prism/";

    // Setup asset paths with the Prism base path for texture loading.
    setupAssetPaths({ prismBasePath });

    // Render each material on its own centred teapot and compare to a per-material baseline.
    for (const auto& [materialName, mtlxFilename] : materialMappings)
    {
        // Fresh scene for each material so there is no cross-contamination.
        auto pScene    = createDefaultScene();
        auto pRenderer = defaultRenderer();
        if (!pRenderer)
            return;

#if defined(__APPLE__)
        pRenderer->options().setBoolean("isGammaCorrectionEnabled", true);
#endif

        // Nice angled light that shows material colour and shape clearly.
        defaultDistantLight()->values().setFloat(
            Aurora::Names::LightProperties::kIntensity, 1.5f);
        defaultDistantLight()->values().setFloat3(
            Aurora::Names::LightProperties::kDirection,
            value_ptr(glm::vec3(0.0f, -0.5f, 1.0f)));
        defaultDistantLight()->values().setFloat3(
            Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

        // Load and apply the Prism MaterialX document.
        string mtlxPath           = prismBasePath + mtlxFilename;
        string processedMtlXStr   = loadAndProcessMaterialXFile(mtlxPath);
        EXPECT_FALSE(processedMtlXStr.empty())
            << "Failed to load MaterialX file: " << mtlxPath;
        if (processedMtlXStr.empty())
            continue;

        Path material(materialName);
        pScene->setMaterialType(material, Names::MaterialTypes::kMaterialX, processedMtlXStr);

        // Place the teapot at the origin so it fills the default FOV nicely.
        Path geometry = createTeapotGeometry(*pScene);
        Path instance("Teapot");
        Properties instProps;
        instProps[Names::InstanceProperties::kMaterial] = material;
        EXPECT_TRUE(pScene->addInstance(instance, geometry, instProps));

        // Render and compare against the per-material baseline image.
        ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(
            currentTestName() + "_" + materialName, "Materials");
    }
}

// Invalid image test.
#if defined(__APPLE__)
TEST_P(MaterialTest, DISABLED_TestMissingOrInvalidImageMaterialX) // TODO: Fix and re-enable this test.
#else
TEST_P(MaterialTest, TestMissingOrInvalidImageMaterialX)
#endif
{
    // Create the default scene (also creates renderer)
    auto pScene    = createDefaultScene();
    auto pRenderer = defaultRenderer();

    // If pRenderer is null this renderer type not supported, skip rest of the test.
    if (!pRenderer)
        return;

    setupAssetPaths();

    defaultDistantLight()->values().setFloat(Aurora::Names::LightProperties::kIntensity, 2.0f);
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kDirection, value_ptr(glm::vec3(0.0f, -0.25f, +1.0f)));
    defaultDistantLight()->values().setFloat3(
        Aurora::Names::LightProperties::kColor, value_ptr(glm::vec3(1, 1, 1)));

    // Create geometry.
    Path teapotPath = createTeapotGeometry(*pScene);

    // Create material from mtlx document containing normal map.
    string materialXFullPath   = dataPath() + "/Materials/MissingOrInvalidImageTest.mtlx";
    string processedMtlXString = loadAndProcessMaterialXFile(materialXFullPath);
    EXPECT_FALSE(processedMtlXString.empty());
    const Path kMaterialPath = "InvalidImageMaterial";
    pScene->setMaterialType(kMaterialPath, Names::MaterialTypes::kMaterialX, processedMtlXString);

    // Create geometry with the material.
    Properties instProps;
    instProps[Names::InstanceProperties::kMaterial]  = kMaterialPath;
    instProps[Names::InstanceProperties::kTransform] = mat4();
    EXPECT_TRUE(pScene->addInstance(nextPath(), teapotPath, instProps));

    // Render the scene and check baseline image.
    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Materials");
}

INSTANTIATE_TEST_SUITE_P(MaterialTests, MaterialTest, TEST_SUITE_RENDERER_TYPES());

} // namespace

#endif
