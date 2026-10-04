/**
 * @file    engine/menus/title_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/title_task.h"

#include <cstddef>
#include <cstring>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

struct TitleTask_t {
  /* 0x000 */ u8 _pad000[0x78];
  /* 0x078 */ be_u32 nextSeqId;
  /* 0x07C */ u8 _pad07C[0x90 - 0x7C];
  /* 0x090 */ be_u32 state; // 6 and 7 are the back states
  /* 0x094 */ be_u32 cursor;
  /* 0x098 */ be_u32 hasSaveData;
  /* 0x09C */ u8 _pad09C[0xA0 - 0x9C];
  /* 0x0A0 */ be_u32 childTask;
  /* 0x0A4 */ char childName[6];
  /* 0x0AA */ u8 _pad0AA[0x104 - 0xAA];
  /* 0x104 */ be_u32 isXboxLive;
  /* 0x108 */ u8 _pad108[0x10C - 0x108];
  /* 0x10C */ be_u32 discSetting;
  /* 0x110 */ u8 _pad110[0x114 - 0x110];
  /* 0x114 */ be_u32 voicePick;
};
static_assert(offsetof(TitleTask_t, nextSeqId) == 0x078);
static_assert(offsetof(TitleTask_t, state) == 0x090);
static_assert(offsetof(TitleTask_t, cursor) == 0x094);
static_assert(offsetof(TitleTask_t, hasSaveData) == 0x098);
static_assert(offsetof(TitleTask_t, childTask) == 0x0A0);
static_assert(offsetof(TitleTask_t, childName) == 0x0A4);
static_assert(offsetof(TitleTask_t, isXboxLive) == 0x104);
static_assert(offsetof(TitleTask_t, discSetting) == 0x10C);
static_assert(offsetof(TitleTask_t, voicePick) == 0x114);

} // namespace

u32 TitleTask::State() const {
  const auto *self = Self<TitleTask_t>();
  return self ? static_cast<u32>(self->state) : 0;
}

void TitleTask::SetState(u32 state) {
  auto *self = Self<TitleTask_t>();
  if (self)
    self->state = state;
}

u32 TitleTask::Cursor() const {
  const auto *self = Self<TitleTask_t>();
  return self ? static_cast<u32>(self->cursor) : 0;
}

void TitleTask::SetCursor(u32 cursor) {
  auto *self = Self<TitleTask_t>();
  if (self)
    self->cursor = cursor;
}

u32 TitleTask::DiscSetting() const {
  const auto *self = Self<TitleTask_t>();
  return self ? static_cast<u32>(self->discSetting) : 0;
}

void TitleTask::SetDiscSetting(u32 setting) {
  auto *self = Self<TitleTask_t>();
  if (self)
    self->discSetting = setting;
}

u32 TitleTask::VoicePick() const {
  const auto *self = Self<TitleTask_t>();
  return self ? static_cast<u32>(self->voicePick) : 0;
}

void TitleTask::SetVoicePick(u32 pick) {
  auto *self = Self<TitleTask_t>();
  if (self)
    self->voicePick = pick;
}

bool TitleTask::HasSaveData() const {
  const auto *self = Self<TitleTask_t>();
  return self && static_cast<u32>(self->hasSaveData) != 0;
}

bool TitleTask::IsXboxLive() const {
  const auto *self = Self<TitleTask_t>();
  return self && static_cast<u32>(self->isXboxLive) != 0;
}

void TitleTask::SetNextSeqId(u32 id) {
  auto *self = Self<TitleTask_t>();
  if (self)
    self->nextSeqId = id;
}

bool TitleTask::HasChild() const {
  const auto *self = Self<TitleTask_t>();
  return self && static_cast<u32>(self->childTask) != 0;
}

void TitleTask::ClearChild() {
  auto *self = Self<TitleTask_t>();
  if (!self)
    return;
  std::memcpy(self->childName, "Title\0", sizeof(self->childName));
  self->childTask = 0;
}

} // namespace bd::engine
