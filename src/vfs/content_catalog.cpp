/**
 * @file    vfs/content_catalog.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "vfs/content_catalog.h"
#include "core/logging.h"
#include "vfs/file_system.h"
#include "vfs/mounts.h"

#include <algorithm>

#include <toml++/toml.h>

namespace bd::vfs {

namespace {

constexpr const char *kPackFile = "content.toml";
constexpr const char *kStagingPrefix = ".incoming-";
constexpr const char *kDownloadDir = ".download";
constexpr const char *kReplacedPrefix = ".replaced-";

i64 ReadVersion(const std::filesystem::path &dir) {
  const auto path = dir / kPackFile;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return 0;
  try {
    auto tbl = toml::parse_file(path.string());
    if (auto v = tbl["content"]["version"].value<i64>())
      return *v;
  } catch (const toml::parse_error &e) {
    REXLOG_WARN(bd, "content {} parse error: {}", path.string(), e.description());
  }
  return 0;
}

} // namespace

void ContentCatalog::Init(const std::filesystem::path &root,
                          FileSystem &files) {
  root_ = root;
  files_ = &files;
}

std::filesystem::path ContentCatalog::DirFor(std::string_view id) const {
  return root_ / id;
}

std::filesystem::path ContentCatalog::StagingDirFor(std::string_view id) const {
  return root_ / (kStagingPrefix + std::string(id));
}

std::filesystem::path ContentCatalog::DownloadPathFor(std::string_view id,
                                                      i64 version) const {
  return root_ / kDownloadDir /
         (std::string(id) + "-" + std::to_string(version) + ".zip");
}

std::vector<ContentPack> ContentCatalog::Snapshot() const {
  std::lock_guard lock(mutex_);
  return packs_;
}

i64 ContentCatalog::InstalledVersion(std::string_view id) const {
  std::lock_guard lock(mutex_);
  auto it = std::find_if(packs_.begin(), packs_.end(),
                         [&](const ContentPack &p) { return p.id == id; });
  return it == packs_.end() ? 0 : it->version;
}

std::filesystem::path ContentCatalog::Find(std::string_view name) const {
  std::error_code ec;
  for (const auto &pack : Snapshot()) {
    auto path = pack.dir / name;
    if (std::filesystem::is_regular_file(path, ec))
      return path;
  }
  return {};
}

void ContentCatalog::Discover() {
  namespace fs = std::filesystem;

  std::vector<ContentPack> found;

  std::error_code ec;
  if (!root_.empty() && fs::is_directory(root_, ec)) {
    for (const auto &entry : fs::directory_iterator(root_, ec)) {
      if (!entry.is_directory(ec))
        continue;
      const auto name = entry.path().filename().string();
      // Staging and downloads share the root and are not packs.
      if (name.empty() || name.front() == '.')
        continue;
      found.push_back({name, ReadVersion(entry.path()), entry.path()});
    }
    std::sort(
        found.begin(), found.end(),
        [](const ContentPack &a, const ContentPack &b) { return a.id < b.id; });
  }

  std::lock_guard lock(mutex_);
  packs_ = std::move(found);
}

size_t ContentCatalog::Mount() {
  Unmount();

  size_t overrides = 0;
  for (const auto &pack : Snapshot()) {
    auto mount = LooseMount::Scan(pack.dir);
    overrides += mount->KeyCount();

    auto name = "content:" + pack.id;
    files_->Add(name, kPriorityContent, std::move(mount));
    mount_names_.push_back(std::move(name));
  }
  return overrides;
}

void ContentCatalog::Unmount() {
  for (const auto &name : mount_names_)
    files_->Remove(name);
  mount_names_.clear();
}

void ContentCatalog::Reload() {
  Discover();
  // Mount unconditionally: it unmounts first, dropping a pack that is no
  // longer there.
  const size_t overrides = Mount();
  if (const size_t count = Snapshot().size(); count != 0)
    REXLOG_INFO(bd, "content {} pack(s), {} override(s)", count, overrides);
}

void ContentCatalog::Commit(const std::vector<std::string> &ids) {
  namespace fs = std::filesystem;
  Unmount();

  std::error_code ec;
  for (const auto &id : ids) {
    const auto staged = StagingDirFor(id);
    const auto dir = DirFor(id);
    // The version already installed moves aside rather than out, so a failed
    // swap leaves the player the pack they had instead of neither. The dot
    // keeps an interrupted commit's leftover out of the next Discover.
    const auto aside = root_ / (kReplacedPrefix + id);
    fs::remove_all(aside, ec);
    const bool had_old = fs::exists(dir, ec);
    if (had_old)
      fs::rename(dir, aside, ec);

    fs::rename(staged, dir, ec);
    if (ec) {
      REXLOG_ERROR(bd, "content could not move '{}' into place: {}", id,
               ec.message());
      if (had_old)
        fs::rename(aside, dir, ec);
      fs::remove_all(staged, ec);
      continue;
    }
    fs::remove_all(aside, ec);
  }

  Reload();
}

} // namespace bd::vfs
