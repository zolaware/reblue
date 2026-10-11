/**
 * @file    engine/d2anime/sel_mes_win_task.h
 * @brief       The engine's SelMesWinTask, the yes/no confirmation popup.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <rex/types.h>

#include "engine/d2anime/command_select_task.h"
#include "engine/task.h"

namespace bd::engine {

// The popup runs its own input, so its owner reads the answer back out of it
// rather than driving it.
class SelMesWinTask : public Task {
public:
  SelMesWinTask() = default;
  explicit SelMesWinTask(u32 address) : Task(address) {}

  bool Confirmed() const;
  bool Canceled() const;

  // The list the popup gives its answers to, and where the chosen one is.
  CommandSelectTask CommandSelect() const;
};

} // namespace bd::engine
