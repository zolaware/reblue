/**
 * @file    engine/task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/task.h"

#include <cstddef>

namespace bd::engine {

namespace {

struct TaskBase_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x0C];
  /* 0x010 */ be_u64 taskUID;
  /* 0x018 */ u8 _pad018[0x20];
  /* 0x038 */ be_u32 firstChild;
  /* 0x03C */ be_u32 nextSibling;
  /* 0x040 */ be_u32 parent;
  /* 0x044 */ u8 _pad044[0x04];
  /* 0x048 */ be_u32 notifyParent;
  /* 0x04C */ u8 _pad04C[0x04];
  /* 0x050 */ be_u64 notifyParentUID;
  /* 0x058 */ be_u32 flags;
  /* 0x05C */ u8 _pad05C[0x04];
  /* 0x060 */ be_u32 destroyFlag;
  /* 0x064 */ u8 _pad064[0x3C];
  /* 0x0A0 */ be_u32 notifyChild;
};
static_assert(offsetof(TaskBase_t, vtable) == 0x000);
static_assert(offsetof(TaskBase_t, taskUID) == 0x010);
static_assert(offsetof(TaskBase_t, firstChild) == 0x038);
static_assert(offsetof(TaskBase_t, nextSibling) == 0x03C);
static_assert(offsetof(TaskBase_t, parent) == 0x040);
static_assert(offsetof(TaskBase_t, notifyParent) == 0x048);
static_assert(offsetof(TaskBase_t, notifyParentUID) == 0x050);
static_assert(offsetof(TaskBase_t, flags) == 0x058);
static_assert(offsetof(TaskBase_t, destroyFlag) == 0x060);
static_assert(offsetof(TaskBase_t, notifyChild) == 0x0A0);

// An address whose flags field no longer resolves counts as dead: a zero
// fallback would otherwise read back as a live task carrying no sentinel.
bool DeadAt(u32 address) {
  if (!address)
    return true;
  const auto *task = mem::try_at<const TaskBase_t>(address);
  return !task || (static_cast<u32>(task->flags) & kTaskDeadSentinel) ==
                      kTaskDeadSentinel;
}

u64 UIDAt(u32 address) {
  const auto *task = mem::try_at<const TaskBase_t>(address);
  return task ? static_cast<u64>(task->taskUID) : 0;
}

} // namespace

Task::Task(u32 address) : Object(address), uid_(UIDAt(address)) {}

bool Task::Live() const {
  return !DeadAt(address_) && UIDAt(address_) == uid_;
}

u64 Task::UID() const { return uid_; }

u32 Task::Vtable() const {
  const auto *self = Self<TaskBase_t>();
  return self ? static_cast<u32>(self->vtable) : 0;
}

u32 Task::Flags() const {
  const auto *self = Self<TaskBase_t>();
  return self ? static_cast<u32>(self->flags) : 0;
}

bool Task::IsDead() const { return !Live(); }

void Task::Kill() {
  auto *task = Self<TaskBase_t>();
  if (!task)
    return;
  task->destroyFlag = 1u;
  task->flags = static_cast<u32>(task->flags) | kTaskDeadSentinel;
}

Task Task::Parent() const {
  const auto *self = Self<TaskBase_t>();
  return Task(self ? static_cast<u32>(self->parent) : 0);
}

Task Task::FirstChild() const {
  const auto *self = Self<TaskBase_t>();
  return Task(self ? static_cast<u32>(self->firstChild) : 0);
}

Task Task::NextSibling() const {
  const auto *self = Self<TaskBase_t>();
  return Task(self ? static_cast<u32>(self->nextSibling) : 0);
}

List<Task> Task::Children() const {
  const auto *self = Self<TaskBase_t>();
  return List<Task>(self ? static_cast<u32>(self->firstChild) : 0,
                    offsetof(TaskBase_t, nextSibling));
}

Task Task::NotifyParent() const {
  const auto *self = Self<TaskBase_t>();
  return Task(self ? static_cast<u32>(self->notifyParent) : 0);
}

u64 Task::NotifyParentUID() const {
  const auto *self = Self<TaskBase_t>();
  return self ? static_cast<u64>(self->notifyParentUID) : 0;
}

void Task::SetNotifyParent(const Task &parent) {
  auto *self = Self<TaskBase_t>();
  if (!self)
    return;
  self->notifyParent = parent.Address();
  self->notifyParentUID = parent.UID();
}

Task Task::NotifyChild() const {
  const auto *self = Self<TaskBase_t>();
  return Task(self ? static_cast<u32>(self->notifyChild) : 0);
}

void Task::SetNotifyChild(const Task &child) {
  auto *self = Self<TaskBase_t>();
  if (!self)
    return;
  self->notifyChild = child.Address();
}

void Task::ClearNotifyChild() {
  auto *self = Self<TaskBase_t>();
  if (!self)
    return;
  self->notifyChild = 0u;
}

bool Task::Is(u32 address) const { return address_ == address && Live(); }

bool Task::Rebind(u32 address) {
  if (Is(address))
    return false;
  *this = Task(address);
  return true;
}

void Task::Reset() { *this = Task(); }

} // namespace bd::engine
