/**
 * @file    engine/menus/camp_diary_main_task.h
 * @brief   The camp Encyclopedia screen task, owner of the record screens and
 *          of the top strip that selects between them.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/d2anime/anime_menu.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

namespace bd::engine {

class CampDiaryMainTask : public Task {
public:
  CampDiaryMainTask() = default;
  explicit CampDiaryMainTask(u32 address) : Task(address) {}

  u32 State() const;

  // The screen a running transition is playing. Reset past the last slot once
  // that transition ends, so a stale index addresses nothing.
  u32 TransScreen() const;

  D2AnimeTask Screen(size_t i) const;

  AnimeMenu TopMenu() const;
};

} // namespace bd::engine
