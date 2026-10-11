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

#include <string>

#include <rex/types.h>

#include <rex/ui/imgui_dialog.h>

#include "engine/engine.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
} // namespace rex::ui

namespace bd::ui {

struct UpdatePromptContext {
  std::string version;
  u64 size = 0; // artifact size in bytes, for the offer text
};

// The download runs in Updates, not here, so closing this mid-download costs
// nothing and the dialog owns no thread.
class UpdatePromptDialog final : public rex::ui::ImGuiDialog {
public:
  UpdatePromptDialog(rex::ui::ImGuiDrawer *drawer, UpdatePromptContext ctx);

protected:
  void OnDraw(ImGuiIO &io) override;

private:
  engine::HostPointerClaim pointer_;
  UpdatePromptContext ctx_;
  bool accepted_ = false;
};

} // namespace bd::ui
