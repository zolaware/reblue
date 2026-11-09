/**
 * @file    engine/frame_meter_hooks.cpp
 * @brief   The two engine brackets the perf overlay times: the end of the 30 Hz
 *          logic tick and the render thread's draw recording.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <rex/ppc/hook.h>

#include "core/frame_meter.h"

// The last call of the tick block in bdMainGameStep passes 0. The other caller
// passes a context pointer and is not the tick.
REX_DETOUR(bdSoundCueUpdate, void, (u32 context)) {
  original(context);
  if (context == 0)
    bd::FrameMeterTickEnd();
}

// The draw recording. bdRenderStep has already passed its wait for DrawStart
// and for device ownership when it calls this.
REX_DETOUR(bdRenderFrame, void, (u32 renderer)) {
  bd::FrameMeterRenderBegin();
  original(renderer);
  bd::FrameMeterRenderEnd();
}
