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

#include "HGIVulkanSwapchain.h"

#if !defined(__APPLE__)

#include <pxr/imaging/hgiVulkan/commandQueue.h>
#include <pxr/imaging/hgiVulkan/device.h>
#include <pxr/imaging/hgiVulkan/hgi.h>
#include <pxr/imaging/hgiVulkan/instance.h>
#include <pxr/imaging/hgiVulkan/texture.h>

using namespace pxr;

BEGIN_AURORA

namespace
{

// Presentation is best-effort, so Vulkan failures stay warnings.
bool checkVk(VkResult result, const char* what)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    AU_WARN("Vulkan presentation: %s failed with VkResult %d.", what, static_cast<int>(result));
    return false;
}

} // namespace

HGIVulkanSwapchain::HGIVulkanSwapchain(
    Hgi* pHgi, void* nativeWindowHandle, uint32_t width, uint32_t height, bool vsyncEnabled) :
    _pHgi(pHgi), _width(width), _height(height), _vsyncEnabled(vsyncEnabled)
{
    auto* pHgiVulkan = dynamic_cast<HgiVulkan*>(pHgi);
    if (!pHgiVulkan)
    {
        AU_WARN("Vulkan presentation: HGI instance is not HgiVulkan; presentation disabled.");
        return;
    }

    HgiVulkanInstance* pInstance = pHgiVulkan->GetVulkanInstance();
    HgiVulkanDevice* pDevice     = pHgiVulkan->GetPrimaryDevice();
    if (!pInstance || !pDevice)
    {
        AU_WARN("Vulkan presentation: no Vulkan instance or device; presentation disabled.");
        return;
    }

    // The instance must have been created with the surface extensions.
    if (!pInstance->HasPresentation())
    {
        AU_WARN("Vulkan presentation: instance has no presentation support; disabled.");
        return;
    }

    _instance         = pInstance->GetVulkanInstance();
    _device           = pDevice->GetVulkanDevice();
    _physicalDevice   = pDevice->GetVulkanPhysicalDevice();
    _queueFamilyIndex = pDevice->GetGfxQueueFamilyIndex();

    HgiVulkanCommandQueue* pQueue = pDevice->GetCommandQueue();
    if (!pQueue)
    {
        AU_WARN("Vulkan presentation: no command queue; presentation disabled.");
        return;
    }
    _graphicsQueue = pQueue->GetVulkanGraphicsQueue();

    if (!createSurface(nativeWindowHandle))
    {
        return;
    }

    // The graphics queue must be able to present to this surface.
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(
        _physicalDevice, _queueFamilyIndex, _surface, &presentSupported);
    if (!presentSupported)
    {
        AU_WARN("Vulkan presentation: graphics queue family %u cannot present to this surface.",
            _queueFamilyIndex);
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
        return;
    }

    if (!createSwapchain(VK_NULL_HANDLE) || !createFrameResources())
    {
        destroyFrameResources();
        destroySwapchain();
    }
}

HGIVulkanSwapchain::~HGIVulkanSwapchain()
{
    if (_device != VK_NULL_HANDLE)
    {
        // Everything below may still be referenced by in-flight work.
        vkDeviceWaitIdle(_device);
    }

    destroyFrameResources();
    destroySwapchain();

    if (_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
    }
}

bool HGIVulkanSwapchain::createSurface(void* nativeWindowHandle)
{
#if defined(_WIN32)
    if (!nativeWindowHandle)
    {
        AU_WARN("Vulkan presentation: null window handle; presentation disabled.");
        return false;
    }

    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance                   = ::GetModuleHandle(nullptr);
    createInfo.hwnd                        = static_cast<HWND>(nativeWindowHandle);

    return checkVk(vkCreateWin32SurfaceKHR(_instance, &createInfo, nullptr, &_surface),
        "vkCreateWin32SurfaceKHR");
#else
    // XXX: Linux presentation (xcb/xlib/wayland) is not wired up.
    (void)nativeWindowHandle;
    AU_WARN("Vulkan presentation: not implemented on this platform; presentation disabled.");
    return false;
#endif
}

