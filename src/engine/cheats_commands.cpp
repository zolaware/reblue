/**
 * @file    engine/cheats_commands.cpp
 * @brief   Console commands (category "GameState") over the cheat toggles.
 *          The toggles are cvars, so setting one by its cvar name works too;
 *          these read as verbs beside the existing game_set_ setters.
 *
 * @license BSD 3-Clause License
 */
#include <string>
#include <cctype>
#include <string_view>

#include <rex/cvar.h>

#include "core/logging.h"
#include "engine/cheats.h"
#include "engine/game_tables.h"

namespace {

// Accepts the spellings a player reaches for, so a toggle is not a guess.
// Empty input means "flip it", which is what a bare command should do.
bool ParseToggle(std::string_view a, bool current, bool &out) {
  size_t i = 0, j = a.size();
  while (i < j && std::isspace(static_cast<unsigned char>(a[i])))
    ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(a[j - 1])))
    --j;
  const std::string_view t = a.substr(i, j - i);
  if (t.empty()) {
    out = !current;
    return true;
  }
  if (t == "1" || t == "on" || t == "true") {
    out = true;
    return true;
  }
  if (t == "0" || t == "off" || t == "false") {
    out = false;
    return true;
  }
  return false;
}

} // namespace

REXCVAR_DEFINE_COMMAND_ARGS(
    game_cheats,
    [](std::string_view) {
      const auto &c = bd::engine::Cheats::Get();
      BD_INFO("[cheats] invincible={} infinite_mp={} infinite_gold={} "
              "status_immune={} unlock_classes={} one_hit_kill={}",
              c.Invincible(), c.InfiniteMP(), c.InfiniteGold(),
              c.StatusImmune(), c.UnlockClasses(), c.OneHitKill());
      BD_INFO("[cheats] attack=x{} magic_attack=x{} defence=x{} "
              "magic_defence=x{} agility=x{} stat_bonus=+{}",
              c.AttackMult(), c.MagicAttackMult(), c.DefenceMult(),
              c.MagicDefenceMult(), c.AgilityMult(), c.StatBonus());
      BD_INFO("[cheats] per fight: exp=x{} sp=x{} gold=x{} medals=x{}",
              c.ExpMult(), c.SpMult(), c.GoldMult(), c.MedalsMult());
      BD_INFO("[cheats] infinite_medals={} infinite_items={}",
              c.InfiniteMedals(), c.InfiniteItems());
      BD_INFO("[cheats] give_all_items={} (an action; clears itself)",
              c.GiveAllItems());
      BD_INFO("[cheats] toggles take on|off or nothing to flip; the "
              "multipliers are the bd_cheat_* cvars");
    },
    "GameState", "Print every cheat setting");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_item_table,
    [](std::string_view args) {
      // Dumps both id spaces so their layout can be read off directly. The
      // categories the Items screen groups by live in a record field nothing
      // here decodes, but ids in a table like this are allocated in blocks, so
      // the names in id order show where one kind ends and the next begins.
      u32 first = 1;
      u32 last = 900;
      {
        // Two optional bounds, whitespace separated. Anything unparsable
        // leaves the default range in place rather than failing the command.
        const char *p = args.data();
        const char *end = p + args.size();
        auto next = [&](u32 &out) {
          while (p < end && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
          const char *start = p;
          u32 val = 0;
          while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
            val = val * 10 + static_cast<u32>(*p++ - '0');
          if (p != start)
            out = val;
        };
        next(first);
        next(last);
      }
      auto &t = bd::engine::GameTables::Get();
      u32 items = 0;
      u32 phen = 0;
      for (u32 id = first; id <= last; ++id) {
        const bool hasItem = t.ItemRecord(id) != 0;
        const bool hasPhen = t.PhenomeRecord(id) != 0;
        if (!hasItem && !hasPhen)
          continue;
        if (hasItem)
          ++items;
        if (hasPhen)
          ++phen;
        BD_INFO("[table] {:4} {:7} {:7} | item='{}' phenome='{}'", id,
                hasItem ? "ITEM" : "", hasPhen ? "PHENOME" : "",
                hasItem ? t.ItemName(id) : std::string(),
                hasPhen ? t.PhenomeName(id) : std::string());
      }
      BD_INFO("[table] ids {}..{}: {} item record(s), {} phenome record(s)",
              first, last, items, phen);
    },
    "GameState", "Dump item/phenome ids and names: game_item_table [lo] [hi]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_items,
    [](std::string_view args) {
      // Dumps both id spaces so their layout can be read off directly. The
      // categories the Items screen groups by live in a record field nothing
      // here decodes, but ids in a table like this are allocated in blocks, so
      // the names in id order show where one kind ends and the next begins.
      u32 first = 1;
      u32 last = 900;
      {
        // Two optional bounds, whitespace separated. Anything unparsable
        // leaves the default range in place rather than failing the command.
        const char *p = args.data();
        const char *end = p + args.size();
        auto next = [&](u32 &out) {
          while (p < end && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
          const char *start = p;
          u32 val = 0;
          while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
            val = val * 10 + static_cast<u32>(*p++ - '0');
          if (p != start)
            out = val;
        };
        next(first);
        next(last);
      }
      auto &t = bd::engine::GameTables::Get();
      u32 items = 0;
      u32 phen = 0;
      for (u32 id = first; id <= last; ++id) {
        const bool hasItem = t.ItemRecord(id) != 0;
        const bool hasPhen = t.PhenomeRecord(id) != 0;
        if (!hasItem && !hasPhen)
          continue;
        if (hasItem)
          ++items;
        if (hasPhen)
          ++phen;
        BD_INFO("[table] {:4} {:7} {:7} | item='{}' phenome='{}'", id,
                hasItem ? "ITEM" : "", hasPhen ? "PHENOME" : "",
                hasItem ? t.ItemName(id) : std::string(),
                hasPhen ? t.PhenomeName(id) : std::string());
      }
      BD_INFO("[table] ids {}..{}: {} item record(s), {} phenome record(s)",
              first, last, items, phen);
    },
    "GameState", "Same dump as game_item_table, under a shorter name");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_one_hit_kill,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.OneHitKill(), want)) {
        BD_WARN("[cheats] usage: game_one_hit_kill [on|off]");
        return;
      }
      if (!c.SetOneHitKill(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] one_hit_kill={}", c.OneHitKill());
    },
    "GameState", "Hold living enemies at 1 HP: [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_cheat_diag,
    [](std::string_view args) {
      const bool now = bd::engine::CheatDiagEnabled();
      bool want = false;
      if (!ParseToggle(args, now, want)) {
        BD_WARN("[cheats] usage: game_cheat_diag [on|off]");
        return;
      }
      if (!rex::cvar::SetFlagByName("bd_cheat_diag",
                                    want ? "true" : "false")) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] cheat_diag={} (logs a line whenever a cheat acts)",
              bd::engine::CheatDiagEnabled());
    },
    "GameState", "Log a line whenever a cheat acts: [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_status_immune,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.StatusImmune(), want)) {
        BD_WARN("[cheats] usage: game_status_immune [on|off]");
        return;
      }
      if (!c.SetStatusImmune(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] status_immune={}", c.StatusImmune());
    },
    "GameState", "Hold status resistances at immune: [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_unlock_classes,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.UnlockClasses(), want)) {
        BD_WARN("[cheats] usage: game_unlock_classes [on|off]");
        return;
      }
      if (!c.SetUnlockClasses(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] unlock_classes={}", c.UnlockClasses());
    },
    "GameState", "Keep all nine classes unlocked: [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_invincible,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.Invincible(), want)) {
        BD_WARN("[cheats] usage: game_invincible [on|off]");
        return;
      }
      if (!c.SetInvincible(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] invincible={}", c.Invincible());
    },
    "GameState", "Refill living party HP each step: game_invincible [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_infinite_mp,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.InfiniteMP(), want)) {
        BD_WARN("[cheats] usage: game_infinite_mp [on|off]");
        return;
      }
      if (!c.SetInfiniteMP(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] infinite_mp={}", c.InfiniteMP());
    },
    "GameState", "Refill living party MP each step: game_infinite_mp [on|off]");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_infinite_gold,
    [](std::string_view args) {
      auto &c = bd::engine::Cheats::Get();
      bool want = false;
      if (!ParseToggle(args, c.InfiniteGold(), want)) {
        BD_WARN("[cheats] usage: game_infinite_gold [on|off]");
        return;
      }
      if (!c.SetInfiniteGold(want)) {
        BD_WARN("[cheats] write failed");
        return;
      }
      BD_INFO("[cheats] infinite_gold={}", c.InfiniteGold());
    },
    "GameState", "Hold gold at its ceiling: game_infinite_gold [on|off]");
