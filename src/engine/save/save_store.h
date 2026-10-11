/**
 * @file        engine/save/save_store.h
 *
 * @brief       Host-backed engine "save:" store replacing the XAM content
 *              manager.
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
#include <vector>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace bd::engine {

// Mount <saves_root> as the save store on the given VFS. Safe to call once,
// and subsequent calls no-op.
void MountSaveStore(rex::filesystem::VirtualFileSystem *vfs,
                    const std::filesystem::path &saves_root);

// Re-point the engine "save:" symlink at <saves>/<slot>. No-op if not mounted.
void SetCurrentSaveSlot(std::string_view slot);

// Create <saves>/<slot>/ if missing. False (and logs) if creation failed, so
// the caller can fail the save instead of silently losing it.
bool CreateSaveSlotDir(std::string_view slot);

// True if <saves>/<slot>/savegame.dat exists.
bool SaveSlotExists(std::string_view slot);

// Names of every slot dir that holds a savegame.dat (i.e. a real save). The
// dir name is the engine XCONTENT szFileName the game created the slot with, so
// it round-trips straight back into enumeration. Empty if not mounted.
std::vector<std::string> ListSaveSlots();

} // namespace bd::engine
