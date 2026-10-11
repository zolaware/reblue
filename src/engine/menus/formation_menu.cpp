/**
 * @file    engine/menus/formation_menu.cpp
 * @brief   Pointer control for the camp formation grid, which is neither of the
 *          engine's two list widgets and so reaches none of the shared code.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <cmath>
#include <cstddef>

#include <rex/hook.h>
#include <rex/types.h>

#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/game.h"
#include "engine/menus/camp_rank_main_task.h"
#include "engine/sfx.h"
#include "engine/settings.h"
#include "reblue_init.h"

REX_EXTERN(__imp__Camp__Rank__MainTask__Update);
REX_IMPORT(__imp__CampRank_UpdateCursorPosName, RankUpdateCursorPosName, u32(u32));
REX_IMPORT(__imp__CampRank_ApplySlotUVs, RankApplySlotUVs, u32(u32));
REX_IMPORT(__imp__bdInputCheckDpadRight, RankPadRight, u32());
REX_IMPORT(__imp__bdInputCheckDpadLeft, RankPadLeft, u32());
REX_IMPORT(__imp__bdInputCheckDpadUp, RankPadUp, u32(u32));
REX_IMPORT(__imp__bdInputCheckDpadDown, RankPadDown, u32(u32));

namespace bd::engine {

namespace {

constexpr int kFormationCols = 5;
constexpr int kFormationRows = 2;
static_assert(kFormationCols * kFormationRows == kFormationCells);

// Phase 1 walks a column and reads whoever is standing in it. Phase 2 carries
// that member and picks a destination cell, which is the only phase where the
// row is a choice rather than a consequence: phase 1 reads up and down into
// locals it never uses.
constexpr u32 kPhaseBrowse = 1;
constexpr u32 kPhaseCarry = 2;

// The bound cell anchors cannot separate the rows. They are cursor arrow points
// and sit at y 148 and 136 in every layout, twelve apart. The floor plates can,
// and their y is the same in all five: front row plates at 344, back row at
// 272, both 256 by 128. This is the midline between those two plate centers, so
// it splits the rows without a per-layout table of rects.
constexpr f32 kRowDividerY = 372.0f;

// The engine right-aligns the populated columns into 0 through 4, so with N
// actives only columns 5-N upward carry anyone. Both derivations are the ones
// CampRank_ApplySlotUVs makes from the same partyOrder.
int ColumnOf(u32 order, int count) {
  return kFormationCols - 1 - int(order % u32(count));
}

size_t CellIndex(int row, int col) {
  return static_cast<size_t>(row * kFormationCols + col);
}

// Whoever holds this column, in either row.
PlyTask MemberAtColumn(int col, int count) {
  for (const PlyTask &member : Game::Get().FieldPlayerEntity().Party())
    if (ColumnOf(member.Chara().PartyOrder(), count) == col)
      return member;
  return PlyTask();
}

// The same four reads the handler itself makes, so the pointer surrenders on
// exactly the input that would have moved the cursor anyway. All four are pure
// reads of the input manager, which is polled once a frame, so asking again
// here cannot consume a press out from under the original.
bool PadIsSteering() {
  return RankPadRight() != 0 || RankPadLeft() != 0 || RankPadUp(0) != 0 ||
         RankPadDown(0) != 0;
}

// Nearest populated column by x, within the row the pointer is over. An
// unpopulated cell keeps the zero its constructor left, which is never a real
// anchor, so the emptiness test doubles as a guard on a layout that has not
// finished loading.
bool ColumnUnderPointer(const CampRankMainTask &task, f32 x, int row, int count,
                        int &out) {
  int best = -1;
  f32 bestDistance = 0.0f;
  for (int col = kFormationCols - count; col < kFormationCols; ++col) {
    const f32 anchorX = task.CellAt(CellIndex(row, col)).x;
    if (anchorX == 0.0f)
      continue;
    const f32 distance = std::fabs(x - anchorX);
    if (best < 0 || distance < bestDistance) {
      best = col;
      bestDistance = distance;
    }
  }
  if (best < 0)
    return false;
  out = best;
  return true;
}

void MoveCursor(CampRankMainTask task, int row, int col) {
  if (task.Phase() == kPhaseCarry) {
    if (int(task.HeldCol()) == col && int(task.HeldRow()) == row)
      return;
    task.SetHeldCol(u32(col));
    task.SetHeldRow(u32(row));
    RankApplySlotUVs(task.Address());
  } else {
    if (int(task.CursorCol()) == col)
      return;
    const PlyTask member = MemberAtColumn(col, int(task.ActiveCount()));
    if (!member)
      return;
    task.SetCursorCol(u32(col));
    task.SetCurrentMember(member);
    RankUpdateCursorPosName(task.Address());
  }
  if (Settings::Get().MouseCursorSFX())
    sfx::Play(sfx::kCursor);
}

// Runs before the handler, so a click hits the cell the pointer is over
// rather than a frame behind it.
void HoverFormationCells(u32 taskVA) {
  const CampRankMainTask task(taskVA);
  if (!task)
    return;
  const u32 phase = task.Phase();
  if (phase != kPhaseBrowse && phase != kPhaseCarry)
    return;

  // Above the pointer gates, the way the title rows do it. This says the
  // formation grid is taking menu input, which nothing else on this screen
  // publishes, and it is what lets escape cancel here and hands the mouse back
  // from camera look for as long as the screen is up.
  MenuMouse::Get().MarkInputOwned();

  if (PadIsSteering()) {
    MenuMouse::Get().SetMouseHasCursor(false);
    return;
  }
  if (!MenuMouse::Get().PointerActive())
    return;

  const int count = int(task.ActiveCount());
  if (count <= 0 || count > kFormationCols)
    return;

  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return;

  const int row = y < kRowDividerY ? 1 : 0;
  int col = 0;
  if (!ColumnUnderPointer(task, x, row, count, col))
    return;

  MoveCursor(task, row, col);
}

} // namespace

} // namespace bd::engine

REX_HOOK_RAW(Camp__Rank__MainTask__Update) {
  const u32 taskVA = ctx.r3.u32;
  bd::engine::HoverFormationCells(taskVA);
  __imp__Camp__Rank__MainTask__Update(ctx, base);
}
