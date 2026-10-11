/**
 * @file    engine/ene_task.h
 * @brief   The battle actor task node, chained off an enemy group, wrapping an
 *          Enemy body.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/chara.h"
#include "engine/list.h"
#include "engine/task.h"

namespace bd::engine {

class EneTask : public Task {
public:
  EneTask() = default;
  explicit EneTask(u32 address) : Task(address) {}

  static EneTask FromChara(const engine::Chara &chara);

  static List<EneTask> GroupFrom(u32 head);

  Enemy Chara() const;
};

} // namespace bd::engine
