/**
 * @file        engine/save/save_store.cpp
 * @brief   Backed by the SDK HostPathDevice. The XContentCreate hook
 *          re-points save: per slot.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/save/save_store.h"

#include "core/logging.h"

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>

#include <memory>
#include <system_error>

namespace bd::engine {

namespace {

// Private internal mount. "save:" is a symlink onto "\SaveStore\<slot>".
constexpr std::string_view kSaveMountPath = "\\SaveStore";
constexpr std::string_view kSaveSymlink = "save:";
constexpr std::string_view kSaveFileName = "savegame.dat";

rex::filesystem::VirtualFileSystem *g_vfs = nullptr;
std::filesystem::path
    g_saves_root; // decoupled save root (default <install_root>/saves)
std::string g_current_slot;

std::filesystem::path SlotDir(std::string_view slot) {
  return g_saves_root / std::filesystem::path(std::string(slot));
}

} // namespace

void MountSaveStore(rex::filesystem::VirtualFileSystem *vfs,
                    const std::filesystem::path &saves_root) {
  if (g_vfs)
    return;
  if (!vfs) {
    BD_ERROR("[savestore] no VFS to mount on");
    return;
  }

  g_saves_root = saves_root;
  std::error_code ec;
  std::filesystem::create_directories(g_saves_root, ec);
  if (ec) {
    BD_ERROR("[savestore] cannot create saves root '{}': {}",
             g_saves_root.string(), ec.message());
    return;
  }

  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      kSaveMountPath, g_saves_root, /*read_only=*/false,
      /*allow_share_delete=*/true);
  if (!device->Initialize()) {
    BD_ERROR("[savestore] device init failed");
    return;
  }
  if (!vfs->RegisterDevice(std::move(device))) {
    BD_ERROR("[savestore] device register failed");
    return;
  }
  g_vfs = vfs;
  BD_INFO("[savestore] mounted save: -> {}", g_saves_root.string());
}

void SetCurrentSaveSlot(std::string_view slot) {
  if (!g_vfs)
    return;
  g_current_slot = std::string(slot);
  std::string target = std::string(kSaveMountPath) + "\\" + g_current_slot;
  // RegisterSymbolicLink uses map::insert (no overwrite), so drop the old one
  // first.
  g_vfs->UnregisterSymbolicLink(kSaveSymlink);
  g_vfs->RegisterSymbolicLink(kSaveSymlink, target);
}

std::filesystem::path CurrentSaveSlotDir() {
  if (g_saves_root.empty() || g_current_slot.empty())
    return {};
  return SlotDir(g_current_slot);
}

bool CreateSaveSlotDir(std::string_view slot) {
  std::error_code ec;
  std::filesystem::create_directories(SlotDir(slot), ec);
  if (ec) {
    BD_ERROR("[savestore] failed to create slot dir '{}': {}", slot,
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
