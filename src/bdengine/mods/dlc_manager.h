/**
 * @file        bdengine/dlc_manager.h
 * @brief       DLC manager - install, list, and persist metadata for DLC packages.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/types.h>

namespace bd {

struct DlcInfo {
    std::string display_name;
    std::string description;
    std::string publisher;
    std::string file_name;           // hex hash, used as key
    std::vector<u8> thumbnail;  // raw thumbnail bytes from STFS header
};

class DlcManager {
public:
    /**
     * @brief Re-query installed DLCs: load dlc.toml, cross-reference with
     *        ContentManager::ListContent(), prune stale / add unknown entries.
     */
    void Refresh();

    /**
     * @brief Install a PIRS/LIVE/CON package: read STFS header for metadata,
     *        call ContentManager::InstallContent(), persist to dlc.toml.
     *        Returns true on success.
     */
    bool Install(const std::filesystem::path& package_path);

    const std::vector<DlcInfo>& GetDlcList() const { return dlc_list_; }

    /**
     * @brief Remove a DLC entry: delete metadata, thumbnail, and persist.
     *        Does NOT call ContentManager::DeleteContent (just removes our tracking).
     */
    bool Delete(size_t index);

    /**
     * @brief Set the data directory (where dlc.toml and dlc_thumbs/ live).
     */
    void SetDataDir(const std::filesystem::path& data_dir) { data_dir_ = data_dir; }

private:
    void SaveMetadata();
    void LoadMetadata();

    std::vector<DlcInfo> dlc_list_;
    std::filesystem::path data_dir_;
};

}  // namespace bd
