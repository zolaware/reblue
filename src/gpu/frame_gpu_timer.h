/**
 * @file    gpu/frame_gpu_timer.h
 * @brief   One GPU timestamp pair per frame for the perf overlay, from the
 *          start of the frame's first command list to the end of its last.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace plume {
struct RenderCommandList;
struct RenderDevice;
} // namespace plume

namespace bd::gpu {

// All entry points need the video state mutex held. Each is a no-op while the
// overlay is off or the device has no timestamp support.

// Resets the pool and writes the start timestamp. Call after a command list
// begins, outside a render pass.
void GpuTimerBegin(plume::RenderDevice *device, plume::RenderCommandList *cmd);

// Writes the end timestamp. Call before the list that presents ends.
void GpuTimerEnd(plume::RenderCommandList *cmd, u32 fenceSlot);

// Reads back every finished pair of the slot. Call after its fence signaled.
void GpuTimerCollect(u32 fenceSlot);

} // namespace bd::gpu
