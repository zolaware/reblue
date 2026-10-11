/**
 * @file    vfs/vfs.cpp
 * @license BSD 3-Clause - see LICENSE
 */
#include "vfs/vfs.h"

#include <system_error>

#include "core/logging.h"

namespace bd::vfs {

namespace {

constexpr std::string_view kPackIndexFile = "pack_index.bin";

std::filesystem::path PackIndexPath(const std::filesystem::path &cache_root) {
  if (cache_root.empty())
    return {};
  return cache_root / kPackIndexFile;
}

} // namespace

Paths::Paths(std::filesystem::path game_root, std::filesystem::path profile)
    : install_(game_root.empty()
                   ? std::filesystem::path{}
                   : std::filesystem::absolute(game_root).parent_path()),
      game_(std::move(game_root)), profile_(std::move(profile)) {
  if (!install_.empty()) {
    mods_ = install_ / "mods";
    dlc_ = install_ / "dlc";
  }
}

VFS &VFS::Get() {
  static VFS v;
  return v;
}

void VFS::Init(const std::filesystem::path &game_root,
               const std::filesystem::path &cache_root) {
  paths_ = vfs::Paths(game_root, paths_.Profile());
  BD_INFO("[vfs] install root {}", paths_.Install().string());
  files_.SetDiscRoot(paths_.Game());
  dlc_.Init(paths_.DLC());
  mods_.Init(paths_.Mods(), files_);
  // Content packs carry no per-profile enable state, so unlike the other two
  // catalogs this one can scan and mount straight away.
  if (!cache_root.empty()) {
    content_.Init(cache_root / "content", files_);
    content_.Reload();
  }

  if (paths_.Game().empty())
    return;
  log_.Init(paths_.Game());
  prefetch_.Init(paths_.Game());
  files_.Add("disc:packs", kPriorityShippedPack,
             ShippedPackMount::Scan(paths_.Game(), PackIndexPath(cache_root)));
}

void VFS::BuildPackIndex(const std::filesystem::path &game_root,
                         const std::filesystem::path &cache_root) {
  if (game_root.empty() || cache_root.empty())
    return;
  ShippedPackMount::Scan(game_root, PackIndexPath(cache_root));
}

void VFS::RebuildPackIndex(const std::filesystem::path &game_root,
                           const std::filesystem::path &cache_root) {
  if (cache_root.empty())
    return;
  std::error_code ec;
  std::filesystem::remove(PackIndexPath(cache_root), ec);
  BuildPackIndex(game_root, cache_root);
}

void VFS::SetProfile(const std::filesystem::path &profile) {
  paths_ = vfs::Paths(paths_.Game(), profile);
  // Both catalogs' enable flags depend on the profile, which Init does not
  // have, so these are each tree's one scan, not a re-sync of an earlier one.
  dlc_.SetProfile(paths_.Profile());
  dlc_.Reload();
  mods_.SetProfile(paths_.Profile());
  mods_.Reload();
}

void VFS::PrepareDevWriteDirs() {
  if (paths_.Game().empty())
    return;
  static constexpr const char *kDirs[] = {"battle/viewer", "battle/layout"};
  for (const char *rel : kDirs) {
    std::error_code ec;
    std::filesystem::create_directories(paths_.Game() / rel, ec);
    if (ec)
      BD_WARN("debug write dir '{}' not created: {}", rel, ec.message());
  }
}

} // namespace bd::vfs
