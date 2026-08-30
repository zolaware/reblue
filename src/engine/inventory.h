/**
 * @file    engine/inventory.h
 * @brief   Gold and the 512-slot item table in ItemSaveData.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// One inventory slot. An Id of zero means the slot is empty.
struct Item {
  u32 id = 0;
  u32 count = 0;
};

class Inventory {
public:
  Inventory() = default;

  explicit operator bool() const;

  u32 Gold() const;
  bool SetGold(u32 v); // clamps to 99999999

  // Ancient medals: the separate currency the ruins inns and healers take.
  u32 Medals() const;
  bool SetMedals(u32 v); // clamps to 9999, as bdScriptOpMedalChange does
  static constexpr u32 kMedalsMax = 9999;

  size_t SlotCount() const; // fixed at 512, empty slots included
  Item At(size_t slot) const;
  bool SetAt(size_t slot, u32 item_id, u32 count); // count clamps to 99

  size_t UsedCount() const; // slots with a non-zero id
};

} // namespace bd::engine
