/**
 * @file    installer/disc_image.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause License; see LICENSE in the project root.
 */
#include "installer/disc_install.h"
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/system/xcontent.h>
#include "core/xcontent.h"

namespace bd::installer {
namespace fs = std::filesystem;
namespace {

std::string DiscMarker(int disc_number) {
  return "bd_disc_" + std::to_string(disc_number) + ".xml";
}

} // namespace

bool ValidateDisc(rex::filesystem::Entry &root, int disc_number) {
  return root.ResolvePath(DiscMarker(disc_number)) != nullptr;
}

std::string DiscFingerprint(size_t content_size, rex::filesystem::Entry &root,
                            int disc_number) {
  size_t marker_size = 0;
  if (auto *entry = root.ResolvePath(DiscMarker(disc_number)); entry != nullptr)
    marker_size = entry->size();
  return std::to_string(content_size) + ":" + std::to_string(marker_size);
}

DiscImage::DiscImage(std::unique_ptr<rex::filesystem::Device> device,
                     size_t content_size)
    : device_(std::move(device)), content_size_(content_size) {
  auto *root = device_->ResolvePath("");
  if (!root)
    return;
  for (int n = 1; n <= kDiscCount; ++n) {
    if (ValidateDisc(*root, n)) {
      roots_[n - 1] = root;
      continue;
    }
    auto *sub = root->ResolvePath("Disc" + std::to_string(n));
    if (sub && ValidateDisc(*sub, n))
      roots_[n - 1] = sub;
  }
}

DiscImage::~DiscImage() = default;

std::unique_ptr<DiscImage> DiscImage::Open(const fs::path &file) {
  std::error_code ec;
  if (!fs::is_regular_file(file, ec))
    return nullptr;

  auto disc = std::make_unique<rex::filesystem::DiscImageDevice>("", file);
  if (disc->Initialize())
    return std::make_unique<DiscImage>(std::move(disc),
                                       fs::file_size(file, ec));

  auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(file);
  if (!header || !header->header.is_magic_valid())
    return nullptr;
  if (static_cast<rex::system::XContentType>(header->metadata.content_type) !=
          rex::system::XContentType::kGamesOnDemand ||
      header->metadata.execution_info.title_id != kBlueDragonTitleId)
    return nullptr;
  auto container =
      std::make_unique<rex::filesystem::StfsContainerDevice>("", file);
  if (!container->Initialize())
    return nullptr;
  return std::make_unique<DiscImage>(
      std::move(container),
      static_cast<size_t>(header->metadata.data_file_size));
}

} // namespace bd::installer
