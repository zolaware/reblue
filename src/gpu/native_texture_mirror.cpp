/**
 * @file    gpu/native_texture_mirror.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/native_texture_mirror.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/math.h>
#include <rex/xenos/texture_conversion.h>
#include <rex/xenos/texture_layout.h>
#include <rex/xenos/xenos.h>
#include <rex/ppc.h>
#include <rex/string/utf8.h>
#include <rex/ppc/midasm.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/host_resource_heap.h"
#include "gpu/texture_upload.h"

namespace bd::gpu {

namespace {

namespace tu = rex::xenos;
namespace tc = rex::xenos;
namespace xe = rex::xenos;

std::mutex g_mirror_mutex;
std::unordered_map<u32, std::unique_ptr<GuestTexture>> g_native_mirrors;

// Asset name per live mirror, normalized to a lowercased basename, so the
// engine can find the loaded instance of a texture it serves and rewrite it.
// The sequence number distinguishes allocations that reuse a VA.
struct NamedNativeTexture {
  std::string name;
  u64 seq = 0;
};
std::unordered_map<u32, NamedNativeTexture> g_native_names;
u64 g_native_name_seq = 0;

std::string NormalizeTextureName(std::string_view raw) {
  const size_t slash = raw.find_last_of("\\/");
  if (slash != std::string_view::npos)
    raw.remove_prefix(slash + 1);
  const size_t dot = raw.rfind('.');
  if (dot != std::string_view::npos)
    raw = raw.substr(0, dot);
  return rex::utf8_lower_ascii(raw);
}

// Evicted (guest thread) but not yet freed. Held per slot until the post-fence
// drain so freeing the texture + rewriting its descriptor can't race an
// in-flight command list.
std::vector<std::unique_ptr<GuestTexture>> g_pending_native_destroy[kNumFrames];

// The name is diagnostic only and BD passes 0 for unnamed allocations, so an
// absent or unmapped name still logs something the line can be read by.
std::string TextureName(u32 name_va) {
  std::string name = bd::mem::cstr(name_va);
  return name.empty() ? "?" : name;
}

// Derived once from the fetch constant and the format's block geometry.
struct MirrorLayout {
  xe::TextureFormat format = xe::TextureFormat::k_8_8_8_8;
  xe::Endian endian = xe::Endian::kNone;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 1;
  u32 texels_per_edge = 1;
  u32 bytes_per_block = 1;
  u32 bpb_log2 = 0;
  u32 block_w = 0;
  u32 block_h = 0;
  // Tiled mip-0 footprint in blocks, padded to the 32-block macro tile.
  u32 aligned_w = 0;
  u32 aligned_h = 0;
  u32 staging_row_bytes = 0; // 256-aligned for the D3D12 copy rule
  u32 row_width_texels = 0;
  // A dimension <= 16 sits offset inside the level-0 tile, not at its origin.
  int pack_off_x = 0;
  int pack_off_y = 0;
};

u32 FetchWidth(const xe::TextureFetch &f) {
  return (f.dimension == xe::DataDimension::k3D ? f.size_3d.width
                                                : f.size_2d.width) +
         1u;
}

u32 FetchHeight(const xe::TextureFetch &f) {
  return (f.dimension == xe::DataDimension::k3D ? f.size_3d.height
                                                : f.size_2d.height) +
         1u;
}

u32 FetchDepth(const xe::TextureFetch &f) {
  return f.dimension == xe::DataDimension::k3D ? f.size_3d.depth + 1u : 1u;
}

bool ReadFetch(u32 guest_va, xe::TextureFetch &fetch) {
  const auto *d3dtex = bd::mem::try_at<const D3DTexture>(guest_va);
  if (!d3dtex)
    return false;
  u32 fc[6];
  for (int i = 0; i < 6; ++i) {
    fc[i] = u32(d3dtex->Format.dword[i]);
  }
  std::memcpy(&fetch, fc, sizeof(fetch));
  return true;
}

MirrorLayout MakeLayout(const xe::TextureFetch &fetch) {
  MirrorLayout L;
  L.format = fetch.format;
  L.endian = fetch.endianness;
  L.width = FetchWidth(fetch);
  L.height = FetchHeight(fetch);
  L.depth = FetchDepth(fetch);

  const xe::FormatInfo *info = xe::FormatInfo::Get(L.format);
  L.texels_per_edge = info->block_width;
  L.bytes_per_block = info->GetBytesPerBlock();
  L.bpb_log2 = rex::log2_floor(L.bytes_per_block);
  L.block_w = rex::RoundUp(L.width, L.texels_per_edge) / L.texels_per_edge;
  L.block_h = rex::RoundUp(L.height, L.texels_per_edge) / L.texels_per_edge;
  L.aligned_w = rex::RoundUp(L.block_w, 32u);
  L.aligned_h = rex::RoundUp(L.block_h, 32u);
  L.staging_row_bytes = rex::RoundUp(L.block_w * L.bytes_per_block, 256u);
  L.row_width_texels =
      (L.staging_row_bytes / L.bytes_per_block) * L.texels_per_edge;

  if (fetch.packed_mips) {
    u32 px = 0, py = 0, pz = 0;
    tu::GetPackedMipOffset(L.width, L.height, 1u, L.format, 0u, px, py, pz);
    L.pack_off_x = static_cast<int>(px);
    L.pack_off_y = static_cast<int>(py);
  }
  return L;
}

size_t FaceStagingSize(const MirrorLayout &L) {
  return size_t(L.staging_row_bytes) * size_t(L.block_h);
}

// Gathers each untiled block from its tiled offset. 'out' is zero-filled by
// the caller, so an out-of-range gather simply leaves its block zero.
void UntileFace(const MirrorLayout &L, const u8 *face_src, u8 *out, int off_x,
                int off_y) {
  const u32 tiled_size = L.aligned_w * L.aligned_h * L.bytes_per_block;
  const int bpb = static_cast<int>(L.bytes_per_block);
  for (int by = 0; by < static_cast<int>(L.block_h); ++by) {
    for (int bx = 0; bx < static_cast<int>(L.block_w); ++bx) {
      const i32 so =
          tu::GetTiledOffset2D(off_x + bx, off_y + by, L.aligned_w, L.bpb_log2);
      if (so < 0 || u32(so) + L.bytes_per_block > tiled_size)
        continue;
      u8 *db =
          out + size_t(by) * L.staging_row_bytes + size_t(bx) * size_t(bpb);
      tc::CopySwapBlock(L.endian, db, face_src + so, size_t(bpb));
    }
  }
}

// The tiled footprint pads W/H to 32 blocks and depth to 4.
GuestTexture *BuildVolumeMirror(const MirrorLayout &L, const u8 *src,
                                u32 guest_va) {
  const size_t slice_size = FaceStagingSize(L);
  const size_t src_size = size_t(L.aligned_w) * size_t(L.aligned_h) *
                          size_t(rex::RoundUp(L.depth, 4u)) *
                          size_t(L.bytes_per_block);

  std::vector<u8> staging(slice_size * L.depth, 0u);
  for (int z = 0; z < static_cast<int>(L.depth); ++z) {
    for (int by = 0; by < static_cast<int>(L.block_h); ++by) {
      for (int bx = 0; bx < static_cast<int>(L.block_w); ++bx) {
        const i32 so = tu::GetTiledOffset3D(bx, by, z, L.aligned_w, L.aligned_h,
                                            L.bpb_log2);
        if (so < 0 || size_t(so) + L.bytes_per_block > src_size)
          continue;
        u8 *db = staging.data() + size_t(z) * slice_size +
                 size_t(by) * L.staging_row_bytes +
                 size_t(bx) * L.bytes_per_block;
        tc::CopySwapBlock(L.endian, db, src + size_t(so), L.bytes_per_block);
      }
    }
  }

  GuestTexture *tex = BuildBCMirrorTextureVolume(
      L.width, L.height, L.depth, u32(L.format), staging.data(), staging.size(),
      L.staging_row_bytes, L.row_width_texels);
  if (!tex) {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: volume mirror build failed "
            "({}x{}x{} fmt=0x{:X})",
            guest_va, L.width, L.height, L.depth, u32(L.format));
    return nullptr;
  }

  // A tfetch2D shader on a 3D resource samples slice 0, and SetTexture routes
  // the volume into both index tables.
  GuestTexture *slice0 = BuildBCMirrorTexture(
      L.width, L.height, u32(L.format), staging.data(), slice_size,
      L.staging_row_bytes, L.row_width_texels);
  if (slice0) {
    tex->companion2D.reset(slice0);
  } else {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: slice-0 2D companion build failed "
            "({}x{} fmt=0x{:X}), 3D-as-2D fetch will read null",
            guest_va, L.width, L.height, u32(L.format));
  }
  return tex;
}

// 6 faces packed consecutively from the base address, per-face stride being
// the exact tiled mip-0 footprint with no mip tail or padding between faces.
GuestTexture *BuildCubeMirror(const MirrorLayout &L, const u8 *src,
                              u32 guest_va) {
  const size_t face_stride =
      size_t(L.aligned_w) * size_t(L.aligned_h) * size_t(L.bytes_per_block);
  const size_t face_size = FaceStagingSize(L);

  std::vector<u8> face_bufs[6];
  const u8 *face_ptrs[6];
  for (int face = 0; face < 6; ++face) {
    face_bufs[face].assign(face_size, 0u);
    UntileFace(L, src + size_t(face) * face_stride, face_bufs[face].data(),
               L.pack_off_x, L.pack_off_y);
    face_ptrs[face] = face_bufs[face].data();
  }

  GuestTexture *tex = BuildBCMirrorTextureCube(
      L.width, L.height, u32(L.format), face_ptrs, face_size,
      L.staging_row_bytes, L.row_width_texels);
  if (!tex) {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: cube mirror build failed "
            "({}x{} fmt=0x{:X})",
            guest_va, L.width, L.height, u32(L.format));
  }
  return tex;
}

// Levels 1..mip_max live at mip_address, with a packed tail for the <=16-texel
// levels. nullptr leaves the caller on the single-level path.
GuestTexture *BuildMippedMirror(const MirrorLayout &L,
                                const xe::TextureFetch &fetch,
                                std::vector<u8> &base_staging) {
  const auto *mip_src = bd::mem::at<const u8>(fetch.mip_address << 12);
  const u32 mip_max_level = std::min<u32>(
      fetch.mip_max_level, rex::log2_floor(std::max(L.width, L.height)));
  if (mip_max_level < 1u || !mip_src)
    return nullptr;

  const tu::GuestTextureLayout layout = tu::GetGuestTextureLayout(
      xe::DataDimension::k2DOrStacked, fetch.pitch, L.width, L.height, 1u,
      /*is_tiled=*/true, L.format,
      /*has_packed_levels=*/fetch.packed_mips != 0u, /*has_base=*/true,
      mip_max_level);

  // packed_level 0 means the base is in the tail too, so there is no chain.
  if (layout.packed_level == 0u)
    return nullptr;

  std::vector<std::vector<u8>> level_bufs(mip_max_level);
  std::vector<BCMipLevel> mip_levels;
  mip_levels.reserve(mip_max_level + 1u);
  mip_levels.push_back({base_staging.data(), base_staging.size(), L.width,
                        L.height, L.row_width_texels});

  for (u32 lvl = 1u; lvl <= mip_max_level; ++lvl) {
    const u32 lw = std::max(L.width >> lvl, 1u);
    const u32 lh = std::max(L.height >> lvl, 1u);
    const int lwb = int(rex::RoundUp(lw, L.texels_per_edge) / L.texels_per_edge);
    const int lhb = int(rex::RoundUp(lh, L.texels_per_edge) / L.texels_per_edge);
    const u32 dst_row = rex::RoundUp(u32(lwb) * L.bytes_per_block, 256u);
    const u32 dst_row_width_texels =
        (dst_row / L.bytes_per_block) * L.texels_per_edge;

    const bool packed =
        layout.packed_level != UINT32_MAX && lvl >= layout.packed_level;
    const u32 storage_level = packed ? layout.packed_level : lvl;
    const tu::GuestTextureLayout::Level &sl = layout.mips[storage_level];
    const u32 pitch_blocks = sl.row_pitch_bytes / L.bytes_per_block;
    const u8 *level_src = mip_src + layout.mip_offsets_bytes[storage_level];

    u32 px = 0, py = 0, pz = 0;
    if (packed) {
      tu::GetPackedMipOffset(L.width, L.height, 1u, L.format, lvl, px, py, pz);
    }

    std::vector<u8> &dst = level_bufs[lvl - 1u];
    dst.assign(size_t(dst_row) * size_t(lhb), 0u);
    for (int by = 0; by < lhb; ++by) {
      for (int bx = 0; bx < lwb; ++bx) {
        const i32 so = tu::GetTiledOffset2D(i32(px) + bx, i32(py) + by,
                                            pitch_blocks, L.bpb_log2);
        if (so < 0 || u32(so) + L.bytes_per_block > sl.level_data_extent_bytes)
          continue;
        u8 *db =
            dst.data() + size_t(by) * dst_row + size_t(bx) * L.bytes_per_block;
        tc::CopySwapBlock(L.endian, db, level_src + so, L.bytes_per_block);
      }
    }
    mip_levels.push_back(
        {dst.data(), dst.size(), lw, lh, dst_row_width_texels});
  }

  return BuildBCMirrorTexture2DMips(L.width, L.height, u32(L.format),
                                           mip_levels.data(),
                                           u32(mip_levels.size()));
}

