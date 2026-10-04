/**
 * @file    engine/menus/field_encounter_menu.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/field_encounter_menu.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "core/memory_helpers.h"
#include "gpu/gpu.h"

namespace bd::engine {

namespace {

struct FieldEncounterItem_t {
  /* 0x00 */ be_u32 id; // zero past the last item
  /* 0x04 */ u8 _pad004[0x08];
};
static_assert(sizeof(FieldEncounterItem_t) == 0x0C);

struct FieldEncounterMenu_t {
  /* 0x000 */ u8 _pad000[0x74];
  /* 0x074 */ be_u32 rows; // first row, nearest enemy first
  /* 0x078 */ u8 _pad078[0x64];
  /* 0x0DC */ be_u32 cursor;    // enemyRows on the fight row, itemRows on use
  /* 0x0E0 */ be_u32 holdAnchor; // 999 while no hold-to-jump is armed
  /* 0x0E4 */ be_u32 spinFrame;
  /* 0x0E8 */ be_u32 enemyRows;
  /* 0x0EC */ be_u32 selected;
  /* 0x0F0 */ be_u32 startDelay;
  /* 0x0F4 */ be_u32 state;
  /* 0x0F8 */ be_u32 skillSlot;
  /* 0x0FC */ be_u32 itemRows;
  /* 0x100 */ be_u32 scroll;
  /* 0x104 */ be_u32 scrollDir;
  /* 0x108 */ be_f32 scrollAnim;
  /* 0x10C */ be_u32 itemScroll;
  /* 0x110 */ be_u32 itemScrollDir;
  /* 0x114 */ be_f32 itemScrollAnim;
  /* 0x118 */ FieldEncounterItem_t items[25];
  /* 0x244 */ u8 _pad244[0x20];
  /* 0x264 */ be_f32 slide; // 0 on the enemy list, 1 with a submenu up
  /* 0x268 */ be_f32 slideStep;
};
static_assert(offsetof(FieldEncounterMenu_t, rows) == 0x074);
static_assert(offsetof(FieldEncounterMenu_t, cursor) == 0x0DC);
static_assert(offsetof(FieldEncounterMenu_t, state) == 0x0F4);
static_assert(offsetof(FieldEncounterMenu_t, items) == 0x118);
static_assert(offsetof(FieldEncounterMenu_t, slide) == 0x264);

struct FieldEncounterRow_t {
  /* 0x000 */ u8 _pad000[0xF4];
  /* 0x0F4 */ be_u32 next;
  /* 0x0F8 */ u8 _pad0F8[0x14];
  /* 0x10C */ be_u32 state;
  /* 0x110 */ u8 _pad110[0x0C];
  /* 0x11C */ be_u32 selected;
};
static_assert(offsetof(FieldEncounterRow_t, next) == 0x0F4);
static_assert(offsetof(FieldEncounterRow_t, state) == 0x10C);
static_assert(offsetof(FieldEncounterRow_t, selected) == 0x11C);

// bdFieldEncounterMenuCursorPos's layout: a row's highlight bar is 34 tall
// starting 6 above its text line, text lines run 36 apart, and the whole
// panel rises 380 into the skill and item states.
constexpr f32 kRowStride = 36.0f;
constexpr f32 kRowTextY = 50.0f; // + (row - scroll + 2) * stride
constexpr f32 kFightRowY = 480.0f;
constexpr f32 kSkillBaseY = 560.0f; // + (slot + 1) * stride
constexpr f32 kItemTextY = 710.0f;  // + (row - scroll + 2) * stride
constexpr f32 kItemUseRowY = 1032.0f;
constexpr f32 kSlideRange = 380.0f;
constexpr f32 kBarX = 936.0f;
constexpr f32 kBarW = 266.0f;
constexpr f32 kItemBarW = 228.0f;
constexpr f32 kBarAbove = 6.0f;
constexpr f32 kBarH = 34.0f;
constexpr int kVisibleRows = 9;
constexpr f32 kSlideStep = 0.5f;
constexpr u32 kNoHoldAnchor = 999;

} // namespace

