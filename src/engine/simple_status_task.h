/**
 * @file    engine/simple_status_task.h
 * @brief   The party card strip, five rows of HP and MP the field and the
 *          battle both put on screen.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "engine/d2anime/anime_data.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

namespace bd::engine {

class SimpleStatusTask : public Task {
public:
  SimpleStatusTask() = default;
  explicit SimpleStatusTask(u32 address) : Task(address) {}

  u32 Mode() const;

  D2AnimeTask Anime() const;

  AnimeData RowVarBag(size_t row) const;
};

} // namespace bd::engine
