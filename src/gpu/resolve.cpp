/**
 * @file    gpu/resolve.cpp
 * @brief   EDRAM resolve emulation: surface-to-texture copy, the lazy resolve
 *          alias, and the fullscreen composite chain seeding.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/frame.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#include <plume_render_interface.h>
#include <rex/platform/perf.h>


#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/format.h"

namespace bd::gpu {

namespace {

inline const plume::RenderTexture *const kDepthFbKey =
    reinterpret_cast<const plume::RenderTexture *>(uintptr_t{1});

GuestTexture *ResolveSourceForFlagsLocked(VideoState &s, u32 flags) {
  if (flags & kResolveDepthStencil)
    return s.depth_stencil;
  if (s.render_target)
    return s.render_target;
  return s.back_buffer_surface;
}

bool CanAliasResolveLocked(const GuestTexture *src, const GuestTexture *dst) {
  return src && dst && src != dst && src->texture && dst->texture &&
         src->sampleCount == plume::RenderSampleCount::COUNT_1 &&
         src->hostWidth == dst->hostWidth && src->hostHeight == dst->hostHeight &&
         src->format == dst->format && dst->resolveExpScale == 1.0f &&
         dst->viewDimension !=
             plume::RenderTextureViewDimension::TEXTURE_CUBE &&
         dst->mipLevels <= 1 && src->descriptorIndex != kInvalidDescriptorIndex;
}

} // namespace

void DetachSourceSurfaceLocked(VideoState &s, GuestTexture *texture) {
  if (!texture || !texture->sourceSurface)
    return;
  GuestTexture *source = texture->sourceSurface;
  source->destinationTextures.erase(texture);
  texture->sourceSurface = nullptr;
}

bool CopySurfaceToTextureLocked(VideoState &s, GuestTexture *src,
                                GuestTexture *dst, const char *reason) {
  if (!dst || !dst->texture || !src || !src->texture || src == dst) {
    return false;
  }
  if (!s.ready)
    return false;
  if (dst->sampleCount != plume::RenderSampleCount::COUNT_1) {
    static std::atomic<u32> s_warn{0};
    if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
      REXBD_WARN("{}: MSAA destination {}x{} rejected (resolve targets must be "
              "single-sample)",
              reason, dst->width, dst->height);
    }
    return false;
  }

  BeginCommandList(s);
  if (!s.command_list_open)
    return false;

  s.draw_framebuffer_bound = false;

  // A source BD never drew reads uninitialized memory, and a cube face copy
  // covers one subresource of the destination.
  InitializeTargetLocked(s, src);
  InitializeTargetLocked(s, dst);

  const bool dims_match =
      src->hostWidth == dst->hostWidth && src->hostHeight == dst->hostHeight;
  const bool format_match = src->format == dst->format;
  const float resolve_scale = dst->resolveExpScale;
  const bool needs_scale = (resolve_scale != 1.0f);
  const bool dst_is_cube =
      dst->viewDimension == plume::RenderTextureViewDimension::TEXTURE_CUBE;

  if (dims_match && format_match && !needs_scale &&
      src->sampleCount == plume::RenderSampleCount::COUNT_1) {
    plume::RenderTextureBarrier pre[2] = {
        plume::RenderTextureBarrier(src->texture,
                                    plume::RenderTextureLayout::COPY_SOURCE),
        plume::RenderTextureBarrier(dst->texture,
                                    plume::RenderTextureLayout::COPY_DEST),
    };
    s.command_list->barriers(plume::RenderBarrierStage::COPY, pre,
                             std::size(pre));
    src->layout = plume::RenderTextureLayout::COPY_SOURCE;
    dst->layout = plume::RenderTextureLayout::COPY_DEST;
    if (dst_is_cube) {
      s.command_list->copyTextureRegion(
          plume::RenderTextureCopyLocation::Subresource(
              dst->texture, dst->resolveLevel, dst->resolveFace),
          plume::RenderTextureCopyLocation::Subresource(src->texture, 0, 0));
    } else {
      s.command_list->copyTexture(dst->texture, src->texture);
    }
  } else if (dst_is_cube) {
    static std::atomic<u32> s_warn{0};
    if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
      REXBD_WARN("{}: cube resolve dst needs convert/scale (src={}x{} fmt={} "
              "dst fmt={} scale={}), no per-face RTV, skipping",
              reason, src->width, src->height, static_cast<int>(src->format),
              static_cast<int>(dst->format), resolve_scale);
    }
    return false;
  } else {
    if (src->descriptorIndex == kInvalidDescriptorIndex) {
      BindTextureSRVLocked(s, src);
    }
    if (src->descriptorIndex == kInvalidDescriptorIndex) {
      static std::atomic<u32> s_warn{0};
      if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
        REXBD_ERROR("{}: source RT 0x{:x} not in bindless heap, can't "
                 "shader-resolve",
                 reason, reinterpret_cast<uintptr_t>(src));
      }
      return false;
    }

    const bool depth_dst = IsDepthFormat(dst->format);
    if (!depth_dst && !IsRenderTargetCapable(dst->format)) {
      static std::atomic<u32> s_warn{0};
      if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
        REXBD_ERROR("{}: dst format {} is not RT-capable, skipping shader "
                 "resolve (src={}x{} fmt={} dst={}x{} fmt={})",
                 reason, static_cast<int>(dst->format), src->width, src->height,
                 static_cast<int>(src->format), dst->width, dst->height,
                 static_cast<int>(dst->format));
      }
      return false;
    }

    const plume::RenderTexture *fb_key = depth_dst ? kDepthFbKey : nullptr;

    auto it = dst->framebuffers.find(fb_key);
    plume::RenderFramebuffer *dst_fb = nullptr;
    if (it != dst->framebuffers.end()) {
      dst_fb = it->second.get();
    } else {
      std::unique_ptr<plume::RenderFramebuffer> fb;
      if (depth_dst) {
        plume::RenderFramebufferDesc desc;
        desc.depthAttachment = dst->texture;
        desc.colorAttachmentsCount = 0;
        fb = s.device->createFramebuffer(desc);
      } else {
        const plume::RenderTexture *attachments[1] = {dst->texture};
        plume::RenderFramebufferDesc desc(attachments, 1);
        fb = s.device->createFramebuffer(desc);
      }
      if (!fb) {
        static std::atomic<u32> s_warn{0};
        if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
          REXBD_ERROR("{}: createFramebuffer failed (depth_dst={}), skipping "
                   "shader resolve (src={}x{} dst={}x{})",
                   reason, depth_dst ? 1 : 0, src->width, src->height,
                   dst->width, dst->height);
        }
        return false;
      }
      dst_fb = fb.get();
      dst->framebufferAttached = true;
      dst->framebuffers.emplace(fb_key, std::move(fb));
      s.framebuffer_owners.insert(dst);
    }

    const plume::RenderTextureLayout dst_write_layout =
        depth_dst ? plume::RenderTextureLayout::DEPTH_WRITE
                  : plume::RenderTextureLayout::COLOR_WRITE;
    const bool discard_dst =
        (dst->layout == plume::RenderTextureLayout::UNKNOWN);
    plume::RenderTextureBarrier pre[2] = {
        plume::RenderTextureBarrier(src->texture,
                                    plume::RenderTextureLayout::SHADER_READ),
        plume::RenderTextureBarrier(dst->texture, dst_write_layout),
    };
    s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, pre,
                             std::size(pre));
    src->layout = plume::RenderTextureLayout::SHADER_READ;
    dst->layout = dst_write_layout;
    if (discard_dst)
      s.command_list->discardTexture(dst->texture);

    s.command_list->setFramebuffer(dst_fb);
    s.command_list->setViewports(
        plume::RenderViewport(0.0f, 0.0f, static_cast<float>(dst->hostWidth),
                              static_cast<float>(dst->hostHeight)));
    s.command_list->setScissors(plume::RenderRect(
        0, 0, static_cast<i32>(dst->hostWidth),
        static_cast<i32>(dst->hostHeight)));
    plume::RenderPipeline *pipeline = nullptr;
    if (src->sampleCount != plume::RenderSampleCount::COUNT_1) {
      pipeline = GetOrCreateResolveMSAAPipeline(s, dst->format,
                                                src->sampleCount, depth_dst);
    } else if (depth_dst) {
      pipeline = GetOrCreateCopyDepthPipeline(s, dst->format);
    } else {
      pipeline = GetOrCreateResolvePipeline(s, dst->format);
    }
    if (!pipeline) {
      static std::atomic<u32> s_warn{0};
      if (s_warn.fetch_add(1, std::memory_order_relaxed) < 8) {
        REXBD_ERROR("{}: createGraphicsPipeline failed for dst format {}", reason,
                 static_cast<int>(dst->format));
      }
      return false;
    }
    s.command_list->setPipeline(pipeline);
    const u32 descriptor_index = src->descriptorIndex;

    u32 box_ratio = 0u;
    if (!depth_dst && src->sampleCount == plume::RenderSampleCount::COUNT_1 &&
        dst->hostWidth != 0u && dst->hostHeight != 0u &&
        src->hostWidth > dst->hostWidth && src->hostHeight > dst->hostHeight &&
        (src->hostWidth % dst->hostWidth) == 0u &&
        (src->hostHeight % dst->hostHeight) == 0u &&
        (src->hostWidth / dst->hostWidth) == (src->hostHeight / dst->hostHeight)) {
      box_ratio = src->hostWidth / dst->hostWidth;
    }

    struct CopyPushConstants {
      u32 resourceDescriptorIndex;
      u32 resourceDescriptorIndex2;
      float param0;
      float param1;
    } push{descriptor_index, 0u, depth_dst ? 1.0f : resolve_scale,
           static_cast<float>(box_ratio)};
    s.command_list->setGraphicsPushConstants(kCopyPushConstantRangeIndex, &push,
                                             kCopyPushConstantByteOffset,
                                             sizeof(push));
    s.command_list->drawInstanced(3, 1, 0, 0);

    s.command_list->setFramebuffer(nullptr);
    s.current_pso = nullptr;
    s.dirtyStates.pipelineState = true;
    s.draw_framebuffer_bound = false;
  }

  plume::RenderTextureBarrier post(dst->texture,
                                   plume::RenderTextureLayout::SHADER_READ);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &post, 1);
  dst->layout = plume::RenderTextureLayout::SHADER_READ;
  return true;
}

namespace {

bool ClearColorTargetLocked(VideoState &s, GuestTexture *dst,
                            plume::RenderColor color) {
  if (!dst || !dst->texture || !s.ready)
    return false;
  if (IsDepthFormat(dst->format) ||
      dst->sampleCount != plume::RenderSampleCount::COUNT_1)
    return false;
  BeginCommandList(s);
  if (!s.command_list_open)
    return false;
  s.draw_framebuffer_bound = false;

  plume::RenderFramebuffer *dst_fb = GetFramebuffer(s, dst, nullptr);
  if (!dst_fb)
    return false;

  plume::RenderTextureBarrier pre(dst->texture,
                                  plume::RenderTextureLayout::COLOR_WRITE);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &pre, 1);
  dst->layout = plume::RenderTextureLayout::COLOR_WRITE;
  s.command_list->setFramebuffer(dst_fb);
  s.command_list->setViewports(
      plume::RenderViewport(0.0f, 0.0f, static_cast<float>(dst->hostWidth),
                            static_cast<float>(dst->hostHeight)));
  s.command_list->setScissors(plume::RenderRect(
      0, 0, static_cast<i32>(dst->hostWidth),
      static_cast<i32>(dst->hostHeight)));
  s.command_list->clearColor(0, color);
  s.command_list->setFramebuffer(nullptr);
  s.draw_framebuffer_bound = false;

  plume::RenderTextureBarrier post(dst->texture,
                                   plume::RenderTextureLayout::SHADER_READ);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &post, 1);
  dst->layout = plume::RenderTextureLayout::SHADER_READ;
  return true;
}

} // namespace

void InitializeTargetLocked(VideoState &s, GuestTexture *t) {
  if (!t || !t->texture || t->layout != plume::RenderTextureLayout::UNKNOWN)
    return;
  const bool depth = IsDepthFormat(t->format);
  // Mirrors the flag choice at creation: color cubes are never render targets.
  if (!depth &&
      (!IsRenderTargetCapable(t->format) ||
       t->viewDimension == plume::RenderTextureViewDimension::TEXTURE_CUBE))
    return;
  BeginCommandList(s);
  if (!s.command_list_open)
    return;
  const plume::RenderTextureLayout write_layout =
      depth ? plume::RenderTextureLayout::DEPTH_WRITE
            : plume::RenderTextureLayout::COLOR_WRITE;
  plume::RenderTextureBarrier b(t->texture, write_layout);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &b, 1);
  s.command_list->discardTexture(t->texture);
  t->layout = write_layout;
  // The discard satisfies D3D12 but leaves heap garbage. A target read before
  // BD ever wrote it should read zero, as it did from a zeroed heap.
  if (!depth && t->sampleCount == plume::RenderSampleCount::COUNT_1 &&
      t->viewDimension == plume::RenderTextureViewDimension::TEXTURE_2D)
    ClearColorTargetLocked(s, t, ArgbToRenderColor(0));
}

void Video::ClearTexture(GuestTexture *texture, u32 color_argb) {
  if (!texture || !texture->texture)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  ClearColorTargetLocked(s, texture, ArgbToRenderColor(color_argb));
}

bool MaterializeOutboundLocked(VideoState &s, GuestTexture *source,
                               bool aliasable_only) {
  if (!source || source->destinationTextures.empty())
    return false;
  bool recorded = false;
  std::vector<GuestTexture *> destinations(source->destinationTextures.begin(),
                                           source->destinationTextures.end());
  for (GuestTexture *dst : destinations) {
    if (!dst || dst == source || dst->sourceSurface != source)
      continue;
    if (dst->pendingDestroy) {
      DetachSourceSurfaceLocked(s, dst);
      continue;
    }
    const bool one_to_one =
        source->hostWidth == dst->hostWidth &&
        source->hostHeight == dst->hostHeight &&
        source->format == dst->format &&
        source->sampleCount == plume::RenderSampleCount::COUNT_1 &&
        dst->resolveExpScale == 1.0f &&
        dst->viewDimension != plume::RenderTextureViewDimension::TEXTURE_CUBE;
    if (aliasable_only && !one_to_one)
      continue;
    if (CopySurfaceToTextureLocked(s, source, dst, "Materialize")) {
      recorded = true;
      DetachSourceSurfaceLocked(s, dst);
    }
  }
  return recorded;
}

void MaterializeInboundLocked(VideoState &s, GuestTexture *dst) {
  if (!dst || !dst->sourceSurface || dst->sourceSurface == dst)
    return;
  if (CopySurfaceToTextureLocked(s, dst->sourceSurface, dst, "Materialize")) {
    DetachSourceSurfaceLocked(s, dst);
  }
}

bool FullscreenChainClassLocked(const VideoState &s, const GuestTexture *t) {
  const GuestTexture *bb = s.back_buffer_surface;
  if (!t || !bb || t == bb || IsDepthFormat(t->format) || t->reflection)
    return false;
  constexpr u32 kDesignWidth = 1280u;
  const u32 min_full_width =
      bb->width > kDesignWidth ? kDesignWidth : bb->width;
  if (t->width < min_full_width)
    return false;
  const u64 a = u64(t->width) * bb->height;
  const u64 b = u64(t->height) * bb->width;
  const u64 diff = a > b ? a - b : b - a;
  return diff * 100ull <= b * 3ull;
}

void Video::TrackResolveSource(u32 flags, GuestTexture *dst, u32 level,
                               u32 face) {
  if (!dst || !dst->texture)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (!s.ready)
    return;
  dst->resolveLevel = level;
  dst->resolveFace = face;
  const int exp_bias = static_cast<i32>(flags) >> kResolveExponentBiasShift;
  dst->resolveExpScale = static_cast<float>(std::ldexp(1.0, exp_bias));
  GuestTexture *src = ResolveSourceForFlagsLocked(s, flags);
  if (!src || !src->texture || src == dst) {
    DetachSourceSurfaceLocked(s, dst);
    return;
  }
  if (dst->sourceSurface && dst->sourceSurface != src)
    dst->sourceSurface->destinationTextures.erase(dst);
  dst->sourceSurface = src;
  src->destinationTextures.insert(dst);
  BindTextureSRVLocked(s, src);
}

namespace {

void NoteTileContentLocked(VideoState &s, GuestTexture *dst) {
  if (!dst || !dst->texture || !FullscreenChainClassLocked(s, dst))
    return;
  s.fullscreen_chain_head = dst;
}

} // namespace

void Video::ResolveRtToTexture(GuestTexture *dst) {
  REX_PROFILE_SCOPE("ResolveRtToTexture");
  if (!dst || !dst->texture)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (!s.ready)
    return;
  GuestTexture *src = dst->sourceSurface;
  if (!src || src == dst || !src->texture)
    return;

  if (CanAliasResolveLocked(src, dst)) {
    s.last_resolved_dst = dst;
    NoteTileContentLocked(s, dst);
    s.draw_framebuffer_bound = false;
    return;
  }

  if (CopySurfaceToTextureLocked(s, src, dst, "Resolve")) {
    s.last_resolved_dst = dst;
    NoteTileContentLocked(s, dst);
    DetachSourceSurfaceLocked(s, dst);
  }
}

} // namespace bd::gpu
