/**
 * @file    engine/menus/camp_rank_main_task.h
 * @brief   The camp formation screen task, its ten cell anchors and the cursor
 *          the two phases move across them.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/ply_task.h"
#include "engine/task.h"

namespace bd::engine {

inline constexpr int kFormationCells = 10;

class CampRankMainTask : public Task {
public:
  struct CellPos {
    f32 x, y, w, h, pri;
  };

  CampRankMainTask() = default;
  explicit CampRankMainTask(u32 address) : Task(address) {}

  u32 Phase() const;

  CellPos CellAt(size_t i) const;

  void SetCurrentMember(const PlyTask &member);

  u32 HeldCol() const;
  void SetHeldCol(u32 col);
  u32 HeldRow() const;
  void SetHeldRow(u32 row);

  u32 ActiveCount() const;

  u32 CursorCol() const;
  void SetCursorCol(u32 col);
};

} // namespace bd::engine
