/**
 * @file    engine/inventory.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/inventory.h"

#include <algorithm>
#include <array>

#include <rex/hook.h>

#include "core/memory_helpers.h"
#include "engine/events.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

constexpr size_t kInv_Count = 512; // fixed slots
constexpr u32 kInv_Stride = 8;     // { be_u32 itemId, be_u32 qty }
constexpr u32 kInv_QtyOffset = 4;
constexpr u32 kInv_QtyMax = 99;
constexpr u32 kInv_GoldMax = 99999999u;

u32 Base() { return bd::mem::try_load<u32>(addr::kItemSaveData); }

// Takes the base rather than reading it, so a whole-table pass validates the
// save block pointer once instead of 512 times.
Item SlotAt(u32 base, size_t slot) {
  Item it;
  if (!base || slot >= kInv_Count)
    return it;
  const u32 entry = base + static_cast<u32>(slot) * kInv_Stride;
  it.id = bd::mem::try_load<u32>(entry);
  it.count = bd::mem::try_load<u32>(entry + kInv_QtyOffset);
  return it;
}

using SlotTable = std::array<Item, kInv_Count>;

SlotTable Slots() {
  SlotTable slots{};
  const u32 base = Base();
  for (size_t i = 0; i < kInv_Count; ++i)
    slots[i] = SlotAt(base, i);
  return slots;
}

// The guest credits the slot already holding the id, or the first empty one,
// and never moves a slot, so one gain is one slot going up. An id of zero means
// nothing was gained.
ItemGained GainedSince(const SlotTable &before) {
  const u32 base = Base();
  for (size_t i = 0; i < kInv_Count; ++i) {
    const Item now = SlotAt(base, i);
    if (now.id == 0)
      continue;
    const u32 was = before[i].id == now.id ? before[i].count : 0;
    if (now.count > was)
      return ItemGained{now.id, now.count - was};
  }
  return ItemGained{};
}

} // namespace

Inventory::operator bool() const { return Base() != 0; }

u32 Inventory::Gold() const {
  const u32 base = Base();
  return base ? bd::mem::try_load<u32>(base + kInv_Gold) : 0;
}

bool Inventory::SetGold(u32 v) {
  const u32 base = Base();
  if (!base)
    return false;
  return bd::mem::try_store<u32>(base + kInv_Gold, std::min(v, kInv_GoldMax));
}

u32 Inventory::Medals() const {
  const u32 base = Base();
  return base ? bd::mem::try_load<u32>(base + kInv_Medals) : 0;
}

bool Inventory::SetMedals(u32 v) {
  const u32 base = Base();
  if (!base)
    return false;
  return bd::mem::try_store<u32>(base + kInv_Medals, std::min(v, kMedalsMax));
}

size_t Inventory::SlotCount() const { return kInv_Count; }

Item Inventory::At(size_t slot) const { return SlotAt(Base(), slot); }

bool Inventory::SetAt(size_t slot, u32 item_id, u32 count) {
  const u32 base = Base();
  if (!base || slot >= kInv_Count)
    return false;
  const u32 entry = base + static_cast<u32>(slot) * kInv_Stride;
  const bool a = bd::mem::try_store<u32>(entry, item_id);
  const bool b = bd::mem::try_store<u32>(entry + kInv_QtyOffset,
                                         std::min(count, kInv_QtyMax));
  return a && b;
}

size_t Inventory::UsedCount() const {
  size_t used = 0;
  const u32 base = Base();
  for (size_t i = 0; i < kInv_Count; ++i)
    if (SlotAt(base, i).id != 0)
      ++used;
  return used;
}

} // namespace bd::engine

// The only two sites that publish. The shop till, the camp item screen, battle
// spoils and item drops reach the same ItemSaveData through routines nobody has
// identified, so a subscriber that needs every edge pairs these with SaveLoaded.
//
// Diffing the save data around the original call rather than trusting the
// operands: the opcodes set and subtract as well as add, they clamp, and they
// refuse an add that would overflow a slot.

REX_EXTERN(__imp__bdScriptOpGiveGold);
REX_HOOK_RAW(bdScriptOpGiveGold) {
  const u32 before = bd::engine::Inventory{}.Gold();
  __imp__bdScriptOpGiveGold(ctx, base);
  const u32 after = bd::engine::Inventory{}.Gold();
  if (after != before)
    bd::engine::Events::Publish(bd::engine::GoldChanged{before, after});
}

REX_EXTERN(__imp__bdScriptOpGiveItem);
REX_HOOK_RAW(bdScriptOpGiveItem) {
  const auto before = bd::engine::Slots();
  __imp__bdScriptOpGiveItem(ctx, base);
  const bd::engine::ItemGained gained = bd::engine::GainedSince(before);
  if (gained.itemId)
    bd::engine::Events::Publish(gained);
}
