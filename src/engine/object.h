/**
 * @file    engine/object.h
 * @brief   The root handle over one engine object in the recompiled address
 *          space.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::engine {

// A value over an address, holding no engine data. Every read goes through
// Address(), which is zero once the object is gone, so a stale handle reads
// fallbacks and refuses writes rather than touching whatever now sits there.
class Object {
public:
  Object() = default;
  explicit Object(u32 address) : address_(address) {}
  virtual ~Object() = default;
  Object(const Object &) = default;
  Object &operator=(const Object &) = default;

  explicit operator bool() const { return Address() != 0; }
  u32 Address() const { return Live() ? address_ : 0; }

protected:
  virtual bool Live() const {
    return mem::try_at<const be_u32>(address_) != nullptr;
  }

  template <class T> T *Self() const { return mem::try_at<T>(Address()); }

  u32 address_ = 0;
};

} // namespace bd::engine
