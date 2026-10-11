/**
 * @file    engine/d2anime/anime_menu.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/anime_menu.h"

#include <cstddef>

#include <rex/hook.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_data.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/d2anime_task.h"

REX_IMPORT(__imp__AnimeMenu_attachToParent, MenuAttachToParent, void(u32));
REX_IMPORT(__imp__AnimeMenu_SetVisibleAndPlay, MenuSetVisibleAndPlay,
           void(u32, u32));
REX_IMPORT(__imp__AnimeMenu_AddEntryData, MenuAddEntryData, u32(u32, u32, u32));
REX_IMPORT(__imp__AnimeMenu_AnimateCursor, MenuAnimateCursor, void(u32, u32));

namespace bd::engine {

namespace {

struct AnimeItemData_t {
  /* 0x00 */ be_u32 index;
  /* 0x04 */ be_u32 enabled; // non-zero = EnableColor, 0 = DisableColor
  /* 0x08 */ be_u32 unk08;
  /* 0x0C */ be_u32 customColor; // pointer to a custom color, 0 = defaults
};
static_assert(sizeof(AnimeItemData_t) == 0x10);

struct AnimeMenu_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x54];
  /* 0x058 */ be_u32 flags;
  /* 0x05C */ u8 _pad05C[0x10];
  /* 0x06C */ be_f32 posX;
  /* 0x070 */ be_f32 posY;
  /* 0x074 */ be_f32 extentW;
  /* 0x078 */ be_f32 extentH;
  /* 0x07C */ be_f32 priority;
  /* 0x080 */ be_f32 originX;
  /* 0x084 */ be_f32 originY;
  /* 0x088 */ be_f32 itemW;
  /* 0x08C */ be_f32 itemH;
  /* 0x090 */ u8 _pad090[0x04];
  /* 0x094 */ be_u32 visible;
  /* 0x098 */ be_u32 cursorShown;
  /* 0x09C */ u8 _pad09C[0x04];
  /* 0x0A0 */ be_u32 cursorTask;
  /* 0x0A4 */ be_u32 gridDimX; // rows
  /* 0x0A8 */ be_u32 gridDimY; // cols
  /* 0x0AC */ u8 _pad0AC[0x04];
  // Entry data. CheckConfirmInput is gated on it.
  /* 0x0B0 */ mem::GuestVec<u32> entryData;
  /* 0x0BC */ u8 _pad0BC[0x04]; // visibleItems vector base
  // The scroll window over the entry list, one element per drawn slot.
  /* 0x0C0 */ mem::GuestVec<u32> itemData;
  /* 0x0CC */ be_u32 cursorIndex;
  // Sixteen bits wide, not thirty-two. Reading a word here picks the halfword
  // at 0x0D2 up in the low bits, so the offset comes back as garbage whenever
  // that halfword is not zero.
  /* 0x0D0 */ be_u16 scrollOffset;
  /* 0x0D2 */ u8 _pad0D2[0x02];
  /* 0x0D4 */ be_u32 orientation; // zero is linear, non-zero a grid
  /* 0x0D8 */ be_u32 activeFlag;
  /* 0x0DC */ u8 _pad0DC[0x04];
  /* 0x0E0 */ u8 inputBits; // MenuInput, rebuilt each frame while active
  /* 0x0E1 */ u8 _pad0E1[0x03];
  /* 0x0E4 */ be_u32 pagedMaxIndex;
  /* 0x0E8 */ be_u32 scrollbarEnabled;
  /* 0x0EC */ be_u32 wrapEnabled;
  /* 0x0F0 */ u8 _pad0F0[0x0C];
  /* 0x0FC */ be_u32 selectAll;
  /* 0x100 */ be_u32 deselectAll;
  /* 0x104 */ be_u32 hasWndType;
  /* 0x108 */ u8 _pad108[0x50];
  /* 0x158 */ mem::GuestVec<u32> templates; // vector<D2AnimeTask*>
  /* 0x164 */ be_u32 enableColor;  // packed ARGB (A<<24|R<<16|G<<8|B)
  /* 0x168 */ be_u32 disableColor; // packed ARGB
  /* 0x16C */ be_u32 needsRebuild;
  /* 0x170 */ be_u32 shoulderPageJump;
};
static_assert(offsetof(AnimeMenu_t, flags) == 0x058);
static_assert(offsetof(AnimeMenu_t, posX) == 0x06C);
static_assert(offsetof(AnimeMenu_t, posY) == 0x070);
static_assert(offsetof(AnimeMenu_t, extentW) == 0x074);
static_assert(offsetof(AnimeMenu_t, extentH) == 0x078);
static_assert(offsetof(AnimeMenu_t, priority) == 0x07C);
static_assert(offsetof(AnimeMenu_t, originX) == 0x080);
static_assert(offsetof(AnimeMenu_t, originY) == 0x084);
static_assert(offsetof(AnimeMenu_t, itemW) == 0x088);
static_assert(offsetof(AnimeMenu_t, itemH) == 0x08C);
static_assert(offsetof(AnimeMenu_t, visible) == 0x094);
static_assert(offsetof(AnimeMenu_t, cursorShown) == 0x098);
static_assert(offsetof(AnimeMenu_t, cursorTask) == 0x0A0);
static_assert(offsetof(AnimeMenu_t, gridDimX) == 0x0A4);
static_assert(offsetof(AnimeMenu_t, gridDimY) == 0x0A8);
static_assert(offsetof(AnimeMenu_t, entryData) == 0x0B0);
static_assert(offsetof(AnimeMenu_t, itemData) == 0x0C0);
static_assert(offsetof(AnimeMenu_t, cursorIndex) == 0x0CC);
static_assert(offsetof(AnimeMenu_t, scrollOffset) == 0x0D0);
static_assert(offsetof(AnimeMenu_t, orientation) == 0x0D4);
static_assert(offsetof(AnimeMenu_t, activeFlag) == 0x0D8);
static_assert(offsetof(AnimeMenu_t, inputBits) == 0x0E0);
static_assert(offsetof(AnimeMenu_t, pagedMaxIndex) == 0x0E4);
static_assert(offsetof(AnimeMenu_t, scrollbarEnabled) == 0x0E8);
static_assert(offsetof(AnimeMenu_t, wrapEnabled) == 0x0EC);
static_assert(offsetof(AnimeMenu_t, selectAll) == 0x0FC);
static_assert(offsetof(AnimeMenu_t, deselectAll) == 0x100);
static_assert(offsetof(AnimeMenu_t, hasWndType) == 0x104);
static_assert(offsetof(AnimeMenu_t, templates) == 0x158);
static_assert(offsetof(AnimeMenu_t, enableColor) == 0x164);
static_assert(offsetof(AnimeMenu_t, disableColor) == 0x168);
static_assert(offsetof(AnimeMenu_t, needsRebuild) == 0x16C);
static_assert(offsetof(AnimeMenu_t, shoulderPageJump) == 0x170);

} // namespace

