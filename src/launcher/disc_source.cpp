// SPDX-License-Identifier: BSD-3-Clause
#include "launcher/recomplauncher.h"
#include <stdexcept>
#include <rex/filesystem/entry.h>
#include "core/xcontent.h"
#include "installer/disc_install.h"

#ifdef REBLUE_BUILD_INSTALLER
namespace bd::launcher {
namespace fs = std::filesystem;
namespace {
bool BlueDragonExecutable(rex::filesystem::Entry &root) {
  auto *entry = root.ResolvePath("default.xex");
  if (!entry || entry->size() > (128u << 20))
    return false;
  const auto xex = bd::ReadEntry(*entry);
  auto be32 = [&](size_t offset) -> uint32_t {
    if (offset > xex.size() || xex.size() - offset < 4)
      throw std::runtime_error("Truncated XEX header in the selected disc.");
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
      value = (value << 8) | static_cast<unsigned char>(xex[offset + i]);
    return value;
  };
  if (xex.size() < 24 || xex.compare(0, 4, "XEX2") != 0)
    return false;
  const auto headers = be32(20);
  if (headers > (xex.size() - 24) / 8)
    return false;
  for (size_t i = 0; i < headers; ++i)
    if (be32(24 + 8 * i) == 0x00040006) {
      const auto offset = static_cast<size_t>(be32(28 + 8 * i));
      return offset <= xex.size() && xex.size() - offset >= 24 &&
             be32(offset + 12) == bd::kBlueDragonTitleId;
    }
  return false;
}

} // namespace

std::array<bool, 3> InspectDiscSource(const fs::path &path) {
  std::array<bool, 3> result{};
  auto image = installer::DiscImage::Open(path);
  if (!image)
    return result;
  for (int n = 1; n <= installer::kDiscCount; ++n)
    if (auto *root = image->Root(n))
      result[n - 1] = BlueDragonExecutable(*root);
  return result;
}

} // namespace bd::launcher
#endif
