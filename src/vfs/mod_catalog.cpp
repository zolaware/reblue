/**
 * @file    vfs/mod_catalog.cpp
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include "vfs/mod_catalog.h"
#include "core/logging.h"
#include "vfs/mounts.h"

#include <algorithm>
#include <format>
#include <fstream>

#include <toml++/toml.h>

namespace bd::vfs {

namespace {

constexpr const char *kPreviewMount = "ui:mod-previews";

// mod_order.txt: one mod folder name per line, whitespace-trimmed, blank
// lines and '#' comment lines skipped.
std::vector<std::string>
ParseOrderFile(const std::filesystem::path &order_file) {
  std::vector<std::string> names;
  std::ifstream ifs(order_file);
  std::string line;
  while (std::getline(ifs, line)) {
    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
      continue;
    auto end = line.find_last_not_of(" \t\r\n");
    auto name = line.substr(start, end - start + 1);
    if (name.empty() || name[0] == '#')
      continue;
    names.push_back(std::move(name));
  }
  return names;
}

size_t CountEnabled(const std::vector<ModPackage> &packages) {
  size_t n = 0;
  for (const auto &pkg : packages)
    if (pkg.enabled)
      ++n;
  return n;
}

void MergeDbRows(const toml::table &tbl, const std::filesystem::path &toml_path,
                 DbRows &rows) {
  const auto *db = tbl["db"].as_table();
  if (!db)
    return;
  for (auto &&[name, node] : *db) {
    auto &table = rows[Key::FromRelative(name.str())];
    if (const auto row = node.value<std::string>()) {
      table.push_back(*row);
    } else if (const auto *list = node.as_array()) {
      for (const auto &element : *list) {
        if (const auto row = element.value<std::string>())
          table.push_back(*row);
        else
          BD_WARN("[mods] {} [db] '{}' holds a row that is not a string",
                  toml_path.string(), name.str());
      }
    } else {
      BD_WARN("[mods] {} [db] '{}' is neither a row nor an array of rows",
              toml_path.string(), name.str());
    }
  }
}

ModPackage MergeEntityDbRows(const std::filesystem::path &mod_dir,
                             ModPackage pkg) {
  namespace fs = std::filesystem;

  std::error_code ec;
  const fs::path entities = mod_dir / "entities";
  if (!fs::is_directory(entities, ec))
    return pkg;
  for (const auto &entry : fs::directory_iterator(entities, ec)) {
    const fs::path toml_path = entry.path() / "entity.toml";
    if (!entry.is_directory() || !fs::exists(toml_path, ec))
      continue;
    try {
      MergeDbRows(toml::parse_file(toml_path.string()), toml_path, pkg.db);
    } catch (const toml::parse_error &e) {
      BD_WARN("[mods] failed to parse {}: {}", toml_path.string(), e.what());
    }
  }
  return pkg;
}

} // namespace

std::filesystem::path ModCatalog::OrderFilePath() const {
  if (!profile_.empty())
    return profile_ / "mod_order.txt";
  return mods_root_ / "mod_order.txt";
}

ModPackage ModCatalog::ParseTOML(const std::filesystem::path &mod_dir,
                                 const std::string &folder_name) {
  namespace fs = std::filesystem;

  ModPackage pkg;
  pkg.folder = folder_name;
  pkg.name = folder_name;

  // A ShadowForge source tree names its toml after the mod instead.
  auto toml_path = mod_dir / "mod.toml";
  if (!fs::exists(toml_path))
    toml_path = mod_dir / (folder_name + ".toml");
  if (!fs::exists(toml_path))
    return MergeEntityDbRows(mod_dir, std::move(pkg));

  try {
    auto tbl = toml::parse_file(toml_path.string());
    MergeDbRows(tbl, toml_path, pkg.db);
    auto mod = tbl["mod"];

    if (auto v = mod["name"].value<std::string>())
      pkg.name = *v;
    if (auto v = mod["author"].value<std::string>())
      pkg.author = *v;
    if (auto v = mod["version"].value<std::string>())
      pkg.version = *v;
    if (auto v = mod["description"].value<std::string>())
      pkg.description = *v;
    if (auto v = mod["created"].value<std::string>())
      pkg.created = *v;
    else if (auto d = mod["created"].value<toml::date>())
      pkg.created = std::format("{:04}-{:02}-{:02}", d->year, d->month, d->day);
    if (auto v = mod["image"].value<std::string>()) {
      auto img = mod_dir / *v;
      if (std::filesystem::exists(img))
        pkg.image = img;
    }
  } catch (const toml::parse_error &e) {
    BD_WARN("[mods] failed to parse {}: {}", toml_path.string(), e.what());
  }

  return MergeEntityDbRows(mod_dir, std::move(pkg));
}

// Rebuilds packages_ from disk: every mod_order.txt entry (enabled, in file
// order) first, then every other folder under mods_root_ (disabled, in
// directory iteration order). A missing order file yields no enabled entries
// and every folder discovered as disabled, which is the ordinary state of a
// fresh profile, not an error. This layout is only where packages_ starts:
// Swap can reorder it afterward, so nothing downstream may assume enabled
// entries stay a prefix (see the comment on MountMods).
void ModCatalog::Discover() {
  namespace fs = std::filesystem;

  std::vector<std::string> enabled_names;
  auto order_file = OrderFilePath();
  if (fs::exists(order_file))
    enabled_names = ParseOrderFile(order_file);

  packages_.clear();

  for (const auto &name : enabled_names) {
    auto pkg = ParseTOML(mods_root_ / name, name);
    pkg.enabled = true;
    packages_.push_back(std::move(pkg));
  }

  if (fs::is_directory(mods_root_)) {
    std::error_code ec;
    for (auto &entry : fs::directory_iterator(mods_root_, ec)) {
      if (!entry.is_directory())
        continue;
      auto name = entry.path().filename().string();
      if (name == "." || name == "..")
        continue;
      if (std::find(enabled_names.begin(), enabled_names.end(), name) !=
          enabled_names.end())
        continue;
      auto pkg = ParseTOML(mods_root_ / name, name);
      pkg.enabled = false;
      packages_.push_back(std::move(pkg));
    }
  }
}

// Sets mods_root_ only. The enabled set and its order live under the
// profile, which Init does not have, so nothing here reads a folder yet:
// SetProfile's Reload does the one scan, once the profile is known.
void ModCatalog::Init(const std::filesystem::path &mods_root,
                      FileSystem &files) {
  mods_root_ = mods_root;
  files_ = &files;
}

void ModCatalog::SetProfile(const std::filesystem::path &profile) {
  profile_ = profile;
}

void ModCatalog::Reload() {
  namespace fs = std::filesystem;

  if (!fs::is_directory(mods_root_)) {
    BD_INFO("[mods] no mods/ directory at {}", mods_root_.string());
    UnmountMods();
    packages_.clear();
    return;
  }

  Discover();
  BD_INFO("[mods] reloaded {} mod(s), {} override(s)", CountEnabled(packages_),
          MountMods());
}

void ModCatalog::Flush() {
  auto order_path = OrderFilePath();
  std::error_code ec;
  std::filesystem::create_directories(order_path.parent_path(), ec);
  std::ofstream ofs(order_path, std::ios::trunc);
  if (!ofs) {
    BD_ERROR("[mods] failed to write {}", order_path.string());
    return;
  }
  size_t count = 0;
  for (const auto &pkg : packages_) {
    if (!pkg.enabled)
      continue;
    ofs << pkg.folder << "\n";
    ++count;
  }
  BD_INFO("[mods] saved mod_order.txt ({} entries)", count);
}

const ModPackage &ModCatalog::At(size_t i) const {
  static const ModPackage s_empty{};
  if (i >= packages_.size())
    return s_empty;
  return packages_[i];
}

bool ModCatalog::IsEnabled(size_t i) const {
  if (i >= packages_.size())
    return false;
  return packages_[i].enabled;
}

bool ModCatalog::IsEnabled(std::string_view folder) const {
  auto it =
      std::find_if(packages_.begin(), packages_.end(),
                   [&](const ModPackage &pkg) { return pkg.folder == folder; });
  return it != packages_.end() && it->enabled;
}

void ModCatalog::SetEnabled(size_t i, bool on) {
  if (i >= packages_.size())
    return;
  packages_[i].enabled = on;
  BD_INFO("[mods] {} mod '{}'", on ? "enabled" : "disabled",
          packages_[i].folder);
}

void ModCatalog::Enable(std::string_view folder) {
  auto it =
      std::find_if(packages_.begin(), packages_.end(),
                   [&](const ModPackage &pkg) { return pkg.folder == folder; });
  if (it == packages_.end()) {
    auto folder_str = std::string(folder);
    auto pkg = ParseTOML(mods_root_ / folder_str, folder_str);
    pkg.enabled = true;
    packages_.push_back(std::move(pkg));
  } else {
    it->enabled = true;
  }
  Flush();
  Reload();
}

void ModCatalog::Swap(size_t a, size_t b) {
  if (a >= packages_.size() || b >= packages_.size())
    return;
  std::swap(packages_[a], packages_[b]);
}

bool ModCatalog::Remove(size_t i) {
  if (i >= packages_.size()) {
    BD_ERROR("[mods] Remove: index {} out of range", i);
    return false;
  }

  auto mod_dir = mods_root_ / packages_[i].folder;

  std::error_code ec;
  std::filesystem::remove_all(mod_dir, ec);
  if (ec) {
    BD_ERROR("[mods] failed to delete mod folder '{}': {}", mod_dir.string(),
             ec.message());
  } else {
    BD_INFO("[mods] deleted mod folder '{}'", mod_dir.string());
  }

  packages_.erase(packages_.begin() + static_cast<ptrdiff_t>(i));

  Flush();
  return true;
}

// Mounts and mod_order.txt both walk packages_ in its current order and act
// on whichever entries are enabled, rather than assuming enabled entries
// form an unbroken prefix. Discover() always produces that prefix, but
// nothing here needs it to stay one to remain correct. A later enabled
// mod's files still win, in packages_ order, either way.
size_t ModCatalog::MountMods() {
  UnmountMods();

  size_t files = 0;
  for (const auto &pkg : packages_) {
    if (!pkg.enabled)
      continue;

    auto mod_dir = mods_root_ / pkg.folder;
    if (!std::filesystem::is_directory(mod_dir)) {
      BD_WARN("[mods] mod folder not found: {}", pkg.folder);
      continue;
    }

    auto mount = LooseMount::Scan(mod_dir);
    files += mount->KeyCount();

    auto name = "mod:" + pkg.folder;
    auto patch_name = name + ":db";
    files_->Add(name, kPriorityMod, std::move(mount));
    mount_names_.push_back(std::move(name));

    // Registered after the mod's own files so it sits above them: rows a mod
    // declares extend a table it also ships whole.
    if (pkg.db.empty())
      continue;
    files_->AddPatch(patch_name, kPriorityMod, pkg.db);
    mount_names_.push_back(std::move(patch_name));
  }
  return files;
}

void ModCatalog::UnmountMods() {
  for (const auto &name : mount_names_)
    files_->Remove(name);
  mount_names_.clear();
}

void ModCatalog::MountPreviews() {
  auto mount = std::make_shared<LooseMount>();
  for (size_t i = 0; i < packages_.size(); ++i) {
    const auto &pkg = packages_[i];
    if (pkg.image.empty())
      continue;
    mount->Set(Key::FromRelative("d2anime\\modmgr\\res\\preview_" +
                                 std::to_string(i) + ".dds"),
               pkg.image);
  }
  files_->Add(kPreviewMount, kPriorityGenerated, std::move(mount));
}

void ModCatalog::UnmountPreviews() { files_->Remove(kPreviewMount); }

} // namespace bd::vfs