bool HGIVulkanSwapchain::createSwapchain(VkSwapchainKHR oldSwapchain)
{
    VkSurfaceCapabilitiesKHR caps = {};
    if (!checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
    {
        return false;
    }

    // A currentExtent of 0xFFFFFFFF means the surface size follows the swapchain; otherwise it is
    // dictated by the window and our requested size is ignored.
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu)
    {
        extent.width  = std::clamp(_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
    {
        // A zero-sized swapchain is invalid.
        return false;
    }
    _width  = extent.width;
    _height = extent.height;

    // Avoid double-encoding the already gamma-corrected source texture.
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, nullptr);
    if (formatCount == 0)
    {
        AU_WARN("Vulkan presentation: surface reports no formats.");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, formats.data());

    _surfaceFormat     = formats[0].format;
    _surfaceColorSpace = formats[0].colorSpace;
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            _surfaceFormat     = format.format;
            _surfaceColorSpace = format.colorSpace;
            break;
        }
    }

    // FIFO is the only mode guaranteed to exist, and is the vsynced one. Without vsync, prefer
    // MAILBOX (no tearing) and fall back to IMMEDIATE.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!_vsyncEnabled)
    {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            _physicalDevice, _surface, &modeCount, modes.data());

        bool hasMailbox   = false;
        bool hasImmediate = false;
        for (VkPresentModeKHR mode : modes)
        {
            hasMailbox   = hasMailbox || (mode == VK_PRESENT_MODE_MAILBOX_KHR);
            hasImmediate = hasImmediate || (mode == VK_PRESENT_MODE_IMMEDIATE_KHR);
        }
        presentMode = hasMailbox ? VK_PRESENT_MODE_MAILBOX_KHR
                                 : (hasImmediate ? VK_PRESENT_MODE_IMMEDIATE_KHR : presentMode);
    }

    uint32_t imageCount = std::max(caps.minImageCount, kFramesInFlight);
    if (caps.maxImageCount > 0)
    {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface                  = _surface;
    createInfo.minImageCount            = imageCount;
    createInfo.imageFormat              = _surfaceFormat;
    createInfo.imageColorSpace          = _surfaceColorSpace;
    createInfo.imageExtent              = extent;
    createInfo.imageArrayLayers         = 1;
    // TRANSFER_DST is what the blit needs; COLOR_ATTACHMENT keeps the images usable if a future
    // change wants to render into them directly.
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform     = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = VK_TRUE;
    createInfo.oldSwapchain   = oldSwapchain;

    if (!checkVk(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain),
            "vkCreateSwapchainKHR"))
    {
        _swapchain = VK_NULL_HANDLE;
        return false;
    }

    // The swapchain owns its images; they must not be destroyed individually.
    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, nullptr);
    _swapchainImages.resize(actualImageCount);
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, _swapchainImages.data());
    _swapchainLayouts.assign(actualImageCount, VK_IMAGE_LAYOUT_UNDEFINED);

    return true;
}

bool HGIVulkanSwapchain::createFrameResources()
{
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex        = _queueFamilyIndex;
    if (!checkVk(
            vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool), "vkCreateCommandPool"))
    {
        _commandPool = VK_NULL_HANDLE;
        return false;
    }

    _commandBuffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool                 = _commandPool;
    allocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount          = kFramesInFlight;
    if (!checkVk(vkAllocateCommandBuffers(_device, &allocInfo, _commandBuffers.data()),
            "vkAllocateCommandBuffers"))
    {
        _commandBuffers.clear();
        return false;
    }

    _acquireSemaphores.resize(kFramesInFlight, VK_NULL_HANDLE);
    _blitCompleteSemaphores.resize(kFramesInFlight, VK_NULL_HANDLE);
    _frameFences.resize(kFramesInFlight, VK_NULL_HANDLE);
    // No submit is outstanding yet, so nothing waits on these fences until one is.
    _frameSubmitted.assign(kFramesInFlight, 0);

    // Fences start unsignalled. _frameSubmitted prevents present() from waiting on an
    // unsignalled fence. Each fence is reset before its corresponding submit.
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo         = {};
    fenceInfo.sType                     = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    for (uint32_t i = 0; i < kFramesInFlight; i++)
    {
        if (!checkVk(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_acquireSemaphores[i]),
                "vkCreateSemaphore") ||
            !checkVk(
                vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_blitCompleteSemaphores[i]),
                "vkCreateSemaphore") ||
            !checkVk(vkCreateFence(_device, &fenceInfo, nullptr, &_frameFences[i]), "vkCreateFence"))
        {
            return false;
        }
    }

    return true;
}

