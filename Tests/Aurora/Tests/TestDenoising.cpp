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

#include <cmath>
#include <cstdlib>

using namespace Aurora;

namespace
{

class DenoisingTest : public TestHelpers::FixtureBase
{
public:
    DenoisingTest() {}
    ~DenoisingTest() {}

    // Render frames for NRD accumulation, then flush GPU work.
    void renderDenoisingFrames(uint32_t frameCount, bool resetHistory = true)
    {
        IRendererPtr pRenderer = defaultRenderer();
        pRenderer->options().setBoolean("isDenoisingEnabled", true);
        if (resetHistory)
            pRenderer->options().setBoolean("isResetHistoryEnabled", true);
        for (uint32_t i = 0; i < frameCount; i++)
            pRenderer->render(0, 1);
        pRenderer->waitForTask();
    }

    // Mean absolute difference between horizontally adjacent pixels, over the whole image.
    // Estimates image noise: 1 spp renders have more high-frequency noise than denoised renders.
    // Measure the rendered image to catch cases where denoising is enabled but insufficient.
    double measureLocalContrast(uint32_t width, uint32_t height)
    {
        size_t stride = 0;
        const auto* pData =
            reinterpret_cast<const uint8_t*>(defaultRenderBuffer()->data(stride, true));
        if (!pData)
            return 0.0;

        double total = 0.0;
        size_t count = 0;
        for (uint32_t y = 0; y < height; y++)
        {
            const uint8_t* pRow = pData + static_cast<size_t>(y) * stride;
            for (uint32_t x = 1; x < width; x++)
            {
                for (uint32_t c = 0; c < 3; c++)
                {
                    int const a = pRow[(x - 1) * 4 + c];
                    int const b = pRow[x * 4 + c];
                    total += std::abs(a - b);
                    count++;
                }
            }
        }
        return count ? total / static_cast<double>(count) : 0.0;
    }
};

// ==================== Denoising image tests ====================

// Test basic denoising functionalities.
TEST_P(DenoisingTest, TestDenoisingTeapot)
{
    if (!backendSupported())
        return;

    // TODO: Support denoising in hgi backend.
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(256, 256);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();

    // Add a metallic-rough teapot so NRD has meaningful specular + diffuse to denoise.
    const Path kMtlPath = "DenoisingTeapotMaterial";
    pScene->setMaterialProperties(kMtlPath,
        { { "base_color", vec3(0.8f, 0.6f, 0.2f) }, { "roughness", 0.4f }, { "metalness", 0.2f } });
    Path geomPath = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(
        nextPath(), geomPath, { { Names::InstanceProperties::kMaterial, kMtlPath } }));

    // Run 32 denoising frames (1 spp each) for NRD temporal accumulation.
    // Suppress the baseline helper's built-in render loop (iterations = 0) so that
    // only our denoising frames contribute to the final image.
    renderDenoisingFrames(32);
    setDefaultRendererPathTracingIterations(0);

    // NRD output varies by GPU, driver and NGX version; allow 2% pixel variance.
    setBaselineImageThresholds(0.2f, 0.02f, 0.05f, 0.10f);

    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Denoising");
}

// Test denoising with ground plane geometry.
TEST_P(DenoisingTest, TestDenoisingWithGroundPlane)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(256, 256);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();

    const Path kMtlPath = "DenoisingGPMaterial";
    pScene->setMaterialProperties(
        kMtlPath, { { "base_color", vec3(0.7f, 0.7f, 0.9f) }, { "roughness", 0.6f } });
    Path teapotGeom = createTeapotGeometry(*pScene);
    Path planeGeom  = createPlaneGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(
        nextPath(), teapotGeom, { { Names::InstanceProperties::kMaterial, kMtlPath } }));
    EXPECT_TRUE(pScene->addInstance(nextPath(), planeGeom,
        { { Names::InstanceProperties::kMaterial, kMtlPath },
            { Names::InstanceProperties::kTransform, glm::translate(glm::vec3(0, -1.5f, 0)) } }));

    renderDenoisingFrames(32);
    setDefaultRendererPathTracingIterations(0);
    setBaselineImageThresholds(0.2f, 0.02f, 0.05f, 0.10f); // see TestDenoisingTeapot

    ASSERT_BASELINE_IMAGE_PASSES_IN_FOLDER(currentTestName(), "Denoising");
}

// ==================== Denoising functional tests ====================

