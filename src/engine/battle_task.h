/**
 * @file    engine/battle_task.h
 * @brief   The battle manager: the phase machine, the enemy groups, and the
 *          command/group/member tree the target step picks out of.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/ene_task.h"
#include "engine/list.h"
#include "engine/task.h"

namespace bd::engine {

// The region an action covers. Mode 0 and kShapeEverything both mean the
// player is not choosing one target out of many.
inline constexpr u32 kShapeEverything = 8;

class BattleShape : public Object {
public:
  BattleShape() = default;
  explicit BattleShape(u32 address) : Object(address) {}

  u32 Mode() const;
  bool Enabled() const;
};

class BattleMember : public Object {
public:
  BattleMember() = default;
  explicit BattleMember(u32 address) : Object(address) {}

  u32 ActorAddress() const;
};

class BattleGroup : public Object {
public:
  BattleGroup() = default;
  explicit BattleGroup(u32 address) : Object(address) {}

  size_t MemberCount() const;
  BattleMember MemberAt(size_t i) const;
};

class BattleCommand : public Object {
public:
  BattleCommand() = default;
  explicit BattleCommand(u32 address) : Object(address) {}

  size_t GroupCount() const;
  BattleGroup GroupAt(size_t i) const;
};

// No root global exists: the manager is only ever passed as 'this', so the
// handle the facade answers with is captured by the scene update hook and is
// empty until that hook has run this step.
class BattleTask : public Task {
public:
  BattleTask() = default;
  explicit BattleTask(u32 address) : Task(address) {}

  // Called once at the top of each bdMainGameStep. The captured manager EA is
  // only trusted within the step that produced it: a freed manager block loses
  // its DEAD sentinel once the heap reuses it, so the sentinel alone cannot
  // prove liveness across frames.
  static void OnBattleGameStep();

  // What an unresolved manager reads as, for the fields where every value the
  // engine writes is a real one.
  static constexpr u32 kNoPhase = ~0u;

  u32 CombinedNum() const;
  bool ResourcesLoaded() const;

  u32 Phase() const; // 0=result, 1=action-dispatch, 2=transition
  u32 SubPhase() const;
  u32 ActionStep() const;

  Task CurrentActor() const;

  List<Task> EnemyGroups() const;
  static List<EneTask> EnemiesOf(const Task &group);

  size_t EnemyCount() const;
  EneTask EnemyAt(size_t i) const;

  size_t CommandCount() const;
  BattleCommand CommandAt(size_t i) const;

  BattleShape TargetShape() const;

  // The block the engine's own selectable test is handed a patched copy of.
  u32 SelectionAddress() const;
};

} // namespace bd::engine
