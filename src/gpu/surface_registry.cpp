/**
 * @file    gpu/surface_registry.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/surface_registry.h"

#include <mutex>
#include <rex/types.h>
#include <unordered_map>
#include <vector>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "core/profiling.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/resources.h"

namespace bd::gpu {

namespace {

constexpr u64 kIdleFramesBeforeRetire = 900;

u64 MakeKey(u32 width, u32 height, u32 plume_format, u32 sample_count,
            bool is_depth) {
  return (u64(width & 0xFFFF)) | (u64(height & 0xFFFF) << 16) |
         (u64(plume_format & 0xFF) << 32) | (u64(sample_count & 0xF) << 40) |
         (u64(is_depth ? 1u : 0u) << 44);
}

struct Entry {
  GuestTexture *surface = nullptr;
  u64 last_frame = 0;
};

struct Registry {
  std::mutex mutex;
  std::unordered_map<u64, std::vector<Entry>> surfaces;
  u64 frame = 0;
};

Registry &registry() {
  static Registry r;
  return r;
}

bool Held(const GuestTexture *surface) {
  return u32(surface->x360.as_surface.resource.ReferenceCount) != 0;
}

GuestTexture *CreateSurface(u32 width, u32 height, u32 guest_format,
                            plume::RenderFormat plume_format,
                            u32 sample_count) {
  const bool is_depth = IsDepthFormat(plume_format);
  auto *surface = HostResourceHeap::Alloc<GuestTexture>(
      is_depth ? ResourceType::DepthStencil : ResourceType::RenderTarget);
  if (!surface) {
    BD_ERROR("CreateSurface: host resource heap exhausted");
    return nullptr;
  }
  InitResourceHeader(surface->x360.as_surface.resource,
                     D3DResourceType::kSurface);

  plume::RenderTextureDesc desc;
  desc.dimension = plume::RenderTextureDimension::TEXTURE_2D;
  desc.width = width;
  desc.height = height;
  desc.depth = 1;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = plume_format;
  desc.flags = is_depth ? plume::RenderTextureFlag::DEPTH_TARGET
                        : plume::RenderTextureFlag::RENDER_TARGET;
  desc.multisampling.sampleCount =
      static_cast<plume::RenderSampleCounts>(sample_count);
  desc.committed = false;

  auto *device = Video::HostDevice();
  if (device) {
    surface->textureHolder = CreateHostTexture(device, desc, "rt-surface");
    surface->texture = surface->textureHolder.get();
    if (surface->texture && !is_depth) {
      plume::RenderTextureViewDesc view_desc;
      view_desc.format = plume_format;
      view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_2D;
      view_desc.mipLevels = 1;
      surface->textureView = surface->texture->createTextureView(view_desc);
      Video::BindTextureSRV(surface);
    }
  } else {
    BD_ERROR("CreateSurface fired before Video host device exists");
  }
  surface->width = width;
  surface->height = height;
  surface->format = plume_format;
  surface->guestFormat = guest_format;
  surface->viewDimension = plume::RenderTextureViewDimension::TEXTURE_2D;
  surface->sampleCount = desc.multisampling.sampleCount;
  surface->registered = true;
  return surface;
}

void ResetIssuedSurface(GuestTexture *surface, u32 guest_format,
                        bool is_depth) {
  InitResourceHeader(surface->x360.as_surface.resource,
                     D3DResourceType::kSurface);
  Video::ReissueSurface(surface);
  surface->resolveScale = 1.0f;
  surface->resolveLevel = 0;
  surface->resolveFace = 0;
  surface->pendingDestroy = false;
  surface->pendingGPURead = false;
  surface->reflection = false;
  surface->layout = plume::RenderTextureLayout::UNKNOWN;
  surface->guestFormat = guest_format;
  if (surface->texture && !is_depth)
    Video::BindTextureSRV(surface);
}

} // namespace

GuestTexture *SurfaceRegistry::Get(u32 width, u32 height, u32 guest_format,
                                   u32 sample_count) {
  const plume::RenderFormat plume_format = ConvertGuestFormat(guest_format);
  const bool is_depth = IsDepthFormat(plume_format);
  const u64 key = MakeKey(width, height, static_cast<u32>(plume_format),
                          sample_count, is_depth);
  Registry &r = registry();
  GuestTexture *surface = nullptr;
  {
    std::lock_guard<std::mutex> lock(r.mutex);
    std::vector<Entry> &entries = r.surfaces[key];
    for (Entry &entry : entries) {
      if (Held(entry.surface))
        continue;
      entry.last_frame = r.frame;
      surface = entry.surface;
      break;
    }
    if (!surface) {
      surface = CreateSurface(width, height, guest_format, plume_format,
                              sample_count);
      if (!surface) {
        if (entries.empty())
          r.surfaces.erase(key);
        return nullptr;
      }
      entries.push_back({surface, r.frame});
      return surface;
    }
  }
  ResetIssuedSurface(surface, guest_format, is_depth);
  return surface;
}

void SurfaceRegistry::Tick() {
  Registry &r = registry();
  std::vector<GuestTexture *> retired;
  {
    std::lock_guard<std::mutex> lock(r.mutex);
    const u64 frame = ++r.frame;
    for (auto it = r.surfaces.begin(); it != r.surfaces.end();) {
      std::vector<Entry> &entries = it->second;
      for (auto e = entries.begin(); e != entries.end();) {
        if (Held(e->surface) ||
            frame - e->last_frame <= kIdleFramesBeforeRetire) {
          ++e;
          continue;
        }
        retired.push_back(e->surface);
        e = entries.erase(e);
      }
      if (entries.empty())
        it = r.surfaces.erase(it);
      else
        ++it;
    }
  }
  if (retired.empty())
    return;
  BD_CPU_ZONE("SurfaceRegistryRetire");
  for (GuestTexture *surface : retired) {
    surface->registered = false;
    Video::NotifyTextureDestroyed(surface);
    ParkTextureGPUObjects(surface);
    HostResourceHeap::Free(surface);
  }
}

} // namespace bd::gpu