// Test toggling denoising on and off in various scenarios.
TEST_P(DenoisingTest, TestDenoisingToggle)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(128, 128);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();
    Path geomPath    = createTeapotGeometry(*pScene);

    // Cold enable: first-ever NRD initialisation.
    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    pRenderer->options().setBoolean("isResetHistoryEnabled", true);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // Disable: denoiser kept alive (warm path optimisation), no GPU stall.
    pRenderer->options().setBoolean("isDenoisingEnabled", false);
    EXPECT_NO_THROW(pRenderer->render(0, 4));

    // Warm re-enable: reuses the existing NRD instance, no rebuild.
    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    pRenderer->options().setBoolean("isResetHistoryEnabled", true);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // Rapid toggle cycle.
    pRenderer->options().setBoolean("isDenoisingEnabled", false);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    pRenderer->options().setBoolean("isResetHistoryEnabled", true);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
}

// Verify that enabling denoising with an empty scene (no geometry) does not crash.
TEST_P(DenoisingTest, TestDenoisingEmptyScene)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(128, 128);
    if (!pRenderer)
        return;

    // Scene with no instances.
    IScenePtr pScene = createDefaultScene();

    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    pRenderer->options().setBoolean("isResetHistoryEnabled", true);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
    EXPECT_NO_THROW(pRenderer->render(0, 1));
}

// Verify denoising by measuring its image effect. Denoiser initialization failure falls back to
// undenoised rendering, which could otherwise make the baseline tests pass without coverage.
TEST_P(DenoisingTest, TestDenoisingIsActuallyActive)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    const uint32_t kSize = 256;

    IRendererPtr pRenderer = createDefaultRenderer(kSize, kSize);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();

    // A rough, non-metallic teapot with mostly indirect diffuse, which is the channel RELAX filters and
    // the one that is unmistakably noisy at 1 spp.
    const Path kMtlPath = "DenoisingActiveMaterial";
    pScene->setMaterialProperties(kMtlPath,
        { { "base_color", vec3(0.7f, 0.7f, 0.7f) }, { "roughness", 0.8f }, { "metalness", 0.0f } });
    Path geomPath = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(
        nextPath(), geomPath, { { Names::InstanceProperties::kMaterial, kMtlPath } }));

    // One sample per pixel, denoiser off: the reference for "as noisy as it gets".
    pRenderer->options().setBoolean("isDenoisingEnabled", false);
    pRenderer->options().setBoolean("isResetHistoryEnabled", true);
    pRenderer->render(0, 1);
    pRenderer->waitForTask();
    double const noisyContrast = measureLocalContrast(kSize, kSize);

    // Same scene and sample count, denoiser on and given enough frames to accumulate.
    renderDenoisingFrames(32);
    double const denoisedContrast = measureLocalContrast(kSize, kSize);

    // Both numbers must be meaningful before the ratio between them means anything.
    ASSERT_GT(noisyContrast, 0.5)
        << "the 1 spp reference is not noisy, so this test cannot detect anything";
    ASSERT_GT(denoisedContrast, 0.0) << "denoised image is blank";

    // A working RELAX pass removes most of the pixel-to-pixel variation. Measured on this scene
    // the ratio is about 0.07 (local contrast 6.70 at 1 spp against 0.45 denoised), so the 0.75
    // threshold leaves an order of magnitude of headroom.
    EXPECT_LT(denoisedContrast, noisyContrast * 0.75)
        << "denoising had little or no effect on the image (1 spp local contrast " << noisyContrast
        << ", denoised " << denoisedContrast
        << "). Check the log for \"NRD RecreateD3D12 failed\", which means the denoiser never "
           "initialized and the renderer fell back to rendering undenoised.";
}

// ==================== Upscaler functional tests ====================
//
// NOTE: there is deliberately no baseline image test here. Real vendor SDK output varies by GPU,
// runtime and driver, so it is not comparable against a checked-in image; coverage for the
// upscaler paths is functional.

// Verify that setting upscalerMode to each of its values does not crash.
TEST_P(DenoisingTest, TestUpscalerMode)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(128, 128);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();
    Path geomPath    = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(nextPath(), geomPath, {}));

    // No upscaling (the default). Render normally.
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kNone);
    EXPECT_NO_THROW(pRenderer->render(0, 4));

    // DLSS Super Resolution request.
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kDLSS);
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // FSR request.
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kFSR);
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // DLSS Ray Reconstruction request. Unlike the other two this replaces the denoiser
    // rather than running after it, so it exercises a different path through updateFrameData()
    // and submitDenoising().
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kDLSSRayReconstruction);
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // An unrecognized name must degrade to no upscaling rather than fail the render.
    pRenderer->options().setString("upscalerMode", "NotAnUpscaler");
    EXPECT_NO_THROW(pRenderer->render(0, 1));

    // Back to off.
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kNone);
    EXPECT_NO_THROW(pRenderer->render(0, 2));
}

