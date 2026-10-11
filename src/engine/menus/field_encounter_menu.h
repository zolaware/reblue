/**
 * @file    engine/menus/field_encounter_menu.h
 * @brief   The field encounter menu, its enemy list and the rows on its chain.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/list.h"
#include "engine/object.h"

namespace bd::engine {

// The fight begins with every row reading kSelected or kSelectedCursor.
enum class RowState : u32 {
  kCleared = 0,
  kCursor = 1,
  kSelected = 3,
  kSelectedCursor = 4,
  kIdle = 5,
};

// One enemy in the list bdFieldEncounterMenuBuildRows sorts by distance.
class FieldEncounterRow : public Object {
public:
  FieldEncounterRow() = default;
  explicit FieldEncounterRow(u32 address) : Object(address) {}

  void SetState(RowState state);
  void SetSelected(bool selected);
};

// The menu the party front entity carries. The engine has no list widget here:
// bdFieldEncounterMenuHandleList reads the pad itself and
// bdFieldEncounterMenuDraw lays the rows out from these fields alone.
class FieldEncounterMenu : public Object {
public:
  FieldEncounterMenu() = default;
  explicit FieldEncounterMenu(u32 address) : Object(address) {}

  static constexpr int kSkillSlots = 2;

  List<FieldEncounterRow> Rows() const;

  // 0 enemy list, 1 field skills, 2 item list.
  u32 State() const;
  u32 Cursor() const;
  u32 EnemyRows() const;
  void SetSelectedCount(u32 count);

  bool StateSettled() const;

  void SetState(u32 state);

  // The cursor value whose highlight bar covers the point, or -1. The inverse
  // of bdFieldEncounterMenuCursorPos, so the pointer and the drawn bar can
  // never disagree about which row was meant.
  int CursorAt(f32 x, f32 y, u32 &state) const;

  // The last cursor value the current state accepts: the fight or use row on
  // the lists, the second skill slot between them.
  int LastCursor() const;

  void SetCursor(u32 cursor);
};

} // namespace bd::engine