// BD ships static reflection cubes (cube_*) as a 2D DXT atlas of six square
// faces left to right, but the water/glass fetch is a hardware cube (RefEnv,
// fetch slot 5), which would otherwise read the null placeholder. Slice the
// untiled mip-0 atlas into the TextureCube companion that slot publishes.
void AttachCubeAtlasCompanion(const MirrorLayout &L, GuestTexture *tex,
                              const std::vector<u8> &staging, u32 guest_va) {
  const u32 face_blocks_w =
      rex::RoundUp(L.height, L.texels_per_edge) / L.texels_per_edge;
  const u32 face_visible_row = face_blocks_w * L.bytes_per_block;
  const u32 face_row_bytes = rex::RoundUp(face_visible_row, 256u);
  const u32 face_size = face_row_bytes * L.block_h;
  const u32 face_row_width_texels =
      (face_row_bytes / L.bytes_per_block) * L.texels_per_edge;

  std::vector<u8> face_bufs[6];
  const u8 *face_ptrs[6];
  for (u32 f = 0; f < 6u; ++f) {
    face_bufs[f].assign(face_size, 0u);
    for (u32 by = 0; by < L.block_h; ++by) {
      const u8 *sb = staging.data() + size_t(by) * L.staging_row_bytes +
                     size_t(f) * face_visible_row;
      std::memcpy(face_bufs[f].data() + size_t(by) * face_row_bytes, sb,
                  face_visible_row);
    }
    face_ptrs[f] = face_bufs[f].data();
  }

  GuestTexture *cube = BuildBCMirrorTextureCube(
      L.height, L.height, u32(L.format), face_ptrs, face_size, face_row_bytes,
      face_row_width_texels);
  if (cube) {
    tex->companionCube.reset(cube);
  } else {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: cube_* atlas companion build failed "
            "({}x{} fmt=0x{:X}), water/glass cube fetch reads null",
            guest_va, L.height, L.height, u32(L.format));
  }
}

