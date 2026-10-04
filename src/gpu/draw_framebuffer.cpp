/**
 * @file    gpu/draw_framebuffer.cpp
 * @brief   The per-draw framebuffer bind: the effective target pair, the
 *          layout barriers and the composite tile seed.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/frame.h"

#include <mutex>
#include <utility>

#include <plume_render_interface.h>

#include "gpu/gpu_profiling.h"

#include "core/logging.h"

namespace bd::gpu {

plume::RenderFramebuffer *GetFramebuffer(VideoState &s, GuestTexture *rt,
                                         GuestTexture *ds) {
  GuestTexture *container = ds ? ds : rt;
  if (!container)
    return nullptr;
  const plume::RenderTexture *key = rt ? rt->texture : nullptr;

  auto it = container->framebuffers.find(key);
  if (it != container->framebuffers.end()) {
    return it->second.get();
  }

  plume::RenderFramebufferDesc desc;
  const plume::RenderTexture *color_attachments[1];
  if (rt) {
    color_attachments[0] = rt->texture;
    desc.colorAttachments = color_attachments;
    desc.colorAttachmentsCount = 1;
  }
  if (ds)
    desc.depthAttachment = ds->texture;
  auto fb = s.device->createFramebuffer(desc);
  if (!fb) {
    BD_ERROR("createFramebuffer failed for (rt={} ds={})",
             static_cast<void *>(rt), static_cast<void *>(ds));
    return nullptr;
  }
  auto *raw = fb.get();
  if (rt)
    rt->framebufferAttached = true;
  if (ds)
    ds->framebufferAttached = true;
  container->framebuffers.emplace(key, std::move(fb));
  s.framebuffer_owners.insert(container);
  return raw;
}

void ResolveEffectiveTargets(VideoState &s, GuestTexture *&rt,
                             GuestTexture *&ds) {
  rt = s.render_target;
  ds = s.depth_stencil;
  if (rt && !rt->texture)
    rt = nullptr;
  if (ds && !ds->texture)
    ds = nullptr;
  if (!rt && !ds && s.back_buffer_surface && s.back_buffer_surface->texture) {
    rt = s.back_buffer_surface;
  }
}

namespace {

void TransitionResolveSources(VideoState &s, const GuestTexture *rt,
                              const GuestTexture *ds) {
  plume::RenderTextureBarrier sampled[16];
  u32 sampled_count = 0;
  for (GuestTexture *t : s.textures) {
    GuestTexture *src = t ? t->sourceSurface : nullptr;
    if (!src || src == t || !src->texture)
      continue;
    if (src == rt || src == ds)
      continue;
    if (src->layout == plume::RenderTextureLayout::SHADER_READ)
      continue;
    sampled[sampled_count++] = plume::RenderTextureBarrier(
        src->texture, plume::RenderTextureLayout::SHADER_READ);
    src->layout = plume::RenderTextureLayout::SHADER_READ;
  }
  if (!sampled_count)
    return;
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, sampled,
                           sampled_count);
}

void TransitionTargetsToWrite(VideoState &s, GuestTexture *rt, GuestTexture *ds,
                              bool &discard_rt, bool &discard_ds) {
  plume::RenderTextureBarrier barriers[2];
  u32 barrier_count = 0;
  discard_rt = false;
  discard_ds = false;
  if (rt && rt->layout != plume::RenderTextureLayout::COLOR_WRITE) {
    discard_rt = (rt->layout == plume::RenderTextureLayout::UNKNOWN);
    barriers[barrier_count++] = plume::RenderTextureBarrier(
        rt->texture, plume::RenderTextureLayout::COLOR_WRITE);
    rt->layout = plume::RenderTextureLayout::COLOR_WRITE;
  }
  if (ds && ds->layout != plume::RenderTextureLayout::DEPTH_WRITE) {
    discard_ds = (ds->layout == plume::RenderTextureLayout::UNKNOWN);
    barriers[barrier_count++] = plume::RenderTextureBarrier(
        ds->texture, plume::RenderTextureLayout::DEPTH_WRITE);
    ds->layout = plume::RenderTextureLayout::DEPTH_WRITE;
  }
  if (!barrier_count)
    return;
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, barriers,
                           barrier_count);
}

void SeedFreshColorTarget(VideoState &s, GuestTexture *rt, bool full_screen) {
  GuestTexture *seed_src = nullptr;
  const char *starved = nullptr;
  if (full_screen) {
    GuestTexture *head = s.fullscreen_chain_head;
    if (head == rt && rt->texture)
      return;
    if (!head) {
      starved = "head null";
    } else if (!head->texture) {
      starved = "head has no texture";
    } else if (!FullscreenChainClassLocked(s, head)) {
      starved = "head not fullscreen class";
    } else {
      seed_src = head;
    }
  }
  if (seed_src && seed_src->sourceSurface &&
      seed_src->sourceSurface != seed_src && seed_src->sourceSurface != rt &&
      seed_src->sourceSurface->texture) {
    seed_src = seed_src->sourceSurface;
  }
  if (seed_src &&
      CopySurfaceToTextureLocked(s, seed_src, rt, "CompositeChainSeed")) {
    if (rt->layout != plume::RenderTextureLayout::COLOR_WRITE) {
      plume::RenderTextureBarrier b(rt->texture,
                                    plume::RenderTextureLayout::COLOR_WRITE);
      s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &b, 1);
      rt->layout = plume::RenderTextureLayout::COLOR_WRITE;
    }
    return;
  }
  if (full_screen) {
    u32 n;
    if (DiagShouldLog(6, rt, &n)) {
      BD_DEV_WARN("[composite-seed] #{} fullscreen {}x{} composite DISCARDED "
                  "({})",
                  n, rt->width, rt->height,
                  starved ? starved : "seed copy failed");
    }
  }
  s.command_list->discardTexture(rt->texture);
}

} // namespace

bool Video::BindDrawFramebuffer() {
  std::lock_guard lock(state().mutex);
  return BindDrawFramebufferLocked();
}

bool Video::BindDrawFramebufferLocked(bool color_clear_follows) {
  auto &s = state();
  if (!s.ready || !s.command_list_open)
    return false;

  GuestTexture *rt = nullptr;
  GuestTexture *ds = nullptr;
  ResolveEffectiveTargets(s, rt, ds);

  for (GuestTexture *t : {rt, ds}) {
    if (!t)
      continue;
    MaterializeInboundLocked(s, t);
    MaterializeOutboundLocked(s, t);
  }

  TransitionResolveSources(s, rt, ds);

  if (s.draw_framebuffer_bound && rt == s.bound_fb_rt && ds == s.bound_fb_ds) {
    return true;
  }
  BD_GPU_ZONE("BindDrawFramebuffer");

  if (!rt && !ds) {
    u32 n;
    if (DiagShouldLog(5, s.render_target, &n)) {
      BD_DEV_WARN("[draw-diag] #{} draw dropped: no effective RT/DS "
                  "(s.render_target={} s.depth_stencil={})",
                  n, static_cast<void *>(s.render_target),
                  static_cast<void *>(s.depth_stencil));
    }
    return false;
  }

  bool discard_rt = false;
  bool discard_ds = false;
  TransitionTargetsToWrite(s, rt, ds, discard_rt, discard_ds);

  const bool full_screen = FullscreenChainClassLocked(s, rt);
  const bool msaa_rt =
      rt && rt->sampleCount != plume::RenderSampleCount::COUNT_1;
  if (discard_rt && (msaa_rt || color_clear_follows)) {
    s.command_list->discardTexture(rt->texture);
  } else if (discard_rt) {
    SeedFreshColorTarget(s, rt, full_screen);
  }
  if (full_screen)
    s.fullscreen_chain_head = rt;
  if (discard_ds)
    s.command_list->discardTexture(ds->texture);

  plume::RenderFramebuffer *fb = GetFramebuffer(s, rt, ds);
  if (!fb)
    return false;
  s.command_list->setFramebuffer(fb);
  s.dirtyStates.viewport = true;
  s.dirtyStates.scissorRect = true;
  Video::FlushViewport();
  if (discard_rt && msaa_rt && !color_clear_follows) {
    s.command_list->clearColor(0, ArgbToRenderColor(0));
  }
  s.draw_framebuffer_bound = true;
  s.bound_fb_rt = rt;
  s.bound_fb_ds = ds;
  if (rt)
    s.last_drawn_rt = rt;
  return true;
}

} // namespace bd::gpu
