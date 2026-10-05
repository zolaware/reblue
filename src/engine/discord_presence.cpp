/**
 * @file    engine/discord_presence.cpp
 * @brief   Discord Rich Presence management and game state synchronization.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/discord_presence.h"

#include <chrono>
#include <format>
#include <string>

#include <discord_rpc.h>

#include "core/logging.h"
#include "core/settings.h"
#include "engine/battle_task.h"
#include "engine/events.h"
#include "engine/field_player_entity.h"
#include "engine/game.h"
#include "engine/script.h"

namespace bd::engine {

namespace {

// Default Discord Application ID for re:Blue. Can be registered on the Discord
// Developer Portal.
constexpr const char *kDefaultAppId = "1346000000000000000";

// Discord IPC rate limits updates to once every 1.5 - 2.0 seconds.
constexpr auto kMinUpdateInterval = std::chrono::milliseconds(1500);

const char *PartyLeaderName(u32 slotId) {
  switch (slotId) {
  case 10:
    return "Shu";
  case 20:
    return "Kluke";
  case 30:
    return "Jiro";
  case 40:
    return "Marumaro";
  case 50:
    return "Zola";
  default:
    return "Party Leader";
  }
}

const char *PartyLeaderIconKey(u32 slotId) {
  switch (slotId) {
  case 10:
    return "char_shu";
  case 20:
    return "char_kluke";
  case 30:
    return "char_jiro";
  case 40:
    return "char_marumaro";
  case 50:
    return "char_zola";
  default:
    return "logo";
  }
}

void HandleReady(const DiscordUser *) {
  BD_INFO("[discord] connected to Discord client");
  DiscordPresence::Get().Resend();
}

void HandleDisconnected(int err, const char *msg) {
  BD_INFO("[discord] disconnected: {} (code {})", msg ? msg : "none", err);
}

void HandleErrored(int err, const char *msg) {
  BD_WARN("[discord] error: {} (code {})", msg ? msg : "none", err);
}

} // namespace

DiscordPresence &DiscordPresence::Get() {
  static DiscordPresence instance;
  return instance;
}

DiscordPresence::DiscordPresence() = default;

DiscordPresence::~DiscordPresence() { Shutdown(); }

void DiscordPresence::Init() {
  if (initialized_ || !bd::Settings::Get().DiscordRpc())
    return;

  sessionStartTimestamp_ = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

  DiscordEventHandlers handlers{};
  handlers.ready = HandleReady;
  handlers.disconnected = HandleDisconnected;
  handlers.errored = HandleErrored;

  Discord_Initialize(kDefaultAppId, &handlers, 1, nullptr);
  initialized_ = true;
  needsUpdate_ = true;

  // React promptly to major state changes. The bus has no unsubscribe, so
  // subscribe once and let the setting toggling Init/Shutdown reuse these.
  if (!subscribed_) {
    Events::Subscribe<StageLoaded>(
        [this](const StageLoaded &) { ForceUpdate(); });
    Events::Subscribe<BattleStarted>(
        [this](const BattleStarted &) { ForceUpdate(); });
    Events::Subscribe<BattleEnded>(
        [this](const BattleEnded &) { ForceUpdate(); });
    Events::Subscribe<SaveLoaded>([this](const SaveLoaded &) { ForceUpdate(); });
    Events::Subscribe<GameOverShown>(
        [this](const GameOverShown &) { ForceUpdate(); });
    subscribed_ = true;
  }

  BD_INFO("[discord] Discord Rich Presence initialized");
}

void DiscordPresence::Shutdown() {
  if (!initialized_)
    return;

  Discord_ClearPresence();
  Discord_Shutdown();
  initialized_ = false;
  Resend();
  BD_INFO("[discord] Discord Rich Presence shut down");
}

void DiscordPresence::ForceUpdate() { needsUpdate_ = true; }

void DiscordPresence::Resend() {
  lastState_.clear();
  lastDetails_.clear();
  lastSmallKey_.clear();
  lastLargeKey_.clear();
  needsUpdate_ = true;
}

void DiscordPresence::Poll() {
  if (!bd::Settings::Get().DiscordRpc()) {
    Shutdown();
    return;
  }

  // Initialize lazily so turning the setting on while the game is running is
  // enough to connect, and turning it off leaves no IPC connection behind.
  Init();
  if (!initialized_)
    return;

  // Run any queued Discord RPC callbacks (non-blocking).
  Discord_RunCallbacks();

  const auto now = std::chrono::steady_clock::now();
  if (!needsUpdate_ && (now - lastUpdatePoint_) < kMinUpdateInterval)
    return;

  lastUpdatePoint_ = now;
  needsUpdate_ = false;
  UpdatePresence();
}

void DiscordPresence::UpdatePresence() {
  const auto &g = Game::Get();

  std::string state;
  std::string details;
  std::string largeImageKey = "logo";
  std::string largeImageText = "re:Blue";
  std::string smallImageKey;
  std::string smallImageText;

  const EngineMode mode = g.Mode();

  switch (mode) {
  case EngineMode::Loading:
    state = "Loading...";
    break;

  case EngineMode::TitleOrMenu:
    state = "Title Screen";
    details = "Main Menu";
    break;

  case EngineMode::FieldTransition:
    state = "Transitioning Areas";
    break;

  case EngineMode::Battle: {
    bool isBoss = false;
    const auto battle = g.BattleTask();
    if (battle) {
      for (size_t i = 0; i < battle.EnemyCount(); ++i) {
        if (battle.EnemyAt(i).Chara().TypeId() >= 10000) {
          isBoss = true;
          break;
        }
      }
    }

    state = isBoss ? "Boss Battle" : "In Battle";

    const auto script = g.ScriptManTask().Script();
    const std::string stageName = script ? script.DisplayName() : "";
    if (!stageName.empty()) {
      details = std::format("Location: {}", stageName);
    } else {
      details = "Combat Encounter";
    }

    smallImageKey = "icon_battle";
    smallImageText = isBoss ? "Boss Encounter" : "In Battle";
    break;
  }

  case EngineMode::FieldActive: {
    const auto script = g.ScriptManTask().Script();
    const std::string stageName = script ? script.DisplayName() : "";

    if (script && (script.Category() == static_cast<u32>(AreaCategory::Wd) ||
                   script.Category() == static_cast<u32>(AreaCategory::Wc))) {
      state = "Traveling the World Map";
    } else if (!stageName.empty()) {
      state = std::format("Exploring {}", stageName);
    } else {
      state = "Exploring";
    }

    // Party leader and stats
    const auto entity = g.FieldPlayerEntity();
    if (entity) {
      const auto leader = entity.Leader().Chara();
      const u32 slotId = leader.SlotId();
      const u32 level = leader.Level();
      const size_t activeCount = entity.ActiveCount();

      const char *charName = PartyLeaderName(slotId);
      smallImageKey = PartyLeaderIconKey(slotId);
      smallImageText = std::format("{} (Lv. {})", charName, level);

      details =
          std::format("{} (Lv. {}) • Party of {}", charName, level, activeCount);
    }
    break;
  }

  case EngineMode::Unknown:
  default:
    state = "Playing re:Blue";
    break;
  }

  // Only issue an IPC update if presence fields changed
  if (state == lastState_ && details == lastDetails_ &&
      smallImageKey == lastSmallKey_ && largeImageKey == lastLargeKey_) {
    return;
  }

  lastState_ = state;
  lastDetails_ = details;
  lastSmallKey_ = smallImageKey;
  lastLargeKey_ = largeImageKey;

  DiscordRichPresence p{};
  p.state = state.c_str();
  p.details = details.empty() ? nullptr : details.c_str();
  p.startTimestamp = sessionStartTimestamp_;
  p.largeImageKey = largeImageKey.c_str();
  p.largeImageText = largeImageText.c_str();
  p.smallImageKey = smallImageKey.empty() ? nullptr : smallImageKey.c_str();
  p.smallImageText = smallImageText.empty() ? nullptr : smallImageText.c_str();
  Discord_UpdatePresence(&p);
}

} // namespace bd::engine
