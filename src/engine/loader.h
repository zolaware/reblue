/**
 * @file    engine/loader.h
 * @brief   The asset-slot manager: whether a load is in flight and whether the
 *          loading screen is up.
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

class Loader : public Task {
public:
  Loader() = default;
  explicit Loader(u32 address) : Task(address) {}

  bool NowLoading() const;
  D2AnimeTask NowLoadingAnime() const;
  bool AnySlotLoading() const;
  bool SetIconFade(f32 v);
};

} // namespace bd::engine
