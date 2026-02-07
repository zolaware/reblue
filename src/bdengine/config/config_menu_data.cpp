/**
 * @file    bdengine/config_menu_data.cpp
 * @brief   Config menu data layer - VFS registration, mod/DLC queries,
 *          install operations.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/config/config_menu_data.h"
#include "bdengine/config/config_layout.h"
#include "bdengine/platform/file_dialogue.h"
#include "bdengine/common/logging.h"
#include "bdengine/resources/mark_config_dds.h"
#include "bdengine/mods/vfs.h"

#include <algorithm>
#include <fstream>

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

namespace {

// Static layout/template objects and DLC manager
bd::ConfigLayout s_config_layout;
bd::SectionTemplate s_section_tpl;
bd::ModItemTemplate s_mod_item_tpl;
bd::NodataTemplate s_nodata_tpl;
bd::DetailTemplate s_detail_tpl;
bd::DlcItemTemplate s_dlc_item_tpl;
bd::DlcDetailTemplate s_dlc_detail_tpl;
bd::DlcManager s_dlc_manager;
std::string s_dlc_thumb_vfs_path;

/**
 * @brief Convert string to bytes for VFS.
 */
std::vector<u8> StringToBytes(const std::string &s) {
  return {s.begin(), s.end()};
}

} // namespace

// Guest DLC re-enumeration
REX_IMPORT(__imp__gsContentFileManager_StartEnumeration, StartDlcEnum,
           u32(u32, u32, u32, u32));

static constexpr u32 kContentFileManagerAddr = 0x82DEBEE4;

