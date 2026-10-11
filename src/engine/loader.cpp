/**
 * @file    engine/loader.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/loader.h"

#include <cstddef>

#include <rex/ppc.h>

#include "engine/sofdec_player.h"

namespace bd::engine {

namespace {

constexpr size_t kSlotScan = 8;

struct LoaderSlot_t {
  /* 0x00 */ be_u32 state;
  /* 0x04 */ u8 _pad04[0x78];
};
static_assert(offsetof(LoaderSlot_t, state) == 0x00);
static_assert(sizeof(LoaderSlot_t) == 124);

struct Loader_t {
  /* 0x00 */ u8 _pad00[0x84];
  /* 0x84 */ be_f32 iconFade;
  /* 0x88 */ be_u32 nowLoading;
  /* 0x8C */ u8 _pad8C[0x04];
  /* 0x90 */ LoaderSlot_t slots[kSlotScan];
};
static_assert(offsetof(Loader_t, iconFade) == 0x84);
static_assert(offsetof(Loader_t, nowLoading) == 0x88);
static_assert(offsetof(Loader_t, slots) == 0x90);

} // namespace

bool Loader::NowLoading() const {
  const auto *self = Self<Loader_t>();
  return self && static_cast<u32>(self->nowLoading) != 0;
}

D2AnimeTask Loader::NowLoadingAnime() const {
  const auto *self = Self<Loader_t>();
  return D2AnimeTask(self ? static_cast<u32>(self->nowLoading) : 0);
}

// Mirrors the per-slot test in bdAssetSlotCheckLoaded: (state - 1) <= 2.
bool Loader::AnySlotLoading() const {
  const auto *self = Self<Loader_t>();
  if (!self)
    return false;
  for (const LoaderSlot_t &slot : self->slots)
    if (static_cast<u32>(slot.state) - 1u <= 2u)
      return true;
  return false;
}

bool Loader::SetIconFade(f32 v) {
  auto *self = Self<Loader_t>();
  if (!self)
    return false;
  self->iconFade = v;
  return true;
}

} // namespace bd::engine

// Loader::vf03, ahead of the show/hide test on the loading icon fade.
// r31 = the Loader.
void bdLoaderIconMovieHideHook(PPCRegister &r31) {
  if (!bd::engine::SofdecPlayer::Playing())
    return;
  bd::engine::Loader(r31.u32).SetIconFade(0.0f);
}
