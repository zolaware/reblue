/**
 * @file    engine/ply_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/ply_task.h"

#include <cstddef>

namespace bd::engine {

namespace {

constexpr u32 kFlagVisible = 0x1;
constexpr u32 kFlagHidden = 0x4;

struct PlyTask_t {
  /* 0x0000 */ u8 _pad0000[0x70];
  /* 0x0070 */ u8 chara[0x2828];
  /* 0x2898 */ be_u32 nextRoster;
  /* 0x289C */ be_u32 nextParty;
  /* 0x28A0 */ u8 _pad28A0[0x04];
  /* 0x28A4 */ be_u32 uniqueId;
  /* 0x28A8 */ u8 _pad28A8[0x18];
  /* 0x28C0 */ be_u32 blinkArm;
};
static_assert(offsetof(PlyTask_t, chara) == 0x0070);
static_assert(offsetof(PlyTask_t, nextRoster) == 0x2898);
static_assert(offsetof(PlyTask_t, nextParty) == 0x289C);
static_assert(offsetof(PlyTask_t, uniqueId) == 0x28A4);
static_assert(offsetof(PlyTask_t, blinkArm) == 0x28C0);

} // namespace

PlyTask PlyTask::FromChara(const engine::Chara &chara) {
  return chara ? PlyTask(chara.Address() - offsetof(PlyTask_t, chara))
               : PlyTask();
}

List<PlyTask> PlyTask::PartyFrom(u32 head) {
  return List<PlyTask>(head, offsetof(PlyTask_t, nextParty));
}

List<PlyTask> PlyTask::RosterFrom(u32 head) {
  return List<PlyTask>(head, offsetof(PlyTask_t, nextRoster));
}

Player PlyTask::Chara() const {
  return Address() ? Player(Address() + offsetof(PlyTask_t, chara)) : Player();
}

u32 PlyTask::UniqueId() const {
  const auto *self = Self<PlyTask_t>();
  return self ? static_cast<u32>(self->uniqueId) : 0;
}

bool PlyTask::IsVisible() const { return (Flags() & kFlagVisible) != 0; }

bool PlyTask::IsHidden() const { return (Flags() & kFlagHidden) != 0; }

u32 PlyTask::BlinkArm() const {
  const auto *self = Self<PlyTask_t>();
  return self ? static_cast<u32>(self->blinkArm) : 0;
}

bool PlyTask::SetBlinkArm(u32 v) {
  auto *self = Self<PlyTask_t>();
  if (!self)
    return false;
  self->blinkArm = v;
  return true;
}

} // namespace bd::engine
