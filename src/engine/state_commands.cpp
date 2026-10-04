/**
 * @file    engine/state_commands.cpp
 * @brief   Console commands in the GameState category. Names use a game_
 *          prefix because the REXCVAR macro stringizes the identifier, so
 *          dotted names are not possible. A bare noun reads, a set_ prefix
 *          writes.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>

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

REXCVAR_DEFINE_COMMAND_ARGS(
    game_mode,
    [](std::string_view) {
      const auto &g = Game::Get();
      if (!g.IsReady()) {
        BD_WARN("[game] engine memory unavailable");
        return;
      }
      // SceneState() is kSceneStateNone off-field. Printing the raw u32 there
      // reads as 4294967295, so name the absence instead.
      const auto scene = g.ScriptManTask();
      const std::string sceneState =
          scene ? std::to_string(scene.SceneState()) : "none";
      BD_INFO("[game] mode={} fieldActive={} fieldState={} inBattle={} "
              "loading={} mindows={}",
              bd::engine::ToString(g.Mode()), g.FieldGameplayActive(),
              sceneState, static_cast<bool>(g.BattleCameraTask()),
              g.IsLoading(), g.MindowsPanelActive());
      const auto script = g.ScriptManTask().Script();
      BD_INFO(
          "[game] stage cat={} combined={} (area {} sub {}) module=0x{:08X}",
          script.Category(), script.CombinedNum(), script.Area(), script.Sub(),
          g.SequenceControl().CurrentModuleAddress());
      BD_INFO("[game] eventScene={} sofdecMovie={}",
              bd::engine::IssEvent::LiveCount() > 0,
              bd::engine::SofdecPlayer::Playing());
    },
    "GameState", "Dump engine-state predicates + current stage");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_stage,
    [](std::string_view) {
      const auto script = Game::Get().ScriptManTask().Script();
      if (!script) {
        BD_WARN("[game] no stage (not in field)");
        return;
      }
      BD_INFO("[game] stage {} category={} combinedNum={} area={} sub={}",
              script.Name(), script.Category(), script.CombinedNum(),
              script.Area(), script.Sub());
    },
    "GameState", "Print the current stage name, category and id");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_cutscene,
    [](std::string_view) {
      const auto event = Game::Get().IssEvent();
      const auto movie = Game::Get().SofdecPlayer();
      if (event) {
        const std::string prefix = event.Prefix();
        BD_INFO("[game] event scene {}{:03d}_evt_{:02d} (id {}) task=0x{:08X} "
                "live={}",
                prefix.empty() ? "??" : prefix.c_str(), event.EventNumber(),
                event.SceneNumber(), event.EventId(), event.Address(),
                bd::engine::IssEvent::LiveCount());
      } else {
        BD_INFO("[game] event scene: none");
      }
      BD_INFO("[game] sofdec movie: {} (status {})",
              movie ? "playing" : "none", movie.Status());
    },
    "GameState", "Report engine event (.evt) and Sofdec movie (.sfd) playback");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_language,
    [](std::string_view) {
      const auto lang = Game::Get().Language();
      if (!lang) {
        BD_WARN("[game] bd_boot.ini languages not parsed yet");
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
      BD_INFO("[game] languages available: {}", available);
      BD_INFO("[game] default={} ({}) current={} ({})", lang.Default().Code(),
              lang.Default().Name(), lang.Current().Code(),
              lang.Current().Name());
    },
    "GameState", "Print the installed UI languages and the latched locale");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_party,
    [](std::string_view) {
      const auto entity = Game::Get().FieldPlayerEntity();
      const auto party = entity.Party();
      if (!party) {
        BD_WARN("[game] no party (no field player entity)");
        return;
      }
      BD_INFO(
          "[game] active party: {} member(s), {} participating, leader slot {}",
          party.Size(), entity.ActiveCount(),
          entity.Leader().Chara().SlotId());
      for (const auto &member : party) {
        const auto c = member.Chara();
        BD_INFO("[game]  slot {} lv {} exp {} HP {}/{} MP {}/{}{}", c.SlotId(),
                c.Level(), c.Exp(), c.HP(), c.MaxHP(), c.MP(), c.MaxMP(),
                c.IsAlive() ? "" : " [KO]");
      }
    },
    "GameState", "List active party members and their stats");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_roster,
    [](std::string_view) {
      const auto roster = Game::Get().FieldPlayerEntity().Roster();
      if (!roster) {
        BD_WARN("[game] no roster");
        return;
      }
      BD_INFO("[game] roster: {} member(s), unlockedSlots=0x{:X}",
              roster.Size(), Game::Get().GameTask().UnlockedSlots());
      for (const auto &member : roster) {
        const auto c = member.Chara();
        BD_INFO("[game]  slot {} flags=0x{:X}{}", c.SlotId(), c.StatusFlags(),
                StatusNames(c.StatusFlags()));
      }
    },
    "GameState", "List the full character roster incl. reserves");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_effects,
    [](std::string_view args) {
      const auto party = Game::Get().FieldPlayerEntity().Party();
      if (!party) {
        BD_WARN("[effects] no party (no field player entity)");
        return;
      }
      const auto idx = ParseInts(args);
      const size_t want = idx.empty() ? 0 : static_cast<size_t>(idx[0]);
      if (want >= party.Size()) {
        BD_WARN("[effects] no party member at index {}", want);
        return;
      }
      const auto c = party.At(want).Chara();
      const u32 flags = c.StatusFlags();
      BD_INFO("[effects] slot {} HP {}/{} status=0x{:X}{}", c.SlotId(), c.HP(),
              c.MaxHP(), flags, StatusNames(flags));
      BD_INFO("[effects] paralyzeTurns={} stunTurns={}", c.ParalyzeTurns(),
              c.StunTurns());
      const auto passive = c.PassiveEffects();
      BD_INFO("[effects] passive n={}", passive.size());
      for (const auto &e : passive)
        BD_INFO("[effects]   type={} value={}", e.type, e.value);
      const auto timed = c.TimedEffects();
      BD_INFO("[effects] timed n={}", timed.size());
      for (const auto &e : timed)
        BD_INFO("[effects]   type={} value={} extra={}", e.type, e.value,
                e.extra);
    },
    "GameState", "Effect lists and status timers: game_effects [index]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_tablename,
    [](std::string_view args) {
      const auto idx = ParseInts(args);
      if (idx.empty()) {
        BD_WARN("[tables] usage: game_tablename <id>");
        return;
      }
      const u32 id = static_cast<u32>(idx[0]);
      const auto &t = bd::engine::GameTables::Get();
      BD_INFO("[tables] id {} phenome rec=0x{:X} name=\"{}\"", id,
              t.PhenomeRecord(id), t.PhenomeName(id));
      BD_INFO("[tables] id {} item    rec=0x{:X} name=\"{}\"", id,
              t.ItemRecord(id), t.ItemName(id));
    },
    "GameState", "Look an id up in the phenome and item tables");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_stats,
    [](std::string_view args) {
      const auto party = Game::Get().FieldPlayerEntity().Party();
      if (!party) {
        BD_WARN("[stats] no party (no field player entity)");
        return;
      }
      const auto idx = ParseInts(args);
      const size_t want = idx.empty() ? 0 : static_cast<size_t>(idx[0]);
      if (want >= party.Size()) {
        BD_WARN("[stats] no party member at index {}", want);
        return;
      }
      const auto c = party.At(want).Chara();
      const auto b = bd::engine::StatBreakdown::For(c);
      if (!b) {
        BD_WARN("[stats] breakdown unavailable for slot {}", c.SlotId());
        return;
      }
      BD_INFO("[stats] slot {} lv {} newTables={} classGate={} slots={}",
              c.SlotId(), c.Level(), b.UsesNewTables(), b.ClassBonusActive(),
              b.SlotCount());
      for (const auto &s : b.Contributions()) {
        const u32 withActive = s.base + s.classActive + s.permanent;
        const u32 withBest = s.base + s.classBest + s.permanent;
        const char *verdict = s.final == withActive ? "active"
                              : s.final == withBest ? "best"
                                                    : "MISMATCH";
        BD_INFO("[stats]   {:<12} final={:<6} base={:<6} clsAct={:<6} "
                "clsBest={:<6} perm={:<6} -> {} (act={} best={}) winner={}",
                s.name, s.final, s.base, s.classActive, s.classBest,
                s.permanent, verdict, withActive, withBest,
                s.winner ? bd::engine::ToString(*s.winner) : "-");
      }
      for (const auto &it : b.Items())
        BD_INFO("[stats]   acc{} id={} \"{}\"", it.slotIndex, it.itemId,
                it.name);
      for (const auto &s : b.Skills()) {
        const char *label = bd::engine::EffectTypeLabel(s.effectType);
        BD_INFO("[stats]   slot{} class={} id={} \"{}\" type={} ({}) value={}",
                s.slotIndex,
                s.sourceClass ? bd::engine::ToString(*s.sourceClass) : "-",
                s.skillId, s.name, s.effectType, label ? label : "unnamed",
                s.value);
      }
    },
    "GameState", "Stat contribution breakdown: game_stats [index]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_character,
    [](std::string_view args) {
      const auto ints = ParseInts(args);
      if (ints.empty()) {
        BD_WARN("[game] usage: game_character <activeIndex>");
        return;
      }
      // Clamping a negative index to 0 would answer with the leader under the
      // wrong label, so reject it the way the index walk used to.
      if (ints[0] < 0) {
        BD_WARN("[game] no active character at index {}", ints[0]);
        return;
      }
      const size_t idx = static_cast<size_t>(ints[0]);
      const auto c = Game::Get().FieldPlayerEntity().Party().At(idx).Chara();
      if (!c) {
        BD_WARN("[game] no active character at index {}", idx);
        return;
      }
      const auto s = c.Stats();
      const auto active = c.ActiveClass();
      BD_INFO("[game] character[{}] slot {} lv {} exp {} HP {}/{} MP {}/{}{}",
              idx, c.SlotId(), c.Level(), c.Exp(), c.HP(), c.MaxHP(), c.MP(),
              c.MaxMP(), StatusNames(c.StatusFlags()));
      BD_INFO("[game]  atk {} def {} hit {} matk {} mdef {} agi {}", s.attack,
              s.defense, s.hitRate, s.magicAttack, s.magicDefense, s.agility);
      BD_INFO("[game]  activeClass={} unlocked=0x{:X}",
              active ? bd::engine::ToString(*active) : "none",
              c.UnlockedClasses());
      for (u32 i = 0; i < bd::engine::kCharaClassCount; ++i) {
        const auto job = static_cast<bd::engine::CharaClass>(i);
        if (!c.IsClassUnlocked(job))
          continue;
        BD_INFO("[game]  {} rank {} sp {}", bd::engine::ToString(job),
                c.ClassRank(job), c.ClassSP(job));
      }
    },
    "GameState",
    "Dump one active character: derived stats and unlocked classes");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_gold,
    [](std::string_view) {
      const auto items = Game::Get().ItemSaveData();
      if (!items) {
        BD_WARN("[game] no item save data");
        return;
      }
      BD_INFO("[game] gold: {}", items.Gold());
    },
    "GameState", "Print current gold");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_inventory,
    [](std::string_view) {
      const auto items = Game::Get().ItemSaveData();
      if (!items) {
        BD_WARN("[game] no item save data");
        return;
      }
      BD_INFO("[game] inventory: {} non-empty slot(s), gold {}",
              items.UsedCount(), items.Gold());
      for (size_t i = 0; i < items.SlotCount(); ++i) {
        const auto it = items.At(i);
        if (it.id == 0)
          continue;
        BD_INFO("[game]  slot {} item {} x{}", i, it.id, it.count);
      }
    },
    "GameState", "Dump gold and every non-empty inventory slot");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_battle,
    [](std::string_view) {
      const auto &g = Game::Get();
      const auto battle = g.BattleTask();
      BD_INFO("[game] inBattle={} party={} enemies={}",
              static_cast<bool>(g.BattleCameraTask()),
              g.FieldPlayerEntity().Party().Size(), battle.EnemyCount());
      // Absent counters and zeroed counters are different answers, and the
      // record separates them.
      const auto record = g.PlayRecord();
      if (record)
        BD_INFO("[game] battlesWon={} escapes={} surroundWins={}",
                record.BattlesWon(), record.Escapes(), record.SurroundWins());
      else
        BD_WARN("[game] battle stats unavailable");
      if (!battle) {
        BD_INFO("[game] phase/enemies require battle-manager root capture");
        return;
      }
      BD_INFO(
          "[game] phase={} sub={} step={} loaded={} combined={} actor=0x{:08X}",
          battle.Phase(), battle.SubPhase(), battle.ActionStep(),
          battle.ResourcesLoaded(), battle.CombinedNum(),
          battle.CurrentActor().Address());
      for (size_t i = 0; i < battle.EnemyCount(); ++i) {
        const auto e = battle.EnemyAt(i).Chara();
        BD_INFO("[game]  enemy type {} HP {}/{}{}", e.TypeId(), e.HP(),
                e.MaxHP(), e.IsAlive() ? "" : " [dead]");
      }
    },
    "GameState",
    "Battle overview: active flag, counters, party/enemy counts, phase");

// ---- setters (verified-safe writes) ----

REXCVAR_DEFINE_COMMAND_ARGS(
    game_set_hp,
    [](std::string_view args) {
      const auto v = ParseInts(args);
      if (v.size() < 2 || v[0] < 0 || v[1] < 0) {
        BD_WARN("[game] usage: game_set_hp <activeIndex> <value>");
        return;
      }
      const auto party = Game::Get().FieldPlayerEntity().Party();
      auto c = party.At(static_cast<size_t>(v[0])).Chara();
      if (!c) {
        BD_WARN("[game] no active member at index {}", v[0]);
        return;
      }
      const u32 before = c.HP();
      if (!c.SetHP(static_cast<u32>(v[1]))) {
        BD_WARN("[game] write failed");
        return;
      }
      BD_INFO("[game] character[{}] HP {} -> {} (max {})", v[0], before, c.HP(),
              c.MaxHP());
    },
    "GameState", "Set an active character's current HP (clamped to maxHP)");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_set_mp,
    [](std::string_view args) {
      const auto v = ParseInts(args);
      if (v.size() < 2 || v[0] < 0 || v[1] < 0) {
        BD_WARN("[game] usage: game_set_mp <activeIndex> <value>");
        return;
      }
      const auto party = Game::Get().FieldPlayerEntity().Party();
      auto c = party.At(static_cast<size_t>(v[0])).Chara();
      if (!c) {
        BD_WARN("[game] no active member at index {}", v[0]);
        return;
      }
      const u32 before = c.MP();
      if (!c.SetMP(static_cast<u32>(v[1]))) {
        BD_WARN("[game] write failed");
        return;
      }
      BD_INFO("[game] character[{}] MP {} -> {} (max {})", v[0], before, c.MP(),
              c.MaxMP());
    },
    "GameState", "Set an active character's current MP (clamped to maxMP)");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_set_gold,
    [](std::string_view args) {
      const auto v = ParseInts(args);
      if (v.empty() || v[0] < 0) {
        BD_WARN("[game] usage: game_set_gold <value>");
        return;
      }
      auto items = Game::Get().ItemSaveData();
      const u32 before = items.Gold();
      if (!items.SetGold(static_cast<u32>(v[0]))) {
        BD_WARN("[game] no item save data");
        return;
      }
      BD_INFO("[game] gold {} -> {}", before, items.Gold());
    },
    "GameState", "Set gold (clamped 0..99999999)");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_set_item,
    [](std::string_view args) {
      const auto v = ParseInts(args);
      if (v.size() < 3 || v[0] < 0 || v[1] < 0 || v[2] < 0) {
        BD_WARN("[game] usage: game_set_item <slot> <itemId> <qty>");
        return;
      }
      auto items = Game::Get().ItemSaveData();
      if (!items.SetAt(static_cast<size_t>(v[0]), static_cast<u32>(v[1]),
                       static_cast<u32>(v[2]))) {
        BD_WARN("[game] write failed (bad slot or no item save data)");
        return;
      }
      BD_INFO("[game] slot {} = item {} x{}", v[0], v[1], v[2]);
    },
    "GameState", "Set an inventory slot's itemId/qty (qty clamped to 99)");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_options,
    [](std::string_view) {
      if (!bd::engine::GameOptionsResolved()) {
        BD_WARN("[game] config globals unavailable");
        return;
      }
      const auto &o = bd::engine::GameOptions::Get();
      BD_INFO("[game] msg_speed={} msg_size={} voice_type={} ruby={} "
              "subtitles={}",
              o.MsgSpeed(), o.MsgSize(), o.VoiceType(), o.Ruby(),
              o.Subtitles());
      BD_INFO("[game] audio_hints={} battle_hints={} skip_events={} camera={} "
              "target_first={}",
              o.AudioHints(), o.BattleHints(), o.SkipEvents(), o.Camera(),
              o.TargetFirst());
      BD_INFO("[game] music={:.2f} se={:.2f} brightness={:.2f} pos_x={:.2f} "
              "pos_y={:.2f}",
              o.MusicVolume(), o.SeVolume(), o.Brightness(), o.ScreenPosX(),
              o.ScreenPosY());
    },
    "GameState", "Dump the stock game options");
