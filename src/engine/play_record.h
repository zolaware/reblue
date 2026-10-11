/**
 * @file    engine/play_record.h
 * @brief   The running play record: counters the save block carries and the
 *          camp screens read, created once and outliving any one battle.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class PlayRecord : public Object {
public:
  PlayRecord() = default;
  explicit PlayRecord(u32 address) : Object(address) {}

  // The three names below are the host's reading of what the battle scene does
  // with them, not the engine's. It reads the first two back to pick a per-side
  // intro, so they may count encounters entered with player and with enemy
  // advantage rather than outcomes.
  u32 BattlesWon() const;
  u32 Escapes() const;
  u32 SurroundWins() const;
};

} // namespace bd::engine
