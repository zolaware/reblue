/**
 * @file        bdengine/dlc_manager.cpp
 * @brief       DLC manager implementation - wraps ContentManager + STFS metadata.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/mods/dlc_manager.h"
#include "bdengine/common/logging.h"

#include <algorithm>
#include <codecvt>
#include <fstream>
#include <locale>

#include <toml++/toml.h>

#include <rex/types.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/filesystem/devices/stfs_container_device.h>

using rex::system::XContentType;
using rex::system::XLanguage;
using rex::filesystem::StfsContainerDevice;

namespace bd {

/**
 * @brief u16string -> UTF-8 helper.
 */
static std::string U16ToUtf8(const std::u16string& s) {
    if (s.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    return conv.to_bytes(s);
}

/**
 * @brief Parse mods/dlc.toml + load thumbnails from dlc_thumbs/.
 */
void DlcManager::LoadMetadata() {
    dlc_list_.clear();

    auto toml_path = data_dir_ / "dlc.toml";
    if (!std::filesystem::exists(toml_path))
        return;

    try {
        auto tbl = toml::parse_file(toml_path.string());
        if (auto arr = tbl["dlc"].as_array()) {
            for (auto& elem : *arr) {
                auto* t = elem.as_table();
                if (!t) continue;

                DlcInfo info;
                if (auto v = (*t)["file_name"].value<std::string>())
                    info.file_name = *v;
                if (auto v = (*t)["display_name"].value<std::string>())
                    info.display_name = *v;
                if (auto v = (*t)["description"].value<std::string>())
                    info.description = *v;
                if (auto v = (*t)["publisher"].value<std::string>())
                    info.publisher = *v;

                if (info.file_name.empty()) continue;

                auto thumb_path = data_dir_ / "dlc_thumbs" / (info.file_name + ".png");
                if (std::filesystem::exists(thumb_path)) {
                    std::ifstream f(thumb_path, std::ios::binary);
                    info.thumbnail.assign(std::istreambuf_iterator<char>(f),
                                          std::istreambuf_iterator<char>());
                }

                dlc_list_.push_back(std::move(info));
            }
        }
    } catch (const toml::parse_error& e) {
        BD_ERROR("[dlc] failed to parse dlc.toml: {}", e.what());
    }
}

/**
 * @brief Write mods/dlc.toml.
 */
void DlcManager::SaveMetadata() {
    auto toml_path = data_dir_ / "dlc.toml";

    toml::array arr;
    for (const auto& info : dlc_list_) {
        toml::table t;
        t.insert("file_name", info.file_name);
        t.insert("display_name", info.display_name);
        t.insert("description", info.description);
        t.insert("publisher", info.publisher);
        arr.push_back(std::move(t));
    }

    toml::table root;
    root.insert("dlc", std::move(arr));

    std::ofstream ofs(toml_path);
    ofs << root;

    BD_INFO("[dlc] saved dlc.toml ({} entries)", dlc_list_.size());
}

/**
 * @brief Load toml, cross-reference with ContentManager.
 */
void DlcManager::Refresh() {
    std::filesystem::create_directories(data_dir_);
    LoadMetadata();

    auto* ks = rex::system::kernel_state();
    if (!ks) return;
    auto* cm = ks->content_manager();
    if (!cm) return;

    auto installed = cm->ListContent(1, 0, XContentType::kMarketplaceContent);

    bool changed = false;

    auto it = dlc_list_.begin();
    while (it != dlc_list_.end()) {
        bool found = false;
        for (const auto& pkg : installed) {
            if (pkg.file_name() == it->file_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            BD_INFO("[dlc] pruning stale entry: {}", it->file_name);
            it = dlc_list_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    for (const auto& pkg : installed) {
        std::string fname = pkg.file_name();
        bool found = false;
        for (const auto& info : dlc_list_) {
            if (info.file_name == fname) {
                found = true;
                break;
            }
        }
        if (!found) {
            DlcInfo info;
            info.file_name = fname;
            info.display_name = U16ToUtf8(pkg.display_name());
            // Description/publisher only available during Install() from the
            // original STFS package header. Discovered entries won't have them.
            BD_INFO("[dlc] discovered installed DLC: {} ({})", info.display_name, fname);
            dlc_list_.push_back(std::move(info));
            changed = true;
        }
    }

    if (changed)
        SaveMetadata();

    std::sort(dlc_list_.begin(), dlc_list_.end(),
              [](const DlcInfo &a, const DlcInfo &b) {
                  return a.display_name < b.display_name;
              });

    BD_INFO("[dlc] refresh complete: {} DLCs", dlc_list_.size());
}

/**
 * @brief Read STFS header, call InstallContent, persist metadata.
 */
bool DlcManager::Install(const std::filesystem::path& package_path) {
    auto header = StfsContainerDevice::ReadPackageHeader(package_path);
    if (!header) {
        BD_ERROR("[dlc] failed to read package header: {}", package_path.string());
        return false;
    }

    std::string display_name = U16ToUtf8(header->metadata.display_name(XLanguage::kEnglish));
    std::string description = U16ToUtf8(header->metadata.description(XLanguage::kEnglish));
    std::string publisher = U16ToUtf8(header->metadata.publisher());

    u32 thumb_size = header->metadata.thumbnail_size;
    std::vector<u8> thumbnail;
    if (thumb_size > 0 && thumb_size <= sizeof(header->metadata.thumbnail)) {
        thumbnail.assign(header->metadata.thumbnail,
                         header->metadata.thumbnail + thumb_size);
    }

    BD_INFO("[dlc] installing '{}' from {}", display_name, package_path.string());

    auto* ks = rex::system::kernel_state();
    if (!ks || !ks->content_manager()) {
        BD_ERROR("[dlc] kernel state or content manager unavailable");
        return false;
    }

    auto result = ks->content_manager()->InstallContent(package_path);
    if (result != 0) {
        BD_ERROR("[dlc] InstallContent failed with 0x{:08X}", result);
        return false;
    }

    auto installed = ks->content_manager()->ListContent(1, 0, XContentType::kMarketplaceContent);
    std::string file_name;
    for (const auto& pkg : installed) {
        std::string pkg_name = U16ToUtf8(pkg.display_name());
        if (pkg_name == display_name) {
            file_name = pkg.file_name();
            break;
        }
    }

    if (file_name.empty()) {
        file_name = package_path.stem().string();
        BD_WARN("[dlc] could not match installed entry, using filename: {}", file_name);
    }

    for (const auto& info : dlc_list_) {
        if (info.file_name == file_name) {
            BD_WARN("[dlc] DLC already in list: {}", file_name);
            return true;
        }
    }

    if (!thumbnail.empty()) {
        auto thumbs_dir = data_dir_ / "dlc_thumbs";
        std::filesystem::create_directories(thumbs_dir);
        auto thumb_path = thumbs_dir / (file_name + ".png");
        std::ofstream f(thumb_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(thumbnail.data()), thumbnail.size());
    }

    DlcInfo info;
    info.file_name = file_name;
    info.display_name = display_name;
    info.description = description;
    info.publisher = publisher;
    info.thumbnail = std::move(thumbnail);
    dlc_list_.push_back(std::move(info));

    SaveMetadata();

    BD_INFO("[dlc] installed successfully: {} ({})", display_name, file_name);
    return true;
}

/**
 * @brief Remove metadata, thumbnail, persist.
 */
bool DlcManager::Delete(size_t index) {
    if (index >= dlc_list_.size()) {
        BD_ERROR("[dlc] Delete: index {} out of range", index);
        return false;
    }

    const auto& info = dlc_list_[index];
    BD_INFO("[dlc] deleting '{}' ({})", info.display_name, info.file_name);

    auto* ks = rex::system::kernel_state();
    if (!ks || !ks->content_manager()) {
        BD_ERROR("[dlc] kernel state or content manager unavailable");
        return false;
    }

    using rex::system::xam::XCONTENT_AGGREGATE_DATA;
    XCONTENT_AGGREGATE_DATA data{};
    data.device_id = 1;
    data.content_type = XContentType::kMarketplaceContent;
    data.set_file_name(info.file_name);
    data.title_id = rex::system::xam::kCurrentlyRunningTitleId;

    auto result = ks->content_manager()->UnmountAndDeleteContent(0, data);
    if (result != 0) {
        BD_ERROR("[dlc] UnmountAndDeleteContent failed: 0x{:08X}", result);
        return false;
    }

    BD_INFO("[dlc] UnmountAndDeleteContent succeeded");

    auto thumb_path = data_dir_ / "dlc_thumbs" / (info.file_name + ".png");
    std::error_code ec;
    std::filesystem::remove(thumb_path, ec);

    dlc_list_.erase(dlc_list_.begin() + static_cast<ptrdiff_t>(index));
    SaveMetadata();
    return true;
}

}  // namespace bd