void FieldEncounterRow::SetState(RowState state) {
  auto *self = Self<FieldEncounterRow_t>();
  if (!self)
    return;
  self->state = static_cast<u32>(state);
}

void FieldEncounterRow::SetSelected(bool selected) {
  auto *self = Self<FieldEncounterRow_t>();
  if (!self)
    return;
  self->selected = selected ? 1u : 0u;
}

List<FieldEncounterRow> FieldEncounterMenu::Rows() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  return List<FieldEncounterRow>(self ? static_cast<u32>(self->rows) : 0,
                                 offsetof(FieldEncounterRow_t, next));
}

u32 FieldEncounterMenu::State() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  return self ? static_cast<u32>(self->state) : 0;
}

u32 FieldEncounterMenu::Cursor() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  return self ? static_cast<u32>(self->cursor) : 0;
}

u32 FieldEncounterMenu::EnemyRows() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  return self ? static_cast<u32>(self->enemyRows) : 0;
}

void FieldEncounterMenu::SetSelectedCount(u32 count) {
  auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return;
  self->selected = count;
}

bool FieldEncounterMenu::StateSettled() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return false;
  const f32 rest = u32(self->state) == 0 ? 0.0f : 1.0f;
  return f32(self->slide) == rest;
}

void FieldEncounterMenu::SetState(u32 state) {
  auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return;
  self->state = state;
  self->slideStep = kSlideStep;
  self->holdAnchor = kNoHoldAnchor;
}

int FieldEncounterMenu::CursorAt(f32 x, f32 y, u32 &state) const {
  const auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return -1;

  x -= bd::gpu::Output::DesignOverscanX();
  if (x < kBarX || x > kBarX + kBarW)
    return -1;
  const bool inItemBar = x <= kBarX + kItemBarW;
  y += f32(self->slide) * kSlideRange;

  const auto onRow = [y](f32 textY) {
    return y >= textY - kBarAbove && y < textY - kBarAbove + kBarH;
  };
  const auto scrolled = [y](f32 textY, int scroll, int rows) {
    const f32 local = y - (textY - kBarAbove);
    if (local < 0.0f)
      return -1;
    const int band = int(local / kRowStride);
    if (band < 2 || band > 1 + kVisibleRows)
      return -1;
    if (local - f32(band) * kRowStride >= kBarH)
      return -1;
    const int row = scroll + band - 2;
    return row >= 0 && row < rows ? row : -1;
  };

  state = 0;
  if (onRow(kFightRowY))
    return int(u32(self->enemyRows));
  if (const int row = scrolled(kRowTextY, int(u32(self->scroll)),
                               int(u32(self->enemyRows)));
      row >= 0)
    return row;

  state = 1;
  for (int slot = 0; slot < kSkillSlots; ++slot)
    if (onRow(kSkillBaseY + f32(slot + 1) * kRowStride))
      return slot;

  state = 2;
  if (!inItemBar)
    return -1;
  if (onRow(kItemUseRowY))
    return int(u32(self->itemRows));
  const int rows =
      std::min(int(u32(self->itemRows)), int(std::size(self->items)));
  const int row = scrolled(kItemTextY, int(u32(self->itemScroll)), rows);
  if (row < 0 || u32(self->items[row].id) == 0)
    return -1;
  return row;
}

int FieldEncounterMenu::LastCursor() const {
  const auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return -1;
  switch (u32(self->state)) {
  case 0:
    return int(u32(self->enemyRows));
  case 1:
    return kSkillSlots - 1;
  case 2:
    return int(u32(self->itemRows));
  default:
    return -1;
  }
}

void FieldEncounterMenu::SetCursor(u32 cursor) {
  auto *self = Self<FieldEncounterMenu_t>();
  if (!self)
    return;
  self->cursor = cursor;
}

} // namespace bd::engine
