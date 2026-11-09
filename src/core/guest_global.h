/**
 * @file    core/guest_global.h
 * @brief   Reads and writes of scalar guest globals, for any thread.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/runtime/guest_global.h>
#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::mem {

// The checked forms validate the page, so they suit any thread and any pointer
// that may be stale. The fast forms skip validation, for a global the guest
// thread already reads unchecked. All of them give the fallback or false when
// the guest address space is not up.

template <typename T, u32 Address>
inline T global_load(rex::GuestGlobal<rex::BE<T>, Address> global,
                     T fallback = T{}) {
  const auto v = view();
  const rex::BE<T> *value = v ? global.TryGet(*v) : nullptr;
  return value ? static_cast<T>(*value) : fallback;
}

template <typename T, u32 Address>
inline bool global_store(rex::GuestGlobal<rex::BE<T>, Address> global,
                         T value) {
  const auto v = view();
  rex::BE<T> *slot = v ? global.TryGet(*v) : nullptr;
  if (!slot)
    return false;
  *slot = value;
  return true;
}

template <typename T, u32 Address>
inline T global_load_fast(rex::GuestGlobal<rex::BE<T>, Address> global,
                          T fallback = T{}) {
  const auto v = view();
  return v ? static_cast<T>(*global.get(*v)) : fallback;
}

template <typename T, u32 Address>
inline bool global_store_fast(rex::GuestGlobal<rex::BE<T>, Address> global,
                              T value) {
  const auto v = view();
  if (!v)
    return false;
  *global.get(*v) = value;
  return true;
}

} // namespace bd::mem