bool AnimeMenu::HasFocus() const {
  const u32 f = Flags();
  if ((f & u32(MenuFlag::Active)) == 0)
    return false;
  if ((f & (u32(MenuFlag::Locked) | u32(MenuFlag::Animating))) != 0)
    return false;
  return ActiveFlag() != 0;
}

f32 AnimeMenu::PosX() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->posX) : 0.0f;
}

f32 AnimeMenu::PosY() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->posY) : 0.0f;
}

f32 AnimeMenu::ExtentW() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->extentW) : 0.0f;
}

f32 AnimeMenu::ExtentH() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->extentH) : 0.0f;
}

void AnimeMenu::SetExtentH(f32 v) {
  if (auto *self = Self<AnimeMenu_t>())
    self->extentH = v;
}

f32 AnimeMenu::Priority() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->priority) : 0.0f;
}

f32 AnimeMenu::OriginX() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->originX) : 0.0f;
}

f32 AnimeMenu::OriginY() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->originY) : 0.0f;
}

f32 AnimeMenu::ItemW() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->itemW) : 0.0f;
}

f32 AnimeMenu::ItemH() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<f32>(self->itemH) : 0.0f;
}

bool AnimeMenu::IsVisible() const {
  const auto *self = Self<AnimeMenu_t>();
  return self && static_cast<u32>(self->visible) != 0;
}

bool AnimeMenu::CursorShown() const {
  const auto *self = Self<AnimeMenu_t>();
  return self && static_cast<u32>(self->cursorShown) != 0;
}

