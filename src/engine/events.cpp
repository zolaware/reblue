/**
 * @file    engine/events.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/events.h"

#include <atomic>

#include "engine/frame_clock.h"

namespace bd::engine {

namespace {

EventTrace g_ring[EventLog::kCapacity];

// Publishing happens only on the engine thread, so a plain counter is enough
// for the write side. The release/acquire pair with the console thread loads
// below is what makes a published entry whole rather than torn. An entry can
// still be overwritten by wraparound mid-read, which is accepted here since
// this is a diagnostic, not a durable log.
std::atomic<u64> g_published{0};

} // namespace

void Events::Record(const char *name, std::array<u32, 2> trace) {
  const u64 seq = g_published.load(std::memory_order_relaxed);
  g_ring[seq % EventLog::kCapacity] = {name, TickCount(), trace[0], trace[1]};
  g_published.store(seq + 1, std::memory_order_release);
}

std::vector<EventTrace> EventLog::Recent() {
  const u64 total = g_published.load(std::memory_order_acquire);
  const u64 count = total < kCapacity ? total : kCapacity;
  std::vector<EventTrace> out;
  out.reserve(static_cast<size_t>(count));
  for (u64 i = total - count; i < total; ++i)
    out.push_back(g_ring[i % kCapacity]);
  return out;
}

u64 EventLog::TotalPublished() {
  return g_published.load(std::memory_order_acquire);
}

} // namespace bd::engine
