/**
 * @file    engine/field_player_entity.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/field_player_entity.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

#include <rex/hook.h>

#include "engine/chara.h"
#include "engine/events.h"
#include "engine/game.h"

namespace bd::engine {

namespace {

// One equipped field skill, the same twelve-byte record the field encounter
// menu keeps its own item list in and matches these against.
struct FieldSkillSlot_t {
  /* 0x00 */ be_u32 id; // zero while the slot is still locked
  /* 0x04 */ u8 _pad04[0x08];
};
static_assert(sizeof(FieldSkillSlot_t) == 0x0C);

enum class PlyManState : u32 {
  kControlled = 1,
  kControlledAlt = 12,
};

struct FieldPlayerEntity_t {
  /* 0x000 */ u8 _pad000[0x70];
  /* 0x070 */ be_u32 state;
  /* 0x074 */ u8 _pad074[0x78 - 0x74];
  /* 0x078 */ be_u32 rosterHead;
  /* 0x07C */ be_u32 activeHead;
  /* 0x080 */ u8 _pad080[0x250 - 0x080];
  /* 0x250 */ FieldSkillSlot_t fieldSkills[2];
};
static_assert(offsetof(FieldPlayerEntity_t, state) == 0x070);
static_assert(offsetof(FieldPlayerEntity_t, rosterHead) == 0x078);
static_assert(offsetof(FieldPlayerEntity_t, activeHead) == 0x07C);
static_assert(offsetof(FieldPlayerEntity_t, fieldSkills) == 0x250);

// The publishers below take the entity from the hook argument rather than from
// addr::kFieldPlayerEntity, so a call made on some other entity reports that
// one or nothing at all.
std::vector<u32> MarchingAddresses(u32 entity) {
  std::vector<u32> addresses;
  for (const PlyTask &member : FieldPlayerEntity(entity).Party())
    addresses.push_back(member.Address());
  return addresses;
}

u32 LeaderAddress(u32 entity) {
  return FieldPlayerEntity(entity).Leader().Address();
}

// The member the marching party gained, or an empty handle. Every publishing
// site adds at most one.
PlyTask JoinedSince(u32 entity, const std::vector<u32> &before) {
  for (const PlyTask &member : FieldPlayerEntity(entity).Party())
    if (std::find(before.begin(), before.end(), member.Address()) ==
        before.end())
      return member;
  return PlyTask();
}

} // namespace

List<PlyTask> FieldPlayerEntity::Party() const {
  const auto *self = Self<FieldPlayerEntity_t>();
  return PlyTask::PartyFrom(self ? static_cast<u32>(self->activeHead) : 0);
}

List<PlyTask> FieldPlayerEntity::Roster() const {
  const auto *self = Self<FieldPlayerEntity_t>();
  return PlyTask::RosterFrom(self ? static_cast<u32>(self->rosterHead) : 0);
}

PlyTask FieldPlayerEntity::Leader() const { return Party().At(0); }

bool FieldPlayerEntity::HasControl() const {
  const auto *self = Self<FieldPlayerEntity_t>();
  if (!self)
    return false;
  switch (static_cast<PlyManState>(static_cast<u32>(self->state))) {
  case PlyManState::kControlled:
  case PlyManState::kControlledAlt:
    return true;
  }
  return false;
}

bool FieldPlayerEntity::HasFieldSkill(int slot) const {
  const auto *self = Self<FieldPlayerEntity_t>();
  if (!self || slot < 0 || slot >= int(std::size(self->fieldSkills)))
    return false;
  return static_cast<u32>(self->fieldSkills[slot].id) != 0;
}

size_t FieldPlayerEntity::ActiveCount() const {
  size_t count = 0;
  for (const PlyTask &member : Party()) {
    const Player c = member.Chara();
    if (c.IsAlive() && !c.HasStatus(CharaStatus::kPetrify) &&
        !c.HasStatus(CharaStatus::kKnockedOut))
      ++count;
  }
  return count;
}

} // namespace bd::engine

// Two sites publish PartyMemberAdded, and both diff the marching list rather
// than reading their arguments. bdPartyAddMember builds the PlyTask nodes, one
// per character, and every caller is a construction path, so on its own the
// event would fire at load and never again. Joining an existing party is the
// script opcode, which finds the member on the roster and links it into the
// marching list itself instead of calling back here. The diff also settles the
// two questions the arguments cannot: bdPartyAddMember registers a member on
// the roster whether or not it marches, and the opcode both adds and removes.

// PlayerSpawned rides the same call and is not a second spelling of
// PartyMemberAdded. bdPartyAddMember builds the PlyTask and links it to the
// roster whatever r5 says, and bdGameTaskUpdate has two alternative session
// init blocks: one passes r5 = 0 for all five members and threads the marching
// list by hand afterwards, so the marching diff sees nothing at all, and the
// other passes the slot's unlock bit, so an unlocked slot there does raise both
// events for the one member. The gap is on the first block rather than on every
// call, and each event still means what it says where the two coincide.
REX_EXTERN(__imp__bdPartyAddMember);
REX_HOOK_RAW(bdPartyAddMember) {
  const u32 entity = ctx.r3.u32;
  const std::vector<u32> before = bd::engine::MarchingAddresses(entity);
  __imp__bdPartyAddMember(ctx, base);
  const bd::engine::PlyTask spawned(ctx.r3.u32);
  // The marching diff is taken before either publish, because a subscriber
  // running engine code can relink the list it reads.
  const bd::engine::PlyTask joined = bd::engine::JoinedSince(entity, before);
  if (spawned)
    bd::engine::Events::Publish(bd::engine::PlayerSpawned{spawned.Address()});
  if (joined)
    bd::engine::Events::Publish(bd::engine::PartyMemberAdded{joined.Address()});
}

REX_EXTERN(__imp__bdScriptOpPartyChange);
REX_HOOK_RAW(bdScriptOpPartyChange) {
  const u32 entity = bd::engine::Game::Get().FieldPlayerEntity().Address();
  const std::vector<u32> before = bd::engine::MarchingAddresses(entity);
  __imp__bdScriptOpPartyChange(ctx, base);
  const bd::engine::PlyTask joined = bd::engine::JoinedSince(entity, before);
  if (joined)
    bd::engine::Events::Publish(bd::engine::PartyMemberAdded{joined.Address()});
}

// The leader is the head of the marching list, and the original rotates the
// list until the member it was asked for reaches it. Callers ask for a leader
// that already leads, which the head comparison absorbs, and the rotation can
// also fail to find one, which leaves the head where it was.
REX_EXTERN(__imp__bdPartySetLeader);
REX_HOOK_RAW(bdPartySetLeader) {
  const u32 entity = ctx.r3.u32;
  const u32 before = bd::engine::LeaderAddress(entity);
  __imp__bdPartySetLeader(ctx, base);
  const u32 after = bd::engine::LeaderAddress(entity);
  if (after && after != before)
    bd::engine::Events::Publish(bd::engine::PartyLeaderChanged{after});
}
