/**
 * @file    engine/achievements/achievements.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/achievements/achievements.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <rex/embedded_metadata.h>
#include <rex/system/achievements.h>
#include <rex/system/kernel_state.h>

#include "embedded.h"
#include "core/logging.h"
#include "engine/cheats.h"
#include "engine/events.h"
#include "platform/reboot.h"
#include "engine/field.h"
#include "engine/inventory.h"

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
  rex::system::AchievementInfo info;
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
  rex::system::UnlockAchievement(a->info.id);
}

} // namespace

u32 Achievements::UnlockAll() {
  Register();

  // The whole catalog, not just the rows in s_achievements. That array holds
  // only re:Blue's own additions; the kernel manager also carries the title's
  // XDBF achievements, and it is the manager the viewer enumerates. Awarding
  // just the custom ones left almost every row still locked on screen.
  auto *ks = rex::system::kernel_state();
  if (!ks) {
    BD_CHEAT_DIAG("[cheat-diag] unlock_achievements: no kernel state");
    return 0;
  }
  auto &mgr = ks->achievements();

  u32 awarded = 0;
  u32 total = 0;
  for (const auto &a : mgr.ListAchievements()) {
    ++total;
    if (mgr.IsUnlocked(a.id))
      continue;
    rex::system::UnlockAchievement(a.id);
    ++awarded;
  }

  // Re-sync our own done flags off the store, so a condition that fires later
  // does not try to award something the store already holds.
  for (auto &a : s_achievements)
    a.done = rex::system::IsAchievementUnlocked(a.info.id);

  BD_CHEAT_DIAG("[cheat-diag] unlock_achievements: {} awarded, {} in catalog",
          awarded, total);
  return awarded;
}

u32 Achievements::LockAll() {
  auto *ks = rex::system::kernel_state();
  if (!ks) {
    BD_CHEAT_DIAG("[cheat-diag] reset_achievements: no kernel state");
    return 0;
  }
  auto &mgr = ks->achievements();

  u32 before = 0;
  for (const auto &a : mgr.ListAchievements())
    if (mgr.IsUnlocked(a.id))
      ++before;

  // The store is <profile>/achievements/<titleid>.toml. The title id is not
  // known here, so every .toml in that directory is reset -- and only exactly
  // ".toml", so the hand-made .toml.backup_* copies beside it are left alone.
  std::error_code ec;
  const auto dir = bd::platform::ConfigFilePath().parent_path() / "achievements";
  u32 files = 0;
  for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
    if (!e.is_regular_file(ec) || e.path().extension() != ".toml")
      continue;
    std::ofstream out(e.path(), std::ios::trunc);
    if (!out)
      continue;
    out << "# Achievement unlock state - managed by ReXGlue runtime\n";
    ++files;
  }
  if (ec || files == 0) {
    BD_CHEAT_DIAG("[cheat-diag] reset_achievements: no store found under {}",
            dir.string());
    return before;
  }

  mgr.LoadUnlockState();

  u32 after = 0;
  for (const auto &a : mgr.ListAchievements())
    if (mgr.IsUnlocked(a.id))
      ++after;

  for (auto &a : s_achievements)
    a.done = rex::system::IsAchievementUnlocked(a.info.id);

  BD_CHEAT_DIAG("[cheat-diag] reset_achievements: {} were unlocked, {} file(s) "
          "cleared, {} still unlocked in memory{}",
          before, files, after,
          after ? " [reload merged; restart to see it]" : "");
  return after;
}

namespace {

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
  if (Inventory{}.Gold() >= kGoldForCashman)
    Unlock(kCashman);
  const Field field;
  if (field && field.NothingsCollected() >= kNothingsForFungusCollector)
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
  for (auto &a : s_achievements) {
    if (!rex::system::RegisterAchievement(a.info)) {
      BD_WARN("[achv] runtime not ready, catalog registration deferred");
      return;
    }
    a.registered = true;
    a.done = rex::system::IsAchievementUnlocked(a.info.id);
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
    // e.stage, never e.field.Stage(): the guest appends the incoming Script
    // task at the tail of its chain, so the field still resolves the outgoing
    // stage here.
    s_stage = e.stage.Name();
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
    if (e.enemy.TypeId() == kPooSnakeTypeId)
      Unlock(kCloacaMaxima);
  });

  Events::Subscribe<GameOverShown>([](const GameOverShown &) {
    if (s_stage == kPrologueStage)
      Unlock(kYUDoDis);
  });
}

} // namespace bd::engine
