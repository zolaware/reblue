/**
 * @file    ui/screen_fade.h
 * @brief   Full-screen fade veil for host-driven screen transitions.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <mutex>

#include <rex/types.h>
#include <rex/ui/imgui_dialog.h>

struct ImGuiIO;

namespace rex::ui {
struct OverlayContext;
} // namespace rex::ui

namespace bd::ui {

// A black veil over the whole output, swept between clear and opaque on wall
// time. The overlay advances it on the present thread, so it moves smoothly
// whatever the guest tick is doing, and the driver in the guest polls IsOpaque
// and IsClear to sequence what happens under it. The lock keeps level, target
// and rate moving together: a present-thread step that read a new target
// against a stale level would draw a frame of the wrong screen, which an
// instant cut has no sweep to hide.
class ScreenFade {
public:
  static ScreenFade &Get();

  // Sweeps toward 'target' (0 clear, 1 opaque) over 'seconds' of full travel.
  // A zero or negative 'seconds' finishes it on this call.
  void FadeTo(f32 target, f32 seconds);
  bool IsOpaque() const;
  bool IsClear() const;

  // Advances toward the target and returns the level. FadeOverlay's step.
  f32 Step(f32 dt);

private:
  mutable std::mutex mutex_;
  f32 level_ = 0.0f;
  f32 target_ = 0.0f;
  f32 rate_ = 0.0f;
};

class FadeOverlay final : public rex::ui::ImGuiDialog {
public:
  explicit FadeOverlay(const rex::ui::OverlayContext &context);
  ~FadeOverlay();

protected:
  void OnDraw(ImGuiIO &io) override;
};

} // namespace bd::ui