GuestTexture *Build2DMirror(const MirrorLayout &L, const u8 *src,
                            const xe::TextureFetch &fetch,
                            u32 guest_va, u32 name_va, bool dxt) {
  std::vector<u8> staging(FaceStagingSize(L), 0u);
  UntileFace(L, src, staging.data(), L.pack_off_x, L.pack_off_y);

  GuestTexture *tex = BuildMippedMirror(L, fetch, staging);
  if (!tex) {
    tex = BuildBCMirrorTexture(L.width, L.height, u32(L.format),
                                      staging.data(), staging.size(),
                                      L.staging_row_bytes, L.row_width_texels);
  }
  if (!tex) {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: 2D mirror build failed "
            "({}x{} fmt=0x{:X})",
            guest_va, L.width, L.height, u32(L.format));
    return nullptr;
  }

  const bool is_cube_atlas =
      bd::mem::cstr(name_va, 5) == "cube_" && dxt &&
      L.height > 0u && L.width == L.height * 6u;
  if (is_cube_atlas) {
    AttachCubeAtlasCompanion(L, tex, staging, guest_va);
  }
  return tex;
}

} // namespace

GuestTexture *GetOrCreateNativeMirror(u32 guest_va, u32 name_va) {
  if (!guest_va)
    return nullptr;
  std::lock_guard<std::mutex> lock(g_mirror_mutex);

  auto it = g_native_mirrors.find(guest_va);
  if (it != g_native_mirrors.end()) {
    // From the bdAllocRenderBuffer hook a hit means the engine re-malloc'd a
    // freed texture struct at this VA: the previous occupant was released via
    // the preload table path (hcgTextureListRelease) that we do not hook,
    // leaving this mirror stale. From NativeTextureReplace it is the rewritten
    // payload wanting a fresh mirror. Either way, evict and rebuild.
    g_pending_native_destroy[Video::RetireSlot("native mirror")].push_back(
        std::move(it->second));
    g_native_mirrors.erase(it);
  }

  xe::TextureFetch fetch;
  if (!ReadFetch(guest_va, fetch)) {
    REXLOG_WARN(bd, "native texture: guest VA 0x{:08X} does not translate", guest_va);
    return nullptr;
  }

  const bool dxt = (fetch.format == xe::TextureFormat::k_DXT1 ||
                    fetch.format == xe::TextureFormat::k_DXT2_3 ||
                    fetch.format == xe::TextureFormat::k_DXT4_5);
  // Uncompressed k_8_8_8_8 (color ramps / UI atlases BD stores raw, e.g. the
  // cloud sky LUT dg03_cbr01). 2D only: cube/volume assume DXT blocks.
  const bool rgba8 = fetch.format == xe::TextureFormat::k_8_8_8_8 &&
                     fetch.dimension <= xe::DataDimension::k2DOrStacked;
  const bool supported_dim = fetch.dimension != xe::DataDimension::k1D;
  // DXT ships endian kNone/k8in16, and k_8_8_8_8 ships k8in32 (full word swap).
  const bool endian_ok =
      fetch.endianness <= (dxt ? xe::Endian::k8in16 : xe::Endian::k8in32);

  // Non-tiled is rejected: the deswizzlers only handle tiled data.
  if (fetch.type != xe::FetchConstantType::kTexture || !supported_dim ||
      !(dxt || rgba8) || !endian_ok || !fetch.tiled) {
    REXLOG_WARN(bd, "native texture rejected name='{}' va=0x{:08X} type={} dim={} "
            "fmt=0x{:X} endian={} tiled={} {}x{}x{}",
            TextureName(name_va), guest_va, u32(fetch.type),
            u32(fetch.dimension), u32(fetch.format), u32(fetch.endianness),
            u32(fetch.tiled), FetchWidth(fetch), FetchHeight(fetch),
            FetchDepth(fetch));
    return nullptr;
  }

  // The XPhysicalAllocEx VA the engine wrote through, so the heap-aware
  // accessor matches how the data was written.
  const auto *src = bd::mem::at<const u8>(fetch.base_address << 12);
  if (!src) {
    REXLOG_WARN(bd, "native texture va=0x{:08X}: base address 0x{:08X} does not "
            "translate",
            guest_va, fetch.base_address << 12);
    return nullptr;
  }

  const MirrorLayout layout = MakeLayout(fetch);
  GuestTexture *tex = nullptr;
  switch (fetch.dimension) {
  case xe::DataDimension::k3D:
    tex = BuildVolumeMirror(layout, src, guest_va);
    break;
  case xe::DataDimension::kCube:
    tex = BuildCubeMirror(layout, src, guest_va);
    break;
  default:
    tex = Build2DMirror(layout, src, fetch, guest_va, name_va, dxt);
    break;
  }
  if (!tex)
    return nullptr;

  auto stored = std::unique_ptr<GuestTexture>(tex);
  GuestTexture *raw = stored.get();
  g_native_mirrors.emplace(guest_va, std::move(stored));
  // A rebuild through NativeTextureReplace passes no name and keeps the entry
  // the allocation registered, sequence included.
  if (name_va) {
    const std::string name = bd::mem::cstr(name_va);
    if (!name.empty())
      g_native_names[guest_va] = {NormalizeTextureName(name),
                                  ++g_native_name_seq};
  }
  return raw;
}

