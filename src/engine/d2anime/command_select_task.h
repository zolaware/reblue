/**
 * @file    engine/d2anime/command_select_task.h
 * @brief       The engine's CommandSelectTask, its second list widget: the
 *              battle command menus and the yes/no popup.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include "engine/d2anime/anime_menu.h"
#include "engine/task.h"

namespace bd::engine {

// A left click arrives as confirm, so only the four direction bits mean the
// pad rather than the pointer moved the cursor.
constexpr u8 kCmdSelectDirectionBits =
    u8(MenuInput::Up) | u8(MenuInput::Down) | u8(MenuInput::Left) |
    u8(MenuInput::Right);

class CommandSelectTask : public Task {
public:
  CommandSelectTask() = default;
  explicit CommandSelectTask(u32 address) : Task(address) {}

  // Whether the engine would hand this list a D-pad press, which is the whole
  // of CommandSelectTask_CheckConfirm's precondition. The shared cursor task's
  // owner word adds nothing: each list's own input-enabled word already
  // separates siblings.
  bool HasFocus() const;

  int EntryCount() const;

  int CursorIndex() const;

  u8 InputBits() const;

  // A point in design canvas space to the entry under it, indexed into the
  // full entry list rather than the visible page.
  bool CellAt(f32 x, f32 y, int &index) const;

  // -1 when the pointer sits above the list's rows, +1 when below, 0 when
  // level with them or outside the window's own width.
  int RowEdgeDirection() const;

  // One row in entry indices: the page's width when it numbers across, one
  // when it numbers down.
  int RowStep() const;
};

} // namespace bd::engine
