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

#include <rex/system.h>

namespace bd::platform {
namespace {

std::atomic<rex::ui::IUserInterface *> g_ui{nullptr};

} // namespace

rex::ui::IUserInterface *UI() { return g_ui.load(std::memory_order_acquire); }

void SetUI(rex::ui::IUserInterface *ui) {
  g_ui.store(ui, std::memory_order_release);
}

void ShowFatal(std::string_view title, std::string_view body) {
  if (rex::ui::IUserInterface *ui = UI()) {
    ui->ShowMessage(rex::ui::MessageKind::kError, title, body);
    return;
  }
  rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::kError,
                            std::string(title) + "\n\n" + std::string(body));
}

} // namespace bd::platform
