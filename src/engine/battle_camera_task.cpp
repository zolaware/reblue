/**
 * @file    engine/battle_camera_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/battle_camera_task.h"

#include <rex/ppc/hook.h>
#include <rex/ppc/midasm.h>

#include "engine/events.h"
#include "engine/game.h"

REX_DETOUR(bdBattleDataLoad, u32, (u32 data)) {
  const bool wasActive =
      static_cast<bool>(bd::engine::Game::Get().BattleCameraTask());
  const u32 result = original(data);
  const bd::engine::BattleCameraTask battle =
      bd::engine::Game::Get().BattleCameraTask();
  if (!wasActive && battle)
    bd::engine::Events::Publish(bd::engine::BattleStarted{battle});
  return result;
}

// Paired with the load above: the BattleCameraTask destructor clearing the same
// global. The task is already DEAD-flagged by then, so the battle the event
// carries reads as inactive, and it is.
REX_MIDASM_HOOK(bdBattleCameraDestroyHook, ()) {
  bd::engine::Events::Publish(
      bd::engine::BattleEnded{bd::engine::Game::Get().BattleCameraTask()});
}
REX_MIDASM_AT(bdBattleCameraDestroyHook, 0x8225432C, before);
