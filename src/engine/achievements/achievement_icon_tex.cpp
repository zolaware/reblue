/**
 * @file    engine/achievements/achievement_icon_tex.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/achievements/achievement_icon_tex.h"

#include <format>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <rex/embedded_metadata.h>
#include <rex/runtime.h>
#include <rex/runtime/achievement_manager.h>
#include <rex/runtime/content_manager.h>

#include <stb_image.h>

#include "core/logging.h"
#include "engine/guest_texture.h"
#include "engine/live_texture_stamp.h"

namespace bd::engine {

namespace {

// Cells are the size the icons ship at, and both atlas dimensions stay powers
// of two so a row's UV rect is exact and no cell bleeds into its neighbor.
constexpr u32 kCellSize = 64;
constexpr u32 kAtlasCols = 8;
constexpr u32 kAtlasWidth = kCellSize * kAtlasCols;
constexpr u32 kMaxAtlasRows = 16;

// Cell 0 is never drawn into: a row that asks for a cell past the end of the
// atlas resolves there and draws nothing, rather than a corner of somebody
// else's icon.
constexpr u32 kFirstIconCell = 1;

std::mutex s_mutex;
std::optional<std::vector<u8>> s_atlas;
u32 s_atlasRows = 0;
// Only achievements that actually got an icon drawn. Everything else falls
// through to the empty cell.
std::unordered_map<u32, u32> s_cells;
// Catalog size the atlas was built from. A larger catalog means a custom row
// registered since and the atlas is missing its icon.
size_t s_builtCount = 0;
LiveTextureStamp s_stamp;

u32 RoundUpPow2(u32 v) {
  u32 p = 1;
  while (p < v)
    p *= 2;
  return p;
}

std::vector<u8> LoadIconPng(const rex::runtime::Achievement &info) {
  // Ours are baked into the executable and registered as metadata assets at
  // catalog time. The title's live in its own XDBF image table.
  if (!info.icon_path.empty()) {
    if (const auto asset = rex::FindEmbeddedMetadataAsset(info.icon_path))
      return {asset->bytes.begin(), asset->bytes.end()};
  }
  if (info.image_id) {
    auto *rt = rex::GetProcessRuntime();
    const auto xdbf =
        rt ? rt->GetContentManager().GetTitleXDBF() : rex::runtime::XDBFTitleData(nullptr, 0);
    if (xdbf.IsValid()) {
      const auto block =
          xdbf.GetEntry(rex::runtime::XDBFSection::kImage, info.image_id);
      if (block)
        return {block.buffer, block.buffer + block.size};
    }
  }
  return {};
}

// Nearest-neighbor into the cell: the icons ship at cell size, so this only
// covers a title whose XDBF images are authored at some other resolution.
void BlitCell(std::vector<u8> &rgba, u32 atlasHeight, u32 cell,
              const stbi_uc *src, int srcW, int srcH) {
  const u32 col = cell % kAtlasCols;
  const u32 row = cell / kAtlasCols;
  if (row >= atlasHeight / kCellSize)
    return;

  for (u32 y = 0; y < kCellSize; ++y) {
    const u32 sy = static_cast<u32>(srcH) * y / kCellSize;
    for (u32 x = 0; x < kCellSize; ++x) {
      const u32 sx = static_cast<u32>(srcW) * x / kCellSize;
      const stbi_uc *px = src + (size_t(sy) * static_cast<u32>(srcW) + sx) * 4;
      u8 *dst = rgba.data() + ((size_t(row) * kCellSize + y) * kAtlasWidth +
                               col * kCellSize + x) *
                                  4;
      dst[0] = px[0];
      dst[1] = px[1];
      dst[2] = px[2];
      dst[3] = px[3];
    }
  }
}

void BuildAtlas() {
  auto *rt = rex::GetProcessRuntime();
  // Registration order, not the viewer's display order: this is cached for the
  // life of the process, and the viewer re-sorts its rows on every visit.
  const auto catalog = rt ? rt->GetAchievementManager().ListAchievements()
                          : std::vector<rex::runtime::Achievement>();
  // Left unbuilt rather than cached empty, so a read once the runtime is up
  // still gets an atlas.
  if (catalog.empty()) {
    REXLOG_WARN(bd, "achv no catalog to build an icon atlas from");
    return;
  }

  s_builtCount = catalog.size();
  const u32 cells = static_cast<u32>(catalog.size()) + kFirstIconCell;
  s_atlasRows = RoundUpPow2((cells + kAtlasCols - 1u) / kAtlasCols);
  if (s_atlasRows > kMaxAtlasRows) {
    s_atlasRows = kMaxAtlasRows;
    REXLOG_WARN(bd, "achv {} achievements exceed the {} the icon atlas holds, the "
            "rest draw without one",
            catalog.size(), kMaxAtlasRows * kAtlasCols - kFirstIconCell);
  }
  const u32 height = s_atlasRows * kCellSize;

  std::vector<u8> rgba(size_t(kAtlasWidth) * height * 4, 0);
  s_cells.clear();
  for (size_t i = 0; i < catalog.size(); ++i) {
    const u32 cell = static_cast<u32>(i) + kFirstIconCell;
    if (cell / kAtlasCols >= s_atlasRows)
      break;

    const std::vector<u8> png = LoadIconPng(catalog[i]);
    if (png.empty())
      continue;

    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(
        png.data(), static_cast<int>(png.size()), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
      if (pixels)
        stbi_image_free(pixels);
      REXLOG_WARN(bd, "achv icon for 0x{:X} is not a decodable image", catalog[i].id);
      continue;
    }
    BlitCell(rgba, height, cell, pixels, w, h);
    stbi_image_free(pixels);
    s_cells[catalog[i].id] = cell;
  }

  s_atlas = BuildGuestTexture(rgba, kAtlasWidth, height);
  REXLOG_DEBUG(bd, "achv icon atlas {}x{} holds {} of {} icons", kAtlasWidth, height,
           s_cells.size(), catalog.size());
}

} // namespace

const std::vector<u8> *GetAchievementIconAtlas() {
  std::lock_guard lock(s_mutex);
  if (!s_atlas)
    BuildAtlas();
  return s_atlas ? &*s_atlas : nullptr;
}

void SyncAchievementIconAtlas() {
  auto *rt = rex::GetProcessRuntime();
  const size_t count = rt ? rt->GetAchievementManager().ListAchievements().size() : 0;

  std::lock_guard lock(s_mutex);
  if (count != 0 && (!s_atlas || count != s_builtCount))
    BuildAtlas();
  if (!s_atlas)
    return;
  s_stamp.Sync(kAchievementIconAtlasRef, static_cast<u32>(s_builtCount),
               [] { return *s_atlas; });
}

AchievementIconUv AchievementIconUVFor(u32 achievementId) {
  std::lock_guard lock(s_mutex);
  if (!s_atlas)
    BuildAtlas();

  if (s_atlasRows == 0)
    return {};

  const auto it = s_cells.find(achievementId);
  const u32 cell = it != s_cells.end() ? it->second : 0;

  const u32 col = cell % kAtlasCols;
  const u32 row = cell / kAtlasCols;

  AchievementIconUv uv;
  uv.u0 = static_cast<float>(col) / kAtlasCols;
  uv.u1 = static_cast<float>(col + 1) / kAtlasCols;
  uv.v0 = static_cast<float>(row) / s_atlasRows;
  uv.v1 = static_cast<float>(row + 1) / s_atlasRows;
  return uv;
}

const std::string_view kAchievementIconDirs[2] = {"d2anime\\camp\\dia",
                                                  "d2anime\\modmgr"};

std::string AchievementIconAtlasVFSKey(std::string_view csvDir) {
  return std::format("{}\\{}.dds", csvDir, kAchievementIconAtlasRef);
}

} // namespace bd::engine
