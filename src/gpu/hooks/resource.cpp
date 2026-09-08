/**
 * @file    gpu/hooks/resource.cpp
 * @brief   Guest hooks that create, describe, lock and release D3D resources.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <cstring>
#include <mutex>
#include <unordered_set>

#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/types.h>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/byte_swap.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/hooks/tweaks.h"
#include "gpu/native_texture_mirror.h"
#include "gpu/output.h"
#include "gpu/physical_buffers.h"
#include "gpu/surface_pool.h"
#include "gpu/texture_upload.h"

namespace {

using bd::gpu::ResolveGuestBufferVa;

// Guest LoadTexture resource object (LoadTexture__vf03 at 0x8217ACF0). Only
// the two host-read fields are modeled, and the rest of the object is opaque.
struct LoadTextureResource {
  u8 pad0[0xBC];
  be_u32 textureVa; // D3DTexture VA owned by the resource
  be_u32 pad_c0;
  be_u32 xphysicalData; // non-zero only for native-malloc textures
};
static_assert(offsetof(LoadTextureResource, textureVa) == 0xBC);
static_assert(offsetof(LoadTextureResource, xphysicalData) == 0xC4);

constexpr double kTargetAlignment = 8.0;

bool IsFullFrameScene(u32 width, u32 height) {
  u32 fit_w = 0;
  u32 fit_h = 0;
  if (!bd::gpu::Output::LatchedFit(fit_w, fit_h))
    return true;
  const double scale =
      bd::gpu::Output::RenderFraction() * bd::gpu::SceneRenderScale();
  return width + kTargetAlignment >= fit_w * scale &&
         height + kTargetAlignment >= fit_h * scale;
}

bd::gpu::GuestTexture *D3DDevice_CreateSurface_hook(u32 width, u32 height,
                                                    u32 format,
                                                    u32 multi_sample,
                                                    u32 params_va) {
  const plume::RenderSampleCounts msaa_count =
      (multi_sample != 0 && IsFullFrameScene(width, height) &&
       bd::gpu::Video::CvarMSAASampleCount() !=
           plume::RenderSampleCount::COUNT_1)
          ? bd::gpu::Video::CvarMSAASampleCount()
          : plume::RenderSampleCount::COUNT_1;

  return bd::gpu::SurfacePool::Acquire(width, height, format,
                                       static_cast<u32>(msaa_count));
}

// D3DDevice_CreateTexture is __stdcall and returns D3DBaseTexture*:
//   (DWORD Width, DWORD Height, DWORD Depth, DWORD Levels, DWORD Usage,
//    D3DFORMAT Format, D3DPOOL UnusedPool, D3DRESOURCETYPE D3DType).
bd::gpu::GuestTexture *D3DDevice_CreateTexture_hook(u32 width, u32 height,
                                                    u32 depth, u32 levels,
                                                    u32 usage, u32 format,
                                                    u32 /*pool*/,
                                                    u32 d3d_type) {
  // d3d_type: 17=D3DRTYPE_VOLUMETEXTURE -> 3D, 18=D3DRTYPE_CUBETEXTURE -> cube
  // (Texture type, 2D dimension + arraySize 6 + CUBE flag), else plain 2D.
  const bool is_volume = (d3d_type == 17);
  const bool is_cube = (d3d_type == 18);
  const auto rtype = is_volume ? bd::gpu::ResourceType::VolumeTexture
                               : bd::gpu::ResourceType::Texture;
  const auto view_dimension =
      is_volume ? plume::RenderTextureViewDimension::TEXTURE_3D
      : is_cube ? plume::RenderTextureViewDimension::TEXTURE_CUBE
                : plume::RenderTextureViewDimension::TEXTURE_2D;
  auto *texture =
      bd::gpu::HostResourceHeap::Alloc<bd::gpu::GuestTexture>(rtype);
  if (!texture) {
    BD_ERROR("CreateTexture: host resource heap exhausted");
    return nullptr;
  }

  bd::gpu::InitResourceHeader(texture->x360.as_texture.resource,
                              bd::gpu::D3DResourceType::kTexture);

  const plume::RenderFormat plume_format = bd::gpu::ConvertGuestFormat(format);
  plume::RenderTextureDesc desc;
  desc.dimension = (rtype == bd::gpu::ResourceType::VolumeTexture)
                       ? plume::RenderTextureDimension::TEXTURE_3D
                       : plume::RenderTextureDimension::TEXTURE_2D;
  desc.width = width;
  desc.height = height;
  // Guest cube creation passes depth=6, but host faces ride arraySize instead.
  desc.depth = is_volume ? depth : 1;
  desc.mipLevels = levels;
  desc.arraySize = is_cube ? 6 : 1;
  desc.format = plume_format;
  // BD binds CreateTexture(usage=0) textures as render targets
  // (R16G16B16A16_UNORM HDR intermediates), so allow RENDER_TARGET on every
  // non-depth texture. IsRenderTargetCapable excludes formats D3D12 rejects
  // (block-compressed etc).
  if (is_cube) {
    desc.flags = plume::RenderTextureFlag::CUBE;
    if (bd::gpu::IsDepthFormat(plume_format)) {
      desc.flags |= plume::RenderTextureFlag::DEPTH_TARGET;
    }
  } else if (bd::gpu::IsDepthFormat(plume_format)) {
    desc.flags = plume::RenderTextureFlag::DEPTH_TARGET;
  } else if (bd::gpu::IsRenderTargetCapable(plume_format)) {
    desc.flags = plume::RenderTextureFlag::RENDER_TARGET;
  } else {
    desc.flags = plume::RenderTextureFlag::NONE;
  }
  // Force committed only for RT/DS: shared heap placement leaves UNDEFINED
  // contents D3D12 GBV fills neon-green (visible if sampled before drawn into).
  // Sampled-only textures are populated before use, and forcing them committed
  // gave per-texture dedicated heaps -> TDR during level load (thousands of
  // small textures). Bitwise test (not equality): a depth cube now carries
  // CUBE|DEPTH_TARGET and needs the same protection as a plain DEPTH_TARGET.
  const bool is_rt_or_ds =
      (desc.flags & (plume::RenderTextureFlag::RENDER_TARGET |
                     plume::RenderTextureFlag::DEPTH_TARGET)) != 0;
  desc.committed = is_rt_or_ds;

  // Set before the SRV block: BindTextureSRV reads viewDimension to pick the
  // SRV dimension. A cube must be TEXTURE_CUBE here or it builds a degenerate
  // 2D SRV.
  texture->viewDimension = view_dimension;

  auto *device = bd::gpu::Video::HostDevice();
  if (device) {
    texture->textureHolder =
        bd::gpu::CreateHostTexture(device, desc, "guest-texture");
    texture->texture = texture->textureHolder.get();
    // Sampleable view + bindless registration for 2D non-depth textures.
    // Without a descriptorIndex the copy_color path in Present can't sample an
    // RT and falls back to clear-only (black). Bindless set is Texture2D-only.
    if (texture->texture && rtype != bd::gpu::ResourceType::VolumeTexture &&
        !bd::gpu::IsDepthFormat(plume_format)) {
      plume::RenderTextureViewDesc view_desc;
      view_desc.format = plume_format;
      view_desc.dimension = view_dimension;
      view_desc.mipLevels = levels;
      texture->textureView = texture->texture->createTextureView(view_desc);
      bd::gpu::Video::BindTextureSRV(texture);
    }
    if (texture->texture && rtype == bd::gpu::ResourceType::VolumeTexture) {
      plume::RenderTextureViewDesc view_desc;
      view_desc.format = plume_format;
      view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_3D;
      view_desc.mipLevels = levels;
      texture->textureView = texture->texture->createTextureView(view_desc);
      bd::gpu::Video::BindTextureSRV(texture);
    }
  } else {
    BD_ERROR("CreateTexture fired before Video host device exists");
  }
  texture->width = width;
  texture->height = height;
  texture->depth = depth;
  texture->mipLevels = levels;
  texture->format = plume_format;
  texture->guestFormat = format;
  (void)usage;
  return texture;
}