TextureContent TextureContent::Scan(u32 guest_va) {
  const TextureContent whole;
  xe::TextureFetch fetch;
  if (!ReadFetch(guest_va, fetch) ||
      fetch.type != xe::FetchConstantType::kTexture ||
      fetch.dimension != xe::DataDimension::k2DOrStacked ||
      !xe::FormatInfo::Get(fetch.format))
    return whole;

  const MirrorLayout L = MakeLayout(fetch);
  if (!L.width || !L.height || !L.bytes_per_block || !L.texels_per_edge)
    return whole;

  const u32 base = fetch.base_address << 12;
  const u32 blocks =
      fetch.tiled ? L.aligned_w * L.aligned_h : L.block_w * L.block_h;
  const auto *src = bd::mem::try_at<const u8>(base);
  // Both ends, since try_at validates an address and not a range.
  if (!src || !bd::mem::try_at<const u8>(base + blocks * L.bytes_per_block - 1))
    return whole;

  const auto block_at = [&](u32 bx, u32 by) -> const u8 * {
    if (!fetch.tiled)
      return src + (size_t(by) * L.block_w + bx) * L.bytes_per_block;
    const i32 offset =
        tu::GetTiledOffset2D(i32(bx), i32(by), L.aligned_w, L.bpb_log2);
    return offset < 0 ? nullptr : src + size_t(offset);
  };

  const u8 *field = block_at(0, 0);
  if (!field)
    return whole;

  u32 min_x = L.block_w;
  u32 min_y = L.block_h;
  u32 max_x = 0;
  u32 max_y = 0;
  for (u32 by = 0; by < L.block_h; ++by) {
    for (u32 bx = 0; bx < L.block_w; ++bx) {
      const u8 *block = block_at(bx, by);
      if (!block || std::memcmp(block, field, L.bytes_per_block) == 0)
        continue;
      min_x = std::min(min_x, bx);
      max_x = std::max(max_x, bx);
      min_y = std::min(min_y, by);
      max_y = std::max(max_y, by);
    }
  }
  if (min_x > max_x || min_y > max_y)
    return whole;

  TextureContent out;
  out.u0 = float(min_x * L.texels_per_edge) / float(L.width);
  out.v0 = float(min_y * L.texels_per_edge) / float(L.height);
  out.u1 =
      std::min(1.0f, float((max_x + 1) * L.texels_per_edge) / float(L.width));
  out.v1 =
      std::min(1.0f, float((max_y + 1) * L.texels_per_edge) / float(L.height));
  return out;
}

