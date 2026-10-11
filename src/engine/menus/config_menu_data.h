/**
 * @file    engine/menus/config_menu_data.h
 * @brief   Config menu data layer - VFS, install, mod/DLC queries.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "engine/menus/config_menu.h"
#include "vfs/vfs.h"

#include <cstddef>
#include <string>
#include <vector>

namespace bd::engine {

class ConfigLayout;

// Generates the menu CSVs for a surface. The section count and the restart row
// filter are baked into them, so this runs before the menu task loads.
void RegisterVFS(ConfigMenu::Surface surface = ConfigMenu::Surface::Title);
void UnregisterVFS();
void SaveAndReload();
void FlipMod(int index);
void ReorderMod(int a, int b);
bool InstallMod();
bool InstallDLC();
bool RemoveMod(int index);
bool DeleteDLC(int index);

size_t ModCount();
size_t DlcCount();
const bd::vfs::ModPackage &ModAt(int index);
bool ModIsEnabled(int index);
bd::vfs::DLCCatalog &DLC();
void RefreshDLC();
void ToggleDLC(int index);
bool IsDLCEnabled(int index);

// True once DLC has been installed or deleted this session. Latched until the
// next process launch (DLC mounts at boot, so applying it needs a restart).
bool DlcChanged();

void RefreshLanguages();
size_t LanguageCount();
std::string LanguageName(int index);
std::string LanguageKinds(int index);
std::string LanguageSize(int index);
bool LanguageRemovable(int index);

enum class LanguageAddResult { Canceled, Missing, NothingNew, Failed, Picked };
LanguageAddResult AddLanguageSources(std::string &detail);
void ClearLanguageSources();
bool RemoveLanguage(int index);

size_t LanguageOfferCount();
std::string LanguageOfferName(int index);
std::string LanguageOfferKinds(int index);
std::string LanguageOfferMovies();
bool StartLanguageJob(const std::vector<bool> &accepted, bool movies,
                      std::string &detail);

enum class LanguageJobOutcome { Running, Done, Canceled, Failed };
std::string LanguageJobStatus();
int LanguageJobPercent();
bool LanguageJobCancelable();
LanguageJobOutcome PollLanguageJob(std::string &error);
void RequestLanguageJobCancel();
void CancelLanguageJob();
bool LanguagesChanged();

ConfigLayout &GetLayout();

} // namespace bd::engine
