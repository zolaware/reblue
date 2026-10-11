/**
 * @file    engine/battle_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/battle_task.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "core/memory_helpers.h"
#include "engine/chara.h"
#include "engine/events.h"
#include "engine/game.h"
#include "engine/ply_task.h"
#include "engine/script_man_task.h"

namespace bd::engine {

namespace {

struct BattleMember_t {
  /* 0x00 */ u8 _pad00[0x18];
  /* 0x18 */ be_u32 actor;
  /* 0x1C */ u8 _pad1C[0x20 - 0x1C];
};
static_assert(offsetof(BattleMember_t, actor) == 0x18);
static_assert(sizeof(BattleMember_t) == 0x20);

struct BattleGroup_t {
  /* 0x00 */ u8 _pad00[0x04];
  /* 0x04 */ mem::GuestVec<BattleMember_t> members;
};
static_assert(offsetof(BattleGroup_t, members) == 0x04);
static_assert(sizeof(BattleGroup_t) == 0x10);

struct BattleCommand_t {
  /* 0x00 */ u8 _pad00[0x04];
  /* 0x04 */ mem::GuestVec<BattleGroup_t> groups;
  /* 0x10 */ u8 _pad10[0x7C - 0x10];
};
static_assert(offsetof(BattleCommand_t, groups) == 0x04);
static_assert(sizeof(BattleCommand_t) == 0x7C);

struct BattleShape_t {
  /* 0x00 */ u8 _pad00[0x20];
  /* 0x20 */ be_u32 mode;
  /* 0x24 */ u8 _pad24[0x28 - 0x24];
  /* 0x28 */ be_u32 enabled;
};
static_assert(offsetof(BattleShape_t, mode) == 0x20);
static_assert(offsetof(BattleShape_t, enabled) == 0x28);

struct EnemyGroup_t {
  /* 0x00 */ u8 _pad00[0xF0];
  /* 0xF0 */ be_u32 next;
  /* 0xF4 */ u8 _padF4[0xFC - 0xF4];
  /* 0xFC */ be_u32 enemyHead;
};
static_assert(offsetof(EnemyGroup_t, next) == 0xF0);
static_assert(offsetof(EnemyGroup_t, enemyHead) == 0xFC);

struct BattleTask_t {
  /* 0x000 */ u8 _pad000[0x074];
  /* 0x074 */ be_u32 combinedNum;
  /* 0x078 */ be_u32 partyHead; // chain PlyTask_t::nextParty
  /* 0x07C */ be_u32 enemyGroups;
  /* 0x080 */ be_u32 loadState;
  /* 0x084 */ u8 _pad084[0x094 - 0x084];
  /* 0x094 */ be_u32 phase;
  /* 0x098 */ be_u32 subPhase;
  /* 0x09C */ u8 _pad09C[0x11C - 0x09C];
  /* 0x11C */ mem::GuestVec<BattleCommand_t> commands;
  /* 0x128 */ u8 _pad128[0x1C0 - 0x128];
  /* 0x1C0 */ u8 selection[0x1C];
  /* 0x1DC */ u8 _pad1DC[0x1F8 - 0x1DC];
  /* 0x1F8 */ be_u32 actionStep;
  /* 0x1FC */ u8 _pad1FC[0x230 - 0x1FC];
  /* 0x230 */ be_u32 currentActor;
  /* 0x234 */ u8 _pad234[0x244 - 0x234];
  /* 0x244 */ be_u32 targetShape;
};
static_assert(offsetof(BattleTask_t, combinedNum) == 0x074);
static_assert(offsetof(BattleTask_t, partyHead) == 0x078);
static_assert(offsetof(BattleTask_t, enemyGroups) == 0x07C);
static_assert(offsetof(BattleTask_t, loadState) == 0x080);
static_assert(offsetof(BattleTask_t, phase) == 0x094);
static_assert(offsetof(BattleTask_t, subPhase) == 0x098);
static_assert(offsetof(BattleTask_t, commands) == 0x11C);
static_assert(offsetof(BattleTask_t, selection) == 0x1C0);
static_assert(sizeof(BattleTask_t::selection) == 0x1C);
static_assert(offsetof(BattleTask_t, actionStep) == 0x1F8);
static_assert(offsetof(BattleTask_t, currentActor) == 0x230);
static_assert(offsetof(BattleTask_t, targetShape) == 0x244);

constexpr u32 kLoadStateLoaded = 0;

constexpr size_t kMaxGroups = 16;
constexpr size_t kMaxEnemies = 64;
constexpr size_t kMembersPerGroup = 2;

// Engine status indices are the bit positions of CharaStatus. Everything except
// the flag word itself passes a status around as its index.
constexpr u32 kStatusIndexKnockedOut = 10;
static_assert(1u << kStatusIndexKnockedOut ==
              StatusBit(CharaStatus::kKnockedOut));

// Read by the console and the state readers off the engine thread, so the
// address and the step it was taken in move together in one word.
std::atomic<u64> g_captured{0};
std::atomic<u32> g_gameStep{0};
constexpr u32 kStepShift = 32;

