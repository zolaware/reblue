/**
 * @file    vfs/mod_catalog.h
 * @brief   The mod loadout, and one LooseMount per enabled mod.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "vfs/file_system.h"

namespace bd::vfs {

struct ModPackage {
  std::string folder; // directory under <install>/mods, and the order-file key
  std::string name;   // display name from mod.toml, falling back to folder
  std::string author;
  std::string version;
  std::string description;
  std::string created;
  std::filesystem::path image; // empty when the mod ships no preview
  DbRows db; // [db] of the mod toml and of every entities/*/entity.toml
  bool enabled = false;
};

// Mod DATA is global, under <install>/mods. Only the loadout (the enabled set
// and its order, mod_order.txt) is per-profile.
class ModCatalog {
public:
  // Sets the root but does not scan it: the enabled set and its order come
  // from the profile, which is not known yet. SetProfile/Reload do the scan.
  // files is where MountMods/MountPreviews register: a ModCatalog owns no
  // registry of its own, so it never mounts into one it was not handed.
  void Init(const std::filesystem::path &mods_root, FileSystem &files);
  void SetProfile(const std::filesystem::path &profile);
  void Reload();
  void Flush();

  size_t Count() const { return packages_.size(); }
  const ModPackage &At(size_t i) const;

  bool IsEnabled(size_t i) const;
  bool IsEnabled(std::string_view folder) const;
  void SetEnabled(size_t i, bool on);

  // Enable a freshly installed mod: add it to the loadout if absent, persist
  // the order file, and reload so its overrides take effect this session.
  void Enable(std::string_view folder);

  void Swap(size_t a, size_t b);
  bool Remove(size_t i);

  // Serve each mod's preview image at d2anime\modmgr\res\preview_N.dds so the
  // mod manager CSVs can name it as an ordinary texture.
  void MountPreviews();
  void UnmountPreviews();

private:
  std::filesystem::path OrderFilePath() const;
  static ModPackage ParseTOML(const std::filesystem::path &mod_dir,
                              const std::string &folder_name);
  size_t MountMods();
  void UnmountMods();
  void Discover();

  std::filesystem::path mods_root_;
  std::filesystem::path profile_;
  FileSystem *files_ = nullptr;
  std::vector<ModPackage> packages_; // load order, with .enabled on the record
  std::vector<std::string> mount_names_; // for UnmountMods
};

} // namespace bd::vfs
