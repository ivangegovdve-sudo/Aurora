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

// Vulkan presentation is only used by the HGI backend on non-Apple platforms.
#if !defined(__APPLE__)

#include <cstdint>
#include <vector>

// Must be defined before the first Vulkan include.
#if defined(_WIN32)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif
#include <vulkan/vulkan.h>

// pxr aliases a versioned namespace, so forward declarations are ambiguous here.
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>

BEGIN_AURORA

/// Presents an HGI texture to a native window through a Vulkan swapchain.
class HGIVulkanSwapchain
{
public:
    /// \param pHgi The HGI instance; must be an HgiVulkan.
    /// \param nativeWindowHandle The platform window handle (HWND on Windows).
    /// \param width Initial width in pixels.
    /// \param height Initial height in pixels.
    /// \param vsyncEnabled Whether to wait for vertical blank when presenting.
    HGIVulkanSwapchain(pxr::Hgi* pHgi, void* nativeWindowHandle, uint32_t width, uint32_t height,
        bool vsyncEnabled);
    ~HGIVulkanSwapchain();

    HGIVulkanSwapchain(const HGIVulkanSwapchain&)            = delete;
    HGIVulkanSwapchain& operator=(const HGIVulkanSwapchain&) = delete;

    /// \brief Whether the swapchain was created successfully and can present.
    ///
    /// Construction fails softly (no exception, no abort) when the platform cannot present, e.g. a
    /// headless session or a device whose graphics queue lacks presentation support, or miss per-
    /// frame resources. The caller is expected to fall back to a CPU blit in that case.
    bool isValid() const { return _swapchain != VK_NULL_HANDLE && !_frameFences.empty(); }

    /// \brief Blits the supplied texture into the next swapchain image and presents it.
    /// \return false if the frame was dropped, e.g. because the swapchain went out of date and was
    ///         recreated. The caller may simply present again on the next frame.
    bool present(pxr::HgiTextureHandle source);

    /// \brief Recreates the swapchain at the new size. A zero dimension destroys it (minimized).
    void resize(uint32_t width, uint32_t height);

    /// \brief Recreates the swapchain with updated vsync mode.
    void setVSyncEnabled(bool enabled);

    uint32_t width() const { return _width; }
    uint32_t height() const { return _height; }

private:
    // Swapchain lifetime.
    bool createSurface(void* nativeWindowHandle);
    bool createSwapchain(VkSwapchainKHR oldSwapchain);
    void recreateSwapchain();
    bool createFrameResources();
    void destroySwapchain();
    void destroyFrameResources();

    void recordBlit(VkCommandBuffer cmd, VkImage sourceImage, VkImageLayout sourceLayout,
        VkImage destImage, uint32_t sourceWidth, uint32_t sourceHeight);

    void retireAcquiredFrame(uint32_t frame);

    // Number of frames that may be recorded before the CPU waits on the GPU.
    static constexpr uint32_t kFramesInFlight = 2;

    pxr::Hgi* _pHgi = nullptr;

    // From HgiVulkan (not owned).
    VkInstance _instance             = VK_NULL_HANDLE;
    VkDevice _device                 = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkQueue _graphicsQueue           = VK_NULL_HANDLE;
    uint32_t _queueFamilyIndex       = 0;

    // Owned.
    VkSurfaceKHR _surface       = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain   = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;

    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageLayout> _swapchainLayouts;

    std::vector<VkCommandBuffer> _commandBuffers;
    std::vector<VkSemaphore> _acquireSemaphores;
    std::vector<VkSemaphore> _blitCompleteSemaphores;
    std::vector<VkFence> _frameFences;
    std::vector<uint8_t> _frameSubmitted;

    VkFormat _surfaceFormat            = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR _surfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    uint32_t _width      = 0;
    uint32_t _height     = 0;
    bool _vsyncEnabled   = true;
    uint32_t _frameIndex = 0;
};

END_AURORA

#endif // !defined(__APPLE__)
