/**
 * @file    engine/game_step.cpp
 * @brief   The bdMainGameStep hook: one guest logic step, and everything that
 *          has to observe it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <rex/hook.h>

#include "engine/battle.h"
#include "engine/cheats.h"
#include "engine/frame_interp.h"
#include "engine/hud_fade.h"

REX_EXTERN(__imp__bdMainGameStep);
REX_HOOK_RAW(bdMainGameStep) {
  bd::engine::OnGuestGameStep();
  bd::engine::OnBattleGameStep();
  bd::engine::HudFade::Get().Poll();
  __imp__bdMainGameStep(ctx, base);
  bd::engine::Cheats::Get().Apply();
}
