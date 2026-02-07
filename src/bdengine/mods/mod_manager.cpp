/**
 * @file        bdengine/mod_manager.cpp
 *
 * @brief       Mod manager: override map, mod ordering, CSV generation.
 *
 *              Override map is built once at init from mods/mod_order.txt
 *              (last-wins priority). File I/O hooks live in vfs.cpp.
 *
 *              CVar bd_mod_log enables file access tracking.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include "bdengine/mods/mod_manager.h"
#include "bdengine/common/logging.h"
#include "bdengine/mods/vfs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>

#include <toml++/toml.h>

#include <rex/types.h>
#include <rex/cvar.h>
#include <rex/logging/api.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

// CVars
REXCVAR_DEFINE_BOOL(bd_mod_log, false, "Blue Dragon",
                    "Log all file accesses to mods/file_access_log.csv");

namespace bd {

static ModManager s_mod_manager;

ModManager &GetModManager() { return s_mod_manager; }

// Guest memory addresses
inline constexpr u32 kBasePathAddr = 0x827A84F8;
inline constexpr u32 kLocaleIdAddr = 0x827A8578;
inline constexpr u32 kHeapHandleAddr = 0x827A8580;
inline constexpr u32 kLocaleTableAddr = 0x82780A24;
inline constexpr u32 kYYIndexArrayAddr = 0x82775710;
inline constexpr u32 kYYSelectorAddr = 0x82DC40D8;

static u8 *GuestBase() {
  auto *ks = rex::system::kernel_state();
  return (ks && ks->memory()) ? ks->memory()->virtual_membase() : nullptr;
}

static std::string ReadGuestString(u32 addr) {
  auto *b = GuestBase();
  if (!b)
    return {};
  return reinterpret_cast<const char *>(b + addr);
}

static i32 ReadGuestBE32(u32 addr) {
  auto *b = GuestBase();
  if (!b)
    return 0;
  return static_cast<i32>(rex::memory::load_and_swap<u32>(b + addr));
}

static std::string ReadLocaleString(int locale_index) {
  auto *b = GuestBase();
  if (!b)
    return {};
  auto table_entry_addr = kLocaleTableAddr + locale_index * 4;
  auto str_addr = rex::memory::load_and_swap<u32>(b + table_entry_addr);
  if (!str_addr)
    return {};
  return reinterpret_cast<const char *>(b + str_addr);
}

/**
 * @brief Split normalized path into pack name + inner path.
 */
static std::pair<std::string, std::string>
SplitPackPath(const std::string &path) {
  auto sep = path.find('\\');
  if (sep == std::string::npos)
    return {path, ""};
  return {path.substr(0, sep), path.substr(sep + 1)};
}

void ModManager::Init(const std::filesystem::path &game_data_root) {
  namespace fs = std::filesystem;

  cached_base_path_ = ReadGuestString(kBasePathAddr);
  game_data_root_ = game_data_root;

  std::string log_file_str = REXCVAR_GET(log_file);
  if (!log_file_str.empty()) {
    log_dir_ = std::filesystem::path(log_file_str).parent_path();
  } else {
    log_dir_ = game_data_root / "mods";
  }

  auto mods_dir = game_data_root / "mods";
  if (!fs::exists(mods_dir) || !fs::is_directory(mods_dir)) {
    BD_INFO("[mods] no mods/ directory at {}", mods_dir.string());
    return;
  }

  auto order_file = mods_dir / "mod_order.txt";
  if (!fs::exists(order_file)) {
    BD_WARN("[mods] no mod_order.txt found in {}", mods_dir.string());
    return;
  }

  std::vector<std::string> mod_names;
  {
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
      mod_names.push_back(std::move(name));
    }
  }

  for (const auto &mod_name : mod_names) {
    auto mod_dir = mods_dir / mod_name;
    if (!fs::exists(mod_dir) || !fs::is_directory(mod_dir)) {
      BD_WARN("[mods] mod folder not found: {}", mod_name);
      continue;
    }

    std::error_code ec;
    for (auto &entry : fs::recursive_directory_iterator(mod_dir, ec)) {
      if (!entry.is_regular_file())
        continue;

      auto relative = fs::relative(entry.path(), mod_dir, ec);
      if (ec)
        continue;

      auto key = relative.string();
      std::transform(key.begin(), key.end(), key.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      std::replace(key.begin(), key.end(), '/', '\\');

      override_map_[key] = entry.path();
    }

    if (ec) {
      BD_ERROR("[mods] error walking {}: {}", mod_name, ec.message());
    }
  }

  BD_INFO("[mods] loaded {} mod(s), {} file override(s)", mod_names.size(),
          override_map_.size());
  for (const auto &[key, path] : override_map_) {
    BD_INFO("[mods]   '{}' -> '{}'", key, path.string());
  }

  mod_names_ = mod_names;

  // Discover all mod folders (for toggle UI)
  all_mod_names_ = mod_names_;
  if (std::filesystem::is_directory(mods_dir)) {
    for (auto &entry : std::filesystem::directory_iterator(mods_dir)) {
      if (!entry.is_directory())
        continue;
      auto name = entry.path().filename().string();
      if (name == "." || name == "..")
        continue;
      if (std::find(all_mod_names_.begin(), all_mod_names_.end(), name) ==
          all_mod_names_.end()) {
        all_mod_names_.push_back(name);
      }
    }
  }

  mod_infos_.clear();
  mod_infos_.reserve(all_mod_names_.size());
  for (const auto &name : all_mod_names_) {
    mod_infos_.push_back(ParseModToml(mods_dir / name, name));
  }

  LoadSummary();
}