// CreateVertexBuffer / CreateIndexBuffer. A guest-visible scratch mirror is
// allocated up-front so Lock returns stable memory and repeated Locks reuse it.
bd::gpu::GuestBuffer *D3DDevice_CreateVertexBuffer_hook(u32 length,
                                                        u32 /*usage*/, u32 fvf,
                                                        u32 /*pool*/) {
  auto *b = bd::gpu::HostResourceHeap::Alloc<bd::gpu::GuestBuffer>(
      bd::gpu::ResourceType::VertexBuffer);
  if (!b) {
    BD_ERROR("CreateVertexBuffer: host resource heap exhausted");
    return nullptr;
  }
  auto *memory = REX_KERNEL_MEMORY();
  b->guestMirrorVa = memory->SystemHeapAlloc(length, 0x20);
  if (!b->guestMirrorVa) {
    BD_ERROR("CreateVertexBuffer: mirror alloc failed ({} bytes)", length);
    bd::gpu::HostResourceHeap::Free(b);
    return nullptr;
  }
  b->ownsMirror = true;
  memory->Zero(b->guestMirrorVa, length);
  b->dataSize = length;
  b->guestFormat = fvf;
  // X360 D3DVertexBuffer prefix: 24-byte D3DResource + 8-byte vertex fetch
  // constant. FetchLo = mirror base, FetchHi = size. Engine reads via
  // GetType / D3D::GetD3DFormat.
  bd::gpu::InitResourceHeader(b->x360.as_vertex_buffer.resource,
                              bd::gpu::D3DResourceType::kVertexBuffer);
  b->x360.as_vertex_buffer.FetchLo = b->guestMirrorVa;
  b->x360.as_vertex_buffer.FetchHi = length;
  return b;
}

