/**
 * @file    engine/menus/mechatt_map_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/mechatt_map_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

struct MechattMapMainTask_t {
  /* 0x000 */ u8 _pad000[0x06C];
  /* 0x06C */ be_u32 state;
  /* 0x070 */ u8 _pad070[0x078 - 0x070];
  /* 0x078 */ be_u32 fade;
  /* 0x07C */ be_u32 layout;
};
static_assert(offsetof(MechattMapMainTask_t, state) == 0x06C);
static_assert(offsetof(MechattMapMainTask_t, fade) == 0x078);
static_assert(offsetof(MechattMapMainTask_t, layout) == 0x07C);

} // namespace

u32 MechattMapMainTask::State() const {
  const auto *self = Self<MechattMapMainTask_t>();
  return self ? static_cast<u32>(self->state) : 0;
}

D2AnimeTask MechattMapMainTask::Fade() const {
  const auto *self = Self<MechattMapMainTask_t>();
  return D2AnimeTask(self ? static_cast<u32>(self->fade) : 0);
}

D2AnimeTask MechattMapMainTask::Layout() const {
  const auto *self = Self<MechattMapMainTask_t>();
  return D2AnimeTask(self ? static_cast<u32>(self->layout) : 0);
}

} // namespace bd::engine
