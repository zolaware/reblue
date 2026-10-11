/**
 * @file    engine/battle_camera_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/battle_camera_task.h"

#include <rex/hook.h>
#include <rex/ppc.h>

#include "engine/events.h"
#include "engine/game.h"

REX_EXTERN(__imp__bdBattleDataLoad);
REX_HOOK_RAW(bdBattleDataLoad) {
  const bool wasActive =
      static_cast<bool>(bd::engine::Game::Get().BattleCameraTask());
  __imp__bdBattleDataLoad(ctx, base);
  const bd::engine::BattleCameraTask battle =
      bd::engine::Game::Get().BattleCameraTask();
  if (!wasActive && battle)
    bd::engine::Events::Publish(bd::engine::BattleStarted{battle});
}

// Paired with the load above: the BattleCameraTask destructor clearing the same
// global. The task is already DEAD-flagged by then, so the battle the event
// carries reads as inactive, and it is.
void bdBattleCameraDestroyHook() {
  bd::engine::Events::Publish(
      bd::engine::BattleEnded{bd::engine::Game::Get().BattleCameraTask()});
}