bd::gpu::GuestBuffer *D3DDevice_CreateIndexBuffer_hook(u32 length,
                                                       u32 /*usage*/,
                                                       u32 format,
                                                       u32 /*pool*/) {
  auto *b = bd::gpu::HostResourceHeap::Alloc<bd::gpu::GuestBuffer>(
      bd::gpu::ResourceType::IndexBuffer);
  if (!b) {
    BD_ERROR("CreateIndexBuffer: host resource heap exhausted");
    return nullptr;
  }
  auto *memory = REX_KERNEL_MEMORY();
  b->guestMirrorVa = memory->SystemHeapAlloc(length, 0x20);
  if (!b->guestMirrorVa) {
    BD_ERROR("CreateIndexBuffer: mirror alloc failed ({} bytes)", length);
    bd::gpu::HostResourceHeap::Free(b);
    return nullptr;
  }
  b->ownsMirror = true;
  memory->Zero(b->guestMirrorVa, length);
  b->dataSize = length;
  b->guestFormat = format;
  // X360 D3DIndexBuffer prefix: 24-byte D3DResource + FetchLo (base) + FetchHi
  // (size).
  bd::gpu::InitResourceHeader(b->x360.as_index_buffer.resource,
                              bd::gpu::D3DResourceType::kIndexBuffer);
  b->x360.as_index_buffer.FetchLo = b->guestMirrorVa;
  b->x360.as_index_buffer.FetchHi = length;
  // The engine creates "untyped" IBs with format=0 (D3DFMT_UNKNOWN), pinning
  // the real format later via SetIndices. Skip ConvertGuestFormat on 0 to avoid
  // a spurious "unknown format" error.
  b->format = (format == 0) ? plume::RenderFormat::UNKNOWN
                            : bd::gpu::ConvertGuestFormat(format);
  return b;
}

