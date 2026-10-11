/**
 * @file    engine/gimmicks_commands.cpp
 * @brief   Console commands (category "GameState") for the gimmick counts.
 *          A bare stem argument reports one loaded stage, no argument the
 *          stage the player is in.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/string.h>

#include "core/logging.h"
#include "engine/game.h"
#include "engine/gimmicks.h"

namespace {

using bd::engine::BarrierColor;
using bd::engine::Gimmicks;
using bd::engine::GimmickKind;
using bd::engine::Tally;

std::string Scope(std::string_view args) {
  const std::string_view a = rex::string::trim(args);
  if (!a.empty())
    return std::string(a);
  return bd::engine::Game::Get().ScriptManTask().Script().Name();
}

std::string Describe(const Tally &t) {
  if (t.total == 0)
    return "none placed";
  if (t.IsComplete())
    return "all " + std::to_string(t.total) + " found";
  return std::to_string(t.Found()) + " of " + std::to_string(t.total) +
         " found";
}

bool Guard(const std::string &stem) {
  if (!Gimmicks::Get().IsReady()) {
    BD_WARN("[gimmicks] no field session, so the flag array is unreadable");
    return false;
  }
  if (!Gimmicks::Get().Has(stem)) {
    BD_WARN("[gimmicks] '{}' is not a stage the engine has loaded", stem);
    return false;
  }
  return true;
}

} // namespace

REXCVAR_DEFINE_COMMAND_ARGS(
    game_gimmicks,
    [](std::string_view args) {
      const std::string stem = Scope(args);
      if (!Guard(stem))
        return;
      const auto &g = Gimmicks::Get();
      BD_INFO("[gimmicks] {}:", stem);
      BD_INFO("[gimmicks]   search points  {}", Describe(g.Points(stem)));
      BD_INFO("[gimmicks]   chests         {}", Describe(g.Chests(stem)));
      BD_INFO("[gimmicks]   barriers       {}", Describe(g.Barriers(stem)));
    },
    "GameState",
    "Gimmicks found. Takes a map stem, or nothing for the current map");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_gimmicks_by_type,
    [](std::string_view args) {
      const std::string stem = Scope(args);
      if (!Guard(stem))
        return;
      const auto &g = Gimmicks::Get();
      BD_INFO("[gimmicks] {} by kind:", stem);
      for (u32 i = 0; i < bd::engine::kSearchKindCount; ++i) {
        const auto kind = static_cast<GimmickKind>(i);
        const Tally t = g.Points(stem, kind);
        if (t.total)
          BD_INFO("[gimmicks]   {:<8} {}", ToString(kind), Describe(t));
      }
      for (u32 i = 0; i < bd::engine::kBarrierColorCount; ++i) {
        const auto color = static_cast<BarrierColor>(i);
        const Tally t = g.Barriers(stem, color);
        if (t.total)
          BD_INFO("[gimmicks]   {:<8} barriers {}", ToString(color),
                  Describe(t));
      }
    },
    "GameState", "Gimmicks found, split by kind and barrier color");

REXCVAR_DEFINE_COMMAND_ARGS(
    game_gimmick_points,
    [](std::string_view args) {
      const std::string stem = Scope(args);
      if (stem.empty()) {
        BD_WARN("[gimmicks] usage: game_gimmick_points [map stem]");
        return;
      }
      if (!Guard(stem))
        return;
      const auto markers = Gimmicks::Get().Markers(stem);
      BD_INFO("[gimmicks] {}: {} placed", stem, markers.size());
      for (size_t i = 0; i < markers.size(); ++i) {
        const auto &m = markers[i];
        BD_INFO("[gimmicks]   {:>3} {:<8} {:>10.2f} {:>10.2f} {:>10.2f}  {} "
                "{} {}={} opens at {}",
                i, ToString(m.kind), m.x, m.y, m.z,
                !m.trackable ? "respawns"
                             : (m.collected ? "found" : "undiscovered"),
                m.opensAt ? "var" : "flag", m.flag, m.value,
                m.opensAt ? m.opensAt : 1);
      }
    },
    "GameState",
    "List one map's gimmicks, chests and barriers with positions and state");
