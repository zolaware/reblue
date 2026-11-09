/**
 * @file    vfs/dlc_publish.cpp
 * @brief   Publishes enabled packs into the profile's XAM content tree and
 *          mounts their IPK archives.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "core/logging.h"
#include "vfs/dlc_catalog.h"
#include "vfs/file_system.h"
#include "vfs/mounts.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <set>
#include <system_error>
#include <utility>

#include <rex/string/text.h>
#include <rex/types.h>

namespace bd::vfs {

namespace {

// XAM marketplace content lives under xuid 0 (ContentType 2 forces it): content
// folders at <profile>/0000000000000000/4D5307DF/00000002/<name>, headers at
// .../Headers/00000002/<name>.header. The header (XCONTENT_AGGREGATE_DATA +
// license mask) is what the guest's bdIsDownloadPackageInstalled reads.
constexpr char kXUIDDir[] = "0000000000000000";
constexpr char kTitleDir[] = "4D5307DF";
constexpr char kTypeDir[] = "00000002";
constexpr char kHeadersDir[] = "Headers";

// DL_Param.ipk is registered natively through the guest's single "download"
// IPK slot (item grants + DL_config parsing). Each pack ships its own, so
// overlaying them would collide on shared paths. Only the map/data IPKs
// (DL_Map_*/DL_Dat_*) lack native backing.
bool IsOverlayArchive(std::string_view file) {
  return file.ends_with(".ipk") && !file.starts_with("dl_param");
}

std::string ArchiveMountName(const std::string &pack, const std::string &file) {
  return "dlc:" + pack + "/" + file;
}

} // namespace

void DLCCatalog::RemovePublished(const std::string &name) const {
  if (profile_.empty() || name.empty())
    return;
  std::error_code ec;
  const auto title = profile_ / kXUIDDir / kTitleDir;
  std::filesystem::remove_all(title / kTypeDir / name, ec);
  if (ec)
    REXBD_WARN("dlc could not remove published pack '{}': {}", name,
            ec.message());
  std::filesystem::remove(title / kHeadersDir / kTypeDir / (name + ".header"),
                          ec);
}