u32 D3DVertexBuffer_Lock_hook(u32 buffer, u32 offset_to_lock, u32 size_to_lock,
                              u32 flags) {
  auto *b = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(buffer);
  // Physical VBs wrap an engine-owned struct FromGuest misses, so resolve it
  // like SetStreamSource. The mirror reflection plane fetch
  // (mcl_mirror_FetchReflectionPlaneTri) Locks such a struct read-only and
  // dereferences the result at base+0, so
  // returning 0 here crashes the guest.
  if (!b && buffer) {
    b = ResolveGuestBufferVa(buffer, bd::gpu::ResourceType::VertexBuffer);
  }
  if (!b || !b->guestMirrorVa)
    return 0;
  return b->guestMirrorVa + offset_to_lock;
}

u32 D3DIndexBuffer_Lock_hook(u32 buffer, u32 offset_to_lock, u32 size_to_lock,
                             u32 flags) {
  auto *b = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(buffer);
  // Same physical buffer struct VA miss as the VB path, so resolve like
  // SetIndices.
  if (!b && buffer) {
    b = ResolveGuestBufferVa(buffer, bd::gpu::ResourceType::IndexBuffer);
  }
  if (!b || !b->guestMirrorVa)
    return 0;
  return b->guestMirrorVa + offset_to_lock;
}

// (Re)allocate the host RenderBuffer to match the mirror size, then byte swap
// the mirror into the upload heap. stride_bytes is 2 or 4: VBs are always
// 4 (engine writes BE dwords), IBs track their own format.
void UnlockGuestBuffer(bd::gpu::GuestBuffer *b, u32 stride_bytes) {
  if (!b || !b->guestMirrorVa || b->dataSize == 0)
    return;
  auto *device = bd::gpu::Video::HostDevice();
  if (!device)
    return;

  const bool is_index = b->type == bd::gpu::ResourceType::IndexBuffer;
  if (!b->buffer) {
    const plume::RenderHeapType heap =
        bd::gpu::GeometryHeapType(device, bd::gpu::GeometryClass::Dynamic);
    plume::RenderBufferDesc desc =
        is_index ? plume::RenderBufferDesc::IndexBuffer(b->dataSize, heap)
                 : plume::RenderBufferDesc::VertexBuffer(b->dataSize, heap);
    b->buffer = bd::gpu::CreateHostBuffer(device, desc,
                                          is_index ? "host-ib" : "host-vb");
    if (!b->buffer)
      return;
  }

  const auto *mirror = bd::mem::at<const u8>(b->guestMirrorVa);
  if (!mirror)
    return;

  void *mapped = b->buffer->map();
  if (!mapped) {
    BD_ERROR("UnlockGuestBuffer: map failed (size={}, {})", b->dataSize,
             is_index ? "index" : "vertex");
    return;
  }

  bd::gpu::ByteSwapElements(mapped, mirror, b->dataSize, stride_bytes);
  b->buffer->unmap();
}

u32 D3DVertexBuffer_Unlock_hook(u32 buffer) {
  auto *b = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(buffer);
  UnlockGuestBuffer(b, /*stride_bytes=*/4);
  return 0;
}

u32 D3DIndexBuffer_Unlock_hook(u32 buffer) {
  auto *b = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestBuffer>(buffer);
  const u32 stride = (b && b->guestFormat == 1 /*D3DFMT_INDEX16*/) ? 2u : 4u;
  UnlockGuestBuffer(b, stride);
  return 0;
}

