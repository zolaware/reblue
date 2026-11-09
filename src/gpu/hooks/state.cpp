/**
 * @file    gpu/hooks/state.cpp
 * @brief   Hooks that set the draw state: targets, viewport, bindings and
 *          render state.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <cstring>

#include <rex/hook.h>
#include <rex/ppc/detour.h>
#include <rex/runtime.h>
#include <rex/types.h>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/native_texture_mirror.h"
#include "gpu/physical_buffers.h"
#include "gpu/shaders/shader_constants.h"

namespace {

using bd::gpu::ResolveGuestBufferVa;

void D3DDevice_SetViewport_hook(
    rex::MappedPtr<bd::gpu::D3DDevice> pDevice,
    rex::MappedPtr<bd::gpu::D3DViewport9> pViewport) {
  if (!pViewport) {
    return;
  }

  auto &s = bd::gpu::state();

  const float fx = static_cast<float>(u32(pViewport->X));
  const float fy = static_cast<float>(u32(pViewport->Y));
  const float fw = static_cast<float>(u32(pViewport->Width));
  const float fh = static_cast<float>(u32(pViewport->Height));
  const float fmin = float(pViewport->MinZ);
  const float fmax = float(pViewport->MaxZ);

  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport, s.viewport.x,
                                       fx);
  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport, s.viewport.y,
                                       fy);
  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport, s.viewport.width,
                                       fw);
  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport,
                                       s.viewport.height, fh);
  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport,
                                       s.viewport.minDepth, fmin);
  bd::gpu::Video::SetDirtyValue<float>(s.dirtyStates.viewport,
                                       s.viewport.maxDepth, fmax);

  s.dirtyStates.scissorRect =
      s.dirtyStates.scissorRect || s.dirtyStates.viewport;

  if (pDevice) {
    pDevice->viewport.X = u32(pViewport->X);
    pDevice->viewport.Y = u32(pViewport->Y);
    pDevice->viewport.Width = u32(pViewport->Width);
    pDevice->viewport.Height = u32(pViewport->Height);
    pDevice->viewport.MinZ = fmin;
    pDevice->viewport.MaxZ = fmax;
  }
}

void D3DDevice_SetRenderTarget_hook(
    rex::MappedPtr<bd::gpu::D3DDevice> pDevice, u32 RenderTargetIndex,
    rex::MappedPtr<bd::gpu::D3DSurface> pRenderTarget) {
  auto *surface = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
      pRenderTarget.GetGuestAddress());

  // Maintain the device RT shadow the recompiled D3DDevice_GetRenderTarget
  // reads: the RT stack push/pop (every Sofdec movie frame) saves via the
  // getter and restores via Set, so a stale shadow un-binds the real RT after
  // each stack pop.
  if (pDevice && RenderTargetIndex < 4) {
    pDevice->renderTargetShadow[RenderTargetIndex] =
        pRenderTarget.GetGuestAddress();
  }

  // MRT is not modeled. BD issues SetRenderTarget(idx=1..3, NULL) every frame
  // after binding RT[0], and writing those into state would clobber the real
  // RT[0].
  if (RenderTargetIndex != 0)
    return;

  bd::gpu::Video::PromoteForGPUWrite(surface);
  auto &s = bd::gpu::state();

  // FlushRenderState bakes the new RT's format into the PSO, so the cached
  // framebuffer holding the old RTV has to go with it.
  if (s.render_target != surface)
    s.draw_framebuffer_bound = false;

  bd::gpu::Video::SetDirtyValue<bd::gpu::GuestTexture *>(
      s.dirtyStates.renderTargetAndDepthStencil, s.render_target, surface);
  bd::gpu::Video::SetDirtyValue<plume::RenderFormat>(
      s.dirtyStates.pipelineState, s.pipelineState.renderTargetFormat,
      surface != nullptr ? surface->format : plume::RenderFormat::UNKNOWN);
  bd::gpu::Video::SetDirtyValue<plume::RenderSampleCounts>(
      s.dirtyStates.pipelineState, s.pipelineState.sampleCount,
      surface != nullptr ? surface->sampleCount
                         : plume::RenderSampleCount::COUNT_1);
  // Alpha test mode tied to sample count.
  bd::gpu::Video::SetAlphaTestMode(
      (s.pipelineState.specConstants & bd::gpu::kSpecConstantAlphaTest) != 0);

  bd::gpu::Video::SetDefaultViewport(pDevice, surface);
}

void D3DDevice_SetDepthStencilSurface_hook(
    rex::MappedPtr<bd::gpu::D3DDevice> pDevice,
    rex::MappedPtr<bd::gpu::D3DSurface> pZStencilSurface) {
  auto *surface = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
      pZStencilSurface.GetGuestAddress());
  // Shadow for the recompiled D3DDevice_GetDepthStencilSurface (+0x2F98). Raw
  // VA as given, the pop's re-Set re-applies the type filter below.
  if (pDevice) {
    pDevice->depthStencilShadow = pZStencilSurface.GetGuestAddress();
  }
  // FromGuest verifies registration, not ResourceType: reject non-depth so a
  if (surface) {
    const bool is_depth =
        surface->type == bd::gpu::ResourceType::DepthStencil ||
        (surface->type == bd::gpu::ResourceType::Texture &&
         bd::gpu::IsDepthFormat(surface->format));
    if (!is_depth)
      surface = nullptr;
  }

  bd::gpu::Video::PromoteForGPUWrite(surface);
  auto &s = bd::gpu::state();

  // Depth surface change drops the cached framebuffer (same #613 reason as
  // SetRenderTarget above).
  if (s.depth_stencil != surface)
    s.draw_framebuffer_bound = false;

  bd::gpu::Video::SetDirtyValue<bd::gpu::GuestTexture *>(
      s.dirtyStates.renderTargetAndDepthStencil, s.depth_stencil, surface);
  bd::gpu::Video::SetDirtyValue<plume::RenderFormat>(
      s.dirtyStates.pipelineState, s.pipelineState.depthStencilFormat,
      surface != nullptr ? surface->format : plume::RenderFormat::UNKNOWN);

  // Remember the most recent real depth surface for the enhanced DOF
  // downsample. BD rebinds the 1280 DOF depth here just before the post chain,
  // so this ends up naming the depth aligned with the 1280 scene resolve.
  if (surface)
    s.scene_depth = surface;

  bd::gpu::Video::SetDefaultViewport(pDevice, surface);
}

void D3DDevice_SetScissorRect_hook(
    rex::MappedPtr<bd::gpu::D3DDevice> /*pDevice*/,
    rex::MappedPtr<bd::gpu::D3DRect> pRect) {
  if (!pRect) {
    return;
  }
  // Scissor always tracks the viewport extent in FlushViewport, so the rect
  // values are unused, and only the dirty mark matters.
  bd::gpu::state().dirtyStates.scissorRect = true;
}

