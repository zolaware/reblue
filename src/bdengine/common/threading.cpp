/**
 * @file    bdengine/threading.cpp
 * @brief   Native threading, sleep, and frame timing hooks.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "bdengine/common/threading.h"

#include <chrono>
#include <immintrin.h>
#include <mutex>
#include <thread>

#include "bdengine/common/logging.h"

#include <rex/types.h>
#include <rex/cvar.h>
#include <rex/ppc.h>
#include <rex/system/xthread.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>

// CVars
REXCVAR_DEFINE_BOOL(bd_frame_limit, true, "Blue Dragon",
                    "Enable native frame limiter");
REXCVAR_DEFINE_INT32(bd_target_fps, 30, "Blue Dragon",
                   "Target frame rate (30 or 60)");
REXCVAR_DEFINE_BOOL(bd_vsync, false, "Blue Dragon",
                    "Enable vsync (interval 1) instead of frame limiter pacing");

namespace {
std::once_flag g_timer_init;
}

namespace bd {

void EnableHighResTimer() {
    std::call_once(g_timer_init, [] {
        timeBeginPeriod(1);
        BD_INFO("[threading] high-res timer enabled");
    });
}

void DisableHighResTimer() {
    timeEndPeriod(1);
    BD_INFO("[threading] high-res timer disabled");
}

} // namespace bd

/**
 * @brief Sleep (0x8248CF50) - PPC kernel bypass.
 */
u32 Sleep_hook(u32 ms) {
    bd::EnableHighResTimer();

    if (ms == 0) {
        SwitchToThread();
        return 0;
    }

    auto target = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(u32(ms));

    if (ms >= 2) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(u32(ms)) - std::chrono::microseconds(1500));
    } else {
        SwitchToThread();
    }

    while (std::chrono::steady_clock::now() < target)
        _mm_pause();

    return 0;
}
PPC_HOOK(rex_Sleep, Sleep_hook);

/**
 * @brief NtSuspendThread (0x82466E80) - PPC kernel bypass.
 */
u32 NtSuspendThread_hook(u32 handle) {
    auto thread = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XThread>(handle);
    if (thread)
        thread->Suspend();
    return 0;
}
PPC_HOOK(rex_NtSuspendThread, NtSuspendThread_hook);

/**
 * @brief ResumeThread (0x8248D7A8) - PPC kernel bypass.
 */
u32 ResumeThread_hook(u32 handle) {
    auto thread = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XThread>(handle);
    if (thread)
        thread->Resume();
    return 0;
}
PPC_HOOK(rex_ResumeThread, ResumeThread_hook);

/**
 * @brief Vsync flip rate override (midasm at 0x8246AB68).
 */
bool bdVsyncFlipRateHook(PPCRegister &r10) {
    r10.u64 = REXCVAR_GET(bd_vsync) ? 1 : 0;
    return true;
}

/**
 * @brief Frame limiter (midasm at 0x82132F08).
 */
void bdRenderFrameLimiterHook() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point frame_start{};

    if (!REXCVAR_GET(bd_frame_limit)) {
        frame_start = Clock::now();
        return;
    }

    int target_fps = REXCVAR_GET(bd_target_fps);
    if (target_fps <= 0)
        target_fps = 30;

    double target_ms = 1000.0 / target_fps;
    auto now = Clock::now();

    if (frame_start.time_since_epoch().count() != 0) {
        double elapsed_ms =
            std::chrono::duration<double, std::milli>(now - frame_start).count();
        double remaining = target_ms - elapsed_ms;

        if (remaining > 1.5) {
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<i64>((remaining - 1.5) * 1000.0)));
        }

        while (std::chrono::duration<double, std::milli>(
                   Clock::now() - frame_start).count() < target_ms)
            _mm_pause();
    }

    frame_start = Clock::now();
}
