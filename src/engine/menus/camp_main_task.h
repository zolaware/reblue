/**
 * @file    engine/menus/camp_main_task.h
 * @brief   The camp screen's root task and the footer task hanging off it,
 *          which between them own every band the camp screens draw into.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

namespace bd::engine {

class CampFtrTask : public Task {
public:
  CampFtrTask() = default;
  explicit CampFtrTask(u32 address) : Task(address) {}

  // d2anime\L_ftr.csv, the prompt band along the bottom.
  D2AnimeTask Layout() const;
};

class CampMainTask : public Task {
public:
  // The layouts bdCampMainTaskConstruct lists, in its order.
  static constexpr size_t kLayoutCount = 16;

  CampMainTask() = default;
  explicit CampMainTask(u32 address) : Task(address) {}

  static CampMainTask Get();

  D2AnimeTask Layout(size_t i) const;

  CampFtrTask Ftr() const;
};

} // namespace bd::engine