// D3DTexture_GetSurfaceLevel hands the engine a raw 0x30 surface struct
// (refcount at +0x04, parent texture VA at +0x18), not a HostResourceHeap
// resource. Track them so Release/Destroy honor the in-struct refcount and free
// the struct to HostHeap at zero instead of leaking it.
namespace {
std::mutex g_surface_level_mutex;
std::unordered_set<u32> g_surface_levels;

void RegisterSurfaceLevel(u32 va) {
  std::lock_guard<std::mutex> lk(g_surface_level_mutex);
  g_surface_levels.insert(va);
}
bool IsSurfaceLevel(u32 va) {
  std::lock_guard<std::mutex> lk(g_surface_level_mutex);
  return g_surface_levels.count(va) != 0;
}
bool TakeSurfaceLevel(u32 va) {
  std::lock_guard<std::mutex> lk(g_surface_level_mutex);
  return g_surface_levels.erase(va) != 0;
}

// Frees the struct to HostHeap at zero. Untracked VAs return 0 (matching the
// unknown-resource path in D3DResource_Release_hook).
u32 ReleaseSurfaceLevel(rex::MappedPtr<bd::gpu::D3DResource> res) {
  const u32 surface_va = res.guest_address();
  if (!IsSurfaceLevel(surface_va))
    return 0;
  const u32 prev = res->ReferenceCount;
  if (prev == 0)
    return 0;
  const u32 next = prev - 1;
  res->ReferenceCount = next;
  if (next == 0) {
    TakeSurfaceLevel(surface_va);
    bd::gpu::HostHeap::Get().FreeGuest(surface_va);
  }
  return next;
}
} // namespace

u32 D3DResource_Release_hook(rex::MappedPtr<bd::gpu::D3DResource> res) {
  if (!res)
    return 0;
  bd::gpu::ResourceType type;
  if (!bd::gpu::HostResourceHeap::GetType(res.guest_address(), &type)) {
    return ReleaseSurfaceLevel(res);
  }
  const u32 prev = res->ReferenceCount;
  if (prev == 0)
    return 0;
  const u32 next = prev - 1;
  res->ReferenceCount = next;
  if (next == 0) {
    bd::gpu::Video::QueueResourceDestroy(res.guest_address(), type);
  }
  return next;
}

u32 D3DResource_AddRef_hook(rex::MappedPtr<bd::gpu::D3DResource> res) {
  if (!res)
    return 0;
  bd::gpu::ResourceType ignored;
  if (!bd::gpu::HostResourceHeap::GetType(res.guest_address(), &ignored)) {
    if (!IsSurfaceLevel(res.guest_address()))
      return 0; // same in-struct refcount
  }
  const u32 next = u32(res->ReferenceCount) + 1;
  res->ReferenceCount = next;
  return next;
}

void D3DResource_Destroy_hook(u32 res_guest) {
  if (!res_guest)
    return;
  bd::gpu::ResourceType type;
  if (!bd::gpu::HostResourceHeap::GetType(res_guest, &type)) {
    if (TakeSurfaceLevel(res_guest)) {
      bd::gpu::HostHeap::Get().FreeGuest(res_guest);
    }
    return;
  }
  bd::gpu::Video::QueueResourceDestroy(res_guest, type);
}

u32 D3DResource_GetType_hook(u32 res_guest) {
  bd::gpu::ResourceType type;
  if (bd::gpu::HostResourceHeap::GetType(res_guest, &type)) {
    switch (type) {
    case bd::gpu::ResourceType::RenderTarget:
    case bd::gpu::ResourceType::DepthStencil:
      return 1; // D3DRTYPE_SURFACE
    case bd::gpu::ResourceType::Texture:
      return 3; // D3DRTYPE_TEXTURE
    case bd::gpu::ResourceType::VolumeTexture:
      return 17; // D3DRTYPE_VOLUMETEXTURE (X360 = 0x11)
    case bd::gpu::ResourceType::VertexBuffer:
      return 6; // D3DRTYPE_VERTEXBUFFER
    case bd::gpu::ResourceType::IndexBuffer:
      return 7; // D3DRTYPE_INDEXBUFFER
    default:
      return 0;
    }
  }
  // Native mirror path (bdAllocRenderBuffer textures registered in
  // g_native_mirrors but not HostResourceHeap). Resolve and check dimension.
  auto *tex = bd::gpu::ResolveGuestTexture(res_guest);
  if (tex) {
    if (tex->viewDimension == plume::RenderTextureViewDimension::TEXTURE_3D)
      return 17; // D3DRTYPE_VOLUMETEXTURE
    if (tex->viewDimension == plume::RenderTextureViewDimension::TEXTURE_CUBE)
      return 5; // D3DRTYPE_CUBETEXTURE
    return 3;   // D3DRTYPE_TEXTURE
  }
  return 0;
}

