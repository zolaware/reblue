/**
 * @file    engine/guest_texture.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/guest_texture.h"

#include <rex/math.h>
#include <rex/xenos/texture_conversion.h>
#include <rex/xenos/texture_layout.h>
#include <rex/xenos/xenos.h>

namespace bd::engine {

namespace {

namespace tc = rex::xenos;
namespace tu = rex::xenos;
namespace xe = rex::xenos;

// Field values are transcribed from the stock UI atlases (d2anime\res\ic_item,
// camp\cfg\res\mark_config) and, for the format word, from the one shipped
// k_8_8_8_8 (map\...\dg03_cbr01).
constexpr size_t kPayloadOffset = 2048;
constexpr u32 kTexelBytes = 4;
constexpr u32 kTexelBytesLog2 = 2;
// The tiled footprint pads both axes to whole 32-block tiles, and the fetch
// constant states the padded width in units of 32 texels.
constexpr u32 kTileEdge = 32;

constexpr u32 kResourceCommon = 0x00000003;
constexpr u32 kResourceRefCount = 0x00000001;
constexpr u32 kFlushInit = 0xFFFF0000;
// Texture type fetch constant, tiled.
constexpr u32 kFetchTiledTexture = 0x80000002;
// k_8_8_8_8 with k8in32 endianness. The base address stays zero here, and
// XGOffsetBaseTextureAddress patches it in once the payload has been copied.
constexpr u32 kFetchFormat = 0x00000086;
// Identity swizzle, unsigned, no exponent bias, what every stock UI texture
// carries.
constexpr u32 kFetchSwizzle = 0x00000D10;
// 2D, no packed mip tail, mip address zero.
constexpr u32 kFetchDimension = 0x00000200;

void PutBe32(std::vector<u8> &out, u32 v) {
  out.push_back(static_cast<u8>(v >> 24));
  out.push_back(static_cast<u8>(v >> 16));
  out.push_back(static_cast<u8>(v >> 8));
  out.push_back(static_cast<u8>(v));
}

} // namespace

// The tiling is the exact inverse of the native texture mirror's gather, with
// the same offset function, the same padded pitch and the same endian swap, so
// what the mirror reads back is what went in, texel for texel.
std::vector<u8> BuildGuestTexture(const std::vector<u8> &rgba, u32 width,
                                  u32 height) {
  const u32 pitch = rex::RoundUp(width, kTileEdge);
  const u32 tiledRows = rex::RoundUp(height, kTileEdge);
  const u32 payloadBytes = pitch * tiledRows * kTexelBytes;

  std::vector<u8> blob;
  blob.reserve(kPayloadOffset + payloadBytes);
  PutBe32(blob, payloadBytes);
  PutBe32(blob, kResourceCommon);
  PutBe32(blob, kResourceRefCount);
  PutBe32(blob, 0);          // Fence
  PutBe32(blob, 0);          // ReadFence
  PutBe32(blob, 0);          // Identifier
  PutBe32(blob, kFlushInit); // BaseFlush
  PutBe32(blob, kFlushInit); // MipFlush
  PutBe32(blob, kFetchTiledTexture | ((pitch / kTileEdge) << 22));
  PutBe32(blob, kFetchFormat);
  PutBe32(blob, (width - 1u) | ((height - 1u) << 13));
  PutBe32(blob, kFetchSwizzle);
  PutBe32(blob, 0); // no mip chain
  PutBe32(blob, kFetchDimension);
  blob.resize(kPayloadOffset + payloadBytes, 0);

  u8 *payload = blob.data() + kPayloadOffset;
  for (u32 y = 0; y < height; ++y) {
    for (u32 x = 0; x < width; ++x) {
      const i32 offset = tu::GetTiledOffset2D(
          static_cast<i32>(x), static_cast<i32>(y), pitch, kTexelBytesLog2);
      if (offset < 0 || static_cast<u32>(offset) + kTexelBytes > payloadBytes) {
        continue;
      }
      tc::CopySwapBlock(xe::Endian::k8in32, payload + offset,
                        rgba.data() + (size_t(y) * width + x) * kTexelBytes,
                        kTexelBytes);
    }
  }
  return blob;
}

} // namespace bd::engine
