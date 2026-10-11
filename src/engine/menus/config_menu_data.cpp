/**
 * @file    engine/menus/config_menu_data.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_menu_data.h"
#include "core/encoding.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings_model.h"
#include "core/zip_unpack.h"
#include "engine/achievements/achievement_list.h"
#include "engine/menus/achievements_layout.h"
#include "engine/menus/achievements_menu.h"
#include "engine/menus/config_layout.h"
#include "engine/sfx.h"
#include "installer/installer.h"
#include "platform/platform.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <system_error>
#include <thread>

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

#include <rex/types.h>

namespace bd::engine {

namespace {

ConfigLayout s_config_layout;
SectionTemplate s_section_tpl;
ListItemTemplate s_mod_item_tpl{"Mod", 420, 370};
NodataTemplate s_nodata_tpl;
DetailTemplate s_detail_tpl;
ListItemTemplate s_dlc_item_tpl{"DLC", 470, 430};
DlcDetailTemplate s_dlc_detail_tpl;
SettingItemTemplate s_setting_item_tpl;
KeybindItemTemplate s_keybind_item_tpl;
LanguageItemTemplate s_lang_item_tpl;
bool s_dlc_changed = false;
bool s_languages_changed = false;

installer::BootLanguages s_boot_languages;
std::vector<std::string> s_language_codes;
std::map<std::string, u64> s_language_bytes;

u64 LanguageBytesOnDisk(const std::filesystem::path &game,
                        const std::string &code) {
  namespace fs = std::filesystem;

  u64 total = 0;
  std::error_code ec;
  const u64 packmem =
      fs::file_size(game / "pack" / ("packmem_" + code + ".ipk"), ec);
  if (!ec)
    total += packmem;

  for (const char *prefix : {"snd_memory_", "snd_stream_"}) {
    std::error_code walk_ec;
    for (const auto &entry :
         fs::recursive_directory_iterator(game / (prefix + code), walk_ec)) {
      std::error_code file_ec;
      if (!entry.is_regular_file(file_ec) || file_ec)
        continue;
      const u64 size = entry.file_size(file_ec);
      if (!file_ec)
        total += size;
    }
  }
  return total;
}

#ifdef REBLUE_BUILD_INSTALLER
std::array<std::filesystem::path, installer::kDiscCount> s_lang_sources;
installer::InstallProgress s_lang_progress;
std::thread s_lang_thread;
std::string s_lang_names;
bool s_lang_removing = false;

struct LanguageOffer {
  std::string code;
  bool text = false;
  bool voice = false;
};

std::vector<LanguageOffer> s_lang_offer;
std::vector<std::string> s_lang_movie_codes;

bool LineCarries(const std::vector<std::string> &line,
                 const std::string &code) {
  return std::find(line.begin(), line.end(), code) != line.end();
}

std::string UpperCode(const std::string &code) {
  std::string out;
  for (char c : code)
    out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

void ResetLanguageProgress() {
  s_lang_progress.files_done.store(0);
  s_lang_progress.files_total.store(0);
  s_lang_progress.bytes_done.store(0);
  s_lang_progress.bytes_total.store(0);
  s_lang_progress.complete.store(false);
  s_lang_progress.failed.store(false);
  s_lang_progress.canceled.store(false);
  s_lang_progress.SetCurrentFile("");
  s_lang_progress.SetError("");
}
#endif

constexpr const char *kMenuMount = "ui:config-menu";

} // namespace

void RegisterVFS(ConfigMenu::Surface surface) {
  const bool inGame = surface == ConfigMenu::Surface::InGame;

  // Restart-bound rows stay listed in-game, grayed rather than hidden, so
  // both surfaces show the same pages.
  SettingsDisableRestartRows(inGame);
  s_config_layout.SetSectionCount(inGame ? kSettingsSectionCount
                                         : ConfigLayout::kSectionCount);
  s_config_layout.SetStandalone(!inGame);

  auto &mods = vfs::VFS::Get().Mods();

  size_t modCount = mods.Count();
  size_t dlcCount = vfs::VFS::Get().DLC().Count();
  s_config_layout.SetModCount(modCount);
  s_config_layout.SetDLCCount(dlcCount);
  s_config_layout.SetAchievementCount(RefreshAchievementList().size());
  RefreshLanguages();
  s_config_layout.SetLanguageCount(LanguageCount());
  // SettingsPage 0..kSettingsSectionCount-1 are the sidebar pages, in order.
  // Slots, not rows: a page's section titles are drawn as list entries of
  // their own.
  size_t pageSlots[kSettingsSectionCount];
  for (int p = 0; p < kSettingsSectionCount; ++p)
    pageSlots[p] = SettingsSlotCount(static_cast<SettingsPage>(p));
  s_config_layout.SetSettingsCounts(pageSlots, BindGridRows());

  // Providers run per read, so each CSV reflects the layout state at that
  // moment. Nothing here needs re-registering when the data behind it changes.
  LayoutMount mount("d2anime\\modmgr\\");
  mount.Add("l_modmgr.csv", &s_config_layout)
      .Add("l_modmgr_info.csv", &s_mod_item_tpl)
      .Add("l_modmgr_detail.csv", &s_detail_tpl)
      .Add("l_modmgr_section.csv", &s_section_tpl)
      .Add("l_modmgr_nodata.csv", &s_nodata_tpl)
      .Add("l_modmgr_dlcinfo.csv", &s_dlc_item_tpl)
      .Add("l_modmgr_dlcdetail.csv", &s_dlc_detail_tpl)
      .Add("l_modmgr_setting.csv", &s_setting_item_tpl)
      .Add("l_modmgr_keybind.csv", &s_keybind_item_tpl)
      .Add("l_modmgr_langinfo.csv", &s_lang_item_tpl)
      // Full-width rows. The camp Encyclopedia screen serves its own narrower
      // instance beside its CSV, since a template path resolves relative to the
      // file naming it.
      .Add("l_modmgr_achv.csv", &AchievementRowTemplate::Config());
  // The row icons live on the achievement viewer's own mount, which the camp
  // screen would otherwise be the only thing to bring up.
  RegisterAchievementsVFS();

  mount.Publish(kMenuMount);
  mods.MountPreviews();
}

void UnregisterVFS() {
  LayoutMount::Remove(kMenuMount);
  vfs::VFS::Get().Mods().UnmountPreviews();
}

void SaveAndReload() {
  auto &mods = vfs::VFS::Get().Mods();
  mods.Flush();
  mods.Reload();
  BD_DEBUG("[config] mod config saved and reloaded");
}

void FlipMod(int index) {
  auto &mods = vfs::VFS::Get().Mods();
  auto i = static_cast<size_t>(index);
  mods.SetEnabled(i, !mods.IsEnabled(i));
  sfx::Play(sfx::kToggle);
}

void ReorderMod(int a, int b) {
  vfs::VFS::Get().Mods().Swap(static_cast<size_t>(a), static_cast<size_t>(b));
}

namespace {

bool InstallModFromFolder(const std::filesystem::path &src,
                          const std::filesystem::path &mods_dir,
                          vfs::ModCatalog &mods) {
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

  BD_DEBUG("[config] installed mod '{}' from {}", folder_name, src.string());

  // Enable in the active profile's loadout (order file), then reload.
  mods.Enable(folder_name);
  return true;
}

bool InstallModFromZip(const std::filesystem::path &zip_path,
                       const std::filesystem::path &mods_dir,
                       vfs::ModCatalog &mods) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    BD_ERROR("[config] failed to open zip: {}", zip_path.string());
    return false;
  }

  int num_files = static_cast<int>(mz_zip_reader_get_num_files(&zip));

  std::string prefix;
  bool found_mod_toml = false;

  if (mz_zip_reader_locate_file(&zip, "mod.toml", nullptr, 0) >= 0) {
    found_mod_toml = true;
  } else {
    // Fall back to a single top-level folder that contains mod.toml.
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
      if (mz_zip_reader_locate_file(&zip, toml_path.c_str(), nullptr, 0) >= 0) {
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

    if (IsUnsafeArchivePath(relative)) {
      BD_ERROR("[config] unsafe entry '{}' in zip {}", name,
               zip_path.string());
      extract_ok = false;
      break;
    }

    auto out_path = dest / relative;
    std::filesystem::create_directories(out_path.parent_path());

    if (!mz_zip_reader_extract_to_file(&zip, i, out_path.string().c_str(), 0)) {
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

  BD_DEBUG("[config] installed mod '{}' from zip {}", folder_name,
           zip_path.string());

  // Enable in the active profile's loadout (order file), then reload.
  mods.Enable(folder_name);
  return true;
}

} // namespace

bool InstallMod() {
  static constexpr bd::platform::FileFilter kModFilters[] = {
      {L"Mod files (*.zip, mod.toml)", L"*.zip;mod.toml"},
  };
  auto selected = bd::platform::ShowOpenFileDialog(
      L"Select Mod (zip or mod.toml)", kModFilters);
  if (!selected) {
    BD_DEBUG("mod install canceled");
    return false;
  }

  auto &path = *selected;
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  auto &mods = vfs::VFS::Get().Mods();
  auto mods_dir = vfs::VFS::Get().Paths().Mods();
  std::filesystem::create_directories(mods_dir);

  if (ext == ".zip")
    return InstallModFromZip(path, mods_dir, mods);
  else
    return InstallModFromFolder(path.parent_path(), mods_dir, mods);
}

bool InstallDLC() {
  auto selected = bd::platform::ShowOpenFileDialog(L"Select DLC Package");
  if (!selected) {
    BD_INFO("dlc install canceled");
    return false;
  }

  if (!vfs::VFS::Get().DLC().Install(*selected)) {
    BD_ERROR("dlc installation failed");
    return false;
  }

  s_dlc_changed = true;
  return true;
}

bool RemoveMod(int index) {
  auto &mods = vfs::VFS::Get().Mods();
  mods.UnmountPreviews();
  return mods.Remove(static_cast<size_t>(index));
}

bool DeleteDLC(int index) {
  bool ok = vfs::VFS::Get().DLC().Remove(static_cast<size_t>(index));
  if (ok)
    s_dlc_changed = true;
  return ok;
}

size_t ModCount() { return vfs::VFS::Get().Mods().Count(); }

size_t DlcCount() { return vfs::VFS::Get().DLC().Count(); }

const vfs::ModPackage &ModAt(int index) {
  return vfs::VFS::Get().Mods().At(static_cast<size_t>(index));
}

bool ModIsEnabled(int index) {
  return vfs::VFS::Get().Mods().IsEnabled(static_cast<size_t>(index));
}

vfs::DLCCatalog &DLC() { return vfs::VFS::Get().DLC(); }

void RefreshDLC() { vfs::VFS::Get().DLC().Reload(); }

bool DlcChanged() { return s_dlc_changed; }

void ToggleDLC(int index) {
  auto &dlc = vfs::VFS::Get().DLC();
  if (index < 0 || index >= static_cast<int>(dlc.Count()))
    return;
  if (dlc.SetEnabled(static_cast<size_t>(index),
                     !dlc.IsEnabled(static_cast<size_t>(index))))
    s_dlc_changed = true;
    sfx::Play(sfx::kToggle);
}

bool IsDLCEnabled(int index) {
  auto &dlc = vfs::VFS::Get().DLC();
  return index >= 0 && index < static_cast<int>(dlc.Count()) &&
         dlc.IsEnabled(static_cast<size_t>(index));
}

void RefreshLanguages() {
  const auto game = vfs::VFS::Get().Paths().Game();
  s_boot_languages = installer::BootLanguages::FromFile(game / "bd_boot.ini");
  s_language_codes = s_boot_languages.Text();
  for (const auto &code : s_boot_languages.Voice()) {
    if (std::find(s_language_codes.begin(), s_language_codes.end(), code) ==
        s_language_codes.end())
      s_language_codes.push_back(code);
  }

  s_language_bytes.clear();
}

size_t LanguageCount() { return s_language_codes.size(); }

std::string LanguageName(int index) {
  if (index < 0 || index >= static_cast<int>(s_language_codes.size()))
    return {};
  return i18n::Text("locale." + s_language_codes[static_cast<size_t>(index)]);
}

std::string LanguageKinds(int index) {
  if (index < 0 || index >= static_cast<int>(s_language_codes.size()))
    return {};
  const std::string &code = s_language_codes[static_cast<size_t>(index)];
  const auto carries = [&code](const std::vector<std::string> &line) {
    return std::find(line.begin(), line.end(), code) != line.end();
  };

  std::string kinds;
  if (carries(s_boot_languages.Text()))
    kinds = i18n::Text("menu.language.text");
  if (carries(s_boot_languages.Voice())) {
    if (!kinds.empty())
      kinds += ", ";
    kinds += i18n::Text("menu.language.voice");
  }
  return kinds;
}

std::string LanguageSize(int index) {
  if (index < 0 || index >= static_cast<int>(s_language_codes.size()))
    return {};
  const std::string &code = s_language_codes[static_cast<size_t>(index)];
  auto it = s_language_bytes.find(code);
  if (it == s_language_bytes.end()) {
    const auto game = vfs::VFS::Get().Paths().Game();
    it = s_language_bytes.emplace(code, LanguageBytesOnDisk(game, code)).first;
  }
  return i18n::Bytes(it->second);
}

bool LanguageRemovable(int index) {
  if (index < 0 || index >= static_cast<int>(s_language_codes.size()))
    return false;
  return s_boot_languages.Removable(
      s_language_codes[static_cast<size_t>(index)]);
}

bool LanguagesChanged() { return s_languages_changed; }

#ifdef REBLUE_BUILD_INSTALLER

void ClearLanguageSources() {
  for (auto &slot : s_lang_sources)
    slot.clear();
  s_lang_offer.clear();
  s_lang_movie_codes.clear();
}

LanguageAddResult AddLanguageSources(std::string &detail) {
  detail.clear();

  const std::wstring anyLabel = Utf8ToWide(i18n::Text("installer.filter.any"));
  const std::wstring isoLabel = Utf8ToWide(i18n::Text("installer.filter.iso"));
  const bd::platform::FileFilter kSourceFilters[] = {
      {anyLabel.c_str(), L"*.*"},
      {isoLabel.c_str(), L"*.iso"},
  };
  const auto picked = bd::platform::ShowOpenFilesDialog(
      Utf8ToWide(i18n::Text("installer.dialog.select_source")).c_str(),
      kSourceFilters);

  for (const auto &file : picked) {
    auto image = installer::DiscImage::Open(file);
    if (!image) {
      BD_WARN("[config] not a readable disc image: {}", file.string());
      continue;
    }

    bool carried = false;
    for (int n = 1; n <= installer::kDiscCount; ++n) {
      if (!image->Root(n))
        continue;
      carried = true;
      auto &slot = s_lang_sources[static_cast<size_t>(n - 1)];
      if (slot.empty())
        slot = file;
    }
    if (!carried)
      BD_WARN("[config] image carries no Blue Dragon disc: {}", file.string());
  }

  if (picked.empty()) {
    ClearLanguageSources();
    return LanguageAddResult::Canceled;
  }

  std::string missing;
  for (int n = 0; n < installer::kDiscCount; ++n) {
    if (!s_lang_sources[static_cast<size_t>(n)].empty())
      continue;
    if (!missing.empty())
      missing += ", ";
    missing += installer::kDiscLabels[n];
  }
  if (!missing.empty()) {
    detail = missing;
    return LanguageAddResult::Missing;
  }

  std::vector<std::unique_ptr<installer::DiscImage>> images;
  installer::BootLanguages disc;
  for (int n = 0; n < installer::kDiscCount; ++n) {
    auto image =
        installer::DiscImage::Open(s_lang_sources[static_cast<size_t>(n)]);
    rex::filesystem::Entry *root = image ? image->Root(n + 1) : nullptr;
    if (!root) {
      BD_ERROR("[config] disc {} image became unreadable", n + 1);
      detail = i18n::Text("installer.status.bad_image");
      ClearLanguageSources();
      return LanguageAddResult::Failed;
    }
    installer::BootLanguages one = installer::BootLanguages::FromDisc(*root);
    if (n == 0)
      disc = one;
    else
      disc.Add(one, one.All());
    images.push_back(std::move(image));
  }

  const auto game = vfs::VFS::Get().Paths().Game();
  const installer::BootLanguages installed =
      installer::BootLanguages::FromFile(game / "bd_boot.ini");

  s_lang_offer.clear();
  s_lang_movie_codes = disc.Voice();
  for (const auto &code : disc.All()) {
    LanguageOffer offer;
    offer.code = code;
    offer.text =
        LineCarries(disc.Text(), code) && !LineCarries(installed.Text(), code);
    offer.voice = LineCarries(disc.Voice(), code) &&
                  !LineCarries(installed.Voice(), code);
    if (offer.text || offer.voice)
      s_lang_offer.push_back(offer);
  }

  if (s_lang_offer.empty()) {
    ClearLanguageSources();
    return LanguageAddResult::NothingNew;
  }

  BD_INFO("[config] disc set offers {} language(s)", s_lang_offer.size());
  return LanguageAddResult::Picked;
}

size_t LanguageOfferCount() { return s_lang_offer.size(); }

std::string LanguageOfferName(int index) {
  if (index < 0 || index >= static_cast<int>(s_lang_offer.size()))
    return {};
  return i18n::Text("locale." + s_lang_offer[static_cast<size_t>(index)].code);
}

std::string LanguageOfferKinds(int index) {
  if (index < 0 || index >= static_cast<int>(s_lang_offer.size()))
    return {};
  const LanguageOffer &offer = s_lang_offer[static_cast<size_t>(index)];
  if (offer.text && offer.voice)
    return i18n::Text("menu.language.text_and_voice");
  return i18n::Text(offer.text ? "menu.language.text" : "menu.language.voice");
}

std::string LanguageOfferMovies() {
  std::string codes;
  for (const auto &code : s_lang_movie_codes) {
    if (!codes.empty())
      codes += ", ";
    codes += UpperCode(code);
  }
  return i18n::Fmt("menu.language.movies_set", codes);
}

bool StartLanguageJob(const std::vector<bool> &accepted, bool movies,
                      std::string &detail) {
  detail.clear();

  installer::InstallSelection selection;
  selection.movies = movies;

  s_lang_names.clear();
  for (size_t i = 0; i < s_lang_offer.size(); ++i) {
    if (i >= accepted.size() || !accepted[i])
      continue;
    const LanguageOffer &offer = s_lang_offer[i];
    selection.languages.push_back({offer.code, offer.text, offer.voice});
    if (!s_lang_names.empty())
      s_lang_names += ", ";
    s_lang_names += i18n::Text("locale." + offer.code);
  }

  if (s_lang_names.empty())
    s_lang_names = i18n::Text("menu.language.movies");

  ResetLanguageProgress();
  s_lang_removing = false;

  const auto game = vfs::VFS::Get().Paths().Game();
  try {
    s_lang_thread = installer::Installer::AddLanguagesAsync(
        s_lang_sources, game, selection, s_lang_progress);
  } catch (const std::system_error &e) {
    BD_ERROR("[config] language install failed to spawn worker: {}", e.what());
    detail = i18n::Fmt("installer.error.spawn", e.what());
    ClearLanguageSources();
    return false;
  }

  BD_INFO("[config] adding languages: {} (movies {})", s_lang_names,
          movies ? "yes" : "no");
  return true;
}

bool RemoveLanguage(int index) {
  if (!LanguageRemovable(index))
    return false;

  const std::string code = s_language_codes[static_cast<size_t>(index)];
  const std::string name = LanguageName(index);
  const auto game = vfs::VFS::Get().Paths().Game();

  ResetLanguageProgress();
  s_lang_removing = true;
  s_lang_names = name;

  try {
    s_lang_thread =
        installer::Installer::RemoveLanguageAsync(game, code, s_lang_progress);
  } catch (const std::system_error &e) {
    BD_ERROR("[config] language removal failed to spawn worker: {}", e.what());
    return false;
  }

  BD_INFO("[config] removing language: {}", name);
  return true;
}

std::string LanguageJobStatus() {
  return i18n::Fmt(s_lang_removing ? "menu.language.removing"
                                   : "menu.language.installing",
                   s_lang_names);
}

int LanguageJobPercent() {
  if (s_lang_removing)
    return -1;
  const u64 total = s_lang_progress.bytes_total.load();
  const u64 done = s_lang_progress.bytes_done.load();
  return total != 0 ? static_cast<int>(done * 100 / total) : 0;
}

bool LanguageJobCancelable() {
  return !s_lang_removing && s_lang_thread.joinable();
}

LanguageJobOutcome PollLanguageJob(std::string &error) {
  if (!s_lang_progress.complete.load())
    return LanguageJobOutcome::Running;

  if (s_lang_thread.joinable())
    s_lang_thread.join();
  ClearLanguageSources();

  if (s_lang_progress.canceled.load())
    return LanguageJobOutcome::Canceled;
  if (s_lang_progress.failed.load()) {
    error = s_lang_progress.GetError();
    return LanguageJobOutcome::Failed;
  }

  s_languages_changed = true;
  return LanguageJobOutcome::Done;
}

void RequestLanguageJobCancel() { s_lang_progress.canceled.store(true); }

void CancelLanguageJob() {
  if (!s_lang_thread.joinable())
    return;
  s_lang_progress.canceled.store(true);
  s_lang_thread.join();
  ClearLanguageSources();
}

#else

LanguageAddResult AddLanguageSources(std::string &detail) {
  detail.clear();
  return LanguageAddResult::Canceled;
}

void ClearLanguageSources() {}

size_t LanguageOfferCount() { return 0; }

std::string LanguageOfferName(int) { return {}; }

std::string LanguageOfferKinds(int) { return {}; }

std::string LanguageOfferMovies() { return {}; }

bool StartLanguageJob(const std::vector<bool> &, bool, std::string &detail) {
  detail.clear();
  return false;
}

bool RemoveLanguage(int) { return false; }

std::string LanguageJobStatus() { return {}; }

int LanguageJobPercent() { return -1; }

bool LanguageJobCancelable() { return false; }

LanguageJobOutcome PollLanguageJob(std::string &) {
  return LanguageJobOutcome::Done;
}

void RequestLanguageJobCancel() {}

void CancelLanguageJob() {}

#endif

ConfigLayout &GetLayout() { return s_config_layout; }

} // namespace bd::engine
