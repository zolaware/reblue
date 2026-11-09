/**
 * @file    gpu/shaders/shader_linker.cpp
 * @brief   Runtime spec constant link: caches the g_SpecConstants() library per
 *          value and links it into the shader's DXIL library.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/shaders/shader_linker.h"

#include <mutex>
#include <unordered_map>

#include "core/logging.h"
#include "gpu/shaders/dxc_link.h"

namespace bd::gpu {
namespace {

// Protects only the library cache below. The link itself runs unlocked: every
// call creates its own DXC instances, which is the supported concurrent usage,
// so callers link in parallel.
std::mutex g_mutex;

// unordered_map never invalidates element references, so the returned pointer
// stays valid once the caller drops g_mutex.
std::unordered_map<u32, std::vector<u8>> g_spec_lib_dxil;

const std::vector<u8> *SpecConstantLib(u32 value) {
  std::lock_guard lock(g_mutex);
  auto it = g_spec_lib_dxil.find(value);
  if (it != g_spec_lib_dxil.end())
    return it->second.empty() ? nullptr : &it->second;

  auto inserted = g_spec_lib_dxil.emplace(value, CompileSpecConstantLib(value));
  if (inserted.first->second.empty()) {
    REXBD_ERROR("shader_linker: g_SpecConstants({}) library compile failed",
             value);
    return nullptr;
  }
  return &inserted.first->second;
}

} // namespace

std::vector<u8> LinkSpecConstant(const u8 *libraryDxil, u32 libraryDxilSize,
                                 bool isPixelShader, u32 specConstants) {
  const std::vector<u8> *specLib = SpecConstantLib(specConstants);
  if (!specLib)
    return {};

  std::vector<u8> linked = LinkSpecConstantLib(
      libraryDxil, libraryDxilSize, specLib->data(), specLib->size(),
      isPixelShader ? L"ps_6_0" : L"vs_6_0");
  if (linked.empty()) {
    REXBD_ERROR("shader_linker: link failed (specConstants={})", specConstants);
  }
  return linked;
}

} // namespace bd::gpu
