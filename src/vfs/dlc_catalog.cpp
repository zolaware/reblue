/**
 * @file    vfs/dlc_catalog.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "vfs/dlc_catalog.h"
#include "core/logging.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include <toml++/toml.h>

#include <rex/platform/file.h>
#include <rex/runtime/xcontent_data.h>
#include <rex/types.h>
#include <rex/string/utf8.h>

namespace bd::vfs {

namespace {

const DLCPackage kEmptyPackage{};

// info.txt holds "<discId> <packageId>". Some packs carry a single id.
i32 ParsePackageId(const std::filesystem::path &info_path) {
  const auto bytes = rex::platform::ReadHostFile(info_path);
  if (!bytes)
    return -1;
  std::istringstream ss(std::string(bytes->begin(), bytes->end()));
  i32 id = -1;
  i32 v;
  while (ss >> v)
    id = v;
  return id;
}

// Parse the [dlc] array of {file_name,enabled} in a dlc_state.toml at path.
// nullopt when the file is absent or fails to parse.
std::optional<std::map<std::string, bool>>
ParseDLCStateTOML(const std::filesystem::path &path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return std::nullopt;

  std::map<std::string, bool> state;
  try {
    auto tbl = toml::parse_file(path.string());
    if (auto arr = tbl["dlc"].as_array()) {
      for (auto &elem : *arr) {
        auto *t = elem.as_table();
        if (!t)
          continue;
        auto name = (*t)["file_name"].value<std::string>();
        if (!name || name->empty())
          continue;
        state[*name] = (*t)["enabled"].value<bool>().value_or(true);
      }
    }
  } catch (const toml::parse_error &e) {
    REXLOG_ERROR(bd, "dlc failed to parse {}: {}", path.string(), e.what());
    return std::nullopt;
  }
  return state;
}

// A discovered pack with no dlc.toml entry would list as its content id hash.
// The .header sidecar carries the STFS display name, so prefer that.
std::string DisplayNameFromHeader(const std::filesystem::path &header_path) {
  std::ifstream in(header_path, std::ios::binary);
  if (!in)
    return {};
  rex::runtime::XCONTENT_AGGREGATE_DATA data{};
  in.read(reinterpret_cast<char *>(&data), sizeof(data));
  if (in.gcount() != static_cast<std::streamsize>(sizeof(data)))
    return {};
  return rex::ToUTF8(data.GetDisplayName());
}

// Prune metadata entries whose pack is no longer present on disk, then add
// entries for newly discovered packs (name and enabled only). display_name
// falls back to the header sidecar, then the folder name. Returns whether
// the metadata list changed (caller should persist it).
bool ReconcileDLCList(std::vector<DLCPackage> &list,
                      const std::vector<std::pair<std::string, bool>> &packs,
                      const std::filesystem::path &sidecar_dir) {
  bool changed = false;

  auto it = list.begin();
  while (it != list.end()) {
    bool found = std::any_of(packs.begin(), packs.end(),
                             [&](const std::pair<std::string, bool> &p) {
                               return p.first == it->name;
                             });
    if (!found) {
      REXLOG_INFO(bd, "dlc pruning stale entry: {}", it->name);
      it = list.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }

  for (const auto &[name, enabled] : packs) {
    bool found =
        std::any_of(list.begin(), list.end(),
                    [&](const DLCPackage &info) { return info.name == name; });
    if (!found) {
      DLCPackage info;
      info.name = name;
      info.display_name =
          DisplayNameFromHeader(sidecar_dir / (name + ".header"));
      if (info.display_name.empty())
        info.display_name = name;
      info.enabled = enabled;
      REXLOG_INFO(bd, "dlc discovered pack: {}", name);
      list.push_back(std::move(info));
      changed = true;
    }
  }

  return changed;
}

} // namespace

// Sets root_ but does not scan: the enable flags a scan reads come from the
// profile, which is not known yet. SetProfile/Reload do the one scan.
void DLCCatalog::Init(const std::filesystem::path &dlc_root) {
  std::lock_guard lock(mutex_);
  root_ = dlc_root;
}

void DLCCatalog::SetProfile(const std::filesystem::path &profile) {
  std::lock_guard lock(mutex_);
  profile_ = profile;
}

void DLCCatalog::ScanLocked() {
  packages_.clear();

  auto enabled_map = LoadEnabledState();
  std::error_code ec;

  if (!std::filesystem::is_directory(root_, ec))
    return;

  for (const auto &entry : std::filesystem::directory_iterator(root_, ec)) {
    if (!entry.is_directory())
      continue;
    auto name = entry.path().filename().string();
    if (name == "dlc_thumbs" || name == ".headers")
      continue;

    DLCPackage pkg;
    pkg.name = name;
    pkg.display_name = name;
    pkg.root = entry.path();
    for (auto candidate :
         {pkg.root / "data" / "Download", pkg.root / "Download"}) {
      if (std::filesystem::is_directory(candidate, ec)) {
        pkg.download_dir = std::move(candidate);
        break;
      }
    }
    if (pkg.download_dir.empty()) {
      REXLOG_WARN(bd, "dlc '{}' has no Download dir, ignoring", name);
      continue;
    }

    pkg.package_id =
        ParsePackageId(pkg.download_dir.parent_path() / "info.txt");

    if (!std::filesystem::is_regular_file(pkg.download_dir / "DL_Param.ipk",
                                          ec))
      REXLOG_WARN(bd, "dlc pack '{}' has no DL_Param.ipk", pkg.name);

    auto it = enabled_map.find(pkg.name);
    pkg.enabled = it == enabled_map.end() ? true : it->second;
    packages_.push_back(std::move(pkg));
  }

  std::sort(
      packages_.begin(), packages_.end(),
      [](const DLCPackage &a, const DLCPackage &b) { return a.name < b.name; });

  auto enabled = std::count_if(packages_.begin(), packages_.end(),
                               [](const DLCPackage &p) { return p.enabled; });
  REXLOG_INFO(bd, "dlc scanned {} pack(s), {} enabled", packages_.size(), enabled);
  for (const auto &pkg : packages_) {
    REXLOG_DEBUG(bd, "dlc   pack '{}' package_id={} {}", pkg.name, pkg.package_id,
             pkg.enabled ? "enabled" : "disabled");
  }
}

void DLCCatalog::LoadMetadataLocked() {
  std::vector<DLCPackage> metadata;

  auto toml_path = root_ / "dlc.toml";
  if (std::filesystem::exists(toml_path)) {
    try {
      auto tbl = toml::parse_file(toml_path.string());
      if (auto arr = tbl["dlc"].as_array()) {
        for (auto &elem : *arr) {
          auto *t = elem.as_table();
          if (!t)
            continue;

          DLCPackage info;
          if (auto v = (*t)["file_name"].value<std::string>())
            info.name = *v;
          if (auto v = (*t)["display_name"].value<std::string>())
            info.display_name = *v;
          if (auto v = (*t)["description"].value<std::string>())
            info.description = *v;
          if (auto v = (*t)["publisher"].value<std::string>())
            info.publisher = *v;

          if (info.name.empty())
            continue;
          if (info.display_name.empty())
            info.display_name = info.name;

          metadata.push_back(std::move(info));
        }
      }
    } catch (const toml::parse_error &e) {
      REXLOG_ERROR(bd, "dlc failed to parse dlc.toml: {}", e.what());
    }
  }

  std::vector<std::pair<std::string, bool>> packs;
  packs.reserve(packages_.size());
  for (const auto &pkg : packages_)
    packs.emplace_back(pkg.name, pkg.enabled);

  bool changed = ReconcileDLCList(metadata, packs, root_ / ".headers");

  for (auto &pkg : packages_) {
    auto it =
        std::find_if(metadata.begin(), metadata.end(),
                     [&](const DLCPackage &m) { return m.name == pkg.name; });
    if (it == metadata.end())
      continue;
    pkg.display_name = it->display_name;
    pkg.description = it->description;
    pkg.publisher = it->publisher;
  }

  if (changed)
    SaveMetadataLocked();
}

void DLCCatalog::SaveMetadataLocked() {
  auto toml_path = root_ / "dlc.toml";

  toml::array arr;
  for (const auto &pkg : packages_) {
    toml::table t;
    t.insert("file_name", pkg.name);
    t.insert("display_name", pkg.display_name);
    t.insert("description", pkg.description);
    t.insert("publisher", pkg.publisher);
    arr.push_back(std::move(t));
  }

  toml::table root;
  root.insert("dlc", std::move(arr));

  std::ofstream ofs(toml_path);
  if (!ofs) {
    REXLOG_ERROR(bd, "dlc cannot open dlc.toml for write: {}", toml_path.string());
    return;
  }
  ofs << root;
  if (!ofs) {
    REXLOG_ERROR(bd, "dlc failed writing dlc.toml: {}", toml_path.string());
    return;
  }

  REXLOG_INFO(bd, "dlc saved dlc.toml ({} entries)", packages_.size());
}

std::map<std::string, bool> DLCCatalog::LoadEnabledState() const {
  if (profile_.empty())
    return {};
  auto parsed = ParseDLCStateTOML(profile_ / "dlc_state.toml");
  return parsed.value_or(std::map<std::string, bool>{});
}

bool DLCCatalog::SaveEnabledState(
    const std::map<std::string, bool> &state) const {
  if (profile_.empty())
    return false;
  std::error_code ec;
  std::filesystem::create_directories(profile_, ec);
  toml::array arr;
  for (const auto &[name, en] : state) {
    toml::table t;
    t.insert("file_name", name);
    t.insert("enabled", en);
    arr.push_back(std::move(t));
  }
  toml::table root;
  root.insert("dlc", std::move(arr));
  std::ofstream ofs(profile_ / "dlc_state.toml");
  if (!ofs) {
    REXLOG_ERROR(bd, "dlc cannot write dlc_state.toml in {}", profile_.string());
    return false;
  }
  ofs << root;
  return static_cast<bool>(ofs);
}

void DLCCatalog::ReloadLocked() {
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  ScanLocked();
  LoadMetadataLocked();
  std::sort(packages_.begin(), packages_.end(),
            [](const DLCPackage &a, const DLCPackage &b) {
              return a.display_name < b.display_name;
            });
}

void DLCCatalog::Reload() {
  std::lock_guard lock(mutex_);
  if (root_.empty())
    return;
  ReloadLocked();
}

void DLCCatalog::FlushLocked() {
  std::map<std::string, bool> state;
  for (const auto &pkg : packages_)
    state[pkg.name] = pkg.enabled;
  if (SaveEnabledState(state))
    REXLOG_INFO(bd, "dlc saved dlc_state.toml ({} entries)", state.size());
  else
    REXLOG_WARN(bd, 
        "dlc dlc_state.toml not saved (no profile bound or write failed)");
}

size_t DLCCatalog::Count() const {
  std::lock_guard lock(mutex_);
  return packages_.size();
}

const DLCPackage &DLCCatalog::At(size_t i) const {
  std::lock_guard lock(mutex_);
  if (i >= packages_.size())
    return kEmptyPackage;
  return packages_[i];
}

bool DLCCatalog::IsEnabled(size_t i) const {
  std::lock_guard lock(mutex_);
  return i < packages_.size() && packages_[i].enabled;
}

bool DLCCatalog::SetEnabled(size_t i, bool on) {
  std::lock_guard lock(mutex_);
  if (i >= packages_.size() || packages_[i].enabled == on)
    return false;
  packages_[i].enabled = on;
  REXLOG_INFO(bd, "dlc '{}' -> {}", packages_[i].display_name,
          on ? "enabled" : "disabled");
  FlushLocked();
  return true;
}

bool DLCCatalog::Remove(size_t i) {
  std::lock_guard lock(mutex_);
  if (i >= packages_.size()) {
    REXLOG_ERROR(bd, "dlc Remove: index {} out of range", i);
    return false;
  }

  const DLCPackage pkg = packages_[i];
  if (pkg.name.empty() || pkg.name.find_first_of("/\\") != std::string::npos) {
    REXLOG_ERROR(bd, "dlc Remove: refusing suspicious pack name '{}'", pkg.name);
    return false;
  }

  REXLOG_INFO(bd, "dlc deleting '{}' ({})", pkg.display_name, pkg.name);

  // Before the delete: a mount holds its archive open, and one left registered
  // would go on answering for a pack that is gone.
  UnmountArchives(pkg);

  std::error_code ec;
  std::filesystem::remove_all(root_ / pkg.name, ec);
  if (ec) {
    REXLOG_ERROR(bd, "dlc failed to remove pack dir: {}", ec.message());
    return false;
  }
  std::filesystem::remove(root_ / ".headers" / (pkg.name + ".header"), ec);
  RemovePublished(pkg.name);

  packages_.erase(packages_.begin() + static_cast<ptrdiff_t>(i));
  SaveMetadataLocked();
  FlushLocked();
  return true;
}

} // namespace bd::vfs
