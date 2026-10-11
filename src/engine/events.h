/**
 * @file    engine/events.h
 * @brief   Engine event bus: what the game announces, and how to hear it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <rex/types.h>

#include "engine/battle_camera_task.h"
#include "engine/ene_task.h"
#include "engine/ply_task.h"
#include "engine/script.h"

namespace bd::engine {

// Publish and subscribe over engine state changes. Three rules bind every user.
//
// Engine thread only. Every publisher is an engine hook, so every subscriber runs
// on the engine thread and hands off its own slow work. The bus does not
// marshal.
//
// Handles are callback-scoped. A PlyTask, EneTask or BattleCameraTask is valid
// for the duration of the call and no longer, so delivery is synchronous
// rather than queued. Extract the scalars you need.
//
// Subscriptions are permanent. There is no unsubscribe, and a subscriber that
// subscribes during a publish appends and does not run that round.

// ---- Battle ----

struct BattleStarted {
  static constexpr const char *kName = "BattleStarted";
  engine::BattleCameraTask battle;
  std::array<u32, 2> Trace() const { return {0, 0}; }
};

struct BattleEnded {
  static constexpr const char *kName = "BattleEnded";
  engine::BattleCameraTask battle;
  std::array<u32, 2> Trace() const { return {0, 0}; }
};

struct EnemySpawned {
  static constexpr const char *kName = "EnemySpawned";
  explicit EnemySpawned(u32 taskAddress) : enemy(taskAddress) {}
  engine::EneTask enemy;
  std::array<u32, 2> Trace() const {
    return {enemy.Chara().TypeId(), enemy.Address()};
  }
};

struct EnemyKilled {
  static constexpr const char *kName = "EnemyKilled";
  explicit EnemyKilled(u32 taskAddress) : enemy(taskAddress) {}
  engine::EneTask enemy;
  std::array<u32, 2> Trace() const {
    return {enemy.Chara().TypeId(), enemy.Address()};
  }
};

struct GameOverShown {
  static constexpr const char *kName = "GameOverShown";
  std::array<u32, 2> Trace() const { return {0, 0}; }
};

struct SummonBegan {
  static constexpr const char *kName = "SummonBegan";
  std::array<u32, 2> Trace() const { return {0, 0}; }
};

// ---- Field and session ----

struct SaveLoaded {
  static constexpr const char *kName = "SaveLoaded";
  std::array<u32, 2> Trace() const { return {0, 0}; }
};

struct StageLoaded {
  static constexpr const char *kName = "StageLoaded";
  engine::Script script;
  std::array<u32, 2> Trace() const {
    return {script.CombinedNum(), script.Category()};
  }
};

// Arrives after StageLoaded for the incoming stage, not before it: the engine
// appends the new Script to the tail of its chain and initializes it there,
// and the outgoing one is only torn down once it reaches its own teardown.
//
// It means the stage was replaced, not that the field is gone. A wholesale
// field shutdown such as a return to title destroys Scripts without this pop.
struct StageUnloading {
  static constexpr const char *kName = "StageUnloading";
  engine::Script script;
  std::array<u32, 2> Trace() const { return {script.CombinedNum(), 0}; }
};

struct PlayerSpawned {
  static constexpr const char *kName = "PlayerSpawned";
  explicit PlayerSpawned(u32 taskAddress) : player(taskAddress) {}
  engine::PlyTask player;
  std::array<u32, 2> Trace() const {
    return {player.Chara().SlotId(), player.Address()};
  }
};

struct PlayerDied {
  static constexpr const char *kName = "PlayerDied";
  explicit PlayerDied(u32 taskAddress) : player(taskAddress) {}
  engine::PlyTask player;
  std::array<u32, 2> Trace() const {
    return {player.Chara().SlotId(), player.Address()};
  }
};

// The lifetime count of search points that turned out to hold nothing, which
// the title keeps in a script global of its own and the Nothing Man reads back
// in his dialogue. Carries the new total, the one number a subscriber wants.
struct NothingCollected {
  static constexpr const char *kName = "NothingCollected";
  u32 total = 0;
  std::array<u32, 2> Trace() const { return {total, 0}; }
};

// ---- Inventory ----

// GoldChanged publishes only from the script gold opcode, and ItemGained only
// from the script item opcode. engine/item_save_data.cpp names the routes that
// reach the same save data without publishing.
struct GoldChanged {
  static constexpr const char *kName = "GoldChanged";
  u32 previous = 0;
  u32 current = 0;
  std::array<u32, 2> Trace() const { return {previous, current}; }
};

struct ItemGained {
  static constexpr const char *kName = "ItemGained";
  u32 itemId = 0;
  u32 count = 0;
  std::array<u32, 2> Trace() const { return {itemId, count}; }
};

// ---- Party ----

struct PartyMemberAdded {
  static constexpr const char *kName = "PartyMemberAdded";
  explicit PartyMemberAdded(u32 taskAddress) : member(taskAddress) {}
  engine::PlyTask member;
  std::array<u32, 2> Trace() const {
    return {member.Chara().SlotId(), member.Address()};
  }
};

struct PartyLeaderChanged {
  static constexpr const char *kName = "PartyLeaderChanged";
  explicit PartyLeaderChanged(u32 taskAddress) : leader(taskAddress) {}
  engine::PlyTask leader;
  std::array<u32, 2> Trace() const {
    return {leader.Chara().SlotId(), leader.Address()};
  }
};

// ---- Cutscene ----

struct CutsceneStarted {
  static constexpr const char *kName = "CutsceneStarted";
  i32 eventId = -1;
  std::array<u32, 2> Trace() const {
    return {static_cast<u32>(eventId), 0};
  }
};

struct CutsceneEnded {
  static constexpr const char *kName = "CutsceneEnded";
  i32 eventId = -1;
  std::array<u32, 2> Trace() const {
    return {static_cast<u32>(eventId), 0};
  }
};

class Events {
public:
  template <class E> static void Subscribe(std::function<void(const E &)> cb) {
    Subscribers<E>().push_back(std::move(cb));
  }

  template <class E> static void Publish(const E &e) {
    Record(E::kName, e.Trace());
    // Count taken first and indexed rather than iterated: a subscriber may
    // publish or subscribe, either of which can grow the vector underneath.
    auto &subs = Subscribers<E>();
    const size_t count = subs.size();
    for (size_t i = 0; i < count; ++i)
      subs[i](e);
  }

private:
  template <class E>
  static std::vector<std::function<void(const E &)>> &Subscribers() {
    static std::vector<std::function<void(const E &)>> subs;
    return subs;
  }

  static void Record(const char *name, std::array<u32, 2> trace);
};

// One published event, as the game_events command prints it.
struct EventTrace {
  const char *name = nullptr;
  u64 tick = 0;
  u32 a = 0;
  u32 b = 0;
};

// The trace ring, oldest first, holding at most kCapacity entries.
class EventLog {
public:
  static constexpr size_t kCapacity = 64;

  static std::vector<EventTrace> Recent();
  static u64 TotalPublished();
};

} // namespace bd::engine