void D3DDevice_SetVertexShader_hook(u32 /*device*/, u32 shader_guest) {
  auto *shader =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestShader>(shader_guest);
  bd::gpu::Video::SetVertexShader(shader);
}

void D3DDevice_SetPixelShader_hook(u32 /*device*/, u32 shader_guest) {
  auto *shader =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestShader>(shader_guest);
  bd::gpu::Video::SetPixelShader(shader);
}

void D3DDevice_SetVertexDeclaration_hook(u32 /*device*/, u32 decl_guest) {
  auto *decl =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestVertexDeclaration>(
          decl_guest);

  // The recompiled VS guards its R11G11B10/SNORM decode on this bit, else it
  // asfloat()s the normal bits to garbage.
  {
    auto &s = bd::gpu::state();
    u32 spec =
        s.pipelineState.specConstants & ~bd::gpu::kSpecConstantR11G11B10Normal;
    if (decl && decl->hasR11G11B10Normal)
      spec |= bd::gpu::kSpecConstantR11G11B10Normal;
    bd::gpu::Video::SetDirtyValue<u32>(s.dirtyStates.pipelineState,
                                       s.pipelineState.specConstants, spec);
  }

  bd::gpu::Video::SetVertexDeclaration(decl);
}

void D3DDevice_SetTexture_hook(u32 /*device*/, u32 sampler, u32 texture_guest) {
  auto *tex = bd::gpu::ResolveGuestTexture(texture_guest);
  const bool unresolved = (!tex && texture_guest);
  if (unresolved) {
    // Unresolved (e.g. outside the supported native format set): green marker.
    tex = bd::gpu::GetOrCreateDebugTexture();
  }
  bd::gpu::Video::SetTexture(sampler, tex);
}

void D3DDevice_SetStreamSource_hook(u32 /*device*/, u32 stream,
                                    u32 buffer_guest, u32 offset, u32 stride) {
  auto *buf =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(buffer_guest);
  // Physical VBs wrap an engine-owned D3DVertexBuffer struct never
  // HostResourceHeap::Alloc'd, so FromGuest misses. Struct VA map covers VBs
  // registered eagerly (XGOffsetResourceAddress), and the lazy bootstrap
  // reads the struct's Xenos fetch constant fields for buffers from unhooked
  // paths.
  if (!buf && buffer_guest) {
    buf =
        ResolveGuestBufferVa(buffer_guest, bd::gpu::ResourceType::VertexBuffer);
  }
  if (buf && buf->hasBuffer()) {
    auto ref = buf->bufferRef(offset);
    const u32 bound_size = offset < buf->dataSize ? buf->dataSize - offset : 0;
    bd::gpu::Video::SetVertexStream(stream, ref, bound_size, stride);
  } else {
    bd::gpu::Video::SetVertexStream(stream, plume::RenderBufferReference{}, 0,
                                    0);
  }
}

