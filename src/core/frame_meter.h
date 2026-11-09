/**
 * @file    core/frame_meter.h
 * @brief   Frame time, CPU main, CPU render and GPU time for the perf overlay.
 *          Every entry point is a no-op while the overlay is off.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd {

inline constexpr u32 kFrameMeterGraphPoints = 120;

enum class FrameBound : u8 { None, CpuMain, CpuRender, Gpu };

struct FrameMeterSnapshot {
  bool valid = false;
  f64 fps = 0.0;
  f64 frameMs = 0.0;
  f64 worstMs = 0.0;
  f64 mainMs = 0.0;
  f64 renderMs = 0.0;
  f64 gpuMs = 0.0;
  bool gpuAvailable = true;
  FrameBound bound = FrameBound::None;
  f32 graph[kFrameMeterGraphPoints] = {};
  u32 graphCount = 0;
};

bool FrameMeterOn();

// Main thread: the 30 Hz logic tick, from the start of the step to the end of
// the last call in the tick block.
void FrameMeterTickBegin();
void FrameMeterTickEnd();

// Render thread: the draw recording, which excludes its waits.
void FrameMeterRenderBegin();
void FrameMeterRenderEnd();

void FrameMeterGpuTime(f64 ms);
void FrameMeterGpuUnavailable();

// Once per presented frame, after the frame's fence wait.
void FrameMeterPresent();

FrameMeterSnapshot FrameMeterRead();
const char *FrameBoundName(FrameBound bound);

} // namespace bd