// The size the recompiled body's own XMemAlloc asks for.
constexpr u32 kD3DSurfaceAllocSize = 0x30;
constexpr u32 kD3DSurfaceAllocAlign = 0x10;

u32 D3DTexture_GetSurfaceLevel_hook(u32 texture_guest, u32 level) {
  // HostHeap-allocated and tracked, so Release/Destroy free it rather than
  // leak.
  const u32 surface_guest = bd::gpu::HostHeap::Get().AllocGuest(
      kD3DSurfaceAllocSize, kD3DSurfaceAllocAlign);
  if (!surface_guest)
    return 0;
  auto *surf = bd::mem::at<bd::gpu::D3DSurface>(surface_guest);
  if (!surf)
    return 0;
  RegisterSurfaceLevel(surface_guest);
  std::memset(surf, 0, sizeof(*surf));
  surf->resource.Common = 0x44100004u;
  surf->resource.ReferenceCount = 1u;
  surf->resource.BaseFlush = 0xFFFF0000u;
  surf->SurfaceInfo = texture_guest; // parent texture VA
  surf->DepthInfo = level << 28;     // encodes mip level
  return surface_guest;
}

// Point the guest D3DLOCKED_RECT at the texture's scratch mirror so the engine
// writes texels into stable guest memory, and D3DResource_Unlock_hook uploads
// it. Returns the pitch, or 0 (rect left zeroed) when there's no host backing.
u32 FillLockedRectScratch(bd::gpu::GuestTexture *tex,
                          bd::gpu::D3DLockedRect *locked) {
  if (!tex || !tex->texture)
    return 0;
  const u32 pitch = bd::gpu::ComputeTexturePitch(tex);
  const u32 slice_pitch = pitch * tex->height;
  if (!slice_pitch)
    return 0; // UNKNOWN format (e.g. BCn): no host pitch
  auto *memory = REX_KERNEL_MEMORY();
  if (!tex->mappedMemory)
    tex->mappedMemory = memory->SystemHeapAlloc(slice_pitch, 0x10);
  if (!tex->mappedMemory)
    return 0;
  locked->Pitch = pitch;
  locked->pBits = tex->mappedMemory;
  return pitch;
}

void D3DSurface_LockRect_hook(rex::MappedPtr<const bd::gpu::D3DSurface> surface,
                              rex::MappedPtr<bd::gpu::D3DLockedRect> locked,
                              u32 /*rect*/, u32 /*flags*/) {
  if (!locked)
    return;
  locked->Pitch = 0;
  locked->pBits = 0;
  if (!surface)
    return;
  // SurfaceInfo holds the parent texture VA (D3DTexture_GetSurfaceLevel_hook).
  auto *tex = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
      surface->SurfaceInfo);
  FillLockedRectScratch(tex, locked);
}

// CRIware/Sofdec movie frames. The guest LockRect path derefs
// *(*VdGlobalDevice+0)+0x2A08, null on the native device -> AV, so hand the
// engine the same scratch mirror surfaces use. pLockedRect is the 3rd arg here
// (textures carry a Level, surfaces don't).
void D3DLineTexture_LockRect_hook(u32 texture_guest, u32 /*level*/,
                                  rex::MappedPtr<bd::gpu::D3DLockedRect> locked,
                                  u32 /*rect*/, u32 /*flags*/) {
  if (!locked)
    return;
  locked->Pitch = 0;
  locked->pBits = 0;
  auto *tex = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
      texture_guest);
  FillLockedRectScratch(tex, locked);
}

