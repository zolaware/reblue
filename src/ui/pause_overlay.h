/**
 * @file    ui/pause_overlay.h
 * @brief   The notice box shown while a cutscene is paused.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/ui/imgui_dialog.h>

struct ImFontAtlas;
struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
} // namespace rex::ui

namespace bd::ui {

class PauseOverlay final : public rex::ui::ImGuiDialog {
public:
  explicit PauseOverlay(rex::ui::ImGuiDrawer *drawer);
  ~PauseOverlay();

  static void InitFonts(ImFontAtlas *atlas);

protected:
  void OnDraw(ImGuiIO &io) override;
};

} // namespace bd::ui
