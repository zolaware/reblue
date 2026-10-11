/**
 * @file    engine/hud_fade.h
 * @brief   Idle fade for the field HUD: the compass, the dungeon minimap and
 *          the party cards.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

class HudFade {
public:
  static HudFade &Get();

  // Once per engine logic step.
  void Poll();

  // What each field HUD element multiplies its own authored alpha by. At 1 no
  // hook touches a register.
  float Alpha() const { return alpha_; }

private:
  HudFade() = default;
  HudFade(const HudFade &) = delete;
  HudFade &operator=(const HudFade &) = delete;

  float alpha_ = 1.0f;
  double idle_ = 0.0;
  double lastPoll_ = 0.0;
};

} // namespace bd::engine
