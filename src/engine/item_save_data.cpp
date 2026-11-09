/**
 * @file    engine/item_save_data.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/item_save_data.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include <rex/ppc/hook.h>

#include "engine/events.h"
#include "engine/game.h"

namespace bd::engine {

namespace {

constexpr size_t kSlotCount = 512;
constexpr u32 kCountMax = 99;
constexpr u32 kGoldMax = 99999999u;

struct ItemSlot_t {
  /* 0x00 */ be_u32 itemId;
  /* 0x04 */ be_u32 count;
};
static_assert(offsetof(ItemSlot_t, itemId) == 0x00);
static_assert(offsetof(ItemSlot_t, count) == 0x04);
static_assert(sizeof(ItemSlot_t) == 0x08);

struct ItemSaveData_t {
  /* 0x0000 */ ItemSlot_t slots[kSlotCount];
  /* 0x1000 */ be_u32 gold;
};
static_assert(offsetof(ItemSaveData_t, slots) == 0x0000);
static_assert(offsetof(ItemSaveData_t, gold) == 0x1000);

using SlotTable = std::array<ItemSaveData::Item, kSlotCount>;

SlotTable Slots(const ItemSaveData &data) {
  SlotTable slots{};
  for (size_t i = 0; i < kSlotCount; ++i)
    slots[i] = data.At(i);
  return slots;
}

// The engine credits the slot already holding the id, or the first empty one,
// and never moves a slot, so one gain is one slot going up. An id of zero means
// nothing was gained.
ItemGained GainedSince(const ItemSaveData &data, const SlotTable &before) {
  for (size_t i = 0; i < kSlotCount; ++i) {
    const ItemSaveData::Item now = data.At(i);
    if (now.id == 0)
      continue;
    const u32 was = before[i].id == now.id ? before[i].count : 0;
    if (now.count > was)
      return ItemGained{now.id, now.count - was};
  }
  return ItemGained{};
}

} // namespace

u32 ItemSaveData::Gold() const {
  const auto *self = Self<ItemSaveData_t>();
  return self ? static_cast<u32>(self->gold) : 0;
}

bool ItemSaveData::SetGold(u32 v) {
  auto *self = Self<ItemSaveData_t>();
  if (!self)
    return false;
  self->gold = std::min(v, kGoldMax);
  return true;
}

size_t ItemSaveData::SlotCount() const { return kSlotCount; }

ItemSaveData::Item ItemSaveData::At(size_t slot) const {
  const auto *self = Self<ItemSaveData_t>();
  if (!self || slot >= kSlotCount)
    return {};
  const ItemSlot_t &entry = self->slots[slot];
  return Item{static_cast<u32>(entry.itemId), static_cast<u32>(entry.count)};
}

bool ItemSaveData::SetAt(size_t slot, u32 itemId, u32 count) {
  auto *self = Self<ItemSaveData_t>();
  if (!self || slot >= kSlotCount)
    return false;
  ItemSlot_t &entry = self->slots[slot];
  entry.itemId = itemId;
  entry.count = std::min(count, kCountMax);
  return true;
}

size_t ItemSaveData::UsedCount() const {
  const auto *self = Self<ItemSaveData_t>();
  if (!self)
    return 0;
  size_t used = 0;
  for (const ItemSlot_t &entry : self->slots)
    if (static_cast<u32>(entry.itemId) != 0)
      ++used;
  return used;
}

} // namespace bd::engine

// The only two sites that publish. The shop till, the camp item screen, battle
// spoils and item drops reach the same item save block through routines nobody
// has identified, so a subscriber that needs every edge pairs these with
// SaveLoaded.
//
// Diffing the save data around the original call rather than trusting the
// operands: the opcodes set and subtract as well as add, they clamp, and they
// refuse an add that would overflow a slot.

REX_DETOUR(bdScriptOpGiveGold, u32, (u32 script, u32 operands)) {
  const u32 before = bd::engine::Game::Get().ItemSaveData().Gold();
  const u32 result = original(script, operands);
  const u32 after = bd::engine::Game::Get().ItemSaveData().Gold();
  if (after != before)
    bd::engine::Events::Publish(bd::engine::GoldChanged{before, after});
  return result;
}

REX_DETOUR(bdScriptOpGiveItem, u32, (u32 script, u32 operands)) {
  const auto before = bd::engine::Slots(bd::engine::Game::Get().ItemSaveData());
  const u32 result = original(script, operands);
  const bd::engine::ItemGained gained = bd::engine::GainedSince(
      bd::engine::Game::Get().ItemSaveData(), before);
  if (gained.itemId)
    bd::engine::Events::Publish(gained);
  return result;
}
