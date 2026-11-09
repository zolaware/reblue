/**
 * @file    gpu/host_heap.h
 * @brief   O(1) coalescing heap for the guest VA shadow structs.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 *
 * Reserves one block in the guest physical window (vA0000000, offset 0) through
 * rex::runtime::GuestAllocator, which runs a single o1heap over it. Many small host shadow structs fragmented the
 * non-coalescing system heap ("BaseHeap::Alloc failed to find contiguous
 * range"), o1heap coalesces on free. Offset-0 pointers round-trip to valid
 * guest VAs and stay FromGuest-resolvable.
 */
#pragma once

#include <cstddef>
#include <rex/types.h>

namespace bd::gpu {

class HostHeap {
public:
  static HostHeap &Get();

  // Reserve the physical block. Call once while the renderer starts, before
  // the first HostResourceHeap::Alloc. False on failure (fatal).
  bool Init();

  // Release the block while title memory is still alive. Call from app
  // shutdown, after the title stops and the renderer is drained. Later Free
  // and FreeGuest calls are ignored.
  void Shutdown();

  // O(1) alloc returning a 16-byte-aligned host pointer, or nullptr on
  // exhaustion (logged once). alignment must be <= O1HEAP_ALIGNMENT (16).
  void *Alloc(std::size_t size, std::size_t alignment);
  void Free(void *host_ptr);

  // For call sites that hand the VA to the engine (e.g.
  // D3DTexture_GetSurfaceLevel). Returns a vA0000000 VA, or 0 on OOM.
  u32 AllocGuest(std::size_t size, std::size_t alignment);
  void FreeGuest(u32 guest_va);

  struct Snapshot {
    std::size_t capacity;
    std::size_t allocated;
    std::size_t peak_allocated;
    u64 oom_count;
    u64 live_count;
    bool ready;
  };
  Snapshot GetSnapshot();
};

} // namespace bd::gpu
