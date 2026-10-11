/**
 * @file    engine/d2anime/anime_mount.h
 * @brief       VFS publication of generated d2anime files.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rex/types.h>

namespace bd::vfs {
class GeneratedMount;
}

namespace bd::engine {

class AnimeLayout;

// One VFS mount of generated files. Providers run per read, so a published CSV
// always reflects the live state of the layout behind it.
class LayoutMount {
public:
  // Content directory every Add() name is taken relative to, trailing separator
  // included (e.g. "d2anime\\modmgr\\").
  explicit LayoutMount(std::string dir = {});
  ~LayoutMount();

  LayoutMount &Add(const char *name, AnimeLayout *layout);
  LayoutMount &Add(const char *name, std::function<std::string()> csv);
  // Complete content-relative key rather than a name under the mount's
  // directory, for a blob that has to sit beside something else.
  LayoutMount &AddRaw(std::string key, std::function<std::vector<u8>()> bytes);

  void Publish(const char *mountName);

  static void Remove(const char *mountName);

private:
  std::string dir_;
  std::shared_ptr<vfs::GeneratedMount> mount_;
};

} // namespace bd::engine
