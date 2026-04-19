#pragma once

#include <filesystem>
#include <optional>
#include <span>

namespace reblue::ui {

struct FileFilter {
  const wchar_t* name;
  const wchar_t* pattern;  // semicolon-separated: "*.zip;*.iso"
};

// Shows a native Open File dialog. Blocks until the user picks or cancels.
// filters is optional; pass {} for no filter.
std::optional<std::filesystem::path> ShowOpenFileDialog(
    const wchar_t* title,
    std::span<const FileFilter> filters = {});

// Shows a native Pick Folder dialog. Blocks until the user picks or cancels.
std::optional<std::filesystem::path> ShowOpenFolderDialog(const wchar_t* title);

}  // namespace reblue::ui
