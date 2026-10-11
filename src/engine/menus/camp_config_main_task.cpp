/**
 * @file    engine/menus/camp_config_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/camp_config_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

struct CampConfigMainTask_t {
  /* 0x000 */ u8 _pad000[0x6C];
  /* 0x06C */ be_u32 state;
  /* 0x070 */ u8 _pad070[0x08];
  /* 0x078 */ be_u32 pages[CampConfigMainTask::kPageCount];
};
static_assert(offsetof(CampConfigMainTask_t, state) == 0x06C);
static_assert(offsetof(CampConfigMainTask_t, pages) == 0x078);

} // namespace

u32 CampConfigMainTask::State() const {
  const auto *self = Self<CampConfigMainTask_t>();
  return self ? static_cast<u32>(self->state) : 0;
}

void CampConfigMainTask::SetState(u32 state) {
  auto *self = Self<CampConfigMainTask_t>();
  if (self)
    self->state = state;
}

D2AnimeTask CampConfigMainTask::Page(size_t i) const {
  const auto *self = Self<CampConfigMainTask_t>();
  if (!self || i >= kPageCount)
    return D2AnimeTask();
  return D2AnimeTask(static_cast<u32>(self->pages[i]));
}

} // namespace bd::engine