// Verify that every upscaler quality preset renders without crashing.
TEST_P(DenoisingTest, TestUpscalerQuality)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(256, 256);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();
    Path geomPath    = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(nextPath(), geomPath, {}));

    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kDLSS);

    // 0 = Native/DLAA (1.0x), 1 = Quality (1.5x), 2 = Balanced (1.7x), 3 = Performance (2.0x).
    for (int quality : { 0, 1, 2, 3 })
    {
        pRenderer->options().setInt("upscalerQuality", quality);
        pRenderer->options().setBoolean("isResetHistoryEnabled", true);
        EXPECT_NO_THROW(pRenderer->render(0, 1));
        EXPECT_NO_THROW(pRenderer->render(0, 1));
    }

    // Returning to Native must not leave any resource sized for the previous preset.
    pRenderer->options().setInt("upscalerQuality", 0);
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kNone);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
}

// Verify the temporal anti-aliasing resolve pass, which is what turns the sub-pixel camera jitter
// into anti-aliasing when denoising is on and no vendor upscaler is active.
TEST_P(DenoisingTest, TestTemporalResolveToggle)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(128, 128);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();
    Path geomPath    = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(nextPath(), geomPath, {}));

    pRenderer->options().setBoolean("isDenoisingEnabled", true);

    // Enabled: several frames so the history buffers actually ping-pong.
    pRenderer->options().setBoolean("isTemporalResolveEnabled", true);
    for (int i = 0; i < 4; i++)
        EXPECT_NO_THROW(pRenderer->render(0, 1));

    // Disabled mid-stream: this also disables camera jitter, so it must reset cleanly rather than
    // blending jittered history into unjittered frames.
    pRenderer->options().setBoolean("isTemporalResolveEnabled", false);
    for (int i = 0; i < 2; i++)
        EXPECT_NO_THROW(pRenderer->render(0, 1));

    // And back on again.
    pRenderer->options().setBoolean("isTemporalResolveEnabled", true);
    for (int i = 0; i < 2; i++)
        EXPECT_NO_THROW(pRenderer->render(0, 1));
}

// Stability check: denoising + both upscaler modes + a live material update do not crash.
TEST_P(DenoisingTest, TestUpscalerWithDenoisingAndMaterialUpdate)
{
    if (!backendSupported())
        return;
    if (!isDirectX())
        return;

    IRendererPtr pRenderer = createDefaultRenderer(128, 128);
    if (!pRenderer)
        return;

    IScenePtr pScene = createDefaultScene();
    Path geomPath    = createTeapotGeometry(*pScene);
    EXPECT_TRUE(pScene->addInstance(nextPath(), geomPath, {}));

    const Path kMtlPath = "UpscalerMaterialUpdateMaterial";
    pScene->setMaterialProperties(kMtlPath,
        { { "base_color", vec3(0.7f, 0.5f, 0.2f) }, { "roughness", 0.3f }, { "metalness", 0.2f } });
    EXPECT_TRUE(pScene->addInstance(
        nextPath(), geomPath, { { Names::InstanceProperties::kMaterial, kMtlPath } }));

    pRenderer->options().setBoolean("isDenoisingEnabled", true);
    for (const string& mode : { Names::UpscalerModes::kDLSS, Names::UpscalerModes::kFSR,
             Names::UpscalerModes::kDLSSRayReconstruction })
    {
        bool const isDLSS = mode == Names::UpscalerModes::kDLSS;

        pRenderer->options().setBoolean("isResetHistoryEnabled", true);
        pRenderer->options().setString("upscalerMode", mode);

        EXPECT_NO_THROW(pRenderer->render(0, 1));
        EXPECT_NO_THROW(pRenderer->render(0, 1));

        // Change the material after temporal history exists.
        pScene->setMaterialProperties(kMtlPath,
            { { "base_color", isDLSS ? vec3(0.2f, 0.55f, 0.8f) : vec3(0.8f, 0.3f, 0.2f) },
                { "roughness", isDLSS ? 0.55f : 0.15f },
                { "metalness", isDLSS ? 0.05f : 0.45f } });

        // Exercise Ray Reconstruction's demodulation albedo split.
        pRenderer->options().setBoolean("isResetHistoryEnabled", true);
        EXPECT_NO_THROW(pRenderer->render(0, 1));
    }

    // Disable upscaling, keep denoising enabled.
    pRenderer->options().setString("upscalerMode", Names::UpscalerModes::kNone);
    EXPECT_NO_THROW(pRenderer->render(0, 1));
}

INSTANTIATE_TEST_SUITE_P(DenoisingTests, DenoisingTest, TEST_SUITE_RENDERER_TYPES());

} // namespace

#endif // !defined(DISABLE_UNIT_TESTS)
