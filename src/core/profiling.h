/**
 * @file    core/profiling.h
 * @brief   Tracy CPU zone and frame mark macros (gated on profiling builds).
 *
 * Separate from gpu_profiling.h because TracyD3D12.hpp is 155k lines and only
 * the handful of TUs that place GPU zones need it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#if defined(REXGLUE_ENABLE_PROFILING)

#include <tracy/Tracy.hpp>

// Every zone gates on TracyIsStarted: with TRACY_MANUAL_LIFETIME the profiler
// only exists once bd_profiler brings it up in OnPreSetup, and may never exist
// at all. GetProfiler() on the un-started profiler is a null deref, and the
// active flag short-circuits before it is reached.
#define BD_CPU_ZONE(name) ZoneNamedN(___tracy_scoped_zone, name, TracyIsStarted)
#define BD_CPU_ZONE_DYN(name)                                                  \
  ZoneTransientN(TracyConcat(__tracy_cpu_zone, TracyLine), name, TracyIsStarted)
#define BD_PROFILER_CONNECTED() (TracyIsStarted && TracyIsConnected)
#define BD_FRAME_MARK()                                                        \
  do {                                                                         \
    if (TracyIsStarted) {                                                      \
      FrameMark;                                                               \
    }                                                                          \
  } while (0)
#define BD_PLOT(name, val)                                                     \
  do {                                                                         \
    if (TracyIsStarted) {                                                      \
      TracyPlot((name), static_cast<double>(val));                             \
    }                                                                          \
  } while (0)

#else

#define BD_CPU_ZONE(name) ((void)0)
#define BD_CPU_ZONE_DYN(name) ((void)0)
#define BD_PROFILER_CONNECTED() (false)
#define BD_FRAME_MARK() ((void)0)
#define BD_PLOT(name, val) ((void)0)

#endif