void HGIVulkanSwapchain::destroyFrameResources()
{
    if (_device == VK_NULL_HANDLE)
    {
        return;
    }

    for (VkSemaphore semaphore : _acquireSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(_device, semaphore, nullptr);
        }
    }
    for (VkSemaphore semaphore : _blitCompleteSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(_device, semaphore, nullptr);
        }
    }
    for (VkFence fence : _frameFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(_device, fence, nullptr);
        }
    }
    _acquireSemaphores.clear();
    _blitCompleteSemaphores.clear();
    _frameFences.clear();
    _frameSubmitted.clear();

    if (_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
    }
    _commandBuffers.clear();
    _frameIndex = 0;
}

void HGIVulkanSwapchain::destroySwapchain()
{
    if (_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);
        _swapchain = VK_NULL_HANDLE;
    }
    _swapchainImages.clear();
    _swapchainLayouts.clear();
}

// Rebuild from the current size and vsync state in-place.
void HGIVulkanSwapchain::recreateSwapchain()
{
    if (_surface == VK_NULL_HANDLE)
    {
        return;
    }

    // In-flight work may still reference the old images; wait for them to complete. 
    vkDeviceWaitIdle(_device);

    VkSwapchainKHR oldSwapchain = _swapchain;
    _swapchain                  = VK_NULL_HANDLE;
    _swapchainImages.clear();
    _swapchainLayouts.clear();

    createSwapchain(oldSwapchain);

    // Retiring the old swapchain is valid once the new one has been created from it.
    if (oldSwapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(_device, oldSwapchain, nullptr);
    }
}

void HGIVulkanSwapchain::resize(uint32_t width, uint32_t height)
{
    if (_surface == VK_NULL_HANDLE)
    {
        return;
    }
    if (width == _width && height == _height && isValid())
    {
        return;
    }

    _width  = width;
    _height = height;
    recreateSwapchain();
}

void HGIVulkanSwapchain::setVSyncEnabled(bool enabled)
{
    if (enabled == _vsyncEnabled)
    {
        return;
    }
    _vsyncEnabled = enabled;

    // The present mode is baked into the swapchain and must be rebuilt.
    recreateSwapchain();
}

// Records the layout transitions and the blit for one frame into cmd.
void HGIVulkanSwapchain::recordBlit(VkCommandBuffer cmd, VkImage sourceImage,
    VkImageLayout sourceLayout, VkImage destImage, uint32_t sourceWidth, uint32_t sourceHeight)
{
    const VkImageSubresourceRange colorRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier barriers[2] = {};
    for (VkImageMemoryBarrier& barrier : barriers)
    {
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange    = colorRange;
    }

    // Source: whatever layout HGI left it in -> TRANSFER_SRC.
    barriers[0].image         = sourceImage;
    barriers[0].oldLayout     = sourceLayout;
    barriers[0].newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    // Destination: the acquired swapchain image -> TRANSFER_DST. Its previous contents are not
    // needed, so UNDEFINED is the correct old layout and lets the driver discard them.
    barriers[1].image         = destImage;
    barriers[1].oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 2, barriers);

    // Identity offsets: the Aurora render target is already y-down, matching the swapchain.
    VkImageBlit blit                   = {};
    blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel       = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount     = 1;
    blit.srcOffsets[0]                 = { 0, 0, 0 };
    blit.srcOffsets[1]                 = { static_cast<int32_t>(sourceWidth),
        static_cast<int32_t>(sourceHeight), 1 };
    blit.dstSubresource                = blit.srcSubresource;
    blit.dstOffsets[0]                 = { 0, 0, 0 };
    blit.dstOffsets[1] = { static_cast<int32_t>(_width), static_cast<int32_t>(_height), 1 };

    vkCmdBlitImage(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Destination -> PRESENT_SRC, and restore the source to the layout HGI expects.
    barriers[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout     = sourceLayout;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    barriers[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
        0, nullptr, 0, nullptr, 2, barriers);
}

// Retires a frame that acquired a swapchain image but will not go on to present it.
void HGIVulkanSwapchain::retireAcquiredFrame(uint32_t frame)
{
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo              = {};
    submitInfo.sType                     = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount        = 1;
    submitInfo.pWaitSemaphores           = &_acquireSemaphores[frame];
    submitInfo.pWaitDstStageMask         = &waitStage;

    // Consume the pending signal on the acquire semaphore with an empty batch,
    // and signal the frame fence to maintain the submit completion invariant.
    vkResetFences(_device, 1, &_frameFences[frame]);
    const bool submitted =
        checkVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _frameFences[frame]),
            "vkQueueSubmit (retire)");
    _frameSubmitted[frame] = submitted ? uint8_t(1) : uint8_t(0);
}