/**
 * @brief Restore cumulative counts from previous sessions.
 */
void ModManager::LoadSummary() {
  auto csv_path = log_dir_ / "file_access_summary.csv";
  std::ifstream ifs(csv_path);
  if (!ifs)
    return; // first run, nothing to load

  std::string line;
  std::getline(ifs, line);

  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;

    // Parse: pack,inner_path,total_accesses,total_overrides[,total_misses]
    std::istringstream ss(line);
    std::string pack, inner, accesses_str, overrides_str, misses_str;
    if (!std::getline(ss, pack, ','))
      continue;
    if (!std::getline(ss, inner, ','))
      continue;
    if (!std::getline(ss, accesses_str, ','))
      continue;
    if (!std::getline(ss, overrides_str, ','))
      continue;
    std::getline(ss, misses_str, ','); // optional, old files won't have it

    auto key = pack + "\\" + inner;
    auto &entry = access_log_[key];
    entry.pack = std::move(pack);
    entry.inner_path = std::move(inner);
    entry.access_count = std::strtoull(accesses_str.c_str(), nullptr, 10);
    entry.override_count = std::strtoull(overrides_str.c_str(), nullptr, 10);
    entry.miss_count =
        misses_str.empty() ? 0 : std::strtoull(misses_str.c_str(), nullptr, 10);
  }

  if (!access_log_.empty()) {
    BD_TRACE("[mods] restored {} entries from previous summary",
             access_log_.size());
  }
}

void ModManager::EnsureInitialized() {
  std::call_once(init_flag_, [this] {
    auto *ks = rex::system::kernel_state();
    if (!ks || !ks->emulator()) {
      BD_WARN("[mods] runtime not available, skipping mod init");
      return;
    }
    const auto &root = ks->emulator()->game_data_root();
    if (root.empty()) {
      BD_WARN("[mods] game_data_root not set, skipping mod init");
      return;
    }
    Init(root);
    LogDlcStatus();
  });
}

/**
 * @brief DLC diagnostic: log what ContentManager sees.
 */
void ModManager::LogDlcStatus() {
  auto *ks = rex::system::kernel_state();
  if (!ks) {
    BD_WARN("[dlc] kernel_state not available");
    return;
  }

  auto *cm = ks->content_manager();
  if (!cm) {
    BD_WARN("[dlc] content_manager not available");
    return;
  }

  // Query marketplace content (DLC) - xuid=0 (common), device_id=1 (HDD)
  auto items =
      cm->ListContent(1, 0, rex::system::XContentType::kMarketplaceContent);

  BD_INFO("[dlc] ContentManager found {} marketplace content item(s)",
          items.size());
  for (const auto &item : items) {
    u32 tid = item.title_id;
    u32 ctype = static_cast<u32>(
        static_cast<rex::system::XContentType>(item.content_type));
    BD_INFO("[dlc]   file_name='{}' title_id={:08X} content_type={:08X}",
            item.file_name(), tid, ctype);
  }

  // Also log the content root path for reference
  auto root = ks->emulator()->user_data_root();
  auto title_id = ks->title_id();
  BD_INFO("[dlc] content root: {} | title_id: {:08X}", root.string(), title_id);
}

const std::filesystem::path *
ModManager::FindOverride(const std::string &normalized_path) const {
  auto it = override_map_.find(normalized_path);
  if (it != override_map_.end())
    return &it->second;
  return nullptr;
}

