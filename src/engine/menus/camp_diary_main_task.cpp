/**
 * @file    engine/menus/camp_diary_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/camp_diary_main_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

// Screens live in bdCampDiaryLoad's order: 0 L_dia, 1 L_dia_adv, 2 L_dia_btl,
// 3 L_dia_mon, 4 L_dia_mon_det, 5 L_dia_itm, 6 L_dia_itm_det, 7 L_dia_mgc,
// 8 L_dia_mgc_det, 9..13 the five S_dia_top_* transitions, 14 M_itm_dia,
// 15 dia_str_xx. Enter(state) shows exactly one.
constexpr size_t kScreenSlotCount = 16;

struct CampDiaryMainTask_t {
  /* 0x00 */ u8 _pad000[0x6C];
  /* 0x6C */ be_u32 state;
  /* 0x70 */ u8 _pad070[0x08]; // 0x70 is a transition's destination state
  /* 0x78 */ be_u32 transScreen;
  /* 0x7C */ be_u32 screens[kScreenSlotCount];
  /* 0xBC */ u8 _pad0BC[0x04];
  // Menus bind by name into task+0xBC+4*n in state order, so the top strip,
  // state 1, takes the second slot.
  /* 0xC0 */ be_u32 topMenu;
};
static_assert(offsetof(CampDiaryMainTask_t, state) == 0x6C);
static_assert(offsetof(CampDiaryMainTask_t, transScreen) == 0x78);
static_assert(offsetof(CampDiaryMainTask_t, screens) == 0x7C);
static_assert(offsetof(CampDiaryMainTask_t, topMenu) == 0xC0);

} // namespace

u32 CampDiaryMainTask::State() const {
  const auto *self = Self<CampDiaryMainTask_t>();
  return self ? static_cast<u32>(self->state) : 0;
}

u32 CampDiaryMainTask::TransScreen() const {
  const auto *self = Self<CampDiaryMainTask_t>();
  return self ? static_cast<u32>(self->transScreen) : 0;
}

D2AnimeTask CampDiaryMainTask::Screen(size_t i) const {
  const auto *self = Self<CampDiaryMainTask_t>();
  if (!self || i >= kScreenSlotCount)
    return D2AnimeTask();
  return D2AnimeTask(static_cast<u32>(self->screens[i]));
}

AnimeMenu CampDiaryMainTask::TopMenu() const {
  const auto *self = Self<CampDiaryMainTask_t>();
  return AnimeMenu(self ? static_cast<u32>(self->topMenu) : 0);
}

} // namespace bd::engine
