/**
 * @file    core/shutdown.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/shutdown.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "core/logging.h"
#include "core/perf.h"
#include "core/settings.h"
#include "core/threading.h"
#include "gpu/gpu.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

namespace bd {
namespace {

std::atomic<bool> g_requested{false};
std::atomic<bool> g_finished{false};

std::mutex g_dispatch_mutex;
std::function<bool(std::function<void()>)> g_dispatch;
std::function<void()> g_ui_pump;

using Clock = std::chrono::steady_clock;

const char *ReasonName(ShutdownReason r) {
  switch (r) {
  case ShutdownReason::WindowClose:
    return "window-close";
  case ShutdownReason::GuestExit:
    return "guest-exit";
  case ShutdownReason::Fatal:
    return "fatal";
  case ShutdownReason::InitFailure:
    return "init-failure";
  }
  return "?";
}

// Every stage is best-effort: a stage that cannot complete must not stop the
// ones after it, and the watchdog bounds any that hangs.
template <typename F> void Stage(const char *name, F &&fn) {
  const auto t0 = Clock::now();
  fn();
  const double ms =
      std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  BD_DEV_INFO("[shutdown] {} ({:.1f} ms)", name, ms);
}

// Armed before any work so every later stall is bounded. A quit never hangs on
// a wedged GPU or a guest thread that will not quiesce. The breadcrumb of the
// last completed stage names what stalled.
void ArmWatchdog(int exit_code) {
  const i32 budget_ms = bd::Settings::Get().ShutdownTimeoutMs();
  std::thread([budget_ms, exit_code] {
    const auto deadline = Clock::now() + std::chrono::milliseconds(budget_ms);
    while (Clock::now() < deadline) {
      if (g_finished.load(std::memory_order_acquire))
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (g_finished.load(std::memory_order_acquire))
      return;
    BD_WARN("[shutdown] watchdog fired after {} ms, terminating", budget_ms);
    rex::FlushLogging();
    TerminateProcessNow(exit_code);
  }).detach();
}

std::function<void()> UiPump() {
  std::lock_guard lock(g_dispatch_mutex);
  return g_ui_pump;
}

void StopGuestThreads() {
  // Cooperative: guest threads poll the flag in the kernel wait primitives and
  // self-exit. Stragglers are left running, never force-killed. They can no
  // longer reach the GPU (Video::BeginShutdown ran first), so leaving them is
  // safe. Reached from the UI thread, where no ThreadState is bound, so the
  // kernel state comes from the runtime rather than REX_KERNEL_STATE().
  auto *rt = rex::Runtime::instance();
  if (!rt || !rt->kernel_state())
    return;
  rt->kernel_state()->TerminateTitle();
}

[[noreturn]] void RunSequence(ShutdownReason reason, int exit_code) {
  BD_WARN("[shutdown] requested ({})", ReasonName(reason));
  ArmWatchdog(exit_code);

  Stage("quiesce-renderer", [] { gpu::Video::BeginShutdown(); });
  Stage("perf-csv", [] { PerfCSVShutdown(); });
  Stage("stop-guest-threads", [] { StopGuestThreads(); });
  Stage("flush-caches", [] { gpu::FlushPSOCapture(); });
  Stage("gpu-drain", [] { gpu::Video::Shutdown(UiPump()); });

  g_finished.store(true, std::memory_order_release);
  BD_INFO("[shutdown] complete, exiting {}", exit_code);
  rex::FlushLogging();
  TerminateProcessNow(exit_code);
}

// A guest thread that requested the shutdown must not return into guest code
// that assumed the process was already gone, and must not run the sequence
// itself. Park until the UI thread's sequence (or the watchdog) kills us.
[[noreturn]] void ParkUntilExit() {
  for (;;)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

} // namespace

void SetShutdownDispatcher(
    std::function<bool(std::function<void()>)> dispatch) {
  std::lock_guard lock(g_dispatch_mutex);
  g_dispatch = std::move(dispatch);
}

void SetShutdownUIPump(std::function<void()> pump) {
  std::lock_guard lock(g_dispatch_mutex);
  g_ui_pump = std::move(pump);
}

bool IsShuttingDown() { return g_requested.load(std::memory_order_acquire); }

void RequestShutdown(ShutdownReason reason, int exit_code) {
  bool expected = false;
  if (!g_requested.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
    // A second request from the thread running the sequence (a fatal path
    // reached during teardown) must fall through, not park.
    return;
  }

  std::function<bool(std::function<void()>)> dispatch;
  {
    std::lock_guard lock(g_dispatch_mutex);
    dispatch = g_dispatch;
  }

  if (!dispatch) {
    RunSequence(reason, exit_code); // never returns
  }

  // Runs inline when the caller is already the UI thread (window close), so
  // this does not return there either.
  if (!dispatch([reason, exit_code] { RunSequence(reason, exit_code); })) {
    BD_WARN("[shutdown] UI loop gone, terminating without the ordered path");
    rex::FlushLogging();
    TerminateProcessNow(exit_code);
  }
  ParkUntilExit();
}

void QuiesceForExit() {
  g_requested.store(true, std::memory_order_release);
  // Same watchdog: a warm reboot that wedges in the drain would otherwise leave
  // a dead-looking window with the replacement never spawned.
  ArmWatchdog(0);
  Stage("quiesce-renderer", [] { gpu::Video::BeginShutdown(); });
  Stage("perf-csv", [] { PerfCSVShutdown(); });
  Stage("stop-guest-threads", [] { StopGuestThreads(); });
  Stage("gpu-drain", [] { gpu::Video::Shutdown(UiPump()); });
  g_finished.store(true, std::memory_order_release);
}

} // namespace bd
