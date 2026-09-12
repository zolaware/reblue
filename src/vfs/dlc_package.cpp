/**
 * @file    vfs/dlc_package.cpp
 * @brief   STFS package validation, extraction, and header sidecar handling.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "core/encoding.h"
#include "core/logging.h"
#include "core/xcontent.h"
#include "vfs/dlc_catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/string.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>
#include <rex/types.h>

using rex::filesystem::StfsContainerDevice;
using rex::system::XLanguage;

namespace bd::vfs {

namespace {

std::string SanitizeFolderName(const std::string &name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' ||
        c == '_' || c == '.')
      out += c;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
    out.pop_back();
  auto b = out.find_first_not_of(' ');
  return b == std::string::npos ? std::string() : out.substr(b);
}

// Publish restores <pack>.header (XCONTENT_AGGREGATE_DATA + license mask)
// from the store sidecar into the profile XAM tree. Without it the engine's
// bdIsDownloadPackageInstalled sees license 0 and grants junk.
bool WriteHeaderSidecar(const std::filesystem::path &sidecar_dir,
                        const std::string &folder,
                        const rex::filesystem::StfsHeader &stfs) {
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = rex::system::XContentType::kMarketplaceContent;
  data.title_id = stfs.metadata.execution_info.title_id;
  data.xuid = 0;
  data.set_display_name(stfs.metadata.display_name(XLanguage::kEnglish));
  data.set_file_name(folder);

  u32 license_mask = 0;
  for (const auto &lic : stfs.header.licenses) {
    if (lic.license_flags)
      license_mask |= lic.license_bits;
  }

  std::error_code ec;
  std::filesystem::create_directories(sidecar_dir, ec);
  std::ofstream out(sidecar_dir / (folder + ".header"), std::ios::binary);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char *>(&data), sizeof(data));
  out.write(reinterpret_cast<const char *>(&license_mask),
            sizeof(license_mask));
  return out.good();
}

} // namespace

DLCValidation DLCCatalog::Validate(const std::filesystem::path &package) {
  DLCValidation result;

  std::error_code ec;
  if (package.empty() || !std::filesystem::is_regular_file(package, ec)) {
    result.error = "File not found.";
    return result;
  }

  // ReadPackageHeader rejects missing/too-small files and invalid magic.
  auto header = StfsContainerDevice::ReadPackageHeader(package);
  if (!header || !header->header.is_magic_valid()) {
    result.error = "Not a valid Xbox 360 content package (STFS).";
    return result;
  }

  const u32 title_id = header->metadata.execution_info.title_id;
  if (title_id != kBlueDragonTitleId) {
    BD_ERROR("[dlc] rejecting '{}': title {:08X} is not Blue Dragon ({:08X})",
             package.string(), title_id, kBlueDragonTitleId);
    result.error = "This package is not Blue Dragon content.";
    return result;
  }

  if (static_cast<rex::system::XContentType>(header->metadata.content_type) !=
      rex::system::XContentType::kMarketplaceContent) {
    result.error = "This package is not downloadable content (DLC).";
    return result;
  }

  result.ok = true;
  result.display_name =
      U16ToUtf8(header->metadata.display_name(XLanguage::kEnglish));
  return result;
}

bool DLCCatalog::Install(const std::filesystem::path &package) {
  auto validation = Validate(package);
  if (!validation.ok) {
    BD_ERROR("[dlc] rejected '{}': {}", package.string(), validation.error);
    return false;
  }

  auto header = StfsContainerDevice::ReadPackageHeader(package);
  if (!header) {
    BD_ERROR("[dlc] failed to read package header: {}", package.string());
    return false;
  }

  std::string display_name =
      U16ToUtf8(header->metadata.display_name(XLanguage::kEnglish));
  std::string description =
      U16ToUtf8(header->metadata.description(XLanguage::kEnglish));
  std::string publisher = U16ToUtf8(header->metadata.publisher());

  std::string folder = SanitizeFolderName(display_name);
  if (folder.empty())
    folder = package.filename().string();
  // XCONTENT_DATA file_name is 42 chars. The .header must match the folder.
  if (folder.size() > 42)
    folder.resize(42);

  std::lock_guard lock(mutex_);

  auto dest = root_ / folder;
  if (std::filesystem::exists(dest)) {
    BD_WARN("[dlc] already installed: {}", folder);
    return false;
  }

  BD_INFO("[dlc] installing '{}' from {}", display_name, package.string());

  StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    BD_ERROR("[dlc] failed to open STFS package: {}", package.string());
    return false;
  }

  auto *root = device.ResolvePath("");
  if (!root) {
    BD_ERROR("[dlc] empty STFS package: {}", package.string());
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(dest, ec);

  bool ok = true;
  std::queue<rex::filesystem::Entry *> queue;
  queue.push(root);
  while (ok && !queue.empty()) {
    auto *entry = queue.front();
    queue.pop();
    for (auto &child : entry->children())
      queue.push(child.get());
    if (entry == root)
      continue;
    const auto entry_dest =
        dest /
        rex::to_path(rex::string::utf8_fix_path_separators(entry->path()));
    if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
      std::filesystem::create_directories(entry_dest, ec);
      ok = !ec;
    } else {
      ok = CopyEntry(*entry, entry_dest);
    }
  }

  if (!ok) {
    BD_ERROR("[dlc] extraction failed: {}", package.string());
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  if (!std::filesystem::exists(dest / "Download" / "DL_Param.ipk") &&
      !std::filesystem::exists(dest / "data" / "Download" / "DL_Param.ipk")) {
    BD_ERROR(
        "[dlc] package has no Download\\DL_Param.ipk, not Blue Dragon DLC");
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  if (!WriteHeaderSidecar(root_ / ".headers", folder, *header)) {
    BD_ERROR("[dlc] failed to write header sidecar for '{}'", folder);
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  ReloadLocked();

  // ReloadLocked's LoadMetadataLocked only knows name/display_name/enabled:
  // description and publisher come from the STFS header read above, so write
  // them onto the freshly reconciled entry before it hits disk.
  auto it = std::find_if(packages_.begin(), packages_.end(),
                         [&](const DLCPackage &p) { return p.name == folder; });
  if (it != packages_.end()) {
    it->description = description;
    it->publisher = publisher;
  }
  SaveMetadataLocked();

  BD_INFO("[dlc] installed successfully: {} ({})", display_name, folder);
  return true;
}

} // namespace bd::vfs
