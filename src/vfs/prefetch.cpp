/**
 * @file    vfs/prefetch.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/prefetch.h"

#include <chrono>
#include <fstream>
#include <string_view>

#include <rex/types.h>

#include "core/logging.h"
#include "core/threading.h"
#include "vfs/settings.h"

namespace bd::vfs {

namespace {

constexpr std::string_view kWarmRoots[] = {"pack", "snd_memory",
                                           "snd_memory_jp"};

} // namespace

Prefetch::~Prefetch() { Shutdown(); }

void Prefetch::Init(const std::filesystem::path &game_root) {
  if (!Settings::Get().DiscPrefetch() || game_root.empty())
    return;

  Shutdown();
  stop_ = false;
  worker_ = std::thread([this, game_root] { Walk(game_root); });
}

void Prefetch::Shutdown() {
  if (!worker_.joinable())
    return;
  stop_ = true;
  worker_.join();
}

void Prefetch::Walk(const std::filesystem::path &game_root) {
  bd::DemoteThreadToBackground();

  const auto started = std::chrono::steady_clock::now();
  u64 warmed = 0;

  for (auto name : kWarmRoots) {
    std::error_code iter_ec;
    std::filesystem::recursive_directory_iterator it(
        game_root / name,
        std::filesystem::directory_options::skip_permission_denied, iter_ec);
    if (iter_ec)
      continue;

    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
      if (stop_)
        return;

      std::error_code stat_ec;
      if (it->is_regular_file(stat_ec) && !stat_ec) {
        std::ifstream file(it->path(), std::ios::binary);
        if (file)
          ++warmed;
      }

      it.increment(iter_ec);
      if (iter_ec)
        break;
    }
  }

  if (stop_)
    return;

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
  BD_INFO("[prefetch] warmed {} file(s) in {} ms", warmed, elapsed);
}

} // namespace bd::vfs
