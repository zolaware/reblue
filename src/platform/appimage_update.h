/**
 * @file    platform/appimage_update.h
 * @brief   Validation and atomic replacement of a running AppImage.
 *
 * @copyright Copyright (c) 2026 Rien Gupta <rgupta9@scu.edu>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <string>

namespace bd::platform {

bool IsType2AppImage(const std::filesystem::path &path);

// Swaps 'incoming' in over 'current' once it checks out as a Type 2 AppImage
// for the same ELF architecture. The old image stays beside it as
// '<current>.replaced' until ClearReplacedAppImage.
bool ReplaceAppImage(const std::filesystem::path &current,
                     const std::filesystem::path &incoming, std::string &error);

bool ClearReplacedAppImage(const std::filesystem::path &current,
                           std::string &error);

} // namespace bd::platform
