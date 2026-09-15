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
#pragma once

#include "HGIRenderBuffer.h"

BEGIN_AURORA

// Forward declarations.
class HGIRenderer;
#if !defined(__APPLE__)
class HGIVulkanSwapchain;
#endif

// An internal implementation for IWindow.
class HGIWindow : public IWindow
{
public:
    // Constructor and destructor.
    HGIWindow(HGIRenderer* pRenderer, WindowHandle window, uint32_t width, uint32_t height);
    ~HGIWindow();

    void resize(uint32_t width, uint32_t height) override;

    void setVSyncEnabled(bool enabled) override;

    // Offscreen buffer for rendering.
    HGIRenderBuffer* renderBuffer() const { return _pRenderBuffer.get(); }

    // Copies the render buffer to the native window. A false return means the frame was dropped
    // (for example the swapchain was rebuilt); the next frame will present normally.
    bool present();

    // Whether this window can present. When false, client must read render buffer and blit.
    bool isPresentable() const;

    uint32_t width() const { return _width; }
    uint32_t height() const { return _height; }

private:
    uint32_t _width    = 0;
    uint32_t _height   = 0;
    bool _vsyncEnabled = true;

    std::shared_ptr<HGIRenderBuffer> _pRenderBuffer;
#if !defined(__APPLE__)
    std::unique_ptr<HGIVulkanSwapchain> _pSwapchain;
#endif
};

END_AURORA