// The state holds at game over for as long as the screen is up, so the publish
// rides the entry into it. One thread only, hence a plain bool.
bool g_gameOverShown = false;

BattleTask Captured() {
  // The manager is only valid while the battle view task is live.
  if (!Game::Get().BattleCameraTask()) {
    g_captured.store(0, std::memory_order_relaxed);
    return {};
  }
  // bdBattleSceneUpdate runs inside the engine step, before any host reader.
  // A capture from an earlier step means the battle scene no longer updates
  // (teardown) and the address must not be dereferenced.
  const u64 packed = g_captured.load(std::memory_order_relaxed);
  const u32 address = static_cast<u32>(packed);
  if (!address || static_cast<u32>(packed >> kStepShift) !=
                      g_gameStep.load(std::memory_order_relaxed))
    return {};
  return BattleTask(address);
}

// Safe with a DEAD or null address, which clears the capture.
void OnBattleManagerSeen(u32 address) {
  const u64 packed =
      BattleTask(address)
          ? (u64{g_gameStep.load(std::memory_order_relaxed)} << kStepShift) |
                address
          : 0;
  g_captured.store(packed, std::memory_order_relaxed);
}

// Every enemy reachable from the manager, in group then chain order.
std::vector<EneTask> EnemiesIn(const BattleTask &task) {
  std::vector<EneTask> out;
  size_t groups = 0;
  for (const Task &group : task.EnemyGroups()) {
    if (groups++ >= kMaxGroups)
      break;
    for (const EneTask &enemy : BattleTask::EnemiesOf(group)) {
      if (out.size() >= kMaxEnemies)
        break;
      out.push_back(enemy);
    }
  }
  return out;
}

// The enemy behind a battle params block, or an empty handle for anything
// else. The engine hands its status API the params rather than the task, and a
// party member has the same layout under it, so this uses the kind test the
// engine's own battle code uses.
EneTask EnemyFromParams(u32 params) {
  const Chara chara = Chara::FromParams(params);
  return chara.IsEnemy() ? EneTask::FromChara(chara) : EneTask();
}

// The party member behind the same block, or an empty handle. There is no
// mirror of the kind test here, because TemplateChara_Npc's constructor stamps
// an NPC with the same kind a party member carries. Roster membership is the
// positive test, and it reaches the battle because BattleCharaTask_Construct is
// handed the field PlyTask's own Chara rather than a battle-local copy.
PlyTask MemberFromParams(u32 params) {
  const PlyTask task = PlyTask::FromChara(Chara::FromParams(params));
  return Game::Get().FieldPlayerEntity().Roster().Contains(task.Address())
             ? task
             : PlyTask();
}

} // namespace

u32 BattleShape::Mode() const {
  const auto *self = Self<BattleShape_t>();
  return self ? static_cast<u32>(self->mode) : 0;
}

bool BattleShape::Enabled() const {
  const auto *self = Self<BattleShape_t>();
  return self && static_cast<u32>(self->enabled) != 0;
}

u32 BattleMember::ActorAddress() const {
  const auto *self = Self<BattleMember_t>();
  return self ? static_cast<u32>(self->actor) : 0;
}

size_t BattleGroup::MemberCount() const {
  const auto *self = Self<BattleGroup_t>();
  return self ? std::min<size_t>(self->members.size(), kMembersPerGroup) : 0;
}

BattleMember BattleGroup::MemberAt(size_t i) const {
  const auto *self = Self<BattleGroup_t>();
  if (!self || i >= std::min<size_t>(self->members.size(), kMembersPerGroup))
    return {};
  return BattleMember(self->members.address(static_cast<u32>(i)));
}

size_t BattleCommand::GroupCount() const {
  const auto *self = Self<BattleCommand_t>();
  return self ? self->groups.size() : 0;
}

BattleGroup BattleCommand::GroupAt(size_t i) const {
  const auto *self = Self<BattleCommand_t>();
  if (!self || i >= self->groups.size())
    return {};
  return BattleGroup(self->groups.address(static_cast<u32>(i)));
}

void BattleTask::OnBattleGameStep() {
  g_gameStep.fetch_add(1, std::memory_order_relaxed);
}

u32 BattleTask::CombinedNum() const {
  const auto *self = Self<BattleTask_t>();
  return self ? static_cast<u32>(self->combinedNum) : kNoPhase;
}

bool BattleTask::ResourcesLoaded() const {
  const auto *self = Self<BattleTask_t>();
  return self && static_cast<u32>(self->loadState) == kLoadStateLoaded;
}

u32 BattleTask::Phase() const {
  const auto *self = Self<BattleTask_t>();
  return self ? static_cast<u32>(self->phase) : kNoPhase;
}

u32 BattleTask::SubPhase() const {
  const auto *self = Self<BattleTask_t>();
  return self ? static_cast<u32>(self->subPhase) : kNoPhase;
}

u32 BattleTask::ActionStep() const {
  const auto *self = Self<BattleTask_t>();
  return self ? static_cast<u32>(self->actionStep) : kNoPhase;
}

