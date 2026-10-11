/**
 * @file    vfs/vfs.h
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <filesystem>

#include "vfs/access_log.h"
#include "vfs/content_catalog.h"
#include "vfs/dlc_catalog.h"
#include "vfs/file_system.h"
#include "vfs/key.h"
#include "vfs/mod_catalog.h"
#include "vfs/mounts.h"
#include "vfs/prefetch.h"
#include "vfs/settings.h"

namespace bd::vfs {

// Every directory the module reads or writes, derived from the install root and
// the active profile. The install root is game_root's parent: the installer
// lays out <install>/game, <install>/mods, <install>/dlc as siblings.
class Paths {
public:
  Paths() = default;
  Paths(std::filesystem::path game_root, std::filesystem::path profile);

  const std::filesystem::path &Install() const { return install_; }
  const std::filesystem::path &Game() const { return game_; }
  const std::filesystem::path &Mods() const { return mods_; }
  const std::filesystem::path &DLC() const { return dlc_; }
  const std::filesystem::path &Profile() const { return profile_; }

private:
  std::filesystem::path install_, game_, mods_, dlc_, profile_;
};

class VFS {
public:
  static VFS &Get();

  // Establishes every root and brings up the catalogs. Cheap. Called once by
  // ReblueApp, in OnPreLaunchModule, with the root it already knows. Nothing
  // here discovers a root, so nothing has to be retried once the guest is up.
  // The installer wizard needs only <install>/dlc, which it derives from its
  // own local vfs::Paths rather than reaching for this facade before there is
  // a profile to bind it to.
  //
  // Mounting the disc walks every archive's record table, and the index it
  // builds goes under 'cache_root' so later runs do not pay it again. Content
  // packs live under that root too.
  void Init(const std::filesystem::path &game_root,
            const std::filesystem::path &cache_root);

  // Writes that same index for a tree the installer has just laid down, while
  // the archives it reads are still the ones the OS cache is holding, so the
  // first boot off a fresh install loads an index instead of building one.
  static void BuildPackIndex(const std::filesystem::path &game_root,
                             const std::filesystem::path &cache_root);

  // Deletes the cached index first, so a matching stamp cannot short-circuit
  // this into the validated load BuildPackIndex allows.
  static void RebuildPackIndex(const std::filesystem::path &game_root,
                               const std::filesystem::path &cache_root);

  // Rebinds every per-profile file to the profile: the mod loadout and the
  // DLC enable state. Replaces two separate per-subsystem rebind calls.
  void SetProfile(const std::filesystem::path &profile);

  // Valid only until the next Init/SetProfile, which replace paths_ wholesale
  // rather than mutate it in place. Fine for a same-call read, but do not hold
  // a reference to the result across a lifecycle call.
  const vfs::Paths &Paths() const { return paths_; }

  vfs::ModCatalog &Mods() { return mods_; }
  vfs::ContentCatalog &Content() { return content_; }
  vfs::DLCCatalog &DLC() { return dlc_; }
  vfs::FileSystem &Files() { return files_; }
  vfs::AccessLog &Log() { return log_; }
  vfs::Prefetch &Prefetch() { return prefetch_; }

  // Creates the disc root subdirectories the devtool writers target. Devmode
  // only: it writes into the game install.
  void PrepareDevWriteDirs();

private:
  VFS() = default;
  VFS(const VFS &) = delete;
  VFS &operator=(const VFS &) = delete;

  vfs::Paths paths_;
  vfs::ModCatalog mods_;
  vfs::ContentCatalog content_;
  vfs::DLCCatalog dlc_;
  vfs::FileSystem files_;
  vfs::AccessLog log_;
  vfs::Prefetch prefetch_;
};

} // namespace bd::vfs