bool HGIVulkanSwapchain::present(HgiTextureHandle source)
{
    if (!isValid() || !source)
    {
        return false;
    }

    auto* pSourceTexture = dynamic_cast<HgiVulkanTexture*>(source.Get());
    if (!pSourceTexture)
    {
        AU_WARN("Vulkan presentation: source texture is not an HgiVulkanTexture.");
        return false;
    }

    const uint32_t frame = _frameIndex;

    // Throttle to kFramesInFlight, and guarantee the command buffer and semaphores for this frame
    // are no longer in use before they are recorded into again.
    if (_frameSubmitted[frame])
    {
        vkWaitForFences(_device, 1, &_frameFences[frame], VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex          = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        _device, _swapchain, UINT64_MAX, _acquireSemaphores[frame], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // The window changed size behind our back.
        recreateSwapchain();
        return false;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        checkVk(acquireResult, "vkAcquireNextImageKHR");
        return false;
    }

    // Hgi may defer submissions, so force rendering to drain before the blit.
    // TODO: Replace this stall with an explicit semaphore once render() is no longer blocking.
    vkQueueWaitIdle(_graphicsQueue);

    VkCommandBuffer cmd = _commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!checkVk(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer"))
    {
        retireAcquiredFrame(frame);
        return false;
    }

    const HgiTextureDesc& sourceDesc = pSourceTexture->GetDescriptor();
    recordBlit(cmd, pSourceTexture->GetImage(), pSourceTexture->GetImageLayout(),
        _swapchainImages[imageIndex], static_cast<uint32_t>(sourceDesc.dimensions[0]),
        static_cast<uint32_t>(sourceDesc.dimensions[1]));
    _swapchainLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    if (!checkVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer"))
    {
        retireAcquiredFrame(frame);
        return false;
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo              = {};
    submitInfo.sType                     = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount        = 1;
    submitInfo.pWaitSemaphores           = &_acquireSemaphores[frame];
    submitInfo.pWaitDstStageMask         = &waitStage;
    submitInfo.commandBufferCount        = 1;
    submitInfo.pCommandBuffers           = &cmd;
    submitInfo.signalSemaphoreCount      = 1;
    submitInfo.pSignalSemaphores         = &_blitCompleteSemaphores[frame];

    // Reset fence before submit; record if submit succeeds
    vkResetFences(_device, 1, &_frameFences[frame]);
    _frameSubmitted[frame] = 0;
    if (!checkVk(
            vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _frameFences[frame]), "vkQueueSubmit"))
    {
        return false;
    }
    _frameSubmitted[frame] = 1;

    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &_blitCompleteSemaphores[frame];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &_swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);

    _frameIndex = (frame + 1) % kFramesInFlight;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapchain();
        return false;
    }

    return checkVk(presentResult, "vkQueuePresentKHR");
}

END_AURORA

#endif // !defined(__APPLE__)
