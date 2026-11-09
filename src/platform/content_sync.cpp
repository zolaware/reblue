/**
 * @file    platform/content_sync.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/content_sync.h"

#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <vector>

#include "core/build_info.h"
#include "core/logging.h"
#include "platform/manifest.h"
#include "platform/package.h"
#include "vfs/vfs.h"

namespace bd::platform {
namespace {

// A pack id names a directory, so it may not reach outside the one it is
// given. Anything else is a malformed manifest, not a pack.
bool IsUsableId(std::string_view id) {
  if (id.empty() || id.size() > 64)
    return false;
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok)
      return false;
  }
  return true;
}

// The version stamp ContentCatalog reads back to decide what is installed.
bool WritePackFile(const std::filesystem::path &dir,
                   const ContentEntry &entry) {
  std::ofstream out(dir / "content.toml", std::ios::binary);
  out << "[content]\nid = \"" << entry.id << "\"\nversion = " << entry.version
      << "\n";
  out.close();
  return static_cast<bool>(out);
}

} // namespace

ContentSync &ContentSync::Get() {
  static ContentSync s;
  return s;
}

ContentSync::Stage ContentSync::State() const { return stage_.load(); }

std::string ContentSync::Current() const {
  std::lock_guard lock(mutex_);
  return current_;
}

size_t ContentSync::Done() const { return done_.load(); }
size_t ContentSync::Total() const { return total_.load(); }

size_t ContentSync::PendingCount() const {
  std::lock_guard lock(mutex_);
  return pending_.size();
}

u64 ContentSync::PendingBytes() const { return pending_bytes_.load(); }

void ContentSync::Start(const std::string &url) {
  if (url.empty())
    return;
  if (started_.exchange(true))
    return;

  // Stamped before the thread: what waits on the app check reads this the
  // instant that turns, and kIdle there would pass for serving no content.
  stage_.store(Stage::kChecking);

  // Detached: the ordered exit kills the process outright, so a check still
  // waiting on the network never holds shutdown up.
  std::thread([this, url] { Check(url); }).detach();
}

void ContentSync::BeginFetch() {
  if (fetching_.exchange(true))
    return;
  stage_.store(Stage::kFetching);
  std::thread([this] { Fetch(); }).detach();
}

void ContentSync::Decline() {
  {
    std::lock_guard lock(mutex_);
    pending_.clear();
  }
  pending_bytes_.store(0);
  total_.store(0);
  stage_.store(Stage::kDone);
}

void ContentSync::Check(const std::string &url) {
  std::string error;
  auto manifest = ContentManifest::Fetch(url, error);
  if (!manifest) {
    REXLOG_WARN(bd, "content manifest fetch from {} failed: {}", url, error);
    stage_.store(Stage::kDone);
    return;
  }
  REXLOG_INFO(bd, "content manifest names {} pack(s)", manifest->packs.size());

  auto &catalog = bd::vfs::VFS::Get().Content();

  std::vector<ContentEntry> wanted;
  u64 bytes = 0;
  for (const auto &entry : manifest->packs) {
    if (!IsUsableId(entry.id)) {
      REXLOG_WARN(bd, "content manifest names an unusable pack id, skipping it");
      continue;
    }
    if (!entry.min_app.empty() &&
        Version::Compare(entry.min_app, REBLUE_VERSION_STRING) > 0) {
      REXLOG_INFO(bd, "content '{}' needs v{}, skipping", entry.id, entry.min_app);
      continue;
    }
    if (catalog.InstalledVersion(entry.id) >= entry.version)
      continue;
    bytes += entry.size;
    wanted.push_back(entry);
  }

  if (wanted.empty()) {
    REXLOG_INFO(bd, "content every pack the manifest names is installed");
    stage_.store(Stage::kDone);
    return;
  }

  REXLOG_INFO(bd, "content {} pack(s) on offer", wanted.size());
  {
    std::lock_guard lock(mutex_);
    pending_ = std::move(wanted);
    total_.store(pending_.size());
  }
  pending_bytes_.store(bytes);
  stage_.store(Stage::kPending);
}

void ContentSync::Fetch() {
  std::vector<ContentEntry> wanted;
  {
    std::lock_guard lock(mutex_);
    wanted = std::move(pending_);
    pending_.clear();
  }
  if (wanted.empty()) {
    stage_.store(Stage::kDone);
    return;
  }

  auto &catalog = bd::vfs::VFS::Get().Content();
  REXLOG_INFO(bd, "content fetching {} pack(s)", wanted.size());

  std::vector<std::string> fetched;
  for (size_t i = 0; i < wanted.size(); ++i) {
    const ContentEntry &entry = wanted[i];
    done_.store(i);
    {
      std::lock_guard lock(mutex_);
      current_ = entry.id;
    }

    const auto zip = catalog.DownloadPathFor(entry.id, entry.version);
    const auto staged = catalog.StagingDirFor(entry.id);
    if (Package::Fetch(entry.url, entry.sha256, zip, staged, nullptr) !=
        Package::Result::kOk)
      continue;

    std::error_code ec;
    if (!WritePackFile(staged, entry)) {
      REXLOG_ERROR(bd, "content could not stamp '{}' with its version", entry.id);
      std::filesystem::remove_all(staged, ec);
      continue;
    }
    std::filesystem::remove(zip, ec);
    fetched.push_back(entry.id);
    REXLOG_INFO(bd, "content fetched '{}' v{}", entry.id, entry.version);
  }

  done_.store(wanted.size());
  {
    std::lock_guard lock(mutex_);
    current_.clear();
  }

  // One commit for the batch: it unmounts, swaps every pack in, then re-scans
  // and re-mounts once.
  if (!fetched.empty())
    catalog.Commit(fetched);
  stage_.store(Stage::kDone);
}

} // namespace bd::platform
