/**
 * @file        engine/save/save_manager.cpp
 * @brief   Backed by an SDK host directory mount. The XContentCreate hook
 *          re-points save: per slot.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/save/save_manager.h"

#include "core/logging.h"

#include <rex/filesystem/file_system.h>
#include <rex/logging/log_category.h>

#include <system_error>

namespace bd::engine {

namespace {

// Private internal mount. "save:" is a symlink onto "\Saves\<slot>".
constexpr std::string_view kSaveMountPath = "\\Saves";
constexpr std::string_view kSaveSymlink = "save:";
constexpr std::string_view kSaveFileName = "savegame.dat";

rex::filesystem::IFileSystem *g_fs = nullptr;
std::filesystem::path
    g_saves_root; // decoupled save root (default <install_root>/saves)

std::filesystem::path SlotDir(std::string_view slot) {
  return g_saves_root / std::filesystem::path(std::string(slot));
}

} // namespace

void MountSaves(rex::filesystem::IFileSystem *fs,
                    const std::filesystem::path &saves_root) {
  if (g_fs)
    return;
  if (!fs) {
    REXBD_ERROR("savestore no file system to mount on");
    return;
  }

  g_saves_root = saves_root;
  std::error_code ec;
  std::filesystem::create_directories(g_saves_root, ec);
  if (ec) {
    REXBD_ERROR("savestore cannot create saves root '{}': {}",
             g_saves_root.string(), ec.message());
    return;
  }

  using rex::filesystem::MountFlags;
  auto mounted = fs->Mount({std::string(kSaveMountPath),
                            rex::filesystem::ProviderType::kHostDirectory,
                            g_saves_root,
                            MountFlags::kWritable});
  if (!mounted) {
    REXBD_ERROR("savestore mount failed: {}", mounted.error().message);
    return;
  }
  g_fs = fs;
  REXBD_INFO("savestore mounted save: -> {}", g_saves_root.string());
}

void SetCurrentSaveSlot(std::string_view slot) {
  if (!g_fs)
    return;
  std::string target = std::string(kSaveMountPath) + "\\" + std::string(slot);
  // Link keeps the first link with a name, so drop the old one first.
  (void)g_fs->Unlink(kSaveSymlink);
  if (auto linked = g_fs->Link(kSaveSymlink, target); !linked)
    REXBD_ERROR("savestore link failed: {}", linked.error().message);
}

bool CreateSaveSlotDir(std::string_view slot) {
  std::error_code ec;
  std::filesystem::create_directories(SlotDir(slot), ec);
  if (ec) {
    REXBD_ERROR("savestore failed to create slot dir '{}': {}", slot,
             ec.message());
    return false;
  }
  return true;
}

bool SaveSlotExists(std::string_view slot) {
  std::error_code ec;
  return std::filesystem::exists(SlotDir(slot) / kSaveFileName, ec);
}

std::vector<std::string> ListSaveSlots() {
  std::vector<std::string> slots;
  if (g_saves_root.empty())
    return slots;
  std::error_code ec;
  for (const auto &entry :
       std::filesystem::directory_iterator(g_saves_root, ec)) {
    if (!entry.is_directory(ec))
      continue;
    if (!std::filesystem::exists(entry.path() / kSaveFileName, ec))
      continue;
    slots.push_back(entry.path().filename().string());
  }
  return slots;
}

} // namespace bd::engine
