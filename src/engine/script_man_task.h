/**
 * @file    engine/script_man_task.h
 * @brief   The field scene root: the running Script, its variable block, and
 *          the minimap task.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/mini_map_task.h"
#include "engine/script.h"
#include "engine/script_vars.h"
#include "engine/task.h"

namespace bd::engine {

// Scene states of the one state machine every field and battle transition
// drives.
inline constexpr u32 kSceneStateStageTransition = 5;
inline constexpr u32 kSceneStateGameOver = 9;

// No scene state at all, which is what an unresolved root reads as.
inline constexpr u32 kSceneStateNone = ~0u;

class ScriptManTask : public Task {
public:
  ScriptManTask() = default;
  explicit ScriptManTask(u32 address) : Task(address) {}

  u32 MapId() const;
  u32 SceneState() const;

  engine::Script Script() const;
  engine::ScriptVars Vars() const;
  engine::MiniMapTask MiniMap() const;
};

} // namespace bd::engine