namespace bd::config {

void RegisterVFS() {
  auto &mgr = bd::GetModManager();

  s_dlc_manager.SetDataDir(mgr.GetGameDataRoot() / "dlc");
  s_dlc_manager.Refresh();

  size_t modCount = mgr.GetAllModNames().size();
  size_t dlcCount = s_dlc_manager.GetDlcList().size();
  s_config_layout.setModCount(modCount);
  s_config_layout.setDlcCount(dlcCount);
  s_config_layout.setDlcList(&s_dlc_manager.GetDlcList());

  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr.csv", [] {
    return StringToBytes(s_config_layout.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_info.csv", [] {
    return StringToBytes(s_mod_item_tpl.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_detail.csv", [] {
    return StringToBytes(s_detail_tpl.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_section.csv", [] {
    return StringToBytes(s_section_tpl.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_nodata.csv", [] {
    return StringToBytes(s_nodata_tpl.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\res\\mark_config.dds", [] {
    return std::vector<u8>(kMarkConfigDds,
                                kMarkConfigDds + kMarkConfigDdsSize);
  });
  mgr.RegisterPreviewHandlers();

  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_dlcinfo.csv", [] {
    return StringToBytes(s_dlc_item_tpl.toCSV());
  });
  bd::vfs::RegisterContentHandler("d2anime\\modmgr\\l_modmgr_dlcdetail.csv", [] {
    return StringToBytes(s_dlc_detail_tpl.toCSV());
  });
}

void UnregisterVFS() {
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_info.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_detail.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_section.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_nodata.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\res\\mark_config.dds");
  bd::GetModManager().UnregisterPreviewHandlers();

  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_dlcinfo.csv");
  bd::vfs::UnregisterContentHandler("d2anime\\modmgr\\l_modmgr_dlcdetail.csv");
  if (!s_dlc_thumb_vfs_path.empty()) {
    bd::vfs::UnregisterContentHandler(s_dlc_thumb_vfs_path);
    s_dlc_thumb_vfs_path.clear();
  }
}

void SaveAndReload() {
  bd::GetModManager().SaveModOrder();
  bd::GetModManager().Reload();
  BD_INFO("[config] mod config saved and reloaded");
}

void ToggleMod(int index) {
  bd::GetModManager().ToggleMod(static_cast<size_t>(index));
}

void SwapMods(int a, int b) {
  bd::GetModManager().SwapMods(static_cast<size_t>(a), static_cast<size_t>(b));
}

// InstallMod helpers
static bool InstallModFromFolder(const std::filesystem::path &src,
                                 const std::filesystem::path &mods_dir,
                                 bd::ModManager &mgr) {
  if (!std::filesystem::exists(src / "mod.toml")) {
    BD_WARN("[config] selected folder has no mod.toml: {}", src.string());
    return false;
  }

  auto folder_name = src.filename().string();
  auto dest = mods_dir / folder_name;

  if (std::filesystem::exists(dest)) {
    BD_WARN("[config] mod folder already exists: {}", dest.string());
    return false;
  }

  std::error_code ec;
  std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive,
                        ec);
  if (ec) {
    BD_ERROR("[config] failed to copy mod: {}", ec.message());
    return false;
  }

  BD_INFO("[config] installed mod '{}' from {}", folder_name, src.string());

  {
    auto order_path = mods_dir / "mod_order.txt";
    std::ofstream ofs(order_path, std::ios::app);
    ofs << folder_name << "\n";
  }

  mgr.Reload();
  return true;
}

static bool InstallModFromZip(const std::filesystem::path &zip_path,
                              const std::filesystem::path &mods_dir,
                              bd::ModManager &mgr) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    BD_ERROR("[config] failed to open zip: {}", zip_path.string());
    return false;
  }

  int num_files = static_cast<int>(mz_zip_reader_get_num_files(&zip));

  // Check for mod.toml at root
  std::string prefix;
  bool found_mod_toml = false;

  if (mz_zip_reader_locate_file(&zip, "mod.toml", nullptr, 0) >= 0) {
    found_mod_toml = true;
  } else {
    // Check for single top-level folder containing mod.toml
    std::string single_dir;
    bool multiple_dirs = false;
    for (int i = 0; i < num_files; i++) {
      char fname[512];
      mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
      std::string name(fname);
      auto slash = name.find('/');
      if (slash == std::string::npos)
        slash = name.find('\\');
      if (slash != std::string::npos) {
        std::string dir = name.substr(0, slash);
        if (single_dir.empty())
          single_dir = dir;
        else if (dir != single_dir) {
          multiple_dirs = true;
          break;
        }
      }
    }
    if (!multiple_dirs && !single_dir.empty()) {
      std::string toml_path = single_dir + "/mod.toml";
      if (mz_zip_reader_locate_file(&zip, toml_path.c_str(), nullptr, 0) >=
          0) {
        found_mod_toml = true;
        prefix = single_dir + "/";
      }
    }
  }

  if (!found_mod_toml) {
    BD_WARN("[config] zip has no mod.toml: {}", zip_path.string());
    mz_zip_reader_end(&zip);
    return false;
  }

  std::string folder_name;
  if (prefix.empty()) {
    folder_name = zip_path.stem().string();
  } else {
    folder_name = prefix.substr(0, prefix.size() - 1);
  }

  auto dest = mods_dir / folder_name;
  if (std::filesystem::exists(dest)) {
    BD_WARN("[config] mod folder already exists: {}", dest.string());
    mz_zip_reader_end(&zip);
    return false;
  }

  std::filesystem::create_directories(dest);
  bool extract_ok = true;
  for (int i = 0; i < num_files; i++) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;

    char fname[512];
    mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
    std::string name(fname);

    std::string relative = name;
    if (!prefix.empty() && name.starts_with(prefix))
      relative = name.substr(prefix.size());

    auto out_path = dest / relative;
    std::filesystem::create_directories(out_path.parent_path());

    if (!mz_zip_reader_extract_to_file(&zip, i, out_path.string().c_str(),
                                       0)) {
      BD_ERROR("[config] failed to extract: {}", name);
      extract_ok = false;
      break;
    }
  }

  mz_zip_reader_end(&zip);

  if (!extract_ok) {
    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  BD_INFO("[config] installed mod '{}' from zip {}", folder_name,
          zip_path.string());

  {
    auto order_path = mods_dir / "mod_order.txt";
    std::ofstream ofs(order_path, std::ios::app);
    ofs << folder_name << "\n";
  }

  mgr.Reload();
  return true;
}

