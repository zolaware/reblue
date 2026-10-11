/**
 * @file    engine/live_texture_stamp.h
 * @brief   Keeps loaded instances of a served texture on the current art.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

// A VFS provider covers every load made after the art changes, this covers the
// instances already sitting in engine memory when it does. Sync rewrites each
// live instance whose stamp is not the given generation, so a tick where
// nothing changed touches no texture and never composes.
class LiveTextureStamp {
public:
  // 'name' as the engine asset name resolves it: a basename, extension
  // optional. compose returns the full blob file image and runs at most once
  // per call.
  void Sync(const char *name, u32 generation,
            const std::function<std::vector<u8>()> &compose);

private:
  // Keyed by the allocation's sequence number, so a freed-and-reused VA can
  // never pass for the instance that was stamped there before it.
  std::unordered_map<u64, u32> stamped_;
};

} // namespace bd::engine
