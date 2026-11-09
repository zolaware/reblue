/**
 * @file    platform/host_ui.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string_view>

#include <rex/gpu/graphics_system.h>
#include <rex/ppc/recompiled_code.h>
#include <rex/kernel/title_runtime.h>
#include <rex/ui/ui_system.h>

namespace bd::platform {

rex::ui::IUISystem *UI();
void SetUI(rex::ui::IUISystem *ui);

// The graphics role, which counts the frames reblue presents itself.
// The running title, null before launch and after unload.
rex::kernel::TitleRuntime *Title();
void SetTitle(rex::kernel::TitleRuntime *title);

// The recompiled function at a title address, null when there is none.
PPCFunc *FindTitleFunction(u32 address);

rex::gpu::IGraphicsSystem *Graphics();
void SetGraphics(rex::gpu::IGraphicsSystem *graphics);
void ShowFatal(std::string_view title, std::string_view body);

} // namespace bd::platform