void D3DDevice_SetIndices_hook(u32 /*device*/, u32 indices_guest) {
  auto *ib =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(indices_guest);
  // Physical IBs wrap an engine-owned struct FromGuest misses (same as
  // SetStreamSource). Without the struct VA bridge + lazy bootstrap, every
  // scene mesh's IB binds null, every index reads 0, scene goes black.
  if (!ib && indices_guest) {
    ib =
        ResolveGuestBufferVa(indices_guest, bd::gpu::ResourceType::IndexBuffer);
  }
  bd::gpu::Video::SetIndices(ib);
}

} // namespace

// SetSamplerState is deliberately NOT hooked: its recompiled body writes the
// sampler state table at device+0x1BC, which FlushRenderState reads directly.
// Constant uploads are gated by the engine's own m_Mask[0] and m_Mask[1], which
// FlushDirtyConstantMasks consumes at draw time.

namespace {

// bdSetRenderState. BD's chokepoint for nearly every D3DRS write. The per-state
// setters are reached through the dispatch table at 0x82751D68, keyed by the
// render state's byte offset (its index * 4). Five are also called directly:
// ClipPlaneEnable, ColorWriteEnable, HalfPixelOffset, PresentInterval and
// PresentImmediateThreshold. ColorWriteEnable is the one the draw flush reads.
constexpr u32 kRsAlphaTestEnable = 24 * 4;
constexpr u32 kRsAlphaRef = 25 * 4;
constexpr float kAlphaRefScale = 1.0f / 256.0f; // ALPHAREF is 0..255

} // namespace

REX_HOOK(D3DDevice_SetViewport, D3DDevice_SetViewport_hook);
REX_HOOK(D3DDevice_SetRenderTarget, D3DDevice_SetRenderTarget_hook);
REX_HOOK(D3DDevice_SetDepthStencilSurface,
         D3DDevice_SetDepthStencilSurface_hook);
REX_HOOK(D3DDevice_SetScissorRect, D3DDevice_SetScissorRect_hook);
REX_HOOK(D3DDevice_SetVertexShader, D3DDevice_SetVertexShader_hook);
REX_HOOK(D3DDevice_SetPixelShader, D3DDevice_SetPixelShader_hook);
REX_HOOK(D3DDevice_SetVertexDeclaration, D3DDevice_SetVertexDeclaration_hook);
REX_HOOK(D3DDevice_SetTexture, D3DDevice_SetTexture_hook);
REX_HOOK(D3DDevice_SetStreamSource, D3DDevice_SetStreamSource_hook);
REX_HOOK(D3DDevice_SetIndices, D3DDevice_SetIndices_hook);
REX_DETOUR(bdSetRenderState, void, (u32 offset, u32 value)) {
  // Depth/cull/color-write are read from g_renderStateCache at draw time
  // (ReadDeviceRenderState), which sees LTCG-inlined sites a function
  // hook can't. Only alpha test feeds SharedConstants: it is fixed-function ROP
  // on X360, and the recompiled PS clips on kSpecConstantAlphaTest instead.
  if (offset == kRsAlphaTestEnable) {
    bd::gpu::Video::SetAlphaTestMode(value != 0);
  } else if (offset == kRsAlphaRef) {
    bd::gpu::Video::SetAlphaThreshold(static_cast<float>(value) *
                                      kAlphaRefScale);
  }
  // bdSetRenderState skips the write when the cache already holds the value, so
  // only a changed value reaches the device and retires the override.
  if (offset == bd::gpu::kRsColorWriteEnable) {
    const auto *cached = bd::mem::at<const be_u32>(
        bd::gpu::kRenderStateCacheVa + bd::gpu::kRsColorWriteEnable);
    if (!cached || u32(*cached) != value)
      bd::gpu::Video::SetColorWriteOverride(-1);
  }
  original(offset, value);
}

// Visual__UnitNormal__vf05 restores 0xF through this setter without going
// through bdSetRenderState, so the cache keeps the 7 vf03 wrote while the
// device holds 0xF. The next bdSetRenderState of that same 7 is skipped on the
// console, so the cache is left alone and the device value rides as an override.
REX_DETOUR(D3DDevice_SetRenderState_ColorWriteEnable, void,
           (u32 device, u32 value)) {
  original(device, value);
  bd::gpu::Video::SetColorWriteOverride(static_cast<i32>(value));
}
