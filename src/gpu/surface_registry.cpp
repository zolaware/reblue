/**
 * @file    gpu/surface_registry.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/surface_registry.h"

#include <functional>
#include <mutex>
#include <rex/dbg.h>
#include <rex/types.h>
#include <unordered_map>
#include <vector>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/output.h"
#include "gpu/resources.h"

namespace bd::gpu {

namespace {

constexpr u64 kIdleFramesBeforeRetire = 900;

struct PendingExemption {
  bool set = false;
  u32 width = 0;
  u32 height = 0;
};
thread_local PendingExemption t_exempt_next;

struct Key {
  u64 shape = 0;
  u64 host = 0;
  bool operator==(const Key &o) const {
    return shape == o.shape && host == o.host;
  }
};

struct KeyHash {
  size_t operator()(const Key &k) const {
    return std::hash<u64>()(k.shape) ^ (std::hash<u64>()(k.host) * 0x9E3779B1u);
  }
};

Key MakeKey(u32 width, u32 height, u32 plume_format, u32 sample_count,
            bool is_depth, bool exempt, u32 host_width, u32 host_height) {
  return {(u64(width & 0xFFFF)) | (u64(height & 0xFFFF) << 16) |
              (u64(plume_format & 0xFF) << 32) |
              (u64(sample_count & 0xF) << 40) |
              (u64(is_depth ? 1u : 0u) << 44) |
              (u64(exempt ? 1u : 0u) << 45),
          (u64(host_width) << 32) | host_height};
}

struct Entry {
  GuestTexture *surface = nullptr;
  u64 last_frame = 0;
};

struct Registry {
  std::mutex mutex;
  std::unordered_map<Key, std::vector<Entry>, KeyHash> surfaces;
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
                            plume::RenderFormat plume_format, u32 sample_count,
                            float scale_x, float scale_y, bool exempt) {
  const bool is_depth = IsDepthFormat(plume_format);
  auto *surface = HostResourceHeap::Alloc<GuestTexture>(
      is_depth ? ResourceType::DepthStencil : ResourceType::RenderTarget);
  if (!surface) {
    REXLOG_ERROR(bd, "CreateSurface: host resource heap exhausted");
    return nullptr;
  }
  InitResourceHeader(surface->x360.as_surface.resource,
                     D3DResourceType::kSurface);

  plume::RenderTextureDesc desc;
  desc.dimension = plume::RenderTextureDimension::TEXTURE_2D;
  surface->scaleX = scale_x;
  surface->scaleY = scale_y;
  surface->scaleExempt = exempt;
  surface->SetSize(width, height);
  desc.width = surface->hostWidth;
  desc.height = surface->hostHeight;
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
    REXLOG_ERROR(bd, "CreateSurface fired before Video host device exists");
  }
  surface->format = plume_format;
  surface->guestFormat = guest_format;
  surface->viewDimension = plume::RenderTextureViewDimension::TEXTURE_2D;
  surface->sampleCount = desc.multisampling.sampleCount;
  surface->registered = true;
  return surface;
}

// Same GuestTexture, new host resource at the new scale. Contents are dropped.
void RecreateSurface(GuestTexture *surface, float scale_x, float scale_y) {
  surface->scaleX = scale_x;
  surface->scaleY = scale_y;
  Video::RecreateTexture(surface);
  if (!surface->texture || IsDepthFormat(surface->format))
    return;
  plume::RenderTextureViewDesc view_desc;
  view_desc.format = surface->format;
  view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_2D;
  view_desc.mipLevels = 1;
  surface->textureView = surface->texture->createTextureView(view_desc);
  Video::BindTextureSRV(surface);
}

Key KeyOf(const GuestTexture *surface) {
  return MakeKey(surface->width, surface->height,
                 static_cast<u32>(surface->format),
                 static_cast<u32>(surface->sampleCount),
                 IsDepthFormat(surface->format), surface->scaleExempt,
                 surface->hostWidth, surface->hostHeight);
}

void ResetIssuedSurface(GuestTexture *surface, u32 guest_format,
                        bool is_depth) {
  InitResourceHeader(surface->x360.as_surface.resource,
                     D3DResourceType::kSurface);
  Video::ReissueSurface(surface);
  surface->resolveExpScale = 1.0f;
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
  float scale_x = 1.0f;
  float scale_y = 1.0f;
  const bool exempt = TakeExemption(width, height);
  if (!exempt)
    Output::HostScale(scale_x, scale_y);
  const Key key = MakeKey(width, height, static_cast<u32>(plume_format),
                          sample_count, is_depth, exempt,
                          GuestTexture::HostExtent(width, scale_x),
                          GuestTexture::HostExtent(height, scale_y));
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
                              sample_count, scale_x, scale_y, exempt);
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

void SurfaceRegistry::ExemptNext(u32 width, u32 height) {
  t_exempt_next = {true, width, height};
}

bool SurfaceRegistry::TakeExemption(u32 width, u32 height) {
  const PendingExemption pending = t_exempt_next;
  t_exempt_next = {};
  return pending.set && pending.width == width && pending.height == height;
}

u32 SurfaceRegistry::Rescale(float kx, float ky) {
  Registry &r = registry();
  std::vector<GuestTexture *> idle;
  u32 recreated = 0;
  {
    std::lock_guard<std::mutex> lock(r.mutex);
    std::vector<Entry> held;
    for (auto it = r.surfaces.begin(); it != r.surfaces.end();) {
      std::vector<Entry> &entries = it->second;
      for (auto e = entries.begin(); e != entries.end();) {
        GuestTexture *surface = e->surface;
        if (surface->scaleExempt ||
            (surface->scaleX == kx && surface->scaleY == ky)) {
          ++e;
          continue;
        }
        if (Held(surface))
          held.push_back(*e);
        else
          idle.push_back(surface);
        e = entries.erase(e);
      }
      if (entries.empty())
        it = r.surfaces.erase(it);
      else
        ++it;
    }
    for (const Entry &entry : held) {
      RecreateSurface(entry.surface, kx, ky);
      r.surfaces[KeyOf(entry.surface)].push_back(entry);
      ++recreated;
    }
  }
  for (GuestTexture *surface : idle) {
    surface->registered = false;
    Video::QueueResourceDestroy(HostResourceHeap::ToGuest(surface),
                                surface->type);
  }
  return recreated + static_cast<u32>(idle.size());
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
  REX_PROFILE_SCOPE("SurfaceRegistryRetire");
  for (GuestTexture *surface : retired) {
    surface->registered = false;
    Video::NotifyTextureDestroyed(surface);
    ParkTextureGPUObjects(surface);
    HostResourceHeap::Free(surface);
  }
}

} // namespace bd::gpu
