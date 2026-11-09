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

#include <rex/ui/ui_system.h>

struct ImFontAtlas;
struct ImGuiIO;

namespace rex::ui {
} // namespace rex::ui

namespace bd::ui {

class PauseOverlay final : public rex::ui::DialogBase {
public:
  explicit PauseOverlay(rex::ui::IUISystem &ui);
  ~PauseOverlay();

  static void InitFonts(ImFontAtlas *atlas);

  bool Draw(ImGuiIO &io) override;
};

} // namespace bd::ui
