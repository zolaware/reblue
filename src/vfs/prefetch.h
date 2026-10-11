/**
 * @file    vfs/prefetch.h
 * @brief   Boot-time metadata warm of the loose game files.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <atomic>
#include <filesystem>
#include <thread>

namespace bd::vfs {

class Prefetch {
public:
  ~Prefetch();

  void Init(const std::filesystem::path &game_root);

  void Shutdown();

private:
  void Walk(const std::filesystem::path &game_root);

  std::atomic<bool> stop_{false};
  std::thread worker_;
};

} // namespace bd::vfs
