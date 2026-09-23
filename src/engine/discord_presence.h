/**
 * @file    engine/discord_presence.h
 * @brief   Discord Rich Presence management and game state synchronization.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace bd::engine {

class DiscordPresence {
public:
  static DiscordPresence &Get();

  // Initializes Discord RPC with the configured Application ID and registers
  // event listeners.
  void Init();

  // Polled each engine logic step (from bdMainGameStep). Runs callbacks and
  // syncs rich presence at a throttled rate.
  void Poll();

  // Clears presence and shuts down the Discord connection cleanly.
  void Shutdown();

  // Signals that an engine event changed state, prompting an immediate update
  // on the next poll.
  void ForceUpdate();

  // Forgets what was last sent, so the next poll sends the full presence even
  // if nothing changed. Called when Discord (re)connects, since anything sent
  // while disconnected was dropped.
  void Resend();

private:
  DiscordPresence();
  ~DiscordPresence();
  DiscordPresence(const DiscordPresence &) = delete;
  DiscordPresence &operator=(const DiscordPresence &) = delete;

  void UpdatePresence();

  bool initialized_{false};
  bool subscribed_{false};
  bool needsUpdate_{true};
  int64_t sessionStartTimestamp_{0};
  std::chrono::steady_clock::time_point lastUpdatePoint_{};

  std::string lastState_;
  std::string lastDetails_;
  std::string lastSmallKey_;
  std::string lastLargeKey_;
};

} // namespace bd::engine

