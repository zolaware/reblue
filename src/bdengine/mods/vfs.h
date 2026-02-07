/**
 * @file        bdengine/vfs.h
 * @brief       Virtual file content handler - serves computed content
 *              when the game requests a file path.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <rex/types.h>
#include <functional>
#include <string>
#include <vector>

namespace bd::vfs {

/**
 * @brief Provider returns file content as bytes. Called on each file read.
 */
using ContentProvider = std::function<std::vector<u8>()>;

/**
 * @brief Register a content handler for a virtual file path.
 *        Path is normalized internally (lowercase, backslash separators,
 *        drive prefix and base path stripped).
 */
void RegisterContentHandler(const std::string& path, ContentProvider provider);

/**
 * @brief Remove a content handler.
 */
void UnregisterContentHandler(const std::string& path);

/**
 * @brief Check if a virtual file is registered at the given normalized path.
 */
bool Exists(const std::string& path);

/**
 * @brief Normalize a raw guest path: strip drive prefix, strip g_bdBasePath,
 *        lowercase, forward-slash to backslash, strip leading backslash.
 */
std::string NormalizePath(const char* raw_path);

}  // namespace bd::vfs
