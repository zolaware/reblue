/**
 * @file        bdengine/file_dialogue.h
 * @brief       Platform file dialogue - file selection for DLC installation.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <filesystem>
#include <optional>

#include <rex/types.h>

namespace bd {

/**
 * @brief Opens a native file selection dialogue with optional filter.
 *        Returns the selected file path, or nullopt if cancelled.
 *        Blocks the calling thread until the dialogue is closed.
 */
std::optional<std::filesystem::path> OpenFileDialogue(
    const wchar_t* title = L"Select File",
    const wchar_t* filter_name = nullptr,
    const wchar_t* filter_pattern = nullptr);

}  // namespace bd
