/**
 * @file    vfs/dlc_package.cpp
 * @brief   STFS package validation, extraction, and header sidecar handling.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "core/logging.h"
#include "core/xcontent.h"
#include "vfs/dlc_catalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

#include <rex/filesystem/container.h>
#include <rex/filesystem/xcontent_layout.h>
#include <rex/string.h>
#include <rex/runtime/xcontent_data.h>
#include <rex/types.h>
#include <rex/string/utf8.h>

using rex::filesystem::ReadSTFSHeader;
using rex::runtime::XLanguage;

namespace bd::vfs {

namespace {

using ContentId = std::array<u8, 0x14>;

constexpr ContentId kUltraHardMod = {0x2E, 0xA1, 0x10, 0xF0, 0xE8, 0xAA, 0x78,
                                     0x9C, 0x05, 0x0A, 0x34, 0xE2, 0x3B, 0x7A,
                                     0x6E, 0xDE, 0x65, 0x11, 0xA6, 0xAC};
constexpr ContentId kSixTreasures = {0x42, 0x89, 0xDC, 0x78, 0xFC, 0xC4, 0x4E,
                                     0x77, 0x36, 0x7C, 0x73, 0x10, 0x16, 0x2E,
                                     0x63, 0x99, 0xCE, 0x09, 0xC1, 0xE3};
constexpr ContentId kShuffleDungeon = {0x54, 0x71, 0x47, 0x7C, 0xA6, 0x38, 0xC7,
                                       0xC0, 0x2C, 0x1C, 0x9C, 0xF7, 0xFB, 0x65,
                                       0xDB, 0xB3, 0x42, 0x98, 0xF8, 0x79};
constexpr std::array<ContentId, 3> kSupportedContentIds = {
    kUltraHardMod, kSixTreasures, kShuffleDungeon};

bool IsSupportedContent(const u8 *content_id) {
  return std::any_of(kSupportedContentIds.begin(), kSupportedContentIds.end(),
                     [&](const ContentId &known) {
                       return std::equal(known.begin(), known.end(),
                                         content_id);
                     });
}

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
                        const rex::filesystem::XContentPackageHeader &stfs) {
  rex::runtime::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = 1;
  data.content_type = rex::runtime::XContentType::kMarketplaceContent;
  data.title_id = stfs.metadata.execution_info.title_id;
  data.xuid = 0;
  data.SetDisplayName(stfs.metadata.GetDisplayName(XLanguage::kEnglish));
  data.SetFileName(folder);

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

  // ReadSTFSHeader rejects missing/too-small files and invalid magic.
  auto header = ReadSTFSHeader(package);
  if (!header) {
    result.error = "Not a valid Xbox 360 content package (STFS).";
    return result;
  }

  const u32 title_id = header->metadata.execution_info.title_id;
  if (title_id != kBlueDragonTitleId) {
    REXLOG_ERROR(bd, "dlc rejecting '{}': title {:08X} is not Blue Dragon ({:08X})",
             package.string(), title_id, kBlueDragonTitleId);
    result.error = "This package is not Blue Dragon content.";
    return result;
  }

  if (static_cast<rex::runtime::XContentType>(header->metadata.content_type) !=
      rex::runtime::XContentType::kMarketplaceContent) {
    result.error = "This package is not downloadable content (DLC).";
    return result;
  }

  if (!IsSupportedContent(header->header.content_id)) {
    REXLOG_ERROR(bd, "dlc rejecting '{}': not a supported pack", package.string());
    result.error = "This package is not a supported Blue Dragon DLC.";
    return result;
  }

  result.ok = true;
  result.display_name =
      rex::ToUTF8(header->metadata.GetDisplayName(XLanguage::kEnglish));
  return result;
}

bool DLCCatalog::Install(const std::filesystem::path &package) {
  auto validation = Validate(package);
  if (!validation.ok) {
    REXLOG_ERROR(bd, "dlc rejected '{}': {}", package.string(), validation.error);
    return false;
  }

  auto header = ReadSTFSHeader(package);
  if (!header) {
    REXLOG_ERROR(bd, "dlc failed to read package header: {}", package.string());
    return false;
  }

  std::string display_name =
      rex::ToUTF8(header->metadata.GetDisplayName(XLanguage::kEnglish));
  std::string description =
      rex::ToUTF8(header->metadata.GetDescription(XLanguage::kEnglish));
  std::string publisher = rex::ToUTF8(header->metadata.GetPublisher());

  std::string folder = SanitizeFolderName(display_name);
  if (folder.empty())
    folder = package.filename().string();
  // XCONTENT_DATA file_name is 42 chars. The .header must match the folder.
  if (folder.size() > 42)
    folder.resize(42);

  std::lock_guard lock(mutex_);

  auto dest = root_ / folder;
  if (std::filesystem::exists(dest)) {
    REXLOG_WARN(bd, "dlc already installed: {}", folder);
    return false;
  }

  REXLOG_INFO(bd, "dlc installing '{}' from {}", display_name, package.string());

  auto container = rex::filesystem::Container::Open(package);
  if (!container) {
    REXLOG_ERROR(bd, "dlc failed to open STFS package: {}: {}", package.string(),
             container.error().message);
    return false;
  }

  std::error_code ec;
  const bool ok = container->Extract(dest).has_value();
  if (!ok) {
    REXLOG_ERROR(bd, "dlc extraction failed: {}", package.string());
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  if (!std::filesystem::exists(dest / "Download" / "DL_Param.ipk") &&
      !std::filesystem::exists(dest / "data" / "Download" / "DL_Param.ipk")) {
    REXLOG_ERROR(bd, 
        "dlc package has no Download\\DL_Param.ipk, not Blue Dragon DLC");
    std::filesystem::remove_all(dest, ec);
    return false;
  }

  if (!WriteHeaderSidecar(root_ / ".headers", folder, *header)) {
    REXLOG_ERROR(bd, "dlc failed to write header sidecar for '{}'", folder);
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

  REXLOG_INFO(bd, "dlc installed successfully: {} ({})", display_name, folder);
  return true;
}

} // namespace bd::vfs