Task BattleTask::CurrentActor() const {
  const auto *self = Self<BattleTask_t>();
  return Task(self ? static_cast<u32>(self->currentActor) : 0);
}

List<Task> BattleTask::EnemyGroups() const {
  const auto *self = Self<BattleTask_t>();
  return List<Task>(self ? static_cast<u32>(self->enemyGroups) : 0,
                    offsetof(EnemyGroup_t, next));
}

List<EneTask> BattleTask::EnemiesOf(const Task &group) {
  const auto *self = mem::try_at<const EnemyGroup_t>(group.Address());
  return self ? EneTask::GroupFrom(static_cast<u32>(self->enemyHead))
              : List<EneTask>();
}

size_t BattleTask::EnemyCount() const { return EnemiesIn(*this).size(); }

EneTask BattleTask::EnemyAt(size_t i) const {
  const std::vector<EneTask> enemies = EnemiesIn(*this);
  return i < enemies.size() ? enemies[i] : EneTask();
}

size_t BattleTask::CommandCount() const {
  const auto *self = Self<BattleTask_t>();
  return self ? self->commands.size() : 0;
}

BattleCommand BattleTask::CommandAt(size_t i) const {
  const auto *self = Self<BattleTask_t>();
  if (!self || i >= self->commands.size())
    return {};
  return BattleCommand(self->commands.address(static_cast<u32>(i)));
}

BattleShape BattleTask::TargetShape() const {
  const auto *self = Self<BattleTask_t>();
  return BattleShape(self ? static_cast<u32>(self->targetShape) : 0);
}

u32 BattleTask::SelectionAddress() const {
  const u32 address = Address();
  return address ? address + offsetof(BattleTask_t, selection) : 0;
}

engine::BattleTask Game::BattleTask() const { return Captured(); }

} // namespace bd::engine

// BattleTask has no root global. The per-frame scene update is the one reliable
// place it appears as 'this' (r3), so the root is captured here.
//
// Raw, on the inherited context: a typed REX_IMPORT re-roots the engine stack
// at ThreadState's r1 and overwrites the frames live underneath it.
REX_EXTERN(__imp__bdBattleSceneUpdate);
REX_HOOK_RAW(bdBattleSceneUpdate) {
  bd::engine::OnBattleManagerSeen(ctx.r3.u32);
  __imp__bdBattleSceneUpdate(ctx, base);
}

// Mirrors the scene state machine after each update, so the GAME OVER screen is
// caught the frame it comes up. The task is taken from r3 first because the
// original returns over it.
REX_EXTERN(__imp__ScriptManTask__Update);
REX_HOOK_RAW(ScriptManTask__Update) {
  const bd::engine::ScriptManTask scene(ctx.r3.u32);
  __imp__ScriptManTask__Update(ctx, base);
  const bool gameOver = scene.SceneState() == bd::engine::kSceneStateGameOver;
  if (gameOver && !bd::engine::g_gameOverShown)
    bd::engine::Events::Publish(bd::engine::GameOverShown{});
  bd::engine::g_gameOverShown = gameOver;
}

// The EneTask append that ends every enemy spawn. The task is 0 on the
// allocation failure path, which still runs the append.
void bdEnemySpawnLinkHook(PPCRegister &r3) {
  const bd::engine::EneTask task(r3.u32);
  if (!task)
    return;
  bd::engine::Events::Publish(bd::engine::EnemySpawned{task.Address()});
}

// Chara_AddStatus, which the whole title funnels a death through: the knocked
// out index is the one that also zeroes current HP. It is generic over every
// combatant, so PlayerDied publishes from here rather than from the field, and
// the two events differ only in which side the block rebases to.
REX_EXTERN(__imp__Chara_AddStatus);
REX_HOOK_RAW(Chara_AddStatus) {
  const u32 params = ctx.r3.u32;
  const bool killing =
      ctx.r4.u32 == bd::engine::kStatusIndexKnockedOut &&
      !bd::engine::Chara::FromParams(params).HasStatus(
          bd::engine::CharaStatus::kKnockedOut);
  const bd::engine::EneTask enemy =
      killing ? bd::engine::EnemyFromParams(params) : bd::engine::EneTask{};
  const bd::engine::PlyTask member =
      killing && !enemy ? bd::engine::MemberFromParams(params)
                        : bd::engine::PlyTask{};

  __imp__Chara_AddStatus(ctx, base);

  if (enemy)
    bd::engine::Events::Publish(bd::engine::EnemyKilled{enemy.Address()});
  else if (member)
    bd::engine::Events::Publish(bd::engine::PlayerDied{member.Address()});
}

// Reached only for battle event scenes with step id 76, which is a Corporeal
// special attack cinematic beginning and pack\summon\swNN.ipk loading.
REX_EXTERN(__imp__bdSummonPackLoad);
REX_HOOK_RAW(bdSummonPackLoad) {
  bd::engine::Events::Publish(bd::engine::SummonBegan{});
  __imp__bdSummonPackLoad(ctx, base);
}
