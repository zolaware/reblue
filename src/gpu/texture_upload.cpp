/**
 * @file    gpu/texture_upload.cpp
 * @brief   BC and RGBA host mirror texture builders, and mapped-memory
 *          texture upload.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/frame.h"

#include <memory>
#include <mutex>
#include <vector>

#include <plume_render_interface.h>

#include "core/profiling.h"

#include "core/logging.h"
#include "gpu/constant_buffers.h"
#include "gpu/format.h"
#include "gpu/texture_upload.h"

namespace bd::gpu {

namespace {

// Dims + per-subresource upload list describing one host BC/RGBA mirror. The
// four BuildBCMirror* wrappers fill this and delegate to BuildBCMirrorCore.
struct BCMirrorDesc {
  ResourceType resource_type;
  plume::RenderTextureDimension tex_dim;
  plume::RenderTextureViewDimension view_dim;
  plume::RenderFormat format;
  u32 width;
  u32 height;
  u32 depth; // 0 for 2D/cube (GuestTexture::depth default), N volume
  u32 mip_levels;
  u32 array_size; // 1 for 2D/volume, 6 for cube
  plume::RenderTextureFlags flags;
  const char *caller_name;
};

struct BCSubresourceUpload {
  const void *data;
  size_t size;
  u32 width;            // footprint texel width
  u32 height;           // footprint texel height
  u32 fp_depth;         // footprint depth (1 for 2D/mip, N for volume)
  u32 row_width_texels; // PlacedFootprint rowWidth
  u32 subresource;      // mip index
  u32 array_index;      // array/face index (0 for non-cube)
};

// No committed=true: every subresource is copyTextureRegion-uploaded before
// first sample. Per-texture committed heaps for thousands of BC textures
// triggered TDR during load. Uploads precede AllocateSlot so a failure path
// deletes the texture without leaking a slot (GuestTexture has no destructor-
// side slot release).
GuestTexture *BuildBCMirrorCore(const BCMirrorDesc &d,
                                const BCSubresourceUpload *uploads,
                                u32 upload_count) {
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (!s.ready)
    return nullptr;
  BeginCommandList(s);
  if (!s.command_list_open)
    return nullptr;

  auto *t = new GuestTexture(d.resource_type);
  t->width = d.width;
  t->height = d.height;
  t->depth = d.depth;
  t->mipLevels = d.mip_levels;
  t->format = d.format;
  t->viewDimension = d.view_dim;

  plume::RenderTextureDesc desc;
  desc.dimension = d.tex_dim;
  desc.width = d.width;
  desc.height = d.height;
  desc.depth = (d.depth > 0) ? d.depth : 1;
  desc.mipLevels = d.mip_levels;
  desc.arraySize = d.array_size;
  desc.format = d.format;
  desc.flags = d.flags;
  desc.multisampling.sampleCount = plume::RenderSampleCount::COUNT_1;
  t->textureHolder = CreateHostTexture(s.device.get(), desc, "bc-mirror");
  t->texture = t->textureHolder.get();
  if (!t->texture) {
    delete t;
    return nullptr;
  }

  plume::RenderTextureViewDesc view_desc;
  view_desc.format = d.format;
  view_desc.dimension = d.view_dim;
  view_desc.mipLevels = d.mip_levels;
  t->textureView = t->texture->createTextureView(view_desc);

  std::vector<ConstantAllocation> allocs(upload_count);
  for (u32 i = 0; i < upload_count; ++i) {
    allocs[i] = UploadHostBytes(uploads[i].data,
                                static_cast<u32>(uploads[i].size), 0x200);
    if (!allocs[i].memory) {
      BD_ERROR("{}: upload of subresource {} ({} bytes) failed", d.caller_name,
               i, uploads[i].size);
      delete t;
      return nullptr;
    }
  }

  const u32 slot = AllocateSlot(s);
  if (slot == kInvalidDescriptorIndex) {
    BD_ERROR("{}: bindless heap full", d.caller_name);
    delete t;
    return nullptr;
  }
  s.texture_descriptor_set->setTexture(slot, t->texture,
                                       plume::RenderTextureLayout::SHADER_READ,
                                       t->textureView.get());
  t->descriptorIndex = slot;

  plume::RenderTextureBarrier pre(t->texture,
                                  plume::RenderTextureLayout::COPY_DEST);
  s.command_list->barriers(plume::RenderBarrierStage::COPY, &pre, 1);
  t->layout = plume::RenderTextureLayout::COPY_DEST;

  for (u32 i = 0; i < upload_count; ++i) {
    s.command_list->copyTextureRegion(
        plume::RenderTextureCopyLocation::Subresource(
            t->texture, uploads[i].subresource, uploads[i].array_index),
        plume::RenderTextureCopyLocation::PlacedFootprint(
            allocs[i].ref.ref, d.format, uploads[i].width, uploads[i].height,
            uploads[i].fp_depth, uploads[i].row_width_texels,
            allocs[i].ref.offset));
  }

  plume::RenderTextureBarrier post(t->texture,
                                   plume::RenderTextureLayout::SHADER_READ);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &post, 1);
  t->layout = plume::RenderTextureLayout::SHADER_READ;
  return t;
}

} // namespace

GuestTexture *BuildBCMirrorTexture(u32 width, u32 height, u32 format,
                                          const void *block_data,
                                          size_t block_data_size,
                                          u32 staging_row_bytes,
                                          u32 row_width_texels) {
  plume::RenderFormat fmt;
  if (!BCFormatFromGuestByte(format, fmt)) {
    BD_WARN("BuildBCMirrorTexture: unsupported BC format 0x{:X}", format);
    return nullptr;
  }
  BCMirrorDesc d{};
  d.resource_type = ResourceType::Texture;
  d.tex_dim = plume::RenderTextureDimension::TEXTURE_2D;
  d.view_dim = plume::RenderTextureViewDimension::TEXTURE_2D;
  d.format = fmt;
  d.width = width;
  d.height = height;
  d.depth = 0;
  d.mip_levels = 1;
  d.array_size = 1;
  d.flags = plume::RenderTextureFlag::NONE;
  d.caller_name = "BuildBCMirrorTexture";
  BCSubresourceUpload up{};
  up.data = block_data;
  up.size = block_data_size;
  up.width = width;
  up.height = height;
  up.fp_depth = 1;
  up.row_width_texels = row_width_texels;
  up.subresource = 0;
  up.array_index = 0;
  (void)staging_row_bytes;
  return BuildBCMirrorCore(d, &up, 1);
}

GuestTexture *BuildBCMirrorTexture2DMips(u32 width, u32 height,
                                                u32 format,
                                                const BCMipLevel *levels,
                                                u32 level_count) {
  if (!levels || level_count == 0)
    return nullptr;
  plume::RenderFormat fmt;
  if (!BCFormatFromGuestByte(format, fmt)) {
    BD_WARN("BuildBCMirrorTexture2DMips: unsupported format 0x{:X}", format);
    return nullptr;
  }
  BCMirrorDesc d{};
  d.resource_type = ResourceType::Texture;
  d.tex_dim = plume::RenderTextureDimension::TEXTURE_2D;
  d.view_dim = plume::RenderTextureViewDimension::TEXTURE_2D;
  d.format = fmt;
  d.width = width;
  d.height = height;
  d.depth = 0;
  d.mip_levels = level_count;
  d.array_size = 1;
  d.flags = plume::RenderTextureFlag::NONE;
  d.caller_name = "BuildBCMirrorTexture2DMips";
  std::vector<BCSubresourceUpload> ups(level_count);
  for (u32 i = 0; i < level_count; ++i) {
    ups[i].data = levels[i].data;
    ups[i].size = levels[i].size;
    ups[i].width = levels[i].width;
    ups[i].height = levels[i].height;
    ups[i].fp_depth = 1;
    ups[i].row_width_texels = levels[i].row_width_texels;
    ups[i].subresource = i;
    ups[i].array_index = 0;
  }
  return BuildBCMirrorCore(d, ups.data(), level_count);
}

GuestTexture *BuildBCMirrorTextureCube(u32 width, u32 height, u32 format,
                                              const u8 *faces[6],
                                              size_t face_byte_size,
                                              u32 staging_row_bytes,
                                              u32 row_width_texels) {
  plume::RenderFormat fmt;
  if (!BCFormatFromGuestByte(format, fmt)) {
    BD_WARN("BuildBCMirrorTextureCube: unsupported BC format 0x{:X}", format);
    return nullptr;
  }
  BCMirrorDesc d{};
  d.resource_type = ResourceType::Texture;
  d.tex_dim = plume::RenderTextureDimension::TEXTURE_2D;
  d.view_dim = plume::RenderTextureViewDimension::TEXTURE_CUBE;
  d.format = fmt;
  d.width = width;
  d.height = height;
  d.depth = 0;
  d.mip_levels = 1;
  d.array_size = 6;
  d.flags = plume::RenderTextureFlag::CUBE;
  d.caller_name = "BuildBCMirrorTextureCube";
  BCSubresourceUpload ups[6];
  for (int face = 0; face < 6; ++face) {
    ups[face].data = faces[face];
    ups[face].size = face_byte_size;
    ups[face].width = width;
    ups[face].height = height;
    ups[face].fp_depth = 1;
    ups[face].row_width_texels = row_width_texels;
    ups[face].subresource = 0;
    ups[face].array_index = static_cast<u32>(face);
  }
  (void)staging_row_bytes;
  return BuildBCMirrorCore(d, ups, 6);
}

GuestTexture *BuildBCMirrorTextureVolume(
    u32 width, u32 height, u32 depth, u32 format, const void *slice_data,
    size_t slice_data_size, u32 staging_row_bytes, u32 row_width_texels) {
  plume::RenderFormat fmt;
  if (!BCFormatFromGuestByte(format, fmt)) {
    BD_WARN("BuildBCMirrorTextureVolume: unsupported BC format 0x{:X}", format);
    return nullptr;
  }
  BCMirrorDesc d{};
  d.resource_type = ResourceType::VolumeTexture;
  d.tex_dim = plume::RenderTextureDimension::TEXTURE_3D;
  d.view_dim = plume::RenderTextureViewDimension::TEXTURE_3D;
  d.format = fmt;
  d.width = width;
  d.height = height;
  d.depth = depth;
  d.mip_levels = 1;
  d.array_size = 1;
  d.flags = plume::RenderTextureFlag::NONE;
  d.caller_name = "BuildBCMirrorTextureVolume";
  BCSubresourceUpload up{};
  up.data = slice_data;
  up.size = slice_data_size;
  up.width = width;
  up.height = height;
  up.fp_depth = depth;
  up.row_width_texels = row_width_texels;
  up.subresource = 0;
  up.array_index = 0;
  (void)staging_row_bytes;
  return BuildBCMirrorCore(d, &up, 1);
}

void UploadTextureFromMapped(GuestTexture *tex) {
  BD_CPU_ZONE("UploadTextureFromMapped");
  if (!tex || !tex->texture || !tex->mappedMemory)
    return;
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (!s.ready)
    return;
  BeginCommandList(s);
  if (!s.command_list_open)
    return;

  // Full-subresource CPU upload replaces any deferred resolve into this
  // texture, so drop the link and a later materialization cannot clobber it.
  DetachSourceSurfaceLocked(s, tex);

  const u32 pitch = ComputeTexturePitch(tex);
  const u32 texel = plume::RenderFormatSize(tex->format);
  if (!pitch || !texel)
    return;
  const u32 slice_pitch = pitch * tex->height;

  // No byte swap: X360 texture texels are not dword-swapped, so this is a
  // plain copy. Per-format channel order is not yet handled.
  ConstantAllocation up =
      UploadGuestBytes(tex->mappedMemory, slice_pitch, 0x200);
  if (!up.memory)
    return;

  plume::RenderTextureBarrier pre(tex->texture,
                                  plume::RenderTextureLayout::COPY_DEST);
  s.command_list->barriers(plume::RenderBarrierStage::COPY, &pre, 1);
  tex->layout = plume::RenderTextureLayout::COPY_DEST;

  // rowWidth is in texels: pitch (bytes) / per-texel size.
  s.command_list->copyTextureRegion(
      plume::RenderTextureCopyLocation::Subresource(tex->texture, 0),
      plume::RenderTextureCopyLocation::PlacedFootprint(
          up.ref.ref, tex->format, tex->width, tex->height, 1, pitch / texel,
          up.ref.offset));

  plume::RenderTextureBarrier post(tex->texture,
                                   plume::RenderTextureLayout::SHADER_READ);
  s.command_list->barriers(plume::RenderBarrierStage::GRAPHICS, &post, 1);
  tex->layout = plume::RenderTextureLayout::SHADER_READ;
}

} // namespace bd::gpu