u32 D3DResource_Unlock_hook(u32 resource, u32 /*a2*/, u32 /*a3*/) {
  auto *tex =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(resource);
  if (tex && tex->mappedMemory) {
    bd::gpu::UploadTextureFromMapped(tex);
  }
  return 0;
}

} // namespace

REX_HOOK(D3DDevice_CreateSurface, D3DDevice_CreateSurface_hook);
REX_HOOK(D3DDevice_CreateTexture, D3DDevice_CreateTexture_hook);
REX_HOOK(D3DDevice_CreateVertexBuffer, D3DDevice_CreateVertexBuffer_hook);
REX_HOOK(D3DDevice_CreateIndexBuffer, D3DDevice_CreateIndexBuffer_hook);
REX_HOOK(D3DVertexBuffer_Lock, D3DVertexBuffer_Lock_hook);
REX_HOOK(D3DIndexBuffer_Lock, D3DIndexBuffer_Lock_hook);
REX_HOOK(D3DVertexBuffer_Unlock, D3DVertexBuffer_Unlock_hook);
REX_HOOK(D3DIndexBuffer_Unlock, D3DIndexBuffer_Unlock_hook);
REX_HOOK(D3DSurface_LockRect, D3DSurface_LockRect_hook);
REX_HOOK(D3DLineTexture_LockRect, D3DLineTexture_LockRect_hook);
REX_HOOK(D3DResource_Unlock, D3DResource_Unlock_hook);
REX_HOOK(D3DResource_Release, D3DResource_Release_hook);
REX_HOOK(D3DResource_AddRef, D3DResource_AddRef_hook);
REX_HOOK(D3DResource_Destroy, D3DResource_Destroy_hook);
REX_HOOK(D3DResource_GetType, D3DResource_GetType_hook);
REX_HOOK(D3DTexture_GetSurfaceLevel, D3DTexture_GetSurfaceLevel_hook);

// The five below run their original raw, on the inherited context: a typed
// REX_IMPORT re-roots the guest stack at ThreadState's r1 and overwrites the
// frames live underneath it.

// bdAllocRenderBuffer's native malloc branch hand-builds a 52-byte D3DTexture
// outside every D3D hook, the sole producer of FromGuest-null textures, so a
// host mirror is built and registered for ResolveGuestTexture to find.
REX_EXTERN(__imp__bdAllocRenderBuffer);
REX_HOOK_RAW(bdAllocRenderBuffer) {
  // r3 is the asset basename, kept so a reject names the dropped texture.
  const u32 name = ctx.r3.u32;
  __imp__bdAllocRenderBuffer(ctx, base);
  const u32 result = ctx.r3.u32;
  if (result &&
      !bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(result)) {
    bd::gpu::GetOrCreateNativeMirror(result, name);
  }
}

// LoadTexture__vf03 releases a texture resource object. A native malloc
// texture has its XPhysical data slot set, so its registry entry is evicted
// before the original frees and zeroes the fields. DDS textures leave that
// slot at 0 and are left alone.
REX_EXTERN(__imp__LoadTexture__vf03);
REX_HOOK_RAW(LoadTexture__vf03) {
  const auto *res = bd::mem::at<const LoadTextureResource>(ctx.r3.u32);
  if (res && u32(res->xphysicalData)) {
    bd::gpu::EvictNativeTexture(res->textureVa);
  }
  __imp__LoadTexture__vf03(ctx, base);
}

