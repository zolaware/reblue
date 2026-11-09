/**
 * @file    engine/camera_view.h
 * @brief   One node of the engine's camera view list, g_bdCameraViewList.
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

// Only the fields the host reads or writes. The list is linked through next and
// the main view is the first node whose id is zero.
struct CameraView_t {
  /* 0x000 */ be_u32 id;
  /* 0x004 */ u8 _pad004[0x1C - 0x04];
  /* 0x01C */ be_u32 next;
  /* 0x020 */ u8 _pad020[0x38 - 0x20];
  /* 0x038 */ be_f32 texSize[2];
  /* 0x040 */ u8 _pad040[0xB8 - 0x40];
  /* 0x0B8 */ be_f32 view[16];
  /* 0x0F8 */ be_f32 proj[16];
  /* 0x138 */ u8 _pad138[0x1D0 - 0x138];
  // The sub-view pass memcpys the block at 0x18 up to 0x1B0, so this is the
  // copy of texSize, which it then scales by rate.
  /* 0x1D0 */ be_f32 renderSize[2];
  /* 0x1D8 */ u8 _pad1D8[0x348 - 0x1D8];
  /* 0x348 */ be_f32 rate;
};
static_assert(offsetof(CameraView_t, id) == 0x00);
static_assert(offsetof(CameraView_t, next) == 0x1C);
static_assert(offsetof(CameraView_t, texSize) == 0x38);
static_assert(offsetof(CameraView_t, view) == 0xB8);
static_assert(offsetof(CameraView_t, proj) == 0xF8);
static_assert(offsetof(CameraView_t, renderSize) == 0x1D0);
static_assert(offsetof(CameraView_t, rate) == 0x348);

} // namespace bd::engine
