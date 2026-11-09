/**
 * @file    platform/native_window.h
 * @brief   Build a plume::RenderWindow swapchain target from the app window.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <plume_render_interface.h>

namespace rex::ui {
class IWindow;
}

namespace bd::platform {

bool GetNativeRenderWindow(rex::ui::IWindow &window, plume::RenderWindow &out);

} // namespace bd::platform
