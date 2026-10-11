/**
 * @file    engine/menus/camp_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/camp_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kCampMainTask = 0x82DC9A34;
} // namespace addr

namespace {

struct CampMainTask_t {
  /* 0x0000 */ u8 _pad0000[0x78];
  /* 0x0078 */ be_u32 layouts[CampMainTask::kLayoutCount];
  /* 0x00B8 */ u8 _pad00B8[0x16AC - 0x00B8];
  /* 0x16AC */ be_u32 ftrTask;
};
static_assert(offsetof(CampMainTask_t, layouts) == 120);
static_assert(offsetof(CampMainTask_t, ftrTask) == 5804);

struct CampFtrTask_t {
  /* 0x0000 */ u8 _pad0000[0x78];
  /* 0x0078 */ be_u32 layout;
};
static_assert(offsetof(CampFtrTask_t, layout) == 120);

} // namespace

D2AnimeTask CampFtrTask::Layout() const {
  const auto *self = Self<CampFtrTask_t>();
  return D2AnimeTask(self ? static_cast<u32>(self->layout) : 0);
}

CampMainTask CampMainTask::Get() {
  return CampMainTask(mem::try_load<u32>(addr::kCampMainTask));
}

D2AnimeTask CampMainTask::Layout(size_t i) const {
  const auto *self = Self<CampMainTask_t>();
  if (!self || i >= kLayoutCount)
    return D2AnimeTask();
  return D2AnimeTask(static_cast<u32>(self->layouts[i]));
}

CampFtrTask CampMainTask::Ftr() const {
  const auto *self = Self<CampMainTask_t>();
  return CampFtrTask(self ? static_cast<u32>(self->ftrTask) : 0);
}

} // namespace bd::engine
