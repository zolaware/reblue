/**
 * @file    engine/field_player_entity.h
 * @brief   The field entity holding both party member chains, the marching
 *          party and the recruited roster.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/list.h"
#include "engine/object.h"
#include "engine/ply_task.h"

namespace bd::engine {

class FieldPlayerEntity : public Object {
public:
  FieldPlayerEntity() = default;
  explicit FieldPlayerEntity(u32 address) : Object(address) {}

  // In formation order. Index 0 leads.
  List<PlyTask> Party() const;

  List<PlyTask> Roster() const;
  PlyTask Leader() const;

  // Whether the player is steering the leader, rather than a cutscene or
  // script moving the party.
  bool HasControl() const;

  // Whether the player has earned the field skill in the slot. The field
  // encounter menu draws a locked one but refuses to put the cursor on it.
  bool HasFieldSkill(int slot) const;

  // Members with HP above zero that are neither petrified nor knocked out.
  // This is the engine's own participation count, taken from the field party
  // walk at 0x823AECE0.
  size_t ActiveCount() const;
};

} // namespace bd::engine