std::string ModManager::NormalizePath(const char *raw_path) const {
  std::string path(raw_path);

  auto colon = path.find(':');
  if (colon != std::string::npos && colon + 1 < path.size()) {
    size_t start = colon + 1;
    if (start < path.size() && (path[start] == '\\' || path[start] == '/'))
      ++start;
    path = path.substr(start);
  }

  if (!cached_base_path_.empty() && path.size() >= cached_base_path_.size()) {
    bool match = true;
    for (size_t i = 0; i < cached_base_path_.size() && match; ++i) {
      match = std::tolower(static_cast<unsigned char>(path[i])) ==
              std::tolower(static_cast<unsigned char>(cached_base_path_[i]));
    }
    if (match)
      path = path.substr(cached_base_path_.size());
  }

  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::replace(path.begin(), path.end(), '/', '\\');

  if (!path.empty() && path[0] == '\\')
    path = path.substr(1);

  return path;
}

std::string ModManager::LocalizePath(const std::string &path) {
  std::string result = path;

  auto xx_pos = result.find("_xx.");
  if (xx_pos != std::string::npos) {
    int locale_id = ReadGuestBE32(kLocaleIdAddr);
    auto locale_str = ReadLocaleString(locale_id);
    if (!locale_str.empty())
      result.replace(xx_pos, 3, locale_str);
    return result;
  }

  auto yy_pos = result.find("_yy.");
  if (yy_pos != std::string::npos) {
    int selector = ReadGuestBE32(kYYSelectorAddr);
    int yy_index = ReadGuestBE32(kYYIndexArrayAddr + selector * 4);
    auto locale_str = ReadLocaleString(yy_index);
    if (!locale_str.empty())
      result.replace(yy_pos, 3, locale_str);
    return result;
  }

  auto zz_pos = result.find("_zz.");
  if (zz_pos != std::string::npos) {
    int locale_id = ReadGuestBE32(kLocaleIdAddr);
    if (locale_id != 0) {
      auto locale_str = ReadLocaleString(locale_id);
      if (!locale_str.empty())
        result.replace(zz_pos, 3, locale_str);
    } else {
      result.erase(zz_pos, 3);
    }
    return result;
  }

  return result;
}

static std::string NowLocalTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
  localtime_s(&local_tm, &tt);
  char buf[20];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
  return buf;
}

void ModManager::RecordAccess(const std::string &normalized_path,
                              FileSource source) {
  bool should_flush = false;
  {
    std::lock_guard lock(access_mutex_);

    auto &entry = access_log_[normalized_path];
    if (entry.access_count == 0) {
      auto [pack, inner] = SplitPackPath(normalized_path);
      entry.pack = pack;
      entry.inner_path = inner;
    }
    entry.access_count++;
    if (source == FileSource::Mod)
      entry.override_count++;
    if (source == FileSource::Missing)
      entry.miss_count++;

    auto [pack, inner] = SplitPackPath(normalized_path);
    pending_detail_.push_back({
        NowLocalTimestamp(),
        std::move(pack),
        std::move(inner),
        source,
    });

    should_flush = (pending_detail_.size() % 100 == 0);
  }

  if (should_flush)
    FlushAccessLog();
}

static const char *FileSourceStr(FileSource s) {
  switch (s) {
  case FileSource::Mod:
    return "mod";
  case FileSource::Ipk:
    return "ipk";
  case FileSource::Disk:
    return "disk";
  case FileSource::Missing:
    return "missing";
  }
  return "unknown";
}

FileSource
ModManager::ClassifyOriginal(const std::string &localized_path) const {
  std::error_code ec;
  if (std::filesystem::exists(game_data_root_ / localized_path, ec))
    return FileSource::Disk;
  return FileSource::Ipk;
}

