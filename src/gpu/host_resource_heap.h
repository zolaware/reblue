/**
 * @file    gpu/host_resource_heap.h
 * @brief   Guest-visible host-object allocator with a guest VA registry.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 *
 * Allocates a guest block of sizeof(T), placement-news the host object, and
 * records the guest VA. The first bytes of each host struct mirror the X360 SDK
 * struct so the engine reads valid Common/ReferenceCount/fetch constant bytes
 * off offset 0, host fields live past the X360 prefix. FromGuest checks the
 * registry to reject engine-supplied sentinel pointers (init writes a
 * non-resource default into all 16 RT slots).
 */
#pragma once

#include <cstring>
#include <new>
#include <rex/memory.h>
#include <rex/types.h>
#include <type_traits>
#include <utility>

#include <rex/kernel/title_runtime.h>
#include "core/memory_helpers.h"

#include "gpu/host_heap.h"
#include "gpu/resources.h"

namespace bd::gpu {

class HostResourceHeap {
public:
  template <typename T, typename... Args> static T *Alloc(Args &&...args) {
    static_assert(std::is_destructible_v<T>);
    const auto memory = bd::mem::view();
    constexpr u32 kAlignment = 16;
    void *host = HostHeap::Get().Alloc(sizeof(T), kAlignment);
    if (!host)
      return nullptr; // HostHeap logs OOM once
    const u32 guest = memory->HostToGuestVirtual(host);
    // Zero so the X360 header bytes the engine reads are defined before the
    // create path sets the real type bits.
    std::memset(host, 0, sizeof(T));
    auto *obj = new (host) T(std::forward<Args>(args)...);
    obj->selfVa = guest;
    Register(guest, obj->type);
    return obj;
  }

  // Returns the ResourceType the VA was registered with. False on miss.
  static bool GetType(u32 guest_va, ResourceType *out_type);

  template <typename T> static void Free(T *host_ptr) {
    if (!host_ptr)
      return;
    const auto memory = bd::mem::view();
    const u32 guest = memory->HostToGuestVirtual(host_ptr);
    Unregister(guest);
    host_ptr->~T();
    HostHeap::Get().Free(host_ptr);
  }

  // Resolve a guest VA to its host struct, or nullptr if not Alloc'd here.
  // Caller must pass the correct T (no runtime type info in the header).
  template <typename T> static T *FromGuest(u32 guest_va) {
    if (!guest_va || !IsRegistered(guest_va))
      return nullptr;
    const auto memory = bd::mem::view();
    return static_cast<T *>(memory->TranslateVirtual<void *>(guest_va));
  }

  template <typename T> static u32 ToGuest(T *host_ptr) {
    if (!host_ptr)
      return 0;
    const auto memory = bd::mem::view();
    return memory->HostToGuestVirtual(host_ptr);
  }

private:
  static void Register(u32 guest_va, ResourceType type);
  static void Unregister(u32 guest_va);
  static bool IsRegistered(u32 guest_va);
};

} // namespace bd::gpu