GuestTexture *ResolveGuestTexture(u32 guest_va) {
  if (!guest_va)
    return nullptr;

  if (auto *host = HostResourceHeap::FromGuest<GuestTexture>(guest_va)) {
    return host;
  }

  // Pure lookup, since mirrors are built eagerly at alloc time. Building on
  // demand would mis-decode arbitrary bytes as a fetch constant and overread
  // small allocations.
  {
    std::lock_guard<std::mutex> lock(g_mirror_mutex);
    auto it = g_native_mirrors.find(guest_va);
    if (it != g_native_mirrors.end())
      return it->second.get();
  }
  return nullptr;
}

void EvictNativeTexture(u32 guest_va) {
  if (!guest_va)
    return;
  std::lock_guard<std::mutex> lock(g_mirror_mutex);
  auto it = g_native_mirrors.find(guest_va);
  if (it == g_native_mirrors.end())
    return;
  // Defer teardown to the post-fence drain, and remove from the registry now so
  // a same-VA re-alloc builds a fresh mirror.
  g_pending_native_destroy[Video::RetireSlot("native mirror")].push_back(
      std::move(it->second));
  g_native_mirrors.erase(it);
  g_native_names.erase(guest_va);
}

std::vector<NativeTextureRef> NativeTexturesByName(std::string_view name) {
  const std::string wanted = NormalizeTextureName(name);
  std::vector<NativeTextureRef> out;
  std::lock_guard<std::mutex> lock(g_mirror_mutex);
  for (const auto &[va, entry] : g_native_names) {
    if (entry.name == wanted)
      out.push_back({va, entry.seq});
  }
  return out;
}

