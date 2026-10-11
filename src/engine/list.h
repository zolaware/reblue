/**
 * @file    engine/list.h
 * @brief   A walk over an engine intrusive list, typed by the handle each
 *          element becomes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd::engine {

// T is constructible from a u32 address. nextOffset is where each element
// keeps the address of the next one.
template <class T> class List {
public:
  List() = default;
  List(u32 head, u32 nextOffset) : head_(head), next_(nextOffset) {}

  explicit operator bool() const { return head_ != 0; }

  // Two cursors, one stepping twice for every step of the other: they can
  // only meet inside a cycle, so a corrupt list ends the walk instead of
  // running forever.
  class Iterator {
  public:
    Iterator() = default;
    Iterator(u32 head, u32 nextOffset)
        : slow_(head), fast_(head), next_(nextOffset) {}

    T operator*() const { return T(slow_); }
    u32 Address() const { return slow_; }

    Iterator &operator++() {
      slow_ = Next(slow_);
      fast_ = Next(fast_);
      if (fast_)
        fast_ = Next(fast_);
      if (fast_ && fast_ == slow_)
        slow_ = 0;
      return *this;
    }

    bool operator==(const Iterator &o) const { return slow_ == o.slow_; }
    bool operator!=(const Iterator &o) const { return slow_ != o.slow_; }

  private:
    u32 Next(u32 node) const { return mem::try_field<u32>(node, next_); }

    u32 slow_ = 0;
    u32 fast_ = 0;
    u32 next_ = 0;
  };

  Iterator begin() const { return Iterator(head_, next_); }
  Iterator end() const { return Iterator(); }

  size_t Size() const {
    size_t count = 0;
    for (auto it = begin(); it != end(); ++it)
      ++count;
    return count;
  }

  // Default T past the end.
  T At(size_t index) const {
    size_t i = 0;
    for (auto it = begin(); it != end(); ++it, ++i)
      if (i == index)
        return *it;
    return T();
  }

  bool Contains(u32 address) const {
    if (!address)
      return false;
    for (auto it = begin(); it != end(); ++it)
      if (it.Address() == address)
        return true;
    return false;
  }

private:
  u32 head_ = 0;
  u32 next_ = 0;
};

} // namespace bd::engine
