/**
 * @file    ui/update_prompt_dialog.h
 * @brief   Offers the newer build a startup check found: accept downloads,
 *          verifies and applies it, decline drops it for the session.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <string>

#include <rex/types.h>

#include <rex/ui/ui_system.h>

#include "engine/engine.h"

struct ImGuiIO;

namespace bd::ui {

struct UpdatePromptContext {
  std::string version;
  u64 size = 0; // artifact size in bytes, for the offer text
};

// The download runs in Updates, not here, so closing this mid-download costs
// nothing and the dialog owns no thread.
class UpdatePromptDialog final : public rex::ui::DialogBase {
public:
  explicit UpdatePromptDialog(UpdatePromptContext ctx);

  bool Draw(ImGuiIO &io) override;
  void OnClose() override { closed_.store(true); }
  bool IsClosed() const { return closed_.load(); }

private:
  engine::HostPointerClaim pointer_;
  UpdatePromptContext ctx_;
  bool accepted_ = false;
  std::atomic<bool> closed_{false};
};

} // namespace bd::ui
