/**
 * @file    gpu/sampler_cache.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/sampler_cache.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <rex/logging/log_category.h>
#include <rex/xenos/fetch.h>

#include "core/logging.h"
#include "gpu/bindless_allocator.h"
#include "gpu/device.h"
#include "core/cvar_table.h"
#include "gpu/settings.h"

namespace bd::gpu {

namespace {

namespace xe = rex::xenos;

// Only these fields ever vary, so they form the key, not the full desc.
// anisoEnabled is separate: at bd_anisotropy 1 both states carry maxAniso 1.
struct DescKey {
  plume::RenderTextureAddressMode u;
  plume::RenderTextureAddressMode v;
  plume::RenderTextureAddressMode w;
  plume::RenderFilter min;
  plume::RenderFilter mag;
  plume::RenderMipmapMode mip;
  plume::RenderBorderColor border;
  u32 maxAniso;
  bool anisoEnabled;

  bool operator==(const DescKey &other) const noexcept {
    return u == other.u && v == other.v && w == other.w && min == other.min &&
           mag == other.mag && mip == other.mip && border == other.border &&
           maxAniso == other.maxAniso && anisoEnabled == other.anisoEnabled;
  }
};

struct DescKeyHash {
  size_t operator()(const DescKey &k) const noexcept {
    u64 bits = 0;
    bits |= static_cast<u64>(k.u);
    bits |= static_cast<u64>(k.v) << 4;
    bits |= static_cast<u64>(k.w) << 8;
    bits |= static_cast<u64>(k.min) << 12;
    bits |= static_cast<u64>(k.mag) << 16;
    bits |= static_cast<u64>(k.mip) << 20;
    bits |= static_cast<u64>(k.border) << 24;
    bits |= static_cast<u64>(k.maxAniso) << 28;
    bits |= static_cast<u64>(k.anisoEnabled) << 36;
    return std::hash<u64>{}(bits);
  }
};

struct CachedSampler {
  std::unique_ptr<plume::RenderSampler> sampler;
  u32 slot = 0;
};

struct Cache {
  std::unordered_map<DescKey, CachedSampler, DescKeyHash> map;
  std::mutex mutex;
};

Cache &cache() {
  static Cache c;
  return c;
}

// plume has no half-way clamp, so those take the plain clamp of the same
// mirroring.
plume::RenderTextureAddressMode ConvertClamp(xe::ClampMode mode) {
  using A = plume::RenderTextureAddressMode;
  switch (mode) {
  case xe::ClampMode::kRepeat:
    return A::WRAP;
  case xe::ClampMode::kMirroredRepeat:
    return A::MIRROR;
  case xe::ClampMode::kClampToEdge:
  case xe::ClampMode::kClampToHalfway:
    return A::CLAMP;
  case xe::ClampMode::kMirrorClampToEdge:
  case xe::ClampMode::kMirrorClampToHalfway:
    return A::MIRROR_ONCE;
  case xe::ClampMode::kClampToBorder:
    return A::BORDER;
  case xe::ClampMode::kMirrorClampToBorder:
    return A::MIRROR_ONCE;
  }
  return A::CLAMP;
}

// Only kPoint is nearest: kLinear, kBaseMap and kUseFetchConst all sample
// linearly.
bool IsNearest(xe::TextureFilter filter) {
  return filter == xe::TextureFilter::kPoint;
}

} // namespace

plume::RenderSamplerDesc DecodeFromFetch(const u32 fc[6]) {
  xe::TextureFetch fetch;
  std::memcpy(&fetch, fc, sizeof(fetch));

  plume::RenderSamplerDesc d;
  d.addressU = ConvertClamp(fetch.clamp_x);
  d.addressV = ConvertClamp(fetch.clamp_y);
  d.addressW = ConvertClamp(fetch.clamp_z);

  d.magFilter = IsNearest(fetch.mag_filter) ? plume::RenderFilter::NEAREST
                                            : plume::RenderFilter::LINEAR;
  d.minFilter = IsNearest(fetch.min_filter) ? plume::RenderFilter::NEAREST
                                            : plume::RenderFilter::LINEAR;
  d.mipmapMode = IsNearest(fetch.mip_filter) ? plume::RenderMipmapMode::NEAREST
                                             : plume::RenderMipmapMode::LINEAR;

  // Anisotropy is off by default, as BD's fetch constant sets aniso_filter to
  // 0. bd_anisotropy opts in, and only when every filter is already LINEAR:
  // D3D12_FILTER_ANISOTROPIC hardcodes LINEAR min/mag, so enabling aniso on a
  // POINT sampler silently upgrades it to bilinear on D3D12 but not on Vulkan.
  const i32 aniso = bd::cvars::Get().anisotropy.Get();
  const bool aniso_on = aniso > 0 &&
                        d.mipmapMode == plume::RenderMipmapMode::LINEAR &&
                        d.minFilter == plume::RenderFilter::LINEAR &&
                        d.magFilter == plume::RenderFilter::LINEAR;
  d.anisotropyEnabled = aniso_on;
  d.maxAnisotropy = aniso_on ? static_cast<u32>(aniso) : 1u;

  d.borderColor = fetch.border_color == xe::BorderColor::k_ABGR_White
                      ? plume::RenderBorderColor::OPAQUE_WHITE
                      : plume::RenderBorderColor::TRANSPARENT_BLACK;
  return d;
}

u32 ResolveSlotLocked(const plume::RenderSamplerDesc &desc) {
  auto &s = state();
  if (!s.ready || !s.device || !s.sampler_descriptor_set)
    return 0;

  const DescKey key{
      desc.addressU,    desc.addressV,      desc.addressW,
      desc.minFilter,   desc.magFilter,     desc.mipmapMode,
      desc.borderColor, desc.maxAnisotropy, desc.anisotropyEnabled,
  };

  auto &c = cache();
  {
    std::lock_guard lock(c.mutex);
    auto it = c.map.find(key);
    if (it != c.map.end())
      return it->second.slot;
  }

  // Heap slot alloc relies on the state().mutex the caller already holds.
  const u32 slot = BindlessAllocateSlot(s.sampler_descriptor_used, 1, 0);
  if (slot == 0) {
    static std::atomic<u32> warned{0};
    if (warned.fetch_add(1, std::memory_order_relaxed) < 4) {
      REXBD_WARN(
          "sampler-cache bindless sampler heap full, falling back to slot 0");
    }
    return 0;
  }

  auto sampler = s.device->createSampler(desc);
  s.sampler_descriptor_set->setSampler(slot, sampler.get());

  std::lock_guard lock(c.mutex);
  // On a race, return the winner's slot. Ours leaks (no reclaim on drop here).
  auto it = c.map.find(key);
  if (it != c.map.end())
    return it->second.slot;
  c.map.emplace(key, CachedSampler{std::move(sampler), slot});
  return slot;
}

} // namespace bd::gpu
