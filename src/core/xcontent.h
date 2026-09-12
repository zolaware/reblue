/**
 * @file    core/xcontent.h
 * @brief   Reading Xbox 360 content entries, shared by the installer and vfs.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <string>

#include <rex/types.h>

namespace rex::filesystem {
class Entry;
}

namespace bd {

inline constexpr u32 kBlueDragonTitleId = 0x4D5307DF;

bool CopyEntry(rex::filesystem::Entry &entry,
               const std::filesystem::path &dest);
std::string ReadEntry(rex::filesystem::Entry &entry);

} // namespace bd
