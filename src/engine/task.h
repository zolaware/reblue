/**
 * @file    engine/task.h
 * @brief   The engine's Task, root of every task subclass reblue touches.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/list.h"
#include "engine/object.h"

namespace bd::engine {

// The address alone is not an identity: the heap hands a freed task's address
// straight to the next allocation, and the task that sits there is mapped,
// live and correctly flagged. Only the UID tells them apart, so a Task
// captures it at construction and every accessor checks it.
class Task : public Object {
public:
  Task() = default;
  explicit Task(u32 address);

  u64 UID() const;
  u32 Vtable() const;
  u32 Flags() const;
  bool IsDead() const;
  void Kill();

  Task Parent() const;
  Task FirstChild() const;
  Task NextSibling() const;
  List<Task> Children() const;

  Task NotifyParent() const;
  u64 NotifyParentUID() const;
  void SetNotifyParent(const Task &parent);
  Task NotifyChild() const;
  void SetNotifyChild(const Task &child);
  void ClearNotifyChild();

  // Same address and still the same object, which a bare == misses.
  bool Is(u32 address) const;

  // Rebinds and reports whether the object changed, a recycled address
  // included.
  bool Rebind(u32 address);
  void Reset();

protected:
  bool Live() const override;

private:
  u64 uid_ = 0;
};

inline constexpr u32 kTaskDeadSentinel = 0xDEAD0000;

} // namespace bd::engine
