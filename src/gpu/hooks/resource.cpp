/**
 * @file    gpu/hooks/resource.cpp
 * @brief   Guest hooks that create, describe, lock and release D3D resources.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_set>

#include <rex/ppc/hook.h>
#include <rex/memory.h>
#include <rex/memory/endian.h>
#include <rex/kernel/title_runtime.h>
#include <rex/types.h>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/hooks/tweaks.h"
#include "gpu/native_texture_mirror.h"
#include "gpu/output.h"
#include "gpu/physical_buffers.h"
#include "gpu/surface_registry.h"
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
  const double scale = bd::gpu::SceneRenderScale();
  return width + kTargetAlignment >= bd::gpu::kDesignCanvasWidth * scale &&
         height + kTargetAlignment >= bd::gpu::kDesignCanvasHeight * scale;
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

  return bd::gpu::SurfaceRegistry::Get(width, height, format,
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
  // d3d_type: 0x11=D3DRTYPE_VOLUMETEXTURE -> 3D, 0x12=D3DRTYPE_CUBETEXTURE ->
  // cube (Texture type, 2D dimension + arraySize 6 + CUBE flag), else plain 2D.
  const bool exempt = bd::gpu::SurfaceRegistry::TakeExemption(width, height);
  const bool is_volume = (d3d_type == bd::gpu::kGetTypeVolumeTexture);
  const bool is_cube = (d3d_type == bd::gpu::kGetTypeCubeTexture);
  const auto rtype = is_volume ? bd::gpu::ResourceType::VolumeTexture
                               : bd::gpu::ResourceType::Texture;
  const auto view_dimension =
      is_volume ? plume::RenderTextureViewDimension::TEXTURE_3D
      : is_cube ? plume::RenderTextureViewDimension::TEXTURE_CUBE
                : plume::RenderTextureViewDimension::TEXTURE_2D;
  auto *texture =
      bd::gpu::HostResourceHeap::Alloc<bd::gpu::GuestTexture>(rtype);
  if (!texture) {
    REXBD_ERROR("CreateTexture: host resource heap exhausted");
    return nullptr;
  }

  bd::gpu::InitResourceHeader(texture->x360.as_texture.resource,
                              bd::gpu::D3DResourceType::kTexture);

  const plume::RenderFormat plume_format = bd::gpu::ConvertGuestFormat(format);
  texture->SetSize(width, height);
  texture->scaleExempt = exempt;
  plume::RenderTextureDesc desc;
  desc.dimension = (rtype == bd::gpu::ResourceType::VolumeTexture)
                       ? plume::RenderTextureDimension::TEXTURE_3D
                       : plume::RenderTextureDimension::TEXTURE_2D;
  desc.width = texture->hostWidth;
  desc.height = texture->hostHeight;
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
  // Render and depth targets are placed rather than committed. A committed
  // texture takes a D3D12 heap of its own, and that driver allocation is what
  // the frame costs every time a surface is recreated at a new size.
  desc.committed = false;

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
    REXBD_ERROR("CreateTexture fired before Video host device exists");
  }
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
    REXBD_ERROR("CreateVertexBuffer: host resource heap exhausted");
    return nullptr;
  }
  b->guestMirrorVa = bd::mem::SystemAlloc(length, 0x20);
  if (!b->guestMirrorVa) {
    REXBD_ERROR("CreateVertexBuffer: mirror alloc failed ({} bytes)", length);
    bd::gpu::HostResourceHeap::Free(b);
    return nullptr;
  }
  b->ownsMirror = true;
  bd::mem::Zero(b->guestMirrorVa, length);
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
    REXBD_ERROR("CreateIndexBuffer: host resource heap exhausted");
    return nullptr;
  }
  b->guestMirrorVa = bd::mem::SystemAlloc(length, 0x20);
  if (!b->guestMirrorVa) {
    REXBD_ERROR("CreateIndexBuffer: mirror alloc failed ({} bytes)", length);
    bd::gpu::HostResourceHeap::Free(b);
    return nullptr;
  }
  b->ownsMirror = true;
  bd::mem::Zero(b->guestMirrorVa, length);
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
    REXBD_ERROR("UnlockGuestBuffer: map failed (size={}, {})", b->dataSize,
             is_index ? "index" : "vertex");
    return;
  }

  if (stride_bytes == 2) {
    rex::memory::copy_and_swap<u16>(static_cast<u16 *>(mapped),
                                    reinterpret_cast<const u16 *>(mirror),
                                    b->dataSize / 2);
  } else {
    rex::memory::copy_and_swap<u32>(static_cast<u32 *>(mapped),
                                    reinterpret_cast<const u32 *>(mirror),
                                    b->dataSize / 4);
  }
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
u32 ReleaseSurfaceLevel(rex::memory::MappedPtr<bd::gpu::D3DResource> res) {
  const u32 surface_va = res.GetGuestAddress();
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

u32 D3DResource_Release_hook(rex::memory::MappedPtr<bd::gpu::D3DResource> res) {
  if (!res)
    return 0;
  bd::gpu::ResourceType type;
  if (!bd::gpu::HostResourceHeap::GetType(res.GetGuestAddress(), &type)) {
    return ReleaseSurfaceLevel(res);
  }
  const u32 prev = res->ReferenceCount;
  if (prev == 0)
    return 0;
  const u32 next = prev - 1;
  res->ReferenceCount = next;
  if (next == 0) {
    bd::gpu::Video::QueueResourceDestroy(res.GetGuestAddress(), type);
  }
  return next;
}

u32 D3DResource_AddRef_hook(rex::memory::MappedPtr<bd::gpu::D3DResource> res) {
  if (!res)
    return 0;
  bd::gpu::ResourceType ignored;
  if (!bd::gpu::HostResourceHeap::GetType(res.GetGuestAddress(), &ignored)) {
    if (!IsSurfaceLevel(res.GetGuestAddress()))
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
      return u32(bd::gpu::D3DResourceType::kSurface);
    case bd::gpu::ResourceType::Texture:
      return u32(bd::gpu::D3DResourceType::kTexture);
    case bd::gpu::ResourceType::VolumeTexture:
      return bd::gpu::kGetTypeVolumeTexture;
    case bd::gpu::ResourceType::VertexBuffer:
      return u32(bd::gpu::D3DResourceType::kVertexBuffer);
    case bd::gpu::ResourceType::IndexBuffer:
      return u32(bd::gpu::D3DResourceType::kIndexBuffer);
    default:
      return 0;
    }
  }
  // Native mirror path (bdAllocRenderBuffer textures registered in
  // g_native_mirrors but not HostResourceHeap). Resolve and check dimension.
  auto *tex = bd::gpu::ResolveGuestTexture(res_guest);
  if (tex) {
    if (tex->viewDimension == plume::RenderTextureViewDimension::TEXTURE_3D)
      return bd::gpu::kGetTypeVolumeTexture;
    if (tex->viewDimension == plume::RenderTextureViewDimension::TEXTURE_CUBE)
      return bd::gpu::kGetTypeCubeTexture;
    return u32(bd::gpu::D3DResourceType::kTexture);
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
void WarnIfScaled(const bd::gpu::GuestTexture *tex, const char *what) {
  if (tex->scaleX == 1.0f && tex->scaleY == 1.0f)
    return;
  static std::atomic<bool> warned{false};
  if (!warned.exchange(true, std::memory_order_relaxed)) {
    REXBD_WARN("{} on a host-scaled texture {}x{} (host {}x{})", what,
                tex->width, tex->height, tex->hostWidth, tex->hostHeight);
  }
}

u32 FillLockedRectScratch(bd::gpu::GuestTexture *tex,
                          bd::gpu::D3DLockedRect *locked) {
  if (!tex || !tex->texture)
    return 0;
  tex->cpuWritten = true;
  WarnIfScaled(tex, "LockRect");
  const u32 pitch = bd::gpu::ComputeTexturePitch(tex);
  const u32 slice_pitch = pitch * tex->height;
  if (!slice_pitch)
    return 0; // UNKNOWN format (e.g. BCn): no host pitch
  if (!tex->mappedMemory)
    tex->mappedMemory = bd::mem::SystemAlloc(slice_pitch, 0x10);
  if (!tex->mappedMemory)
    return 0;
  locked->Pitch = pitch;
  locked->pBits = tex->mappedMemory;
  return pitch;
}

void D3DSurface_LockRect_hook(rex::memory::MappedPtr<const bd::gpu::D3DSurface> surface,
                              rex::memory::MappedPtr<bd::gpu::D3DLockedRect> locked,
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
                                  rex::memory::MappedPtr<bd::gpu::D3DLockedRect> locked,
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

// bdAllocRenderBuffer's native malloc branch hand-builds a 52-byte D3DTexture
// outside every D3D hook, the sole producer of FromGuest-null textures, so a
// host mirror is built and registered for ResolveGuestTexture to find.
REX_DETOUR(bdAllocRenderBuffer, u32,
           (u32 name, u32 texture, u32 data, u32 param, u32 outTexture,
            u32 outData)) {
  const u32 result = original(name, texture, data, param, outTexture, outData);
  if (result &&
      !bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(result)) {
    bd::gpu::GetOrCreateNativeMirror(result, name);
  }
  return result;
}

// LoadTexture__vf03 releases a texture resource object. A native malloc
// texture has its XPhysical data slot set, so its registry entry is evicted
// before the original frees and zeroes the fields. DDS textures leave that
// slot at 0 and are left alone.
REX_DETOUR(LoadTexture__vf03, u32, (u32 self)) {
  const auto *res = bd::mem::at<const LoadTextureResource>(self);
  if (res && u32(res->xphysicalData)) {
    bd::gpu::EvictNativeTexture(res->textureVa);
  }
  return original(self);
}

REX_IMPORT(__imp__D3DSurface_GetDesc, OriginalSurfaceGetDesc,
           void(u32, u32));
REX_IMPORT(__imp__D3DTexture_GetLevelDesc, OriginalTextureGetLevelDesc,
           void(u32, u32, u32));
REX_IMPORT(__imp__D3DVolumeTexture_GetLevelDesc,
           OriginalVolumeTextureGetLevelDesc, void(u32, u32, u32));

namespace {

void D3DSurface_GetDesc_hook(u32 surfaceVA, u32 descVA) {
  auto *desc = bd::mem::at<bd::gpu::D3DSurfaceDesc>(descVA);
  if (!desc)
    return;

  const auto *surface = bd::mem::at<const bd::gpu::D3DSurface>(surfaceVA);
  auto *tex = bd::gpu::ResolveGuestTexture(surfaceVA);
  if (!tex && surface) {
    tex = bd::gpu::ResolveGuestTexture(surface->SurfaceInfo);
  }
  if (!tex) {
    // Unresolved: let the recompiled body decode the real SizeBits.
    OriginalSurfaceGetDesc(surfaceVA, descVA);
    return;
  }

  desc->Format = tex->guestFormat;
  desc->Type = u32(bd::gpu::D3DResourceType::kSurface);
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->MultiSampleType = 0u; // reblue surfaces are COUNT_1
  desc->MultiSampleQuality = 0u;
  desc->Width = tex->width;
  desc->Height = tex->height;
}

} // namespace

REX_HOOK(D3DSurface_GetDesc, D3DSurface_GetDesc_hook);

// The recompiled D3DTexture_GetLevelDesc runs XGComputeTextureTailSize, which
// divides by zero when the texture has no Xenos fetch constant, as reblue host
// textures do not. Fill D3DSURFACE_DESC from host fields instead.
namespace {

void D3DTexture_GetLevelDesc_hook(u32 texture_guest, u32 level, u32 descVA) {
  auto *desc = bd::mem::at<bd::gpu::D3DSurfaceDesc>(descVA);
  if (!desc)
    return;

  auto *tex = bd::gpu::ResolveGuestTexture(texture_guest);
  if (!tex) {
    // Unresolved: let the recompiled body decode it if it has a fetch constant.
    OriginalTextureGetLevelDesc(texture_guest, level, descVA);
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
                   ? bd::gpu::kGetTypeVolumeTexture
                   : u32(bd::gpu::D3DResourceType::kTexture);
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->MultiSampleType = 0u;
  desc->MultiSampleQuality = 0u;
  desc->Width = w;
  desc->Height = h;
}

} // namespace

REX_HOOK(D3DTexture_GetLevelDesc, D3DTexture_GetLevelDesc_hook);

// Volume twin of D3DTexture_GetLevelDesc. D3DVOLUME_DESC adds Depth and has no
// MultiSample fields.
namespace {

void D3DVolumeTexture_GetLevelDesc_hook(u32 texture_guest, u32 level,
                                        u32 descVA) {
  auto *desc = bd::mem::at<bd::gpu::D3DVolumeDesc>(descVA);
  if (!desc)
    return;

  auto *tex = bd::gpu::ResolveGuestTexture(texture_guest);
  if (!tex) {
    // Unresolved: let the recompiled body decode it if it has a fetch constant.
    OriginalVolumeTextureGetLevelDesc(texture_guest, level, descVA);
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
  desc->Type = bd::gpu::kGetTypeVolumeTexture;
  desc->Usage = 0u;
  desc->Pool = 0u;
  desc->Width = w;
  desc->Height = h;
  desc->Depth = d;
}

} // namespace

REX_HOOK(D3DVolumeTexture_GetLevelDesc, D3DVolumeTexture_GetLevelDesc_hook);
