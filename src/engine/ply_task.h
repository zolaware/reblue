/**
 * @file    engine/ply_task.h
 * @brief   The party member task node, one link on both the marching and the
 *          roster chains, wrapping a Player body.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/chara.h"
#include "engine/list.h"
#include "engine/task.h"

namespace bd::engine {

class PlyTask : public Task {
public:
  PlyTask() = default;
  explicit PlyTask(u32 address) : Task(address) {}

  static PlyTask FromChara(const engine::Chara &chara);

  static List<PlyTask> PartyFrom(u32 head);
  static List<PlyTask> RosterFrom(u32 head);

  Player Chara() const;

  u32 UniqueId() const;
  bool IsVisible() const;
  bool IsHidden() const;

  u32 BlinkArm() const;
  bool SetBlinkArm(u32 v);
};

} // namespace bd::engine
