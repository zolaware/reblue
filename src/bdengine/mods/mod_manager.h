/**
 * @file        bdengine/mod_manager.h
 *
 * @brief       Mod manager: loose file overrides for .ipk pack data.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bd {

/**
 * @brief Where a file access was ultimately resolved.
 */
enum class FileSource : u8 { Mod, Ipk, Disk, Missing };

/**
 * @brief Cumulative per-file stats for the summary CSV (persists across sessions).
 */
struct FileAccessEntry {
  std::string pack;       // source pack name (first path component)
  std::string inner_path; // path within pack
  u64 access_count = 0;
  u64 override_count = 0;
  u64 miss_count = 0;
};

/**
 * @brief Single file access record for the detail CSV (append-only).
 */
struct DetailEntry {
  std::string datetime; // local time, YYYY-MM-DD HH:MM:SS
  std::string pack;
  std::string inner_path;
  FileSource source;
};

/**
 * @brief Metadata from a mod's mod.toml file.
 */
struct ModInfo {
  std::string name; // display name (falls back to folder name)
  std::string author;
  std::string version;
  std::string description;
  std::string created;
  std::string image_path; // absolute path to preview DDS, empty if none
};

class ModManager {
public:
  /**
   * @brief Lazily initialize on first call (thread-safe via std::call_once).
   */
  void EnsureInitialized();

  /**
   * @brief Returns pointer to override path if found, nullptr if no override.
   */
  const std::filesystem::path *
  FindOverride(const std::string &normalized_path) const;

  /**
   * @brief Strip drive prefix, strip g_bdBasePath, lowercase, normalize separators.
   */
  std::string NormalizePath(const char *raw_path) const;

  /**
   * @brief Replace _xx./_yy./_zz. locale markers using g_bdLocaleId from guest
   *        memory.
   */
  static std::string LocalizePath(const std::string &path);

  /**
   * @brief Record a file access (thread-safe). Called from hooks when bd_mod_log is
   *        on.
   */
  void RecordAccess(const std::string &normalized_path, FileSource source);

  /**
   * @brief Classify a non-overridden found file as Ipk or Disk.
   */
  FileSource ClassifyOriginal(const std::string &localized_path) const;

  /**
   * @brief Write accumulated access log to CSV. Called on shutdown or manually.
   */
  void FlushAccessLog();

  /**
   * @brief Returns the list of mod folder names from mod_order.txt in load order.
   */
  const std::vector<std::string> &GetModNames() const { return mod_names_; }

  void SaveModOrder();
  void Reload();
  void ToggleMod(size_t index);
  void SwapMods(size_t a, size_t b);
  bool DeleteMod(size_t index);
  bool IsModEnabled(size_t index) const;
  const std::vector<std::string> &GetAllModNames() const {
    return all_mod_names_;
  }
  const std::filesystem::path &GetGameDataRoot() const {
    return game_data_root_;
  }

  const ModInfo &GetModInfo(size_t index) const;
  static ModInfo ParseModToml(const std::filesystem::path &mod_dir,
                              const std::string &folder_name);

  void RegisterPreviewHandlers();
  void UnregisterPreviewHandlers();

  /**
   * @brief Diagnostic: log what the ContentManager sees for DLC.
   */
  void LogDlcStatus();

private:
  void Init(const std::filesystem::path &game_data_root);
  void LoadSummary();

  std::unordered_map<std::string, std::filesystem::path> override_map_;
  std::string cached_base_path_;
  std::filesystem::path game_data_root_;
  std::filesystem::path log_dir_;
  std::once_flag init_flag_;

  // Access tracking (guarded by access_mutex_)
  std::mutex access_mutex_;
  std::unordered_map<std::string, FileAccessEntry> access_log_;
  std::vector<DetailEntry> pending_detail_;
  std::vector<std::string> mod_names_;
  std::vector<std::string> all_mod_names_;
  std::vector<ModInfo> mod_infos_;
  std::vector<std::string> preview_vfs_paths_;
};

/**
 * @brief Singleton accessor.
 */
ModManager &GetModManager();

} // namespace bd
