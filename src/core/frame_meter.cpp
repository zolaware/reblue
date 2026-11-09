/**
 * @file    core/frame_meter.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/frame_meter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

#include "core/settings.h"

namespace bd {

namespace {

constexpr u64 kWindowNs = 500'000'000ull;
constexpr u64 kWorstNs = 1'000'000'000ull;
constexpr u32 kHistory = 1024;

u64 NowNs() {
  return static_cast<u64>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::atomic<u64> g_tickStartNs{0};
std::atomic<u64> g_tickNs{0};
std::atomic<u32> g_tickCount{0};
std::atomic<u64> g_renderNs{0};
std::atomic<u32> g_renderCount{0};
std::atomic<u64> g_gpuNs{0};
std::atomic<u32> g_gpuCount{0};
std::atomic<bool> g_gpuUnavailable{false};
thread_local u64 t_renderStartNs = 0;

struct Sample {
  u64 ns;
  f32 ms;
};

// The present thread owns everything up to the snapshot, so only the snapshot
// needs the lock.
bool g_wasOn = false;
u64 g_lastNs = 0;
u64 g_windowStartNs = 0;
u32 g_windowFrames = 0;
f32 g_ring[kFrameMeterGraphPoints] = {};
u32 g_ringHead = 0;
u32 g_ringCount = 0;
Sample g_history[kHistory] = {};
u32 g_historyHead = 0;
u32 g_historyCount = 0;

std::mutex g_snapshotMutex;
FrameMeterSnapshot g_snapshot;

void ResetAccumulators() {
  g_tickStartNs.store(0, std::memory_order_relaxed);
  g_tickNs.store(0, std::memory_order_relaxed);
  g_tickCount.store(0, std::memory_order_relaxed);
  g_renderNs.store(0, std::memory_order_relaxed);
  g_renderCount.store(0, std::memory_order_relaxed);
  g_gpuNs.store(0, std::memory_order_relaxed);
  g_gpuCount.store(0, std::memory_order_relaxed);
  g_windowFrames = 0;
  g_ringHead = g_ringCount = 0;
  g_historyHead = g_historyCount = 0;
}

f64 PerCount(std::atomic<u64> &ns, std::atomic<u32> &count, bool &has) {
  const u32 n = count.exchange(0, std::memory_order_relaxed);
  const u64 total = ns.exchange(0, std::memory_order_relaxed);
  has = n != 0;
  return n ? static_cast<f64>(total) * 1e-6 / static_cast<f64>(n) : 0.0;
}

} // namespace

bool FrameMeterOn() { return Settings::Get().PerfOverlay(); }

void FrameMeterTickBegin() {
  if (!FrameMeterOn())
    return;
  g_tickStartNs.store(NowNs(), std::memory_order_relaxed);
}

void FrameMeterTickEnd() {
  const u64 start = g_tickStartNs.exchange(0, std::memory_order_relaxed);
  if (!start)
    return;
  g_tickNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
  g_tickCount.fetch_add(1, std::memory_order_relaxed);
}

void FrameMeterRenderBegin() {
  t_renderStartNs = FrameMeterOn() ? NowNs() : 0;
}

void FrameMeterRenderEnd() {
  if (!t_renderStartNs)
    return;
  g_renderNs.fetch_add(NowNs() - t_renderStartNs, std::memory_order_relaxed);
  g_renderCount.fetch_add(1, std::memory_order_relaxed);
  t_renderStartNs = 0;
}

void FrameMeterGpuTime(f64 ms) {
  g_gpuNs.fetch_add(static_cast<u64>(ms * 1e6), std::memory_order_relaxed);
  g_gpuCount.fetch_add(1, std::memory_order_relaxed);
}

void FrameMeterGpuUnavailable() {
  g_gpuUnavailable.store(true, std::memory_order_relaxed);
}

void FrameMeterPresent() {
  if (!FrameMeterOn()) {
    g_wasOn = false;
    return;
  }
  const u64 now = NowNs();
  if (!g_wasOn) {
    ResetAccumulators();
    g_lastNs = g_windowStartNs = now;
    g_wasOn = true;
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_snapshot = FrameMeterSnapshot{};
    return;
  }

  const f32 dtMs = static_cast<f32>(static_cast<f64>(now - g_lastNs) * 1e-6);
  g_lastNs = now;
  g_ring[g_ringHead] = dtMs;
  g_ringHead = (g_ringHead + 1) % kFrameMeterGraphPoints;
  g_ringCount = std::min(g_ringCount + 1, kFrameMeterGraphPoints);
  g_history[g_historyHead] = {now, dtMs};
  g_historyHead = (g_historyHead + 1) % kHistory;
  g_historyCount = std::min(g_historyCount + 1, kHistory);
  ++g_windowFrames;

  f32 worst = 0.0f;
  for (u32 i = 0; i < g_historyCount; ++i) {
    const Sample &s = g_history[i];
    if (now - s.ns <= kWorstNs)
      worst = std::max(worst, s.ms);
  }

  std::lock_guard<std::mutex> lock(g_snapshotMutex);
  FrameMeterSnapshot &snap = g_snapshot;
  snap.worstMs = worst;
  snap.graphCount = g_ringCount;
  constexpr u32 kN = kFrameMeterGraphPoints;
  const u32 first = (g_ringHead + kN - g_ringCount) % kN;
  for (u32 i = 0; i < g_ringCount; ++i)
    snap.graph[i] = g_ring[(first + i) % kN];

  if (now - g_windowStartNs < kWindowNs)
    return;
  const f64 seconds = static_cast<f64>(now - g_windowStartNs) * 1e-9;
  snap.fps = static_cast<f64>(g_windowFrames) / seconds;
  snap.frameMs = seconds * 1000.0 / static_cast<f64>(g_windowFrames);
  g_windowStartNs = now;
  g_windowFrames = 0;

  bool hasMain = false, hasRender = false, hasGpu = false;
  snap.mainMs = PerCount(g_tickNs, g_tickCount, hasMain);
  snap.renderMs = PerCount(g_renderNs, g_renderCount, hasRender);
  snap.gpuMs = PerCount(g_gpuNs, g_gpuCount, hasGpu);
  snap.gpuAvailable = !g_gpuUnavailable.load(std::memory_order_relaxed);
  hasGpu = hasGpu && snap.gpuAvailable;

  snap.bound = FrameBound::None;
  f64 top = 0.0;
  if (hasMain && snap.mainMs > top) {
    top = snap.mainMs;
    snap.bound = FrameBound::CpuMain;
  }
  if (hasRender && snap.renderMs > top) {
    top = snap.renderMs;
    snap.bound = FrameBound::CpuRender;
  }
  if (hasGpu && snap.gpuMs > top)
    snap.bound = FrameBound::Gpu;
  snap.valid = true;
}

FrameMeterSnapshot FrameMeterRead() {
  std::lock_guard<std::mutex> lock(g_snapshotMutex);
  return g_snapshot;
}

const char *FrameBoundName(FrameBound bound) {
  switch (bound) {
  case FrameBound::CpuMain:
    return "CPU main";
  case FrameBound::CpuRender:
    return "CPU render";
  case FrameBound::Gpu:
    return "GPU";
  default:
    return "none";
  }
}

} // namespace bd
