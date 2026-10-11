/**
 * @file    engine/menus/camp_config_main_task.h
 * @brief   The stock camp Config screen task and the page layouts it loads.
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

class CampConfigMainTask : public Task {
public:
  // The CSVs bdCampConfigDataLoad loads, in the order it lists them.
  static constexpr size_t kPageCount = 11;

  CampConfigMainTask() = default;
  explicit CampConfigMainTask(u32 address) : Task(address) {}

  u32 State() const;
  void SetState(u32 state);

  D2AnimeTask Page(size_t i) const;
};

} // namespace bd::engine
