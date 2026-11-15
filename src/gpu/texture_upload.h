/**
 * @file    gpu/texture_upload.h
 * @brief   Host mirror textures built from already-untiled guest data, and the
 *          mapped-memory upload path.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

#include "gpu/resources.h"

namespace bd::gpu {

// Copy a texture's mappedMemory scratch into subresource 0 through the upload
// ring and copyTextureRegion.
void UploadTextureFromMapped(GuestTexture *tex);

// One already-untiled, 256-row-aligned mip level.
struct BCMipLevel {
  const void *data;
  size_t size;
  u32 width;
  u32 height;
  u32 row_width_texels; // PlacedFootprint rowWidth for this level
};

// Every builder below takes data that is already untiled with 256-byte-aligned
// rows, and returns a heap-new GuestTexture the caller owns, or nullptr.
GuestTexture *BuildBCMirrorTexture(u32 width, u32 height, u32 format,
                                   const void *block_data,
                                   size_t block_data_size,
                                   u32 row_width_texels);

// levels[0] is the base.
GuestTexture *BuildBCMirrorTexture2DMips(u32 width, u32 height, u32 format,
                                         const BCMipLevel *levels,
                                         u32 level_count);

// arraySize=6 CUBE.
GuestTexture *BuildBCMirrorTextureCube(u32 width, u32 height, u32 format,
                                       const u8 *faces[6],
                                       size_t face_byte_size,
                                       u32 row_width_texels);

// Slices contiguous in slice_data, slice 0 first, uploaded in one copy.
GuestTexture *BuildBCMirrorTextureVolume(u32 width, u32 height, u32 depth,
                                         u32 format, const void *slice_data,
                                         size_t slice_data_size,
                                         u32 row_width_texels);

} // namespace bd::gpu