void AnimeMenu::SetCursorShown(bool shown) {
  if (auto *self = Self<AnimeMenu_t>())
    self->cursorShown = shown ? 1u : 0u;
}

Task AnimeMenu::CursorTask() const {
  const auto *self = Self<AnimeMenu_t>();
  return Task(self ? static_cast<u32>(self->cursorTask) : 0);
}

u32 AnimeMenu::GridRows() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->gridDimX) : 0;
}

void AnimeMenu::SetGridRows(u32 rows) {
  if (auto *self = Self<AnimeMenu_t>())
    self->gridDimX = rows;
}

u32 AnimeMenu::GridCols() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->gridDimY) : 0;
}

u32 AnimeMenu::Orientation() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->orientation) : 0;
}

u16 AnimeMenu::ScrollOffset() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u16>(self->scrollOffset) : 0;
}

void AnimeMenu::SetScrollOffset(u16 offset) {
  if (auto *self = Self<AnimeMenu_t>())
    self->scrollOffset = offset;
}

u32 AnimeMenu::ActiveFlag() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->activeFlag) : 0;
}

void AnimeMenu::SetActiveFlag(u32 flag) {
  if (auto *self = Self<AnimeMenu_t>())
    self->activeFlag = flag;
}

u8 AnimeMenu::InputBits() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? self->inputBits : 0;
}

void AnimeMenu::SetInputBits(u8 bits) {
  if (auto *self = Self<AnimeMenu_t>())
    self->inputBits = bits;
}

u32 AnimeMenu::PagedMaxIndex() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->pagedMaxIndex) : 0;
}

bool AnimeMenu::ScrollbarEnabled() const {
  const auto *self = Self<AnimeMenu_t>();
  return self && static_cast<u32>(self->scrollbarEnabled) != 0;
}

bool AnimeMenu::WrapEnabled() const {
  const auto *self = Self<AnimeMenu_t>();
  return self && static_cast<u32>(self->wrapEnabled) != 0;
}

u32 AnimeMenu::SelectAll() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->selectAll) : 0;
}

u32 AnimeMenu::DeselectAll() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->deselectAll) : 0;
}

void AnimeMenu::SetDeselectAll(u32 v) {
  if (auto *self = Self<AnimeMenu_t>())
    self->deselectAll = v;
}

bool AnimeMenu::HasWndType() const {
  const auto *self = Self<AnimeMenu_t>();
  return self && static_cast<u32>(self->hasWndType) != 0;
}

size_t AnimeMenu::TemplateCount() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? self->templates.size() : 0;
}

D2AnimeTask AnimeMenu::TemplateAt(size_t i) const {
  const auto *self = Self<AnimeMenu_t>();
  return D2AnimeTask(self ? self->templates[static_cast<u32>(i)] : 0);
}

u32 AnimeMenu::EnableColor() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->enableColor) : 0xFFFFFFFF;
}

u32 AnimeMenu::DisableColor() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->disableColor) : 0x7F7F7FFF;
}

u32 AnimeMenu::NeedsRebuild() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->needsRebuild) : 0;
}

void AnimeMenu::SetNeedsRebuild(u32 v) {
  if (auto *self = Self<AnimeMenu_t>())
    self->needsRebuild = v;
}

u32 AnimeMenu::ShoulderPageJump() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<u32>(self->shoulderPageJump) : 0;
}

void AnimeMenu::SetActive(bool active) {
  auto *self = Self<AnimeMenu_t>();
  if (!self)
    return;
  self->activeFlag = active ? 1u : 0u;
  self->deselectAll = active ? 0u : 1u;
  self->cursorShown = active ? 1u : 0u;
  self->needsRebuild = 1u;
}

void AnimeMenu::SetVisibleAndPlay(bool visible) {
  const u32 addr = Address();
  if (!addr)
    return;
  MenuSetVisibleAndPlay(addr, visible ? 1 : 0);
}

void AnimeMenu::AttachCursor() {
  const u32 addr = Address();
  if (!addr)
    return;
  MenuAttachToParent(addr);
}

int AnimeMenu::CursorIndex() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<int>(static_cast<u32>(self->cursorIndex)) : 0;
}

void AnimeMenu::SetCursorIndex(int index) {
  auto *self = Self<AnimeMenu_t>();
  if (!self)
    return;
  self->cursorIndex = static_cast<u32>(index);
  self->needsRebuild = 1u;
  MenuAnimateCursor(Address(), 0);
}