void DLCCatalog::Publish() {
  std::vector<DLCPackage> packages;
  std::filesystem::path root, profile;
  {
    std::lock_guard lock(mutex_);
    packages = packages_;
    root = root_;
    profile = profile_;
  }
  if (profile.empty())
    return;

  const auto content_dir = profile / kXUIDDir / kTitleDir / kTypeDir;
  const auto headers_dir =
      profile / kXUIDDir / kTitleDir / kHeadersDir / kTypeDir;

  std::error_code ec;

  // Self-heal: a pack present only in the profile content tree means the
  // store copy was lost (e.g. the pre-fix wizard latch bug emptied it).
  // Adopt it back so the DLC menu and IPK overlay see it again.
  if (!root.empty() && std::filesystem::is_directory(content_dir, ec)) {
    bool adopted = false;
    for (const auto &e : std::filesystem::directory_iterator(content_dir, ec)) {
      std::error_code se;
      if (!e.is_directory(se) || e.is_symlink(se))
        continue;
      const auto name = e.path().filename().string();
      if (std::any_of(packages.begin(), packages.end(),
                      [&](const DLCPackage &p) { return p.name == name; }))
        continue;
      if (!std::filesystem::is_directory(e.path() / "Download", se) &&
          !std::filesystem::is_directory(e.path() / "data" / "Download", se))
        continue;
      const auto dest = root / name;
      if (std::filesystem::exists(dest, se))
        continue;
      std::error_code cp;
      std::filesystem::create_directories(root, cp);
      std::filesystem::copy(e.path(), dest,
                            std::filesystem::copy_options::recursive, cp);
      if (cp) {
        REXBD_ERROR("dlc failed to adopt orphaned pack '{}': {}", name,
                 cp.message());
        std::filesystem::remove_all(dest, cp);
        continue;
      }
      const auto header = headers_dir / (name + ".header");
      if (std::filesystem::is_regular_file(header, se)) {
        std::error_code hp;
        std::filesystem::create_directories(root / ".headers", hp);
        std::filesystem::copy_file(
            header, root / ".headers" / (name + ".header"),
            std::filesystem::copy_options::overwrite_existing, hp);
      }
      REXBD_DEBUG("dlc adopted orphaned pack '{}' from profile content tree",
               name);
      adopted = true;
    }
    if (adopted) {
      std::lock_guard lock(mutex_);
      if (!root_.empty())
        ReloadLocked();
      packages = packages_;
      root = root_;
      profile = profile_;
    }
  }

  std::set<std::string> enabled;
  for (const auto &p : packages)
    if (p.enabled)
      enabled.insert(p.name);

  // Clear entries we manage that are no longer enabled, plus any symlink left
  // on disk by installs that published packs as links. Unmanaged folders are
  // left for the enumerator.
  if (std::filesystem::is_directory(content_dir, ec)) {
    for (const auto &e : std::filesystem::directory_iterator(content_dir, ec)) {
      const auto name = e.path().filename().string();
      std::error_code se;
      if (e.is_symlink(se)) {
        // Never remove_all a symlink: remove only the link, not its target
        // (which is the master pack data in the store).
        std::filesystem::remove(e.path(), se);
        std::filesystem::remove(headers_dir / (name + ".header"), se);
        continue;
      }
      const bool is_pack =
          std::any_of(packages.begin(), packages.end(),
                      [&](const DLCPackage &p) { return p.name == name; });
      if (is_pack && !enabled.count(name)) {
        std::filesystem::remove_all(e.path(), se);
        std::filesystem::remove(headers_dir / (name + ".header"), se);
      }
    }
  }

  i32 ready = 0;
  for (const auto &p : packages) {
    if (!p.enabled)
      continue;
    const auto dest = content_dir / p.name;
    if (!std::filesystem::exists(dest, ec)) {
      std::filesystem::create_directories(content_dir, ec);
      std::error_code cp;
      std::filesystem::copy(p.root, dest,
                            std::filesystem::copy_options::recursive, cp);
      if (cp) {
        REXBD_ERROR("dlc failed to stage pack '{}': {}", p.name, cp.message());
        std::filesystem::remove_all(dest, cp);
        continue;
      }
    }
    // Header priority: already in profile -> store sidecar. The header's
    // license mask is what the guest's bdIsDownloadPackageInstalled reads.
    // Missing it -> items granted as worthless junk.
    const auto header = headers_dir / (p.name + ".header");
    if (!std::filesystem::exists(header, ec)) {
      const auto sidecar = root / ".headers" / (p.name + ".header");
      if (std::filesystem::exists(sidecar, ec)) {
        std::error_code cp;
        std::filesystem::create_directories(headers_dir, cp);
        std::filesystem::copy_file(
            sidecar, header, std::filesystem::copy_options::overwrite_existing,
            cp);
        if (!cp)
          REXBD_DEBUG("dlc restored header for '{}' from store sidecar", p.name);
      }
      if (!std::filesystem::exists(header, ec))
        REXBD_WARN("dlc no header sidecar for '{}': its items will not be "
                "granted (worthless junk)",
                p.name);
    }
    ++ready;
  }
  REXBD_DEBUG("dlc published {} enabled pack(s) into profile content tree",
           ready);
}

void DLCCatalog::MountArchives() {
  std::vector<DLCPackage> packages;
  {
    std::lock_guard lock(mutex_);
    packages = packages_;
  }

  // packages_ is display_name order (ReloadLocked sorts it for the UI list),
  // but this walk needs name order, matching the key IPKMount::Find resolves a
  // bare record name against, so two packs shipping the same record name
  // collide the same way regardless of how their display names sort.
  std::sort(
      packages.begin(), packages.end(),
      [](const DLCPackage &a, const DLCPackage &b) { return a.name < b.name; });

  i32 packs = 0, archives = 0;
  for (const auto &pack : packages) {
    if (!pack.enabled || pack.download_dir.empty())
      continue;

    std::error_code ec;
    bool any = false;
    for (const auto &de :
         std::filesystem::directory_iterator(pack.download_dir, ec)) {
      auto file = rex::string::utf8_lower_ascii(de.path().filename().string());
      if (!IsOverlayArchive(file))
        continue;

      // Prefixed so a browse of the guest file tree finds these records under
      // the pack that shipped them rather than loose at the disc root.
      auto mount = IPKMount::Open(de.path(), Key::FromRelative("dlc") /
                                                 Key::FromRelative(pack.name) /
                                                 Key::FromRelative(file));
      if (!mount)
        continue;
      VFS::Get().Files().Add(ArchiveMountName(pack.name, file), kPriorityDLC,
                             std::move(mount));
      ++archives;
      any = true;
    }
    if (any)
      ++packs;
  }

  REXBD_INFO("dlc mounted {} archive(s) from {} pack(s)", archives, packs);
}

void DLCCatalog::UnmountArchives(const DLCPackage &pack) const {
  if (pack.download_dir.empty())
    return;

  std::error_code ec;
  for (const auto &de :
       std::filesystem::directory_iterator(pack.download_dir, ec)) {
    const auto file = rex::string::utf8_lower_ascii(de.path().filename().string());
    if (IsOverlayArchive(file))
      VFS::Get().Files().Remove(ArchiveMountName(pack.name, file));
  }
}

} // namespace bd::vfs
