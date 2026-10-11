/**
 * @file    engine/menus/camp_rank_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/camp_rank_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

// AnimeVarBag_GetElementXYWHP's destination struct, five floats behind a
// vftable.
struct AnimePosVar_t {
  /* 0x00 */ be_u32 vtable;
  /* 0x04 */ be_f32 x;
  /* 0x08 */ be_f32 y;
  /* 0x0C */ be_f32 w;
  /* 0x10 */ be_f32 h;
  /* 0x14 */ be_f32 pri;
};
static_assert(sizeof(AnimePosVar_t) == 0x18);

struct CampRankMainTask_t {
  /* 0x0000 */ u8 _pad0000[0x6C];
  /* 0x006C */ be_u32 phase;
  /* 0x0070 */ u8 _pad0070[0xE0];
  /* 0x0150 */ AnimePosVar_t cellPos[kFormationCells];
  /* 0x0240 */ u8 _pad0240[0x28];
  /* 0x0268 */ be_u32 currentMember; // PlyTask under the browse cursor
  /* 0x026C */ be_u32 heldCol;
  /* 0x0270 */ be_u32 heldRow;
  /* 0x0274 */ be_u32 activeCount;
  /* 0x0278 */ u8 _pad0278[0x08];
  /* 0x0280 */ be_u32 cursorCol;
  /* 0x0284 */ be_u32 resetFlag;
};
static_assert(offsetof(CampRankMainTask_t, phase) == 0x6C);
static_assert(offsetof(CampRankMainTask_t, cellPos) == 0x150);
static_assert(offsetof(CampRankMainTask_t, currentMember) == 0x268);
static_assert(offsetof(CampRankMainTask_t, heldCol) == 0x26C);
static_assert(offsetof(CampRankMainTask_t, heldRow) == 0x270);
static_assert(offsetof(CampRankMainTask_t, activeCount) == 0x274);
static_assert(offsetof(CampRankMainTask_t, cursorCol) == 0x280);
static_assert(offsetof(CampRankMainTask_t, resetFlag) == 0x284);

} // namespace

u32 CampRankMainTask::Phase() const {
  const auto *self = Self<CampRankMainTask_t>();
  return self ? static_cast<u32>(self->phase) : 0;
}

CampRankMainTask::CellPos CampRankMainTask::CellAt(size_t i) const {
  const auto *self = Self<CampRankMainTask_t>();
  if (!self || i >= static_cast<size_t>(kFormationCells))
    return CellPos{};
  const AnimePosVar_t &cell = self->cellPos[i];
  return CellPos{static_cast<f32>(cell.x), static_cast<f32>(cell.y),
                 static_cast<f32>(cell.w), static_cast<f32>(cell.h),
                 static_cast<f32>(cell.pri)};
}

void CampRankMainTask::SetCurrentMember(const PlyTask &member) {
  auto *self = Self<CampRankMainTask_t>();
  if (self)
    self->currentMember = member.Address();
}

u32 CampRankMainTask::HeldCol() const {
  const auto *self = Self<CampRankMainTask_t>();
  return self ? static_cast<u32>(self->heldCol) : 0;
}

void CampRankMainTask::SetHeldCol(u32 col) {
  auto *self = Self<CampRankMainTask_t>();
  if (self)
    self->heldCol = col;
}

u32 CampRankMainTask::HeldRow() const {
  const auto *self = Self<CampRankMainTask_t>();
  return self ? static_cast<u32>(self->heldRow) : 0;
}

void CampRankMainTask::SetHeldRow(u32 row) {
  auto *self = Self<CampRankMainTask_t>();
  if (self)
    self->heldRow = row;
}

u32 CampRankMainTask::ActiveCount() const {
  const auto *self = Self<CampRankMainTask_t>();
  return self ? static_cast<u32>(self->activeCount) : 0;
}

u32 CampRankMainTask::CursorCol() const {
  const auto *self = Self<CampRankMainTask_t>();
  return self ? static_cast<u32>(self->cursorCol) : 0;
}

void CampRankMainTask::SetCursorCol(u32 col) {
  auto *self = Self<CampRankMainTask_t>();
  if (self)
    self->cursorCol = col;
}

} // namespace bd::engine
