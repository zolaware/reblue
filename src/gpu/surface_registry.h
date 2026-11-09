/**
 * @file    gpu/surface_registry.h
 * @brief   One persistent host surface per RT/DS the engine creates.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::gpu {

struct GuestTexture;

class SurfaceRegistry {
public:
  static GuestTexture *Get(u32 width, u32 height, u32 guest_format,
                           u32 sample_count);

  // The next surface or texture created on this thread keeps scale 1 if it has
  // these dims. Any create clears the pending exemption, matched or not, so a
  // site whose create is skipped cannot leak it to a later one.
  static void ExemptNext(u32 width, u32 height);
  static bool TakeExemption(u32 width, u32 height);

  static void Tick();

  // The render rect changed: surfaces idle at another scale retire, held ones
  // are recreated in place at the new scale under the same pointer. Returns the
  // count touched.
  static u32 Rescale(float kx, float ky);
};

} // namespace bd::gpu
