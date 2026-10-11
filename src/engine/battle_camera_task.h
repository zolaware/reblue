/**
 * @file    engine/battle_camera_task.h
 * @brief   The battle view task. Its liveness is the title's only answer to
 *          whether a battle is running.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/task.h"

namespace bd::engine {

class BattleCameraTask : public Task {
public:
  BattleCameraTask() = default;
  explicit BattleCameraTask(u32 address) : Task(address) {}
};

} // namespace bd::engine
