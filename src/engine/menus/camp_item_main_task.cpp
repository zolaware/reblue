/**
 * @file    engine/menus/camp_item_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/camp_item_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

constexpr size_t kScreenSlotCount = 10;

// bdCampItemLoad fills this with ten screens, of which L_itm_cate.csv, the
// Items screen itself, is index 1. The Encyclopedia pane beside the category
// column is not a screen of its own, just five window / tex / message triplets
// inside that same CSV, each gated on its DiaryAlpha float, which the Items
// screen raises only while the cursor sits on the Encyclopedia category.
// M_itm_dia.csv (index 9) never becomes visible at all.
struct CampItemMainTask_t {
  /* 0x00 */ u8 _pad000[0x84];
  /* 0x84 */ be_u32 screens[kScreenSlotCount];
};
static_assert(offsetof(CampItemMainTask_t, screens) == 0x84);

} // namespace

D2AnimeTask CampItemMainTask::Screen(size_t i) const {
  const auto *self = Self<CampItemMainTask_t>();
  if (!self || i >= kScreenSlotCount)
    return D2AnimeTask();
  return D2AnimeTask(static_cast<u32>(self->screens[i]));
}

} // namespace bd::engine
