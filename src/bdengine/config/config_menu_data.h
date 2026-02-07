/**
 * @file    bdengine/config_menu_data.h
 * @brief   Config menu data layer - VFS, install, mod/DLC queries.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/mods/dlc_manager.h"
#include "bdengine/mods/mod_manager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bd { class ConfigLayout; }

namespace bd::config {

void RegisterVFS();
void UnregisterVFS();
void SaveAndReload();
void ToggleMod(int index);
void SwapMods(int a, int b);
bool InstallMod();
bool InstallDlc();
bool DeleteMod(int index);
bool DeleteDlc(int index);

size_t ModCount();
size_t DlcCount();
const bd::ModInfo &GetModInfo(int index);
bool IsModEnabled(int index);
const std::vector<bd::DlcInfo> &GetDlcList();
void RefreshDlc();
void RescanDlc();
void UnregisterDlcThumb();
std::string RegisterDlcThumb(int index);
bd::ConfigLayout &GetLayout();

} // namespace bd::config