void ModManager::FlushAccessLog() {
  std::lock_guard lock(access_mutex_);

  if (access_log_.empty() && pending_detail_.empty())
    return;

  std::filesystem::create_directories(log_dir_);

  // Summary CSV (truncate + rewrite with cumulative totals)
  if (!access_log_.empty()) {
    auto summary_path = log_dir_ / "file_access_summary.csv";
    std::ofstream ofs(summary_path, std::ios::trunc);
    if (!ofs) {
      BD_ERROR("[mods] failed to write summary to {}", summary_path.string());
    } else {
      ofs << "pack,inner_path,total_accesses,total_overrides,total_misses\n";

      std::vector<const FileAccessEntry *> sorted;
      sorted.reserve(access_log_.size());
      for (const auto &[key, entry] : access_log_)
        sorted.push_back(&entry);
      std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) {
        if (a->pack != b->pack)
          return a->pack < b->pack;
        return a->inner_path < b->inner_path;
      });

      for (const auto *e : sorted) {
        ofs << e->pack << "," << e->inner_path << "," << e->access_count << ","
            << e->override_count << "," << e->miss_count << "\n";
      }

      BD_TRACE("[mods] wrote summary ({} entries) to {}", sorted.size(),
               summary_path.string());
    }
  }

  // Detail CSV (append-only with timestamps)
  if (!pending_detail_.empty()) {
    auto detail_path = log_dir_ / "file_access_detail.csv";
    bool needs_header = !std::filesystem::exists(detail_path) ||
                        std::filesystem::file_size(detail_path) == 0;

    std::ofstream ofs(detail_path, std::ios::app);
    if (!ofs) {
      BD_ERROR("[mods] failed to write detail log to {}", detail_path.string());
    } else {
      if (needs_header)
        ofs << "datetime,pack,inner_path,source\n";

      for (const auto &d : pending_detail_) {
        ofs << d.datetime << "," << d.pack << "," << d.inner_path << ","
            << FileSourceStr(d.source) << "\n";
      }

      BD_TRACE("[mods] appended {} detail entries to {}",
               pending_detail_.size(), detail_path.string());
      pending_detail_.clear();
    }
  }
}

ModInfo ModManager::ParseModToml(const std::filesystem::path &mod_dir,
                                 const std::string &folder_name) {
  ModInfo info;
  info.name = folder_name;

  auto toml_path = mod_dir / "mod.toml";
  if (!std::filesystem::exists(toml_path))
    return info;

  try {
    auto tbl = toml::parse_file(toml_path.string());
    auto mod = tbl["mod"];

    if (auto v = mod["name"].value<std::string>())
      info.name = *v;
    if (auto v = mod["author"].value<std::string>())
      info.author = *v;
    if (auto v = mod["version"].value<std::string>())
      info.version = *v;
    if (auto v = mod["description"].value<std::string>())
      info.description = *v;
    if (auto v = mod["created"].value<std::string>())
      info.created = *v;
    else if (auto d = mod["created"].value<toml::date>())
      info.created =
          std::format("{:04}-{:02}-{:02}", d->year, d->month, d->day);
    if (auto v = mod["image"].value<std::string>()) {
      auto img = mod_dir / *v;
      if (std::filesystem::exists(img))
        info.image_path = img.string();
    }
  } catch (const toml::parse_error &e) {
    BD_WARN("[mods] failed to parse {}: {}", toml_path.string(), e.what());
  }

  return info;
}

const ModInfo &ModManager::GetModInfo(size_t index) const {
  static const ModInfo s_empty{};
  if (index >= mod_infos_.size())
    return s_empty;
  return mod_infos_[index];
}

void ModManager::SaveModOrder() {
  auto order_path = game_data_root_ / "mods" / "mod_order.txt";
  std::ofstream ofs(order_path, std::ios::trunc);
  if (!ofs) {
    BD_ERROR("[mods] failed to write mod_order.txt");
    return;
  }
  for (const auto &name : mod_names_) {
    ofs << name << "\n";
  }
  BD_INFO("[mods] saved mod_order.txt ({} entries)", mod_names_.size());
}

void ModManager::Reload() {
  override_map_.clear();
  mod_names_.clear();

  auto mods_dir = game_data_root_ / "mods";
  auto order_path = mods_dir / "mod_order.txt";

  if (!std::filesystem::exists(order_path)) {
    BD_WARN("[mods] no mod_order.txt found during reload");
    return;
  }

  std::ifstream ifs(order_path);
  std::string line;
  while (std::getline(ifs, line)) {
    if (line.empty())
      continue;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    if (line.empty())
      continue;

    mod_names_.push_back(line);

    auto mod_dir = mods_dir / line;
    if (!std::filesystem::is_directory(mod_dir)) {
      BD_WARN("[mods] mod folder missing: {}", mod_dir.string());
      continue;
    }

    for (auto &entry : std::filesystem::recursive_directory_iterator(mod_dir)) {
      if (!entry.is_regular_file())
        continue;
      auto rel = std::filesystem::relative(entry.path(), mod_dir);
      std::string key = rel.string();
      std::transform(key.begin(), key.end(), key.begin(), ::tolower);
      std::replace(key.begin(), key.end(), '/', '\\');
      override_map_[key] = entry.path();
    }
  }

  BD_INFO("[mods] reloaded {} mod(s), {} override(s)", mod_names_.size(),
          override_map_.size());

  // Re-discover all mod folders (same as Init)
  all_mod_names_ = mod_names_;
  if (std::filesystem::is_directory(mods_dir)) {
    for (auto &entry : std::filesystem::directory_iterator(mods_dir)) {
      if (!entry.is_directory())
        continue;
      auto name = entry.path().filename().string();
      if (name == "." || name == "..")
        continue;
      if (std::find(all_mod_names_.begin(), all_mod_names_.end(), name) ==
          all_mod_names_.end()) {
        all_mod_names_.push_back(name);
      }
    }
  }

  mod_infos_.clear();
  mod_infos_.reserve(all_mod_names_.size());
  for (const auto &name : all_mod_names_) {
    mod_infos_.push_back(ParseModToml(mods_dir / name, name));
  }
}

