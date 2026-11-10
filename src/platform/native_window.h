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
#include <rex/platform/native_window.h>

namespace rex {
struct NativeWindow;
}

namespace bd::platform {

bool GetNativeRenderWindow(const rex::NativeWindow &native,
                           plume::RenderWindow &out);

} // namespace bd::platform
