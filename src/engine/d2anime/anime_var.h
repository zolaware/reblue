/**
 * @file    engine/d2anime/anime_var.h
 * @brief   One entry of an AnimeVarBag.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

namespace bd::engine {

// AnimeVar_t::type, the ones the engine's own getters and setters check.
enum class AnimeVarType : u32 {
  Element = 0,
  Float = 2,
  String = 3,
};

// One entry of an AnimeVarBag, as AnimeVarBag_SetStringVar,
// AnimeVarBag_GetFloatVar and AnimeVarBag_GetElementXYWHP read it.
struct AnimeVar_t {
  /* 0x00 */ u8 _pad000[0x04];
  /* 0x04 */ be_u32 type;
  /* 0x08 */ u8 _pad008[0x1C];
  // The scalar for a float var. A string var stores its std::wstring in the
  // same slot, so a wide assignment takes the address of this field rather
  // than its value.
  /* 0x24 */ be_f32 value;
  // The five floats of an element var: x, y, w, h and priority. Uv.csv puts a
  // UV rect in the first four, so w is really u1 and h is v1.
  /* 0x28 */ be_f32 element[5];

  bool Is(AnimeVarType t) const {
    return static_cast<u32>(type) == static_cast<u32>(t);
  }
};
static_assert(offsetof(AnimeVar_t, type) == 0x04);
static_assert(offsetof(AnimeVar_t, value) == 0x24);
static_assert(offsetof(AnimeVar_t, element) == 0x28);

} // namespace bd::engine