void ModManager::ToggleMod(size_t index) {
  if (index >= all_mod_names_.size())
    return;

  const auto &name = all_mod_names_[index];
  auto it = std::find(mod_names_.begin(), mod_names_.end(), name);

  if (it != mod_names_.end()) {
    mod_names_.erase(it);
    BD_INFO("[modmgr] disabled mod '{}'", name);
  } else {
    mod_names_.push_back(name);
    BD_INFO("[modmgr] enabled mod '{}'", name);
  }
}

bool ModManager::IsModEnabled(size_t index) const {
  if (index >= all_mod_names_.size())
    return false;
  const auto &name = all_mod_names_[index];
  return std::find(mod_names_.begin(), mod_names_.end(), name) !=
         mod_names_.end();
}

void ModManager::SwapMods(size_t a, size_t b) {
  if (a >= all_mod_names_.size() || b >= all_mod_names_.size())
    return;
  std::swap(all_mod_names_[a], all_mod_names_[b]);
  if (a < mod_infos_.size() && b < mod_infos_.size())
    std::swap(mod_infos_[a], mod_infos_[b]);
}

bool ModManager::DeleteMod(size_t index) {
  if (index >= all_mod_names_.size()) {
    BD_ERROR("[modmgr] DeleteMod: index {} out of range", index);
    return false;
  }

  const auto &name = all_mod_names_[index];
  auto mod_dir = game_data_root_ / "mods" / name;

  auto it = std::find(mod_names_.begin(), mod_names_.end(), name);
  if (it != mod_names_.end())
    mod_names_.erase(it);

  all_mod_names_.erase(all_mod_names_.begin() +
                       static_cast<ptrdiff_t>(index));
  if (index < mod_infos_.size())
    mod_infos_.erase(mod_infos_.begin() + static_cast<ptrdiff_t>(index));

  std::error_code ec;
  std::filesystem::remove_all(mod_dir, ec);
  if (ec) {
    BD_ERROR("[modmgr] failed to delete mod folder '{}': {}", mod_dir.string(),
             ec.message());
    // Already removed from lists - save what we have
  } else {
    BD_INFO("[modmgr] deleted mod folder '{}'", mod_dir.string());
  }

  SaveModOrder();
  return true;
}

void ModManager::RegisterPreviewHandlers() {
  preview_vfs_paths_.clear();
  for (size_t i = 0; i < mod_infos_.size(); ++i) {
    const auto &info = mod_infos_[i];
    if (info.image_path.empty())
      continue;

    std::string vfs_path =
        "d2anime\\modmgr\\res\\preview_" + std::to_string(i) + ".dds";
    std::string disk_path = info.image_path;

    bd::vfs::RegisterContentHandler(
        vfs_path, [disk_path]() -> std::vector<u8> {
          std::ifstream ifs(disk_path, std::ios::binary | std::ios::ate);
          if (!ifs)
            return {};
          auto size = ifs.tellg();
          ifs.seekg(0);
          std::vector<u8> data(static_cast<size_t>(size));
          ifs.read(reinterpret_cast<char *>(data.data()), size);
          return data;
        });

    preview_vfs_paths_.push_back(vfs_path);
    BD_INFO("[modmgr] registered preview VFS: {} -> {}", vfs_path, disk_path);
  }
}

void ModManager::UnregisterPreviewHandlers() {
  for (const auto &path : preview_vfs_paths_) {
    bd::vfs::UnregisterContentHandler(path);
  }
  preview_vfs_paths_.clear();
}

} // namespace bd
