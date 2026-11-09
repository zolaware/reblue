/**
 * @file    core/guest_vector.h
 * @brief   Element reads for rex::be_vector that work on any thread.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/runtime/xtypes.h>
#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::mem {

// Element index of a guest vector of scalars in host order. The fallback comes
// back past the end, for a null vector, and when the element is unreadable.
template <typename T>
inline T vec_at(const rex::be_vector<rex::BE<T>> &vec, size_t index,
                T fallback = T{}) {
  const std::optional<rex::runtime::MemoryView> v = view();
  if (!v)
    return fallback;
  const rex::BE<T> *element = vec.TryAt(index, *v);
  return element ? static_cast<T>(*element) : fallback;
}

} // namespace bd::mem
