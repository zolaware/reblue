/**
 * @file    engine/visual_object.h
 * @brief   The engine's VisualObject, the model instance the loader and the
 *          bone update both work on.
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

// Only the fields the host reads. boneBuffer is the head of the bone matrix
// double buffer, which the engine takes by address.
struct VisualObject_t {
  /* 0x000 */ u8 _pad000[0x740];
  // The view-toon class carries a marker here, and its models reach the toon
  // closure on top of whatever base technique they carry.
  /* 0x740 */ be_u32 classMarker;
  /* 0x744 */ u8 _pad744[0x74C - 0x744];
  /* 0x74C */ be_u32 boneCount;
  /* 0x750 */ u8 _pad750[0xA48 - 0x750];
  /* 0xA48 */ be_u32 boneBuffer;
  /* 0xA4C */ u8 _padA4C[0xBB8 - 0xA4C];
  /* 0xBB8 */ be_u32 baseTechnique;
};
static_assert(offsetof(VisualObject_t, classMarker) == 0x740);
static_assert(offsetof(VisualObject_t, boneCount) == 0x74C);
static_assert(offsetof(VisualObject_t, boneBuffer) == 0xA48);
static_assert(offsetof(VisualObject_t, baseTechnique) == 0xBB8);

} // namespace bd::engine