bool NativeTextureReplace(u32 guest_va, const u8 *blob, size_t size) {
  if (!blob || size < 2048u)
    return false;
  const auto *d3dtex = bd::mem::at<const D3DTexture>(guest_va);
  if (!d3dtex)
    return false;

  u32 fc[6];
  for (int i = 0; i < 6; ++i)
    fc[i] = u32(d3dtex->Format.dword[i]);
  xe::TextureFetch fetch;
  std::memcpy(&fetch, fc, sizeof(fetch));

  u32 blobFc[6];
  for (int i = 0; i < 6; ++i) {
    const u8 *w = blob + 4u + 28u + u32(i) * 4u;
    blobFc[i] = (u32(w[0]) << 24) | (u32(w[1]) << 16) | (u32(w[2]) << 8) |
                u32(w[3]);
  }
  xe::TextureFetch blobFetch;
  std::memcpy(&blobFetch, blobFc, sizeof(blobFetch));

  // Same geometry or nothing: the physical allocation was sized for the live
  // texture, and the mirror build below trusts the live fetch constant.
  if (blobFetch.format != fetch.format || blobFc[2] != fc[2] ||
      blobFetch.pitch != fetch.pitch || !fetch.tiled ||
      fetch.dimension != xe::DataDimension::k2DOrStacked || fetch.mip_address) {
    REXLOG_WARN(bd, "native texture replace refused va=0x{:08X}: geometry mismatch",
            guest_va);
    return false;
  }

  const MirrorLayout L = MakeLayout(fetch);
  const size_t payload =
      size_t(L.aligned_w) * size_t(L.aligned_h) * size_t(L.bytes_per_block);
  const u32 blobPayload = (u32(blob[0]) << 24) | (u32(blob[1]) << 16) |
                          (u32(blob[2]) << 8) | u32(blob[3]);
  if (blobPayload != payload || size < 2048u + payload) {
    REXLOG_WARN(bd, "native texture replace refused va=0x{:08X}: payload {} vs {}",
            guest_va, blobPayload, payload);
    return false;
  }

  auto *dst = bd::mem::at<u8>(fetch.base_address << 12);
  if (!dst)
    return false;
  std::memcpy(dst, blob + 2048, payload);
  return GetOrCreateNativeMirror(guest_va) != nullptr;
}

