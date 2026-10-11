/**
 * @file    engine/menus/camp_item_main_task.h
 * @brief   The camp Items screen task and the layouts it loads.
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

class CampItemMainTask : public Task {
public:
  CampItemMainTask() = default;
  explicit CampItemMainTask(u32 address) : Task(address) {}

  D2AnimeTask Screen(size_t i) const;
};

} // namespace bd::engine
