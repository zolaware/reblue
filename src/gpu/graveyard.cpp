/**
 * @file    gpu/graveyard.cpp
 * @brief   Parking a dying guest resource's GPU objects in the per-slot
 *          graveyards, and freeing them once the fence they were parked
 *          behind has been awaited.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/device.h"

#include <atomic>
#include <memory>
#include <mutex>

#include <plume_render_interface.h>
#include <rex/runtime.h>

#include "core/profiling.h"

#include "core/logging.h"
#include "gpu/frame.h"
#include "gpu/host_resource_heap.h"

namespace bd::gpu {

namespace {

void DetachMirrorsLocked(VideoState &s, GuestTexture *dead) {
  if (s.render_target == dead)
    s.render_target = nullptr;
  if (s.depth_stencil == dead)
    s.depth_stencil = nullptr;
  if (s.last_drawn_rt == dead)
    s.last_drawn_rt = nullptr;
  if (s.fullscreen_chain_head == dead)
    s.fullscreen_chain_head = nullptr;
  if (s.scene_depth == dead)
    s.scene_depth = nullptr;
  if (s.last_resolved_dst == dead)
    s.last_resolved_dst = nullptr;
  for (auto &slot : s.textures) {
    if (slot == dead)
      slot = nullptr;
  }
  if (dead->mappedMemory) {
    REX_KERNEL_MEMORY()->SystemHeapFree(dead->mappedMemory);
    dead->mappedMemory = 0;
  }
  s.draw_framebuffer_bound = false;
}

bool DrainResolveLinksLocked(VideoState &s, GuestTexture *tex,
                             bool aliasable_only) {
  const bool recorded = MaterializeOutboundLocked(s, tex, aliasable_only);
  DetachSourceSurfaceLocked(s, tex);
  for (GuestTexture *dst : tex->destinationTextures) {
    if (dst && dst->sourceSurface == tex) {
      if (!dst->pendingDestroy) {
        static std::atomic<u32> s_lost{0};
        const u32 n = s_lost.fetch_add(1, std::memory_order_relaxed);
        if (n < 8) {
          BD_WARN("#{} unmaterialized resolve link into {}x{} fmt={} dropped "
                  "(content lost)",
                  n, dst->width, dst->height, u32(dst->format));
        }
      }
      dst->sourceSurface = nullptr;
    }
  }
  tex->destinationTextures.clear();
  return recorded;
}

} // namespace

void ParkTextureGPUObjects(GuestTexture *tex) {
  if (!tex)
    return;
  if (tex->textureHolder) {
    Video::ParkTextureUntilFence(std::move(tex->textureHolder));
  }
  tex->texture = nullptr;
  if (tex->textureView) {
    Video::ParkTextureUntilFence(std::move(tex->textureView));
  }
  if (tex->companion2D)
    ParkTextureGPUObjects(tex->companion2D.get());
  if (tex->companionCube)
    ParkTextureGPUObjects(tex->companionCube.get());
}

void DestroyResourceNow(u32 guest_va, ResourceType type) {
  auto *memory = REX_KERNEL_MEMORY();
  void *host = memory->TranslateVirtual<void *>(guest_va);
  switch (type) {
  case ResourceType::Texture:
  case ResourceType::VolumeTexture:
  case ResourceType::RenderTarget:
  case ResourceType::DepthStencil: {
    auto *tex = static_cast<GuestTexture *>(host);
    Video::NotifyTextureDestroyed(tex);
    ParkTextureGPUObjects(tex);
    HostResourceHeap::Free(tex);
    break;
  }
  case ResourceType::VertexBuffer:
  case ResourceType::IndexBuffer: {
    auto *buf = static_cast<GuestBuffer *>(host);
    if (buf->buffer) {
      Video::ScrubBufferBindings(buf->buffer.get());
      Video::ParkBufferUntilFence(std::move(buf->buffer));
    }
    if (buf->ownsMirror && buf->guestMirrorVa) {
      memory->SystemHeapFree(buf->guestMirrorVa);
    }
    HostResourceHeap::Free(buf);
    break;
  }
  case ResourceType::VertexDeclaration:
    HostResourceHeap::Free(static_cast<GuestVertexDeclaration *>(host));
    break;
  case ResourceType::VertexShader:
  case ResourceType::PixelShader:
    HostResourceHeap::Free(static_cast<GuestShader *>(host));
    break;
  }
}

void Video::ParkTextureUntilFence(std::unique_ptr<plume::RenderTexture> tex) {
  if (!tex)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  s.texture_graveyard[Video::RetireSlot("texture")].push_back(std::move(tex));
}

void Video::ParkTextureUntilFence(
    std::unique_ptr<plume::RenderTextureView> view) {
  if (!view)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  s.texture_view_graveyard[Video::RetireSlot("texture view")].push_back(
      std::move(view));
}

void Video::ParkBufferUntilFence(std::unique_ptr<plume::RenderBuffer> buffer) {
  if (!buffer)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  s.buffer_graveyard[Video::RetireSlot("buffer")].push_back(std::move(buffer));
}

void RetireTextureBindingsLocked(VideoState &s, GuestTexture *dead) {
  if (dead->framebufferAttached) {
    BD_CPU_ZONE("FbCacheInvalidate");
    const plume::RenderTexture *deadTex = dead->texture;
    for (auto it = s.framebuffer_owners.begin();
         it != s.framebuffer_owners.end();) {
      GuestTexture *owner = *it;
      if (owner == dead) {
        owner->framebuffers.clear();
        it = s.framebuffer_owners.erase(it);
      } else {
        if (deadTex)
          owner->framebuffers.erase(deadTex);
        if (owner->framebuffers.empty())
          it = s.framebuffer_owners.erase(it);
        else
          ++it;
      }
    }
  }
  ReleaseTextureSRVLocked(s, dead);
  if (dead->companion2D) {
    ReleaseTextureSRVLocked(s, dead->companion2D.get());
  }
  if (dead->companionCube) {
    ReleaseTextureSRVLocked(s, dead->companionCube.get());
  }
}

void Video::RetireTextureBindings(GuestTexture *tex) {
  if (!tex)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  RetireTextureBindingsLocked(s, tex);
}

void Video::NotifyTextureDestroyed(GuestTexture *dead) {
  if (!dead)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  RetireTextureBindingsLocked(s, dead);
  dead->pendingGPURead =
      DrainResolveLinksLocked(s, dead, /*aliasable_only=*/true);
  DetachMirrorsLocked(s, dead);
}

void Video::ReissueSurface(GuestTexture *surface) {
  if (!surface)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  DrainResolveLinksLocked(s, surface, /*aliasable_only=*/false);
}

} // namespace bd::gpu
