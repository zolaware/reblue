/**
 * @file    engine/menus/mechatt_map_main_task.h
 * @brief   MechattMap::MainTask, the world map screen: its state word and the
 *          two L_wrmap.csv layouts it draws the map through.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

namespace bd::engine {

class MechattMapMainTask : public Task {
public:
  MechattMapMainTask() = default;
  explicit MechattMapMainTask(u32 address) : Task(address) {}

  u32 State() const;

  // WorldMapScreen_LoadLayouts fills both with the same variables. The fade
  // one carries the open and the close, the other the settled map, so the area
  // view has to veil both.
  D2AnimeTask Fade() const;
  D2AnimeTask Layout() const;
};

} // namespace bd::engine
