/**
 * @file    platform/host_ui.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/host_ui.h"

#include <atomic>
#include <string>

#include <rex/platform/process.h>

namespace bd::platform {
namespace {

std::atomic<rex::ui::IUISystem *> g_ui{nullptr};

std::atomic<rex::gpu::IGraphicsSystem *> g_graphics{nullptr};
std::atomic<rex::kernel::TitleRuntime *> g_title{nullptr};

} // namespace

rex::ui::IUISystem *UI() { return g_ui.load(std::memory_order_acquire); }

void SetUI(rex::ui::IUISystem *ui) {
  g_ui.store(ui, std::memory_order_release);
}

rex::kernel::TitleRuntime *Title() { return g_title.load(std::memory_order_acquire); }

void SetTitle(rex::kernel::TitleRuntime *title) {
  g_title.store(title, std::memory_order_release);
}

PPCFunc *FindTitleFunction(u32 address) {
  rex::kernel::TitleRuntime *title = Title();
  if (!title)
    return nullptr;
  auto found = title->FindFunction(address);
  return found ? *found : nullptr;
}

rex::gpu::IGraphicsSystem *Graphics() {
  return g_graphics.load(std::memory_order_acquire);
}

void SetGraphics(rex::gpu::IGraphicsSystem *graphics) {
  g_graphics.store(graphics, std::memory_order_release);
}

void ShowFatal(std::string_view title, std::string_view body) {
  if (rex::ui::IUISystem *ui = UI()) {
    ui->ShowMessage(rex::MessageType::kError, title, body);
    return;
  }
  rex::ShowMessageBox(rex::MessageType::kError,
                      std::string(title) + "\n\n" + std::string(body));
}

} // namespace bd::platform
