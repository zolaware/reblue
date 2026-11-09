/**
 * @file    ui/update_status.h
 * @brief   Corner readout for the startup update and content checks.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

namespace bd::ui {

class UpdateStatusOverlay final : public rex::ui::ImGuiDialog {
public:
  UpdateStatusOverlay() = default;
  ~UpdateStatusOverlay();

  // True once neither check has anything left to say, so the owner can drop
  // this rather than keep drawing an empty overlay.
  static bool Finished();

protected:
  void OnDraw(ImGuiIO &io) override;
};

} // namespace bd::ui
