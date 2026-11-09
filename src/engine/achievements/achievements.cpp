/**
 * @file    engine/achievements/achievements.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/achievements/achievements.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <rex/embedded_metadata.h>
#include <rex/runtime.h>
#include <rex/runtime/achievement_manager.h>

#include "embedded.h"
#include "core/logging.h"
#include "engine/events.h"
#include "engine/game.h"
#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

constexpr std::string_view kPrologueStage = "bg01_01";
constexpr std::string_view kBathroomStage = "bi13h01";
constexpr std::string_view kShuffleDungeonStage = "dg51_01";
constexpr std::string_view kSheepTribeCamp = "bg02_01";
// Custom IDs live at 0x10000+ so they never collide with the title's XDBF
// achievement IDs. Unlock state persists per-profile alongside the title's
// (<user_data_root>/achievements/<titleid>.toml), keyed by ID only, so IDs
// must stay stable across builds.
constexpr u32 kBathroomAchievement = 0x10001;
constexpr u32 kLeWoah = 0x10002;
constexpr u32 kShuffleBaus = 0x10003;
constexpr u32 kFungusCollector = 0x10004;
constexpr u32 kCashman = 0x10005;
constexpr u32 kCloacaMaxima = 0x10006;
constexpr u32 kYUDoDis = 0x10007;
constexpr u32 kBaldTribe = 0x10008;

constexpr u32 kNothingsForFungusCollector = 27;
constexpr u32 kGoldForCashman = 666;

// Poo Snake is em001, and enemy type ids < 10000 map 1:1 to the em number
// (bdEnemyResourceInit formats em%03d from the id below 10000, bs%02d from
// id - 10000 above it).
constexpr u32 kPooSnakeTypeId = 1;

struct CustomAchievement {
  rex::runtime::Achievement info;
  bool registered = false;
  bool done = false;
};

CustomAchievement s_achievements[] = {
    {{kBathroomAchievement, "Where is the bathroom?",
      "Where does Zola sh**? (Dedicated to InfernoZotza)", "???",
      "achievements/0x10001.png", 0, 69, 0}},
    {{kLeWoah, "Le WOooAH",
      "Use a Corporeal attack. (Dedicated to wolfaeterni)", "???",
      "achievements/0x10002.png", 0, 50, 0}},
    {{kShuffleBaus, "Shuffle Baus",
      "Enter the shuffle dungeon. (Dedicated to baus.98)", "???",
      "achievements/0x10003.png", 0, 50, 0}},
    {{kFungusCollector, "The Fungus Collector",
      "Collect 27 Nothings. (Dedicated to Fungus, the king of Nothings)", "???",
      "achievements/0x10004.png", 0, 50, 0}},
    {{kCashman, "The Cashman", "Obtain 666 gold. (Dedicated to griever666)",
      "???", "achievements/0x10005.png", 0, 50, 0}},
    {{kCloacaMaxima, "Cloaca Maxima",
      "Slay a Poo Snake and purge the Great Sewer. (Dedicated to rcold)", "???",
      "achievements/0x10006.png", 0, 420, 0}},
    {{kYUDoDis, "y u do dis",
      "Landshark... y u do dis (Dedicated to Graine25)", "???",
      "achievements/0x10007.png", 0, 50, 0}},
    {{kBaldTribe, "Please Stop Shaving Me",
      "Where did the bald people use to be? (Dedicated to crack - by rcold)", "???",
      "achievements/0x10008.png", 0, 7, 0}},
};

// Not cleared on a return to title: nothing publishes that edge, and clearing
// it from SaveLoaded would race the new game's first StageLoaded.
std::string s_stage;

CustomAchievement *Find(u32 id) {
  for (auto &a : s_achievements)
    if (a.info.id == id)
      return &a;
  return nullptr;
}

void Unlock(u32 id) {
  Achievements::Register();
  CustomAchievement *a = Find(id);
  if (!a || !a->registered || a->done)
    return;
  a->done = true;
  if (auto *rt = bd::mem::runtime())
    (void)rt->GetAchievementManager().UnlockAchievement(
        a->info.id, rex::runtime::AchievementNotification::kShow);
}

// The two predicates that describe a state rather than an edge. Both are
// evaluated on SaveLoaded, catching a save loaded already holding enough gold
// or enough Nothings.
//
// Gold is also evaluated on BattleEnded, because spoils credit it through a
// route with no publisher. That still leaves the shop till, the camp item
// screen and field item drops uncovered, so this predicate is not proof that
// gold never crossed the line, only that it had crossed it at one of the
// moments something looked.
void EvaluateLevelTriggered() {
  if (Game::Get().ItemSaveData().Gold() >= kGoldForCashman)
    Unlock(kCashman);
  const ScriptVars vars = Game::Get().ScriptManTask().Vars();
  if (vars && vars.NothingsCollected() >= kNothingsForFungusCollector)
    Unlock(kFungusCollector);
}

// The two stage predicates. Evaluated on StageLoaded and again on SaveLoaded,
// because save-anywhere puts a save inside either stage and the order the two
// events publish in on a continue is not established.
void EvaluateStageTriggered() {
  if (s_stage == kBathroomStage)
    Unlock(kBathroomAchievement);
  if (s_stage == kShuffleDungeonStage)
    Unlock(kShuffleBaus);
  if (s_stage == kSheepTribeCamp)
    Unlock(kBaldTribe);
}

} // namespace

void Achievements::Register() {
  for (const EmbeddedAsset &icon : EmbeddedGroup("achievements"))
    rex::RegisterEmbeddedMetadataAsset(icon.name, icon.data, icon.size);
  rex::Runtime *runtime = bd::mem::runtime();
  if (!runtime) {
    REXLOG_WARN(bd, "achv runtime not ready, catalog registration deferred");
    return;
  }
  auto &manager = runtime->GetAchievementManager();
  std::vector<rex::runtime::Achievement> infos;
  infos.reserve(std::size(s_achievements));
  for (const CustomAchievement &a : s_achievements)
    infos.push_back(a.info);
  if (auto registered = manager.RegisterAchievements(infos); !registered) {
    REXLOG_WARN(bd, "achv catalog not registered: {}", registered.error().message);
    return;
  }
  for (CustomAchievement &a : s_achievements) {
    a.registered = a.info.id != 0;
    a.done = manager.IsUnlocked(a.info.id);
  }
}

void Achievements::Init() {
  Events::Subscribe<SaveLoaded>([](const SaveLoaded &) {
    Register();
    EvaluateStageTriggered();
    EvaluateLevelTriggered();
  });

  Events::Subscribe<StageLoaded>([](const StageLoaded &e) {
    Register();
    // e.script, never the ScriptManTask's own: the engine appends the
    // incoming Script at the tail of its chain, so the root still resolves the
    // outgoing stage here.
    s_stage = e.script.Name();
    EvaluateStageTriggered();
  });

  Events::Subscribe<SummonBegan>([](const SummonBegan &) { Unlock(kLeWoah); });

  Events::Subscribe<GoldChanged>([](const GoldChanged &e) {
    if (e.current >= kGoldForCashman)
      Unlock(kCashman);
  });

  Events::Subscribe<BattleEnded>(
      [](const BattleEnded &) { EvaluateLevelTriggered(); });

  // Nothings are not inventory items. They are search points that gave nothing,
  // counted in a script global, so ItemGained never sees one.
  Events::Subscribe<NothingCollected>([](const NothingCollected &e) {
    if (e.total >= kNothingsForFungusCollector)
      Unlock(kFungusCollector);
  });

  Events::Subscribe<EnemyKilled>([](const EnemyKilled &e) {
    if (e.enemy.Chara().TypeId() == kPooSnakeTypeId)
      Unlock(kCloacaMaxima);
  });

  Events::Subscribe<GameOverShown>([](const GameOverShown &) {
    if (s_stage == kPrologueStage)
      Unlock(kYUDoDis);
  });
}

} // namespace bd::engine
