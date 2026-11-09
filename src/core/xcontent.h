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
#include <string_view>

#include <rex/types.h>

namespace rex::filesystem {
class IContainerImage;
struct ContainerNode;
}

namespace bd {

inline constexpr u32 kBlueDragonTitleId = 0x4D5307DF;

bool CopyEntry(const rex::filesystem::IContainerImage &container,
               const rex::filesystem::ContainerNode &node,
               const std::filesystem::path &dest);
std::string ReadEntry(const rex::filesystem::IContainerImage &container,
                      const rex::filesystem::ContainerNode &node);
const rex::filesystem::ContainerNode *
FindEntry(const rex::filesystem::IContainerImage &container,
          const rex::filesystem::ContainerNode &root, std::string_view path);
bool IsDirectory(const rex::filesystem::ContainerNode &node);

} // namespace bd