void DrainEvictedNativeTextures(u32 slot) {
  std::vector<std::unique_ptr<GuestTexture>> dead;
  {
    std::lock_guard<std::mutex> lock(g_mirror_mutex);
    dead.swap(g_pending_native_destroy[slot]);
  }
  // Lock released before NotifyTextureDestroyed, which takes Video's mutex.
  for (auto &tex : dead) {
    Video::NotifyTextureDestroyed(tex.get());
    // Only fence(slot) was awaited, and the other in-flight slot may still
    // reference the image/view, so park them instead of freeing here.
    ParkTextureGPUObjects(tex.get());
  }
}

} // namespace bd::gpu

// Midasm hook at global scope, externed by the generated recompiler dispatch,
// so name and signature must match the hook config. Fires in
// hcgTextureListRelease's refcount-zero block when the name-refcounted
// preload table releases a native texture's last reference. hcgTextureCacheAdd
// registered flt_82DBB148[k] with bdAllocRenderBuffer's return (== the
// g_native_mirrors key) and unk_82DBB170[k] with the XPhysical allocation. That
// free path calls XPhysicalFree directly, bypassing LoadTexture__vf03, so the
// host mirror must be evicted here.
//   r25 = flt_82DBB148[k] = mirror key   r26 = unk_82DBB170[k] = XPhysical
//   marker
REX_MIDASM_HOOK(bdPreloadTableFreeTextureHook, (PPCRegister &r25, PPCRegister &r26)) {
  if (r26.u32)
    bd::gpu::EvictNativeTexture(r25.u32);
}
REX_MIDASM_AT(bdPreloadTableFreeTextureHook, 0x8228522C, before, r25, r26);
