/**
 * @file    engine/game_task.h
 * @brief   The session task, holder of the character slot unlock word.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/task.h"

namespace bd::engine {

class GameTask : public Task {
public:
  GameTask() = default;
  explicit GameTask(u32 address) : Task(address) {}

  u32 UnlockedSlots() const;
  u32 ShutdownFlag() const;
  bool IsSlotUnlocked(u32 slotId) const;
};

} // namespace bd::engine