bool InstallMod() {
  auto selected = bd::OpenFileDialogue(L"Select Mod (zip or mod.toml)",
                                       L"Mod files (*.zip, mod.toml)",
                                       L"*.zip;mod.toml");
  if (!selected) {
    BD_INFO("[config] install cancelled");
    return false;
  }

  auto &path = *selected;
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  auto &mgr = bd::GetModManager();
  auto mods_dir = mgr.GetGameDataRoot() / "mods";
  std::filesystem::create_directories(mods_dir);

  if (ext == ".zip")
    return InstallModFromZip(path, mods_dir, mgr);
  else
    return InstallModFromFolder(path.parent_path(), mods_dir, mgr);
}

bool InstallDlc() {
  auto selected = bd::OpenFileDialogue(L"Select DLC Package");
  if (!selected) {
    BD_INFO("[dlc] install cancelled");
    return false;
  }

  if (!s_dlc_manager.Install(*selected)) {
    BD_ERROR("[dlc] installation failed");
    return false;
  }

  BD_INFO("[dlc] installed, restarting menu");
  return true;
}

bool DeleteMod(int index) {
  auto &mgr = bd::GetModManager();
  mgr.UnregisterPreviewHandlers();
  return mgr.DeleteMod(static_cast<size_t>(index));
}

bool DeleteDlc(int index) {
  return s_dlc_manager.Delete(static_cast<size_t>(index));
}

// Accessors
size_t ModCount() {
  return bd::GetModManager().GetAllModNames().size();
}

size_t DlcCount() {
  return s_dlc_manager.GetDlcList().size();
}

const bd::ModInfo &GetModInfo(int index) {
  return bd::GetModManager().GetModInfo(static_cast<size_t>(index));
}

bool IsModEnabled(int index) {
  return bd::GetModManager().IsModEnabled(static_cast<size_t>(index));
}

const std::vector<bd::DlcInfo> &GetDlcList() {
  return s_dlc_manager.GetDlcList();
}

void RefreshDlc() {
  s_dlc_manager.Refresh();
}

void RescanDlc() {
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 cfm = rex::memory::load_and_swap<u32>(
      base + kContentFileManagerAddr);
  if (!cfm) {
    BD_WARN("[dlc] gsContentFileManager is NULL, cannot rescan");
    return;
  }

  rex::ppc::stack_guard guard(
      *rex::runtime::ThreadState::Get()->context());
  u32 driveAddr = rex::ppc::stack_push_string(
      *rex::runtime::ThreadState::Get()->context(), base, "driveA");
  u32 result = StartDlcEnum(cfm, driveAddr, 1, 0);
  BD_INFO("[dlc] gsContentFileManager_StartEnumeration -> {}", result);
}

// DLC thumbnail VFS helpers
void UnregisterDlcThumb() {
  if (!s_dlc_thumb_vfs_path.empty()) {
    bd::vfs::UnregisterContentHandler(s_dlc_thumb_vfs_path);
    s_dlc_thumb_vfs_path.clear();
  }
}

std::string RegisterDlcThumb(int index) {
  UnregisterDlcThumb();

  auto &dlcList = s_dlc_manager.GetDlcList();
  if (index < 0 || index >= static_cast<int>(dlcList.size()))
    return {};

  const auto &info = dlcList[index];
  if (info.thumbnail.empty())
    return {};

  s_dlc_thumb_vfs_path =
      "d2anime\\modmgr\\res\\dlc_thumb_" + std::to_string(index) + ".dds";
  auto thumb_data = info.thumbnail;
  bd::vfs::RegisterContentHandler(s_dlc_thumb_vfs_path,
                                  [thumb_data] { return thumb_data; });

  return "res\\dlc_thumb_" + std::to_string(index);
}

bd::ConfigLayout &GetLayout() {
  return s_config_layout;
}

} // namespace bd::config
