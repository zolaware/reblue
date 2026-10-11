/**
 * @file    engine/guest_texlist.h
 * @brief   Shipped textures loaded by name, for host draws that want the
 *          game's own art without a d2anime CSV behind it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <initializer_list>

#include <rex/types.h>

namespace bd::engine {

// One texlist request: a directory and the basenames under it, which the
// engine's loader joins as '%s%s.dds'. The engine block it builds lives for the
// run, since the loader keeps reading the directory and the descriptor for as
// long as the request exists.
class Texlist {
public:
  static constexpr u32 kMaxTextures = 4;

  // Both the directory and the names are borrowed rather than copied, so they
  // have to outlive the list. Literals do.
  constexpr Texlist(const char *dir, std::initializer_list<const char *> names)
      : dir_(dir) {
    for (const char *name : names) {
      if (count_ == kMaxTextures)
        break;
      names_[count_++] = name;
    }
  }

  // Drives the load, and has to keep running for the finished table to be
  // published. True once every texture in the list has loaded.
  bool Poll();

  // Safe from any thread, unlike Poll.
  bool Ready() const { return ready_.load(std::memory_order_acquire); }

  // The texture object a prim binds, or zero before the list is ready.
  u32 Texture(u32 slot) const;

  // Leaves one texture in the prim state, for the draws that read it from
  // there rather than taking it themselves.
  void Select(u32 slot) const;

private:
  bool Request();
  bool Resolved() const;

  enum class State { kIdle, kLoading, kReady, kFailed };

  const char *dir_ = nullptr;
  const char *names_[kMaxTextures] = {};
  u32 count_ = 0;
  // Read by draws that may not be on the thread Poll runs on.
  std::atomic<u32> block_{0};
  std::atomic<bool> ready_{false};
  State state_ = State::kIdle;
};

} // namespace bd::engine
