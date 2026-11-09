/**
 * @file    gpu/vertex_declaration.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/vertex_declaration.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <xxhash.h>

#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/device.h"
#include "gpu/format.h"
#include "gpu/host_resource_heap.h"
#include "gpu/pipeline/pso_recorder.h"
#include "gpu/shaders/shader_constants.h"

namespace bd::gpu {

namespace {
// Element hash -> live GuestVertexDeclaration, so the PSO recorder/replay can
// resolve a serialized decl hash back to its host object. Written by
// CreateVertexDeclaration, read by FindVertexDeclarationByHash on the
// background replay thread, hence its own mutex.
std::mutex g_declByHashMutex;
std::unordered_map<u64, GuestVertexDeclaration *> g_declByHash;
// The table the recompiler tags [[vk::location]] from.
struct Location {
  D3DDeclUsage usage;
  u32 usageIndex;
  u32 location;
};
#define REBLUE_VERTEX_LOCATION_ROW(u, i, l) {D3DDeclUsage::k##u, i, l},
constexpr Location kLocations[] = {
    REBLUE_VERTEX_INPUT_LOCATIONS(REBLUE_VERTEX_LOCATION_ROW)};
#undef REBLUE_VERTEX_LOCATION_ROW

constexpr u32 kNoLocation = ~u32{0};

constexpr auto resolve_location = [](D3DDeclUsage usage, u32 usage_index) {
  for (const auto &loc : kLocations) {
    if (loc.usage == usage && loc.usageIndex == usage_index) {
      return loc.location;
    }
  }
  return kNoLocation;
};

// bd_2d_blit_vs.hlsl writes these three [[vk::location]] values by hand.
static_assert(resolve_location(D3DDeclUsage::kPosition, 0) == 0 &&
              resolve_location(D3DDeclUsage::kTexCoord, 0) == 7 &&
              resolve_location(D3DDeclUsage::kColor, 0) == 10);
} // namespace

GuestVertexDeclaration *CreateVertexDeclaration(GuestVertexElement *elements) {
  auto decl_type_of = [](const GuestVertexElement &e) {
    return static_cast<D3DDeclType>(u32(e.type));
  };

  u32 element_count = 0;
  for (auto *e = elements;
       e->stream != 0xFF && decl_type_of(*e) != D3DDeclType::kUnused; ++e) {
    ++element_count;
  }

  // The guest D3DDevice_CreateVertexDeclaration treats the caller's
  // element array as const, only reading ->Stream and memcpy'ing it, and BD
  // passes some decls from read-only .rdata (the CRI/Sofdec movie quad decl at
  // word_82069D80), so normalizing padding in place AVs. Zero the padding in a
  // local copy and hash that for a stable cache key.
  std::vector<GuestVertexElement> normalized(elements,
                                             elements + element_count + 1);
  for (auto &ne : normalized)
    ne.padding = 0;

  const u64 hash = XXH3_64bits(normalized.data(),
                               element_count * sizeof(GuestVertexElement));

  auto *decl = HostResourceHeap::Alloc<GuestVertexDeclaration>();
  if (!decl) {
    REXLOG_ERROR(bd, "CreateVertexDeclaration: HostResourceHeap::Alloc failed "
             "(elements={}, hash=0x{:X}), returning null decl",
             element_count, hash);
    return nullptr;
  }
  decl->hash = hash;

  // Any 16-bit-packed format (SHORT2/4, *_N, USHORT2/4N, FLOAT16_2/4) needs
  // the shader-side .yxwz swap to undo the engine's bswap32 of physical VBs.
  // Binding the natural IA format (SNORM for SHORT4N, and so on) decodes to
  // the float4 the shader declares, and the shader swaps via
  // swapFloats(g_SwappedXxx, value, semanticIndex).
  auto needs_bswap_swap = [](D3DDeclType type) {
    switch (type) {
    case D3DDeclType::kShort2:
    case D3DDeclType::kShort4:
    case D3DDeclType::kShort2N:
    case D3DDeclType::kShort4N:
    case D3DDeclType::kUShort2N:
    case D3DDeclType::kUShort4N:
    case D3DDeclType::kFloat16_2:
    case D3DDeclType::kFloat16_4:
      return true;
    default:
      return false;
    }
  };

  std::vector<plume::RenderInputElement> input_elements;
  for (auto *e = elements;
       e->stream != 0xFF && decl_type_of(*e) != D3DDeclType::kUnused; ++e) {
    const D3DDeclType type = decl_type_of(*e);
    const auto usage = static_cast<D3DDeclUsage>(e->usage);
    const u32 usage_index = e->usageIndex;
    const u32 stream = e->stream;

    plume::RenderInputElement input;
    input.semanticName = ConvertDeclUsage(usage);
    input.semanticIndex = usage_index;
    input.location = resolve_location(usage, usage_index);
    input.format = ConvertDeclType(type);
    input.slotIndex = stream;
    input.alignedByteOffset = e->offset;

    // MoltenVK forces an integer-typed shader input only for the unsigned
    // 8/16-bit classes (rdar://45922847). Signed 16-bit escapes that, so plain
    // SINT survives against XenosRecomp's float4 input and Metal does the
    // non-normalized int->float conversion itself. UByte4/UByte4Alt has no such
    // dodge: check Graine25 if that class ever renders wrong.
    if (g_mvk && usage != D3DDeclUsage::kBlendIndices) {
      switch (type) {
      case D3DDeclType::kShort2:
        input.format = plume::RenderFormat::R16G16_SINT;
        break;
      case D3DDeclType::kShort4:
        input.format = plume::RenderFormat::R16G16B16A16_SINT;
        break;
      default:
        break;
      }
    }

    switch (usage) {
    case D3DDeclUsage::kPosition:
      if (usage_index == 1)
        decl->indexVertexStream = stream;
      if (needs_bswap_swap(type)) {
        decl->swappedPositions |= 1u << usage_index;
      }
      break;
    // The tangent basis decodes identically for all three semantics, only the
    // swap mask differs. The shader inputs are float4 and 32-bit attribute
    // fetches are raw bit copies in either numeric class, but Vulkan validates
    // the class against the shader input (VUID 08733), so bind FLOAT3/DEC3N as
    // float there. D3D12 keeps UINT.
    case D3DDeclUsage::kNormal:
    case D3DDeclUsage::kTangent:
    case D3DDeclUsage::kBinormal: {
      u32 &swapped = usage == D3DDeclUsage::kNormal    ? decl->swappedNormals
                     : usage == D3DDeclUsage::kTangent ? decl->swappedTangents
                                                       : decl->swappedBinormals;
      if (type == D3DDeclType::kFloat3) {
        input.format = g_vulkan ? plume::RenderFormat::R32G32B32_FLOAT
                                : plume::RenderFormat::R32G32B32_UINT;
      } else if (type == D3DDeclType::kDec3NAlt ||
                 type == D3DDeclType::kDec3NAlt2) {
        // X360 packed 10/11/11 normal: one 32-bit lane, decoded by the
        // kSpecConstantR11G11B10Normal branch in tfetchR11G11B10.
        decl->hasR11G11B10Normal = true;
        if (g_vulkan)
          input.format = plume::RenderFormat::R32_FLOAT;
      } else if (needs_bswap_swap(type)) {
        swapped |= 1u << usage_index;
      }
      break;
    }
    case D3DDeclUsage::kBlendWeight:
      if (needs_bswap_swap(type)) {
        decl->swappedBlendWeights |= 1u << usage_index;
      }
      break;
    case D3DDeclUsage::kTexCoord:
      if (needs_bswap_swap(type)) {
        decl->swappedTexcoords |= 1u << usage_index;
      }
      // Metal already converts raw signed 16-bit to float, so MoltenVK needs
      // no shader-side recovery. Vulkan and D3D12 keep UINT plus shader-side
      // sign extension.
      if (type == D3DDeclType::kShort2) {
        if (!g_mvk) {
          input.format = plume::RenderFormat::R16G16_UINT;
          decl->sintTexcoords |= 1u << usage_index;
        }
      } else if (type == D3DDeclType::kShort4) {
        if (!g_mvk) {
          input.format = plume::RenderFormat::R16G16B16A16_UINT;
          decl->sintTexcoords |= 1u << usage_index;
        }
      }
      break;
    default:
      break;
    }

    if (stream < std::size(decl->vertexStreams)) {
      decl->vertexStreams[stream] = true;
    }
    // Off the table means no shader declares it, so on Vulkan it would only
    // add an attribute at an invalid location.
    if (g_vulkan && input.location == kNoLocation) {
      continue;
    }
    input_elements.push_back(input);
  }

  // Every table semantic this decl omits still needs an element: D3D12 wants
  // one per declared VS input (STATE_CREATION MISSINGELEMENT #65) and Vulkan
  // wants an attribute at every declared location. These read slot 15, which
  // is zero-filled.
  auto add_synthetic = [&](D3DDeclUsage usage, u32 usage_index) {
    const char *name = ConvertDeclUsage(usage);
    // D3D12 links input layout elements to the VS input signature by semantic
    // name + index, and 'location' is Vulkan-only, so dedup on that key.
    for (const auto &existing : input_elements) {
      if (existing.semanticIndex == usage_index && existing.semanticName &&
          std::strcmp(existing.semanticName, name) == 0) {
        return;
      }
    }
    // Every shader input is float4, so a filler must be float class too. Its
    // lane count decides what the unwritten lanes read.
    plume::RenderFormat format = plume::RenderFormat::R32_FLOAT;
    switch (usage) {
    case D3DDeclUsage::kPosition:
      // POSITION/1..4 are BD's 'constrain' hardware vertex blend streams, used
      // by the _vs_env shaders behind the g_bConstrain0/1/2 bool constants. A
      // static mesh's decl omits them, so they read the slot-15 null buffer.
      // float4 makes every lane read 0, including the .w blend weight, and a
      // zero weight disables each contribution through the shader's
      // 'r*.w != 0.0' gate, the right no-blend result. R32_FLOAT would leave
      // .w defaulted to 1.0.
      format = plume::RenderFormat::R32G32B32A32_FLOAT;
      break;
    case D3DDeclUsage::kNormal:
    case D3DDeclUsage::kTangent:
      format = plume::RenderFormat::R32G32B32_FLOAT;
      break;
    default:
      break;
    }
    input_elements.push_back(plume::RenderInputElement(
        name, usage_index, resolve_location(usage, usage_index), format, 15,
        0));
  };
  for (const auto &loc : kLocations)
    add_synthetic(loc.usage, loc.usageIndex);

  decl->inputElements =
      std::make_unique<plume::RenderInputElement[]>(input_elements.size());
  std::copy(input_elements.begin(), input_elements.end(),
            decl->inputElements.get());
  decl->inputElementCount = static_cast<u32>(input_elements.size());

  decl->vertexElements =
      std::make_unique<GuestVertexElement[]>(element_count + 1);
  std::copy(normalized.begin(), normalized.end(), decl->vertexElements.get());
  decl->vertexElementCount = element_count + 1;

  {
    std::lock_guard<std::mutex> lock(g_declByHashMutex);
    // First creation wins: identical element arrays hash equal and build an
    // equivalent declaration, so a later duplicate need not overwrite.
    g_declByHash.try_emplace(hash, decl);
  }

  // Let boot cache replay enqueue any pending PSO waiting on this decl hash.
  OnVertexDeclarationCreated(hash);

  return decl;
}

GuestVertexDeclaration *FindVertexDeclarationByHash(u64 hash) {
  std::lock_guard<std::mutex> lock(g_declByHashMutex);
  auto it = g_declByHash.find(hash);
  return it != g_declByHash.end() ? it->second : nullptr;
}

} // namespace bd::gpu
