/**
 * @file    gpu/host_heap.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/host_heap.h"

#include <memory>
#include <mutex>

#include <o1heap.h>
#include <rex/dbg.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/runtime/guest_allocator.h>
#include <rex/runtime/xmemory.h>

#include "core/logging.h"
#include "core/memory_helpers.h"

// Tracy mem/plot macros. These compile to nothing without
// REXGLUE_ENABLE_PROFILING.
#if defined(REXGLUE_ENABLE_PROFILING)
#define BD_MEM_ALLOC(ptr, size)                                                \
  do {                                                                         \
    if (TracyIsStarted)                                                        \
      TracyAllocN((ptr), (size), "HostResourceHeap");                          \
  } while (0)
#define BD_MEM_FREE(ptr)                                                       \
  do {                                                                         \
    if (TracyIsStarted)                                                        \
      TracyFreeN((ptr), "HostResourceHeap");                                   \
  } while (0)
#define BD_MEM_PLOT(name, val)                                                 \
  do {                                                                         \
    if (TracyIsStarted)                                                        \
      TracyPlot((name), static_cast<double>(val));                             \
  } while (0)
#else
#define BD_MEM_ALLOC(ptr, size) ((void)0)
#define BD_MEM_FREE(ptr) ((void)0)
#define BD_MEM_PLOT(name, val) ((void)0)
#endif

namespace bd::gpu {

namespace {

// 64 MiB in the 512 MiB vA0000000 window. Host shadow structs are small, so
// this is generous. The "HostHeap Peak Bytes" plot says when to raise it.
constexpr u32 kHeapSize = 64u * 1024u * 1024u;
constexpr u32 kPageSize64K = 64u * 1024u;

struct HeapState {
  std::mutex mutex;
  std::unique_ptr<rex::runtime::GuestAllocator> allocator;
  u64 live_count = 0;
  bool oom_logged = false;
};

HeapState &state() {
  static HeapState s;
  return s;
}

#if defined(REXGLUE_ENABLE_PROFILING)
// Caller holds state().mutex.
void EmitPlotsLocked(const HeapState &s) {
  const auto d = s.allocator->GetDiagnostics();
  BD_MEM_PLOT("HostHeap Live Bytes", d.allocated);
  BD_MEM_PLOT("HostHeap Peak Bytes", d.peak_allocated);
  BD_MEM_PLOT("HostHeap Live Allocs", s.live_count);
  BD_MEM_PLOT("HostHeap OOM Count", d.oom_count);
}
#else
void EmitPlotsLocked(const HeapState &) {}
#endif

} // namespace

HostHeap &HostHeap::Get() {
  static HostHeap instance;
  return instance;
}

bool HostHeap::Init() {
  HeapState &s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.allocator)
    return true;

  auto *memory = bd::mem::memory();
  if (!memory) {
    REXLOG_ERROR(bd, "HostHeap::Init: no Memory instance");
    return false;
  }

  // The physical 64K heap is vA0000000 with host_address_offset 0.
  auto allocator = rex::runtime::GuestAllocator::Create(
      *memory, kHeapSize, kPageSize64K, /*physical=*/true, kPageSize64K, 0);
  if (!allocator) {
    REXLOG_ERROR(bd, "HostHeap::Init: failed to reserve {} bytes in vA0000000: {}",
                 kHeapSize, allocator.error().GetDescription());
    return false;
  }
  s.allocator = std::move(*allocator);

#if defined(REXGLUE_ENABLE_PROFILING)
  if (TracyIsStarted) {
    TracyPlotConfig("HostHeap Live Bytes", tracy::PlotFormatType::Memory, true,
                    true, 0);
    TracyPlotConfig("HostHeap Peak Bytes", tracy::PlotFormatType::Memory, true,
                    true, 0);
    TracyPlotConfig("HostHeap Live Allocs", tracy::PlotFormatType::Number, true,
                    true, 0);
    TracyPlotConfig("HostHeap OOM Count", tracy::PlotFormatType::Number, true,
                    true, 0);
  }
#endif
  return true;
}

void HostHeap::Shutdown() {
  HeapState &s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.allocator)
    return;
  if (s.live_count)
    REXLOG_DEBUG(bd, "HostHeap::Shutdown: releasing {} live blocks", s.live_count);
  s.allocator.reset();
  s.live_count = 0;
  s.oom_logged = false;
}

void *HostHeap::Alloc(std::size_t size, std::size_t alignment) {
  REX_PROFILE_SCOPE("HostHeap::Alloc");
  HeapState &s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.allocator) {
    REXLOG_ERROR(bd, "HostHeap::Alloc before Init or after Shutdown");
    return nullptr;
  }
  if (size == 0)
    size = 1;
  if (size > kHeapSize) {
    REXLOG_ERROR(bd, "HostHeap::Alloc size {} exceeds the heap", size);
    return nullptr;
  }
  // o1heap only guarantees O1HEAP_ALIGNMENT (16), so reject larger to avoid
  // silent under-alignment.
  if (alignment > O1HEAP_ALIGNMENT) {
    REXLOG_ERROR(bd, "HostHeap::Alloc alignment {} > {} unsupported", alignment,
             static_cast<std::size_t>(O1HEAP_ALIGNMENT));
    return nullptr;
  }

  void *p = s.allocator->Allocate(static_cast<u32>(size));
  if (!p) {
    if (!s.oom_logged) {
      const auto d = s.allocator->GetDiagnostics();
      REXLOG_ERROR(bd, "HostHeap OOM: req {} allocated {}/{} peak {} (raise "
               "kHeapSize)",
               size, d.allocated, d.capacity, d.peak_allocated);
      s.oom_logged = true;
    }
    EmitPlotsLocked(s);
    return nullptr;
  }

  ++s.live_count;
  BD_MEM_ALLOC(p, size);
  EmitPlotsLocked(s);
  return p;
}

void HostHeap::Free(void *host_ptr) {
  if (!host_ptr)
    return;
  REX_PROFILE_SCOPE("HostHeap::Free");
  HeapState &s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.allocator)
    return;
  BD_MEM_FREE(host_ptr);
  s.allocator->Free(host_ptr);
  if (s.live_count)
    --s.live_count;
  EmitPlotsLocked(s);
}

u32 HostHeap::AllocGuest(std::size_t size, std::size_t alignment) {
  void *host = Alloc(size, alignment);
  if (!host)
    return 0;
  return bd::mem::view()->HostToGuestVirtual(host);
}

void HostHeap::FreeGuest(u32 guest_va) {
  if (!guest_va)
    return;
  void *host = bd::mem::view()->TranslateVirtual<void *>(guest_va);
  Free(host);
}

HostHeap::Snapshot HostHeap::GetSnapshot() {
  HeapState &s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  Snapshot out{};
  out.ready = s.allocator != nullptr;
  out.live_count = s.live_count;
  if (s.allocator) {
    const auto d = s.allocator->GetDiagnostics();
    out.capacity = d.capacity;
    out.allocated = d.allocated;
    out.peak_allocated = d.peak_allocated;
    out.oom_count = d.oom_count;
  }
  return out;
}

} // namespace bd::gpu
