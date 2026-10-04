/**
 * @file    core/app_root.h
 * @brief   Anchor directory for the app data the host keeps (cache, logs,
 *          legacy config).
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>

namespace bd {

// True when running from a read-only/self-contained application package rather
// than a loose developer build.
bool IsPackagedApplication();

// The exe dir for loose builds. AppImage and macOS app bundle runs use a
// writable per-user location instead of modifying their package contents.
std::filesystem::path AppRootFolder();

// Overrides the above once an install root is known, so cache and logs sit
// with the game data instead of beside the executable.
void SetAppRoot(const std::filesystem::path &root);

// The cache directory for an install rooted at 'root'. Honors the cache path
// override, so it is the same answer the game reaches at boot.
std::filesystem::path CacheRootFor(const std::filesystem::path &root);

// XDG_CONFIG_HOME or ~/.config with reblue's folder appended. Empty when
// neither is set, and on Windows, which anchors this data in HKCU.
std::filesystem::path UserConfigFolder();

// True when a file can be created in dir. A read-only mount and a package
// both answer false.
bool DirectoryWritable(const std::filesystem::path &dir);

} // namespace bd
