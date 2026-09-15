// Copyright 2025 Autodesk, Inc.
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

#include "HGIWindow.h"

#include "HGIRenderer.h"
#if !defined(__APPLE__)
#include "HGIVulkanSwapchain.h"
#endif

BEGIN_AURORA

HGIWindow::HGIWindow(
    HGIRenderer* pRenderer, WindowHandle window, uint32_t width, uint32_t height) :
    _width(width), _height(height)
{
    AU_ASSERT(pRenderer, "HGIWindow requires a renderer.");

#if defined(__APPLE__)
    (void)window;
#endif

    // Offscreen buffer for presentation. Use float format to match renderer output.
    _pRenderBuffer =
        std::make_shared<HGIRenderBuffer>(pRenderer, width, height, ImageFormat::Float_RGBA);

#if !defined(__APPLE__)
    // Vulkan can present to the window. Falls back to render buffer if unavailable.
    _pSwapchain = std::make_unique<HGIVulkanSwapchain>(
        pRenderer->hgi().get(), window, width, height, _vsyncEnabled);
    if (!_pSwapchain->isValid())
    {
        AU_WARN("HGIWindow: Vulkan presentation unavailable; the client must present the render "
                "buffer itself.");
        _pSwapchain.reset();
    }
#endif
}

HGIWindow::~HGIWindow() = default;

bool HGIWindow::isPresentable() const
{
#if !defined(__APPLE__)
    return _pSwapchain && _pSwapchain->isValid();
#else
    return false;
#endif
}

bool HGIWindow::present()
{
#if !defined(__APPLE__)
    if (!_pSwapchain || !_pRenderBuffer)
    {
        return false;
    }
    return _pSwapchain->present(_pRenderBuffer->storageTex());
#else
    return false;
#endif
}

void HGIWindow::resize(uint32_t width, uint32_t height)
{
    if (width == _width && height == _height)
    {
        return;
    }
    _width  = width;
    _height = height;

    if (_pRenderBuffer)
    {
        _pRenderBuffer->resize(width, height);
    }
#if !defined(__APPLE__)
    if (_pSwapchain)
    {
        _pSwapchain->resize(width, height);
    }
#endif
}

void HGIWindow::setVSyncEnabled(bool enabled)
{
    _vsyncEnabled = enabled;
#if !defined(__APPLE__)
    if (_pSwapchain)
    {
        _pSwapchain->setVSyncEnabled(enabled);
    }
#endif
}

END_AURORA