REX_EXTERN(__imp__D3DSurface_GetDesc);
REX_HOOK_RAW(D3DSurface_GetDesc) {
  const u32 surfaceVA = ctx.r3.u32;
  auto *desc = bd::mem::at<bd::gpu::D3DSurfaceDesc>(ctx.r4.u32);
  if (!desc)
    return;

  const auto *surface = bd::mem::at<const bd::gpu::D3DSurface>(surfaceVA);
  auto *tex = bd::gpu::ResolveGuestTexture(surfaceVA);
  if (!tex && surface) {
    tex = bd::gpu::ResolveGuestTexture(surface->SurfaceInfo);
  }
  if (!tex) {
    // Unresolved: let the recompiled body decode the real SizeBits.
    __imp__D3DSurface_GetDesc(ctx, base);
    return;
  }

  desc->Format = tex->guestFormat;
  desc->Type = 4u; // D3DRTYPE_SURFACE
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->MultiSampleType = 0u; // reblue surfaces are COUNT_1
  desc->MultiSampleQuality = 0u;
  desc->Width = tex->width;
  desc->Height = tex->height;
}

// The recompiled D3DTexture_GetLevelDesc runs XGComputeTextureTailSize, which
// divides by zero when the texture has no Xenos fetch constant, as reblue host
// textures do not. Fill D3DSURFACE_DESC from host fields instead.
REX_EXTERN(__imp__D3DTexture_GetLevelDesc);
REX_HOOK_RAW(D3DTexture_GetLevelDesc) {
  const u32 texture_guest = ctx.r3.u32;
  const u32 level = ctx.r4.u32;
  auto *desc = bd::mem::at<bd::gpu::D3DSurfaceDesc>(ctx.r5.u32);
  if (!desc)
    return;

  auto *tex = bd::gpu::ResolveGuestTexture(texture_guest);
  if (!tex) {
    // Unresolved: let the recompiled body decode it if it has a fetch constant.
    __imp__D3DTexture_GetLevelDesc(ctx, base);
    return;
  }

  // Mip dims: base >> Level, clamped to 1. Guard the shift against a bad Level.
  const u32 lv = (level < 16u) ? level : 0u;
  u32 w = tex->width >> lv;
  u32 h = tex->height >> lv;
  if (!w)
    w = 1u;
  if (!h)
    h = 1u;

  desc->Format = tex->guestFormat;
  desc->Type = (tex->type == bd::gpu::ResourceType::VolumeTexture)
                   ? 4u  // D3DRTYPE_VOLUMETEXTURE
                   : 3u; // D3DRTYPE_TEXTURE
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->MultiSampleType = 0u;
  desc->MultiSampleQuality = 0u;
  desc->Width = w;
  desc->Height = h;
}

// Volume twin of D3DTexture_GetLevelDesc. D3DVOLUME_DESC adds Depth and has no
// MultiSample fields.
REX_EXTERN(__imp__D3DVolumeTexture_GetLevelDesc);
REX_HOOK_RAW(D3DVolumeTexture_GetLevelDesc) {
  const u32 texture_guest = ctx.r3.u32;
  const u32 level = ctx.r4.u32;
  auto *desc = bd::mem::at<bd::gpu::D3DVolumeDesc>(ctx.r5.u32);
  if (!desc)
    return;

  auto *tex = bd::gpu::ResolveGuestTexture(texture_guest);
  if (!tex) {
    // Unresolved: let the recompiled body decode it if it has a fetch constant.
    __imp__D3DVolumeTexture_GetLevelDesc(ctx, base);
    return;
  }

  const u32 lv = (level < 16u) ? level : 0u;
  u32 w = tex->width >> lv;
  u32 h = tex->height >> lv;
  u32 d = tex->depth >> lv;
  if (!w)
    w = 1u;
  if (!h)
    h = 1u;
  if (!d)
    d = 1u;

  desc->Format = tex->guestFormat;
  desc->Type = 4u; // D3DRTYPE_VOLUMETEXTURE
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->Width = w;
  desc->Height = h;
  desc->Depth = d;
}
