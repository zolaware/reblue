/**
 * @file    core/guest_vector.h
 * @brief   Element reads for rex::kernel::GuestVector that work on any thread.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <optional>

#include <rex/kernel/types/memory.h>
#include <rex/kernel/memory_view.h>
#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::mem {

// Element index of a guest vector of scalars in host order. The fallback comes
// back past the end, for a null vector, and when the element is unreadable.
template <typename T>
inline T vec_at(const rex::kernel::GuestVector<T> &vec, size_t index,
                T fallback = T{}) {
  const std::optional<rex::kernel::MemoryView> v = view();
  return v ? vec.TryGet(index, *v).value_or(fallback) : fallback;
}

} // namespace bd::mem
