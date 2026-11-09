/**
 * @file    ui/perf_overlay.h
 * @brief   The frame time box: FPS, worst frame, CPU main, CPU render, GPU and
 *          which of them bounds the frame.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/ui/ui_system.h>

struct ImGuiIO;

namespace rex::ui {
} // namespace rex::ui

namespace bd::ui {

class PerfOverlay final : public rex::ui::DialogBase {
public:
  explicit PerfOverlay(rex::ui::IUISystem &ui);
  ~PerfOverlay();

  bool Draw(ImGuiIO &io) override;
};

} // namespace bd::ui
