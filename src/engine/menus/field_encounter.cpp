/**
 * @file    engine/menus/field_encounter.cpp
 * @brief   Point at the enemies you mean in the field encounter menu.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <algorithm>

#include <rex/hook.h>
#include <rex/types.h>

#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/game.h"
#include "engine/menus/field_encounter_menu.h"
#include "engine/settings.h"
#include "engine/sfx.h"
#include "reblue_init.h"

REX_EXTERN(__imp__bdFieldEncounterMenuOpen);
REX_EXTERN(__imp__bdFieldEncounterMenuUpdate);

namespace bd::engine {

namespace {

// The same four inputs bdFieldEncounterMenuHandleList polls first out of its
// own frame.
bool PadMovedCursor() {
  return CheckButton(Button::Up) || CheckButton(Button::Down) ||
         CheckButton(Button::LSUp) || CheckButton(Button::LSDown);
}

// Runs ahead of the engine's own input read, so a click in the same frame
// confirms the row the pointer had just moved to.
void MoveCursor(FieldEncounterMenu menu, int to) {
  menu.SetCursor(u32(to));
  if (Settings::Get().MouseCursorSFX())
    sfx::Play(sfx::kCursor);
}

void DriveEncounterMouse(FieldEncounterMenu menu) {
  auto &mm = MenuMouse::Get();

  // Reaching here at all means the encounter menu is up: escape and
  // right-click have to read as cancel, the arrow keys have to reach the
  // pad, and mouse look has to stand down so there is a pointer at all.
  mm.MarkInputOwned();

  if (!menu)
    return;
  if (!Settings::Get().MouseInput())
    return;

  if (PadMovedCursor()) {
    mm.SetMouseHasCursor(false);
    return;
  }

  const int cursor = int(menu.Cursor());
  const int last = menu.LastCursor();
  if (last < 0)
    return;

  // Wheel up walks toward the top of the list. The engine's own update pulls
  // the scroll window after the cursor, so a step past the window's edge
  // needs nothing more here.
  if (const int detents = mm.TakeWheelDetents()) {
    const int next = std::clamp(cursor - detents, 0, last);
    if (next != cursor &&
        (menu.State() != 1 ||
         Game::Get().FieldPlayerEntity().HasFieldSkill(next))) {
      mm.ArmWheelGuard();
      MoveCursor(menu, next);
    }
    return;
  }

  if (!mm.MouseHasCursor())
    return;
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return;
  const int hit = menu.CursorAt(x, y);
  if (hit < 0 || hit == cursor)
    return;
  MoveCursor(menu, hit);
}

// Every enemy starts in the fight. The engine opens with none selected, which
// the fight row already treats as all of them, so this changes what the panel
// shows and what a deselect starts from.
void SelectAllRows(FieldEncounterMenu menu) {
  const u32 rows = menu.EnemyRows();
  u32 selected = 0;
  for (FieldEncounterRow row : menu.Rows()) {
    if (selected >= rows)
      break;
    row.SetSelected(true);
    row.SetState(selected == 0 ? RowState::kSelectedCursor
                               : RowState::kSelected);
    ++selected;
  }
  menu.SetSelectedCount(selected);
}

} // namespace

} // namespace bd::engine

REX_HOOK_RAW(bdFieldEncounterMenuUpdate) {
  bd::engine::DriveEncounterMouse(bd::engine::FieldEncounterMenu(ctx.r3.u32));
  __imp__bdFieldEncounterMenuUpdate(ctx, base);
}

// The menu's one-shot open. It builds the row list, so there is nothing to
// select until it has returned.
REX_HOOK_RAW(bdFieldEncounterMenuOpen) {
  const bd::engine::FieldEncounterMenu menu(ctx.r3.u32);
  __imp__bdFieldEncounterMenuOpen(ctx, base);
  bd::engine::SelectAllRows(menu);
}
