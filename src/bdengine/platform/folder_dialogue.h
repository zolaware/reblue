/**
 * @file        bdengine/folder_dialogue.h
 * @brief       Platform folder dialogue - folder selection for mod installation.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <filesystem>
#include <optional>

namespace bd {

/**
 * @brief Opens a native folder selection dialogue.
 *        Returns the selected folder path, or nullopt if cancelled.
 *        Blocks the calling thread until the dialogue is closed.
 */
std::optional<std::filesystem::path> OpenFolderDialogue();

}  // namespace bd
