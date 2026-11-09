/**
 * @file    engine/state_commands.cpp
 * @brief   Console commands under the bd scope for the engine state. A bare
 *          noun reads, a set_ prefix writes.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#include "core/cvar_table.h"

#include "core/logging.h"
#include "engine/effect_names.h"
#include "engine/game.h"
#include "engine/game_options.h"
#include "engine/game_tables.h"
#include "engine/stat_breakdown.h"

using bd::engine::Game;

namespace {

std::vector<long> ParseInts(std::string_view a) {
  std::vector<long> out;
  size_t i = 0;
  while (i < a.size()) {
    while (i < a.size() && std::isspace(static_cast<unsigned char>(a[i])))
      ++i;
    size_t j = i;
    while (j < a.size() && !std::isspace(static_cast<unsigned char>(a[j])))
      ++j;
    if (j > i) {
      long v = 0;
      if (std::from_chars(a.data() + i, a.data() + j, v).ec == std::errc())
        out.push_back(v);
    }
    i = j;
  }
  return out;
}

// " [poison, petrify]", or empty when nothing is set.
std::string StatusNames(u32 flags) {
  std::string out;
  for (const auto bit : bd::engine::kCharaStatusBits) {
    if (!bd::engine::HasStatus(flags, bit))
      continue;
    out += out.empty() ? " [" : ", ";
    out += bd::engine::ToString(bit);
  }
  if (!out.empty())
    out += ']';
  return out;
}

} // namespace

void bd::commands::GameMode(std::string_view) {
  const auto &g = Game::Get();
  if (!g.IsReady()) {
    REXLOG_WARN(bd, "game engine memory unavailable");
    return;
  }
  // SceneState() is kSceneStateNone off-field. Printing the raw u32 there
  // reads as 4294967295, so name the absence instead.
  const auto scene = g.ScriptManTask();
  const std::string sceneState =
      scene ? std::to_string(scene.SceneState()) : "none";
  REXLOG_INFO(bd, "game mode={} fieldActive={} fieldState={} inBattle={} "
          "loading={} mindows={}",
          bd::engine::ToString(g.Mode()), g.FieldGameplayActive(),
          sceneState, static_cast<bool>(g.BattleCameraTask()),
          g.IsLoading(), g.MindowsPanelActive());
  const auto script = g.ScriptManTask().Script();
  REXLOG_INFO(bd, 
      "game stage cat={} combined={} (area {} sub {}) module=0x{:08X}",
      script.Category(), script.CombinedNum(), script.Area(), script.Sub(),
      g.SequenceControl().CurrentModuleAddress());
  REXLOG_INFO(bd, "game eventScene={} sofdecMovie={}",
          bd::engine::IssEvent::LiveCount() > 0,
          bd::engine::SofdecPlayer::Playing());
}


void bd::commands::GameStage(std::string_view) {
  const auto script = Game::Get().ScriptManTask().Script();
  if (!script) {
    REXLOG_WARN(bd, "game no stage (not in field)");
    return;
  }
  REXLOG_INFO(bd, "game stage {} category={} combinedNum={} area={} sub={}",
          script.Name(), script.Category(), script.CombinedNum(),
          script.Area(), script.Sub());
}


void bd::commands::GameCutscene(std::string_view) {
  const auto event = Game::Get().IssEvent();
  const auto movie = Game::Get().SofdecPlayer();
  if (event) {
    const std::string prefix = event.Prefix();
    REXLOG_INFO(bd, "game event scene {}{:03d}_evt_{:02d} (id {}) task=0x{:08X} "
            "live={}",
            prefix.empty() ? "??" : prefix.c_str(), event.EventNumber(),
            event.SceneNumber(), event.EventId(), event.Address(),
            bd::engine::IssEvent::LiveCount());
  } else {
    REXLOG_INFO(bd, "game event scene: none");
  }
  REXLOG_INFO(bd, "game sofdec movie: {} (status {})",
          movie ? "playing" : "none", movie.Status());
}


void bd::commands::GameLanguage(std::string_view) {
  const auto lang = Game::Get().Language();
  if (!lang) {
    REXLOG_WARN(bd, "game bd_boot.ini languages not parsed yet");
    return;
  }
  std::string available;
  for (u32 i = 0; i < bd::engine::kLocaleCount; ++i) {
    const bd::engine::Locale l{i};
    if (!lang.IsAvailable(l))
      continue;
    if (!available.empty())
      available += ' ';
    available += l.Code();
  }
  REXLOG_INFO(bd, "game languages available: {}", available);
  REXLOG_INFO(bd, "game default={} ({}) current={} ({})", lang.Default().Code(),
          lang.Default().Name(), lang.Current().Code(),
          lang.Current().Name());
}


void bd::commands::GameParty(std::string_view) {
  const auto entity = Game::Get().FieldPlayerEntity();
  const auto party = entity.Party();
  if (!party) {
    REXLOG_WARN(bd, "game no party (no field player entity)");
    return;
  }
  REXLOG_INFO(bd, 
      "game active party: {} member(s), {} participating, leader slot {}",
      party.Size(), entity.ActiveCount(),
      entity.Leader().Chara().SlotId());
  for (const auto &member : party) {
    const auto c = member.Chara();
    REXLOG_INFO(bd, "game  slot {} lv {} exp {} HP {}/{} MP {}/{}{}", c.SlotId(),
            c.Level(), c.Exp(), c.HP(), c.MaxHP(), c.MP(), c.MaxMP(),
            c.IsAlive() ? "" : " [KO]");
  }
}


void bd::commands::GameRoster(std::string_view) {
  const auto roster = Game::Get().FieldPlayerEntity().Roster();
  if (!roster) {
    REXLOG_WARN(bd, "game no roster");
    return;
  }
  REXLOG_INFO(bd, "game roster: {} member(s), unlockedSlots=0x{:X}",
          roster.Size(), Game::Get().GameTask().UnlockedSlots());
  for (const auto &member : roster) {
    const auto c = member.Chara();
    REXLOG_INFO(bd, "game  slot {} flags=0x{:X}{}", c.SlotId(), c.StatusFlags(),
            StatusNames(c.StatusFlags()));
  }
}


void bd::commands::GameEffects(std::string_view args) {
  const auto party = Game::Get().FieldPlayerEntity().Party();
  if (!party) {
    REXLOG_WARN(bd, "effects no party (no field player entity)");
    return;
  }
  const auto idx = ParseInts(args);
  const size_t want = idx.empty() ? 0 : static_cast<size_t>(idx[0]);
  if (want >= party.Size()) {
    REXLOG_WARN(bd, "effects no party member at index {}", want);
    return;
  }
  const auto c = party.At(want).Chara();
  const u32 flags = c.StatusFlags();
  REXLOG_INFO(bd, "effects slot {} HP {}/{} status=0x{:X}{}", c.SlotId(), c.HP(),
          c.MaxHP(), flags, StatusNames(flags));
  REXLOG_INFO(bd, "effects paralyzeTurns={} stunTurns={}", c.ParalyzeTurns(),
          c.StunTurns());
  const auto passive = c.PassiveEffects();
  REXLOG_INFO(bd, "effects passive n={}", passive.size());
  for (const auto &e : passive)
    REXLOG_INFO(bd, "effects   type={} value={}", e.type, e.value);
  const auto timed = c.TimedEffects();
  REXLOG_INFO(bd, "effects timed n={}", timed.size());
  for (const auto &e : timed)
    REXLOG_INFO(bd, "effects   type={} value={} extra={}", e.type, e.value,
            e.extra);
}


void bd::commands::GameTableName(std::string_view args) {
  const auto idx = ParseInts(args);
  if (idx.empty()) {
    REXLOG_WARN(bd, "tables usage: game_tablename <id>");
    return;
  }
  const u32 id = static_cast<u32>(idx[0]);
  const auto &t = bd::engine::GameTables::Get();
  REXLOG_INFO(bd, "tables id {} phenome rec=0x{:X} name=\"{}\"", id,
          t.PhenomeRecord(id), t.PhenomeName(id));
  REXLOG_INFO(bd, "tables id {} item    rec=0x{:X} name=\"{}\"", id,
          t.ItemRecord(id), t.ItemName(id));
}


void bd::commands::GameStats(std::string_view args) {
  const auto party = Game::Get().FieldPlayerEntity().Party();
  if (!party) {
    REXLOG_WARN(bd, "stats no party (no field player entity)");
    return;
  }
  const auto idx = ParseInts(args);
  const size_t want = idx.empty() ? 0 : static_cast<size_t>(idx[0]);
  if (want >= party.Size()) {
    REXLOG_WARN(bd, "stats no party member at index {}", want);
    return;
  }
  const auto c = party.At(want).Chara();
  const auto b = bd::engine::StatBreakdown::For(c);
  if (!b) {
    REXLOG_WARN(bd, "stats breakdown unavailable for slot {}", c.SlotId());
    return;
  }
  REXLOG_INFO(bd, "stats slot {} lv {} newTables={} classGate={} slots={}",
          c.SlotId(), c.Level(), b.UsesNewTables(), b.ClassBonusActive(),
          b.SlotCount());
  for (const auto &s : b.Contributions()) {
    const u32 withActive = s.base + s.classActive + s.permanent;
    const u32 withBest = s.base + s.classBest + s.permanent;
    const char *verdict = s.final == withActive ? "active"
                          : s.final == withBest ? "best"
                                                : "MISMATCH";
    REXLOG_INFO(bd, "stats   {:<12} final={:<6} base={:<6} clsAct={:<6} "
            "clsBest={:<6} perm={:<6} -> {} (act={} best={}) winner={}",
            s.name, s.final, s.base, s.classActive, s.classBest,
            s.permanent, verdict, withActive, withBest,
            s.winner ? bd::engine::ToString(*s.winner) : "-");
  }
  for (const auto &it : b.Items())
    REXLOG_INFO(bd, "stats   acc{} id={} \"{}\"", it.slotIndex, it.itemId,
            it.name);
  for (const auto &s : b.Skills()) {
    const char *label = bd::engine::EffectTypeLabel(s.effectType);
    REXLOG_INFO(bd, "stats   slot{} class={} id={} \"{}\" type={} ({}) value={}",
            s.slotIndex,
            s.sourceClass ? bd::engine::ToString(*s.sourceClass) : "-",
            s.skillId, s.name, s.effectType, label ? label : "unnamed",
            s.value);
  }
}


void bd::commands::GameCharacter(std::string_view args) {
  const auto ints = ParseInts(args);
  if (ints.empty()) {
    REXLOG_WARN(bd, "game usage: game_character <activeIndex>");
    return;
  }
  // Clamping a negative index to 0 would answer with the leader under the
  // wrong label, so reject it the way the index walk used to.
  if (ints[0] < 0) {
    REXLOG_WARN(bd, "game no active character at index {}", ints[0]);
    return;
  }
  const size_t idx = static_cast<size_t>(ints[0]);
  const auto c = Game::Get().FieldPlayerEntity().Party().At(idx).Chara();
  if (!c) {
    REXLOG_WARN(bd, "game no active character at index {}", idx);
    return;
  }
  const auto s = c.Stats();
  const auto active = c.ActiveClass();
  REXLOG_INFO(bd, "game character[{}] slot {} lv {} exp {} HP {}/{} MP {}/{}{}",
          idx, c.SlotId(), c.Level(), c.Exp(), c.HP(), c.MaxHP(), c.MP(),
          c.MaxMP(), StatusNames(c.StatusFlags()));
  REXLOG_INFO(bd, "game  atk {} def {} hit {} matk {} mdef {} agi {}", s.attack,
          s.defense, s.hitRate, s.magicAttack, s.magicDefense, s.agility);
  REXLOG_INFO(bd, "game  activeClass={} unlocked=0x{:X}",
          active ? bd::engine::ToString(*active) : "none",
          c.UnlockedClasses());
  for (u32 i = 0; i < bd::engine::kCharaClassCount; ++i) {
    const auto job = static_cast<bd::engine::CharaClass>(i);
    if (!c.IsClassUnlocked(job))
      continue;
    REXLOG_INFO(bd, "game  {} rank {} sp {}", bd::engine::ToString(job),
            c.ClassRank(job), c.ClassSP(job));
  }
}


void bd::commands::GameGold(std::string_view) {
  const auto items = Game::Get().ItemSaveData();
  if (!items) {
    REXLOG_WARN(bd, "game no item save data");
    return;
  }
  REXLOG_INFO(bd, "game gold: {}", items.Gold());
}


void bd::commands::GameInventory(std::string_view) {
  const auto items = Game::Get().ItemSaveData();
  if (!items) {
    REXLOG_WARN(bd, "game no item save data");
    return;
  }
  REXLOG_INFO(bd, "game inventory: {} non-empty slot(s), gold {}",
          items.UsedCount(), items.Gold());
  for (size_t i = 0; i < items.SlotCount(); ++i) {
    const auto it = items.At(i);
    if (it.id == 0)
      continue;
    REXLOG_INFO(bd, "game  slot {} item {} x{}", i, it.id, it.count);
  }
}


void bd::commands::GameBattle(std::string_view) {
  const auto &g = Game::Get();
  const auto battle = g.BattleTask();
  REXLOG_INFO(bd, "game inBattle={} party={} enemies={}",
          static_cast<bool>(g.BattleCameraTask()),
          g.FieldPlayerEntity().Party().Size(), battle.EnemyCount());
  // Absent counters and zeroed counters are different answers, and the
  // record separates them.
  const auto record = g.PlayRecord();
  if (record)
    REXLOG_INFO(bd, "game battlesWon={} escapes={} surroundWins={}",
            record.BattlesWon(), record.Escapes(), record.SurroundWins());
  else
    REXLOG_WARN(bd, "game battle stats unavailable");
  if (!battle) {
    REXLOG_INFO(bd, "game phase/enemies require battle-manager root capture");
    return;
  }
  REXLOG_INFO(bd, 
      "game phase={} sub={} step={} loaded={} combined={} actor=0x{:08X}",
      battle.Phase(), battle.SubPhase(), battle.ActionStep(),
      battle.ResourcesLoaded(), battle.CombinedNum(),
      battle.CurrentActor().Address());
  for (size_t i = 0; i < battle.EnemyCount(); ++i) {
    const auto e = battle.EnemyAt(i).Chara();
    REXLOG_INFO(bd, "game  enemy type {} HP {}/{}{}", e.TypeId(), e.HP(),
            e.MaxHP(), e.IsAlive() ? "" : " [dead]");
  }
}


// ---- setters (verified-safe writes) ----

void bd::commands::GameSetHP(std::string_view args) {
  const auto v = ParseInts(args);
  if (v.size() < 2 || v[0] < 0 || v[1] < 0) {
    REXLOG_WARN(bd, "game usage: game_set_hp <activeIndex> <value>");
    return;
  }
  const auto party = Game::Get().FieldPlayerEntity().Party();
  auto c = party.At(static_cast<size_t>(v[0])).Chara();
  if (!c) {
    REXLOG_WARN(bd, "game no active member at index {}", v[0]);
    return;
  }
  const u32 before = c.HP();
  if (!c.SetHP(static_cast<u32>(v[1]))) {
    REXLOG_WARN(bd, "game write failed");
    return;
  }
  REXLOG_INFO(bd, "game character[{}] HP {} -> {} (max {})", v[0], before, c.HP(),
          c.MaxHP());
}


void bd::commands::GameSetMP(std::string_view args) {
  const auto v = ParseInts(args);
  if (v.size() < 2 || v[0] < 0 || v[1] < 0) {
    REXLOG_WARN(bd, "game usage: game_set_mp <activeIndex> <value>");
    return;
  }
  const auto party = Game::Get().FieldPlayerEntity().Party();
  auto c = party.At(static_cast<size_t>(v[0])).Chara();
  if (!c) {
    REXLOG_WARN(bd, "game no active member at index {}", v[0]);
    return;
  }
  const u32 before = c.MP();
  if (!c.SetMP(static_cast<u32>(v[1]))) {
    REXLOG_WARN(bd, "game write failed");
    return;
  }
  REXLOG_INFO(bd, "game character[{}] MP {} -> {} (max {})", v[0], before, c.MP(),
          c.MaxMP());
}


void bd::commands::GameSetGold(std::string_view args) {
  const auto v = ParseInts(args);
  if (v.empty() || v[0] < 0) {
    REXLOG_WARN(bd, "game usage: game_set_gold <value>");
    return;
  }
  auto items = Game::Get().ItemSaveData();
  const u32 before = items.Gold();
  if (!items.SetGold(static_cast<u32>(v[0]))) {
    REXLOG_WARN(bd, "game no item save data");
    return;
  }
  REXLOG_INFO(bd, "game gold {} -> {}", before, items.Gold());
}


void bd::commands::GameSetItem(std::string_view args) {
  const auto v = ParseInts(args);
  if (v.size() < 3 || v[0] < 0 || v[1] < 0 || v[2] < 0) {
    REXLOG_WARN(bd, "game usage: game_set_item <slot> <itemId> <qty>");
    return;
  }
  auto items = Game::Get().ItemSaveData();
  if (!items.SetAt(static_cast<size_t>(v[0]), static_cast<u32>(v[1]),
                   static_cast<u32>(v[2]))) {
    REXLOG_WARN(bd, "game write failed (bad slot or no item save data)");
    return;
  }
  REXLOG_INFO(bd, "game slot {} = item {} x{}", v[0], v[1], v[2]);
}


void bd::commands::GameOptionsDump(std::string_view) {
  if (!bd::engine::GameOptionsResolved()) {
    REXLOG_WARN(bd, "game config globals unavailable");
    return;
  }
  const auto &o = bd::engine::GameOptions::Get();
  REXLOG_INFO(bd, "game msg_speed={} msg_size={} voice_type={} ruby={} "
          "subtitles={}",
          o.MsgSpeed(), o.MsgSize(), o.VoiceType(), o.Ruby(),
          o.Subtitles());
  REXLOG_INFO(bd, "game audio_hints={} battle_hints={} skip_events={} camera={} "
          "target_first={}",
          o.AudioHints(), o.BattleHints(), o.SkipEvents(), o.Camera(),
          o.TargetFirst());
  REXLOG_INFO(bd, "game music={:.2f} se={:.2f} brightness={:.2f} pos_x={:.2f} "
          "pos_y={:.2f}",
          o.MusicVolume(), o.SeVolume(), o.Brightness(), o.ScreenPosX(),
          o.ScreenPosY());
}

