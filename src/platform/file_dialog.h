/**
 * @file    platform/file_dialog.h
 * @brief   Native open-file and pick-folder dialog helpers.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace bd::platform {

struct FileFilter {
  const wchar_t *name;
  const wchar_t *pattern; // semicolon-separated: "*.zip;*.iso"
};

// Blocking. Returns nullopt on cancel.
std::optional<std::filesystem::path>
ShowOpenFileDialog(const wchar_t *title,
                   std::span<const FileFilter> filters = {});

std::vector<std::filesystem::path>
ShowOpenFilesDialog(const wchar_t *title,
                    std::span<const FileFilter> filters = {});

std::optional<std::filesystem::path> ShowOpenFolderDialog(const wchar_t *title);

} // namespace bd::platform
