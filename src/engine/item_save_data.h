/**
 * @file    engine/item_save_data.h
 * @brief   Gold and the 512-slot item table in the item save block.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class ItemSaveData : public Object {
public:
  // An id of zero means the slot is empty.
  struct Item {
    u32 id = 0;
    u32 count = 0;
  };

  ItemSaveData() = default;
  explicit ItemSaveData(u32 address) : Object(address) {}

  u32 Gold() const;
  bool SetGold(u32 v); // clamps to 99999999

  // Ancient medals: the separate currency the ruins inns and healers take.
  u32 Medals() const;
  bool SetMedals(u32 v); // clamps to 9999, as bdScriptOpMedalChange does
  static constexpr u32 kMedalsMax = 9999;

  size_t SlotCount() const; // fixed at 512, empty slots included
  Item At(size_t slot) const;
  bool SetAt(size_t slot, u32 itemId, u32 count); // count clamps to 99

  size_t UsedCount() const; // slots with a non-zero id
};

} // namespace bd::engine
