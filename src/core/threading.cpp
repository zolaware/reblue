/**
 * @file    core/threading.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "core/threading.h"

#include <chrono>
#include <thread>

#include "core/logging.h"

#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/runtime/thread_registry.h>
#include <rex/types.h>

#if defined(_WIN32)
#include "core/windows_lean.h"
#else
#include <sys/resource.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace {
#if defined(__x86_64__) || defined(_M_X64)
inline void CpuPause() { _mm_pause(); }
#else
inline void CpuPause() { std::this_thread::yield(); }
#endif
} // namespace

namespace bd {

void DemoteThreadToBackground() {
#if defined(_WIN32)
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#else
  // who=0 is the calling THREAD on Linux, not the process.
  ::setpriority(PRIO_PROCESS, 0, 10);
#endif
}

} // namespace bd

// PPC kernel bypass for Sleep.
u32 Sleep_hook(u32 ms) {
  if (ms == 0) {
    std::this_thread::yield();
    return 0;
  }

  auto target =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(u32(ms));

  if (ms >= 2) {
    std::this_thread::sleep_for(std::chrono::milliseconds(u32(ms)) -
                                std::chrono::microseconds(1500));
  } else {
    std::this_thread::yield();
  }

  while (std::chrono::steady_clock::now() < target)
    CpuPause();

  return 0;
}
REX_HOOK(rex_Sleep, Sleep_hook);

// PPC kernel bypass for NtSuspendThread.
u32 NtSuspendThread_hook(u32 handle) {
  auto thread = rex::runtime::GetCurrent().GetThreadRegistry().FindByHandle(handle);
  if (thread)
    thread->Suspend();
  return 0;
}
REX_HOOK(rex_NtSuspendThread, NtSuspendThread_hook);

// PPC kernel bypass for ResumeThread.
u32 ResumeThread_hook(u32 handle) {
  auto thread = rex::runtime::GetCurrent().GetThreadRegistry().FindByHandle(handle);
  if (thread)
    thread->Resume();
  return 0;
}
REX_HOOK(rex_ResumeThread, ResumeThread_hook);