bool AnimeMenu::PointerRowX(int &index, f32 &x) const {
  return *this && MenuCellPointerX(*this, index, x);
}

bool AnimeMenu::RowPointerX(int index, f32 &x) const {
  return *this && MenuRowPointerX(*this, index, x);
}

void AnimeMenu::ForEachTemplate(std::function<void(int, AnimeData)> cb) const {
  const size_t count = TemplateCount();
  for (size_t i = 0; i < count; ++i) {
    D2AnimeTask tpl = TemplateAt(i);
    if (!tpl)
      continue;
    cb(static_cast<int>(i), tpl.AnimeData());
  }
}

int AnimeMenu::ItemDataIndex(int slot) const {
  const auto *self = Self<AnimeMenu_t>();
  if (!self)
    return slot;
  const auto *item =
      mem::at<const AnimeItemData_t>(self->itemData[static_cast<u32>(slot)]);
  return item ? static_cast<int>(static_cast<u32>(item->index)) : slot;
}

bool AnimeMenu::ItemDataEnabled(int slot) const {
  const auto *self = Self<AnimeMenu_t>();
  if (!self)
    return false;
  const auto *item =
      mem::at<const AnimeItemData_t>(self->itemData[static_cast<u32>(slot)]);
  return item && static_cast<u32>(item->enabled) != 0;
}

void AnimeMenu::ForEachSlot(const RowFn &fn) const {
  ForEachTemplate(
      [&](int slot, AnimeData vb) { fn(slot, ItemDataIndex(slot), vb); });
}

void AnimeMenu::ForEachRow(size_t count, const RowFn &fn) const {
  ForEachSlot([&](int slot, int index, AnimeData vb) {
    if (index >= 0 && index < static_cast<int>(count))
      fn(slot, index, vb);
  });
}

void AnimeMenu::SetItemEnabled(int index, bool enabled) {
  auto *self = Self<AnimeMenu_t>();
  if (!self)
    return;
  auto *item =
      mem::at<AnimeItemData_t>(self->itemData[static_cast<u32>(index)]);
  if (!item)
    return;
  item->enabled = enabled ? 1u : 0u;
}

void AnimeMenu::SetToggleRow(int slot, AnimeData varBag, bool enabled,
                             u32 color) {
  SetItemEnabled(slot, enabled);
  varBag.SetColor("Color", color);
  varBag.SetFloat("ChkOn", enabled ? 1.0 : -1.0);
  varBag.SetFloat("ChkOff", enabled ? -1.0 : 1.0);
}

void AnimeMenu::AddEntryData(int index, bool enabled) {
  const u32 addr = Address();
  if (!addr)
    return;
  MenuAddEntryData(addr, static_cast<u32>(index), enabled ? 1 : 0);
}

int AnimeMenu::EntryCount() const {
  const auto *self = Self<AnimeMenu_t>();
  return self ? static_cast<int>(self->entryData.size()) : 0;
}

int AnimeMenu::RowCount() const { return static_cast<int>(GridRows()); }

void AnimeMenu::GrowGridByOneRow() {
  if (!*this)
    return;
  const int rows = RowCount();
  const f32 itemH = ItemH();
  const f32 extentH = ExtentH();
  const f32 gap =
      rows > 1 ? (extentH - itemH * static_cast<f32>(rows)) /
                     static_cast<f32>(rows - 1)
               : 0.0f;

  const int newRows = rows + 1;
  SetGridRows(static_cast<u32>(newRows));
  SetExtentH(itemH * static_cast<f32>(newRows) +
             gap * static_cast<f32>(newRows - 1));
}

void AnimeMenu::SeedEntries(size_t count, bool enabled) {
  for (size_t i = 0; i < count; ++i)
    AddEntryData(static_cast<int>(i), enabled);
}

void D2AnimeCursor::Poll(const AnimeMenu &menu, size_t count,
                         const std::function<void(int index)> &fn) {
  const int cursor = menu.CursorIndex();
  if (cursor == last_)
    return;
  if (count != kUnbounded && (cursor < 0 || cursor >= static_cast<int>(count)))
    return;
  last_ = cursor;
  fn(cursor);
}

} // namespace bd::engine
