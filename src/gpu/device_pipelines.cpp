/**
 * @file    gpu/device_pipelines.cpp
 * @brief   Null texture descriptors, the pipeline layout every draw shares,
 *          and the copy and resolve pipelines built from the renderer's own
 *          shader blobs.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/device.h"

#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>

#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/bindless_allocator.h"
#include "gpu/format.h"
#include "gpu/settings.h"

#if defined(REBLUE_D3D12)
#include "src/gpu/shaders/hlsl/copy_color_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/copy_depth_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/copy_vs.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/gamma_correction_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/pfx_occlusion_count_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_2x.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_4x.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_8x.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_2x.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_4x.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_8x.hlsl.dxil.h"
#else
#include "src/gpu/shaders/hlsl/copy_color_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/copy_depth_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/copy_vs.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/gamma_correction_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/pfx_occlusion_count_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_2x.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_4x.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_color_8x.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_2x.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_4x.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/resolve_msaa_depth_8x.hlsl.spirv.h"
#endif

namespace bd::gpu {

namespace {

bool BuildNullTextureDescriptors(VideoState &s) {
  for (u32 i = 0; i < kNullTextureDescriptorCount; ++i) {
    plume::RenderTextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.format = plume::RenderFormat::R8_UNORM;
    desc.flags = plume::RenderTextureFlag::NONE;

    plume::RenderTextureViewDesc view_desc;
    view_desc.format = desc.format;
    view_desc.mipLevels = 1;
    view_desc.componentMapping = plume::RenderComponentMapping(
        plume::RenderSwizzle::ZERO, plume::RenderSwizzle::ZERO,
        plume::RenderSwizzle::ZERO, plume::RenderSwizzle::ZERO);

    switch (i) {
    case kNullTexture2DDescriptorIndex:
      desc.dimension = plume::RenderTextureDimension::TEXTURE_2D;
      view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_2D;
      break;
    case kNullTexture3DDescriptorIndex:
      desc.dimension = plume::RenderTextureDimension::TEXTURE_3D;
      view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_3D;
      break;
    case kNullTextureCubeDescriptorIndex:
      desc.dimension = plume::RenderTextureDimension::TEXTURE_2D;
      desc.arraySize = 6;
      desc.flags = plume::RenderTextureFlag::CUBE;
      view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_CUBE;
      break;
    default:
      return false;
    }
    desc.committed = true; // avoid placed-resource uninit GBV debug fill

    auto texture = CreateHostTexture(s.device.get(), desc, "null-descriptor");
    if (!texture) {
      REXBD_ERROR("Create null texture descriptor {} failed", i);
      return false;
    }
    auto view = texture->createTextureView(view_desc);
    if (!view) {
      REXBD_ERROR("Create null texture view descriptor {} failed", i);
      return false;
    }
    s.texture_descriptor_set->setTexture(
        i, texture.get(), plume::RenderTextureLayout::SHADER_READ, view.get());
    s.null_textures[i] = std::move(texture);
    s.null_texture_views[i] = std::move(view);
  }
  return true;
}

} // namespace

bool BuildPipelineLayout(VideoState &s) {
  plume::RenderPipelineLayoutBuilder layout_builder;
  layout_builder.begin(false, true);

  plume::RenderDescriptorSetBuilder tex_set_builder;
  tex_set_builder.begin();
  tex_set_builder.addTexture(0, kBindlessTextureCount);
  tex_set_builder.end(true, kBindlessTextureCount);

  s.texture_descriptor_set = tex_set_builder.create(s.device.get());
  if (!s.texture_descriptor_set) {
    REXBD_ERROR("Plume createDescriptorSet for bindless textures failed");
    return false;
  }
  s.descriptor_slot_used.assign(kBindlessTextureCount, false);
  for (u32 i = 0; i < kNullTextureDescriptorCount; ++i) {
    s.descriptor_slot_used[i] = true;
  }
  if (!BuildNullTextureDescriptors(s)) {
    return false;
  }

  // space 0 (Texture2D[]), space 1 (Texture3D[]), space 2 (TextureCube[]).
  // All three runtime-bind to the same physical s.texture_descriptor_set.
  layout_builder.addDescriptorSet(tex_set_builder);
  layout_builder.addDescriptorSet(tex_set_builder);
  layout_builder.addDescriptorSet(tex_set_builder);

  plume::RenderDescriptorSetBuilder sampler_set_builder;
  sampler_set_builder.begin();
  sampler_set_builder.addSampler(0, kBindlessSamplerCount);
  sampler_set_builder.end(true, kBindlessSamplerCount);

  s.sampler_descriptor_set = sampler_set_builder.create(s.device.get());
  if (!s.sampler_descriptor_set) {
    REXBD_ERROR("Plume createDescriptorSet for bindless samplers failed");
    return false;
  }
  s.sampler_descriptor_used.assign(kBindlessSamplerCount, false);
  s.sampler_descriptor_used[0] = true; // reserved for default sampler

  // Default sampler at slot 0: LINEAR/CLAMP = D3D9 reset state.
  plume::RenderSamplerDesc default_desc;
  default_desc.minFilter = plume::RenderFilter::LINEAR;
  default_desc.magFilter = plume::RenderFilter::LINEAR;
  default_desc.mipmapMode = plume::RenderMipmapMode::LINEAR;
  default_desc.addressU = plume::RenderTextureAddressMode::CLAMP;
  default_desc.addressV = plume::RenderTextureAddressMode::CLAMP;
  default_desc.addressW = plume::RenderTextureAddressMode::CLAMP;
  s.default_sampler = s.device->createSampler(default_desc);
  if (!s.default_sampler) {
    REXBD_ERROR("Plume createSampler for default sampler failed");
    return false;
  }
  s.sampler_descriptor_set->setSampler(0, s.default_sampler.get());

  // Point sampler at slot 1: reserved for host fullscreen blits.
  // Slot 0 (LINEAR) on a same-size blit blurs the scene every frame.
  s.sampler_descriptor_used[1] = true;
  plume::RenderSamplerDesc point_desc;
  point_desc.minFilter = plume::RenderFilter::NEAREST;
  point_desc.magFilter = plume::RenderFilter::NEAREST;
  point_desc.mipmapMode = plume::RenderMipmapMode::NEAREST;
  point_desc.addressU = plume::RenderTextureAddressMode::CLAMP;
  point_desc.addressV = plume::RenderTextureAddressMode::CLAMP;
  point_desc.addressW = plume::RenderTextureAddressMode::CLAMP;
  s.point_sampler = s.device->createSampler(point_desc);
  if (!s.point_sampler) {
    REXBD_ERROR("Plume createSampler for point sampler failed");
    return false;
  }
  s.sampler_descriptor_set->setSampler(1, s.point_sampler.get());

  layout_builder.addDescriptorSet(sampler_set_builder);

#if defined(REBLUE_D3D12)
  // VS/PS/Shared CBVs.
  layout_builder.addRootDescriptor(
      0, 4, plume::RenderRootDescriptorType::CONSTANT_BUFFER);
  layout_builder.addRootDescriptor(
      1, 4, plume::RenderRootDescriptorType::CONSTANT_BUFFER);
  layout_builder.addRootDescriptor(
      2, 4, plume::RenderRootDescriptorType::CONSTANT_BUFFER);
  // Sun occlusion counter UAV (u0, space5), bound only for the lens flare
  // occlusion count draw, unused by every normal draw.
  layout_builder.addRootDescriptor(
      0, 5, plume::RenderRootDescriptorType::UNORDERED_ACCESS);

  // register(b3, space4) in the copy helpers, PIXEL-only. 4 dwords:
  // [0] primary SRV slot, [1] secondary SRV slot, [2]/[3] per-pass floats.
  layout_builder.addPushConstant(3, 4, sizeof(u32) * 4,
                                 plume::RenderShaderStageFlag::PIXEL);
#else
  // Single VERTEX|PIXEL push range (see the binding model note on VideoState):
  // guest VS/PS/Shared device addresses at [0,24), copy helper block at
  // [24,40).
  layout_builder.addPushConstant(0, 4,
                                 kCopyPushConstantByteOffset + sizeof(u32) * 4,
                                 plume::RenderShaderStageFlag::VERTEX |
                                     plume::RenderShaderStageFlag::PIXEL);

  // Sun occlusion counter UAV, set 4: one set per frame slot, pointed at the
  // lazily created counter by Occlusion::Begin.
  plume::RenderDescriptorSetBuilder occlusion_set_builder;
  occlusion_set_builder.begin();
  occlusion_set_builder.addReadWriteByteAddressBuffer(0);
  occlusion_set_builder.end();
  for (u32 i = 0; i < kNumFrames; ++i) {
    s.occlusion_descriptor_set[i] =
        occlusion_set_builder.create(s.device.get());
    if (!s.occlusion_descriptor_set[i]) {
      REXBD_ERROR("Plume createDescriptorSet for occlusion counter failed");
      return false;
    }
  }
  layout_builder.addDescriptorSet(occlusion_set_builder);
#endif

  layout_builder.end();
  s.pipeline_layout = layout_builder.create(s.device.get());
  if (!s.pipeline_layout) {
    REXBD_ERROR("Plume createPipelineLayout for main pipeline failed");
    return false;
  }
  return true;
}

bool BuildCopyPipeline(VideoState &s) {
  s.copy_vs = s.device->createShader(REBLUE_SHADER_BLOB(copy_vs), "main",
                                     kHostShaderFormat);
  s.copy_color_ps = s.device->createShader(REBLUE_SHADER_BLOB(copy_color_ps),
                                           "main", kHostShaderFormat);
  s.copy_depth_ps = s.device->createShader(REBLUE_SHADER_BLOB(copy_depth_ps),
                                           "main", kHostShaderFormat);
  s.gamma_correction_ps = s.device->createShader(
      REBLUE_SHADER_BLOB(gamma_correction_ps), "main", kHostShaderFormat);
  s.occlusion_count_ps = s.device->createShader(
      REBLUE_SHADER_BLOB(pfx_occlusion_count_ps), "main", kHostShaderFormat);
  if (!s.copy_vs || !s.copy_color_ps || !s.copy_depth_ps ||
      !s.gamma_correction_ps || !s.occlusion_count_ps) {
    REXBD_ERROR("Plume createShader for copy helpers failed");
    return false;
  }

  s.resolve_msaa_color_ps[0] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_color_2x), "main", kHostShaderFormat);
  s.resolve_msaa_color_ps[1] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_color_4x), "main", kHostShaderFormat);
  s.resolve_msaa_color_ps[2] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_color_8x), "main", kHostShaderFormat);
  s.resolve_msaa_depth_ps[0] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_depth_2x), "main", kHostShaderFormat);
  s.resolve_msaa_depth_ps[1] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_depth_4x), "main", kHostShaderFormat);
  s.resolve_msaa_depth_ps[2] = s.device->createShader(
      REBLUE_SHADER_BLOB(resolve_msaa_depth_8x), "main", kHostShaderFormat);
  for (int i = 0; i < 3; i++) {
    if (!s.resolve_msaa_color_ps[i] || !s.resolve_msaa_depth_ps[i]) {
      REXBD_ERROR("Plume createShader for MSAA resolve helpers failed");
      return false;
    }
  }

  plume::RenderGraphicsPipelineDesc pipe_desc;
  pipe_desc.pipelineLayout = s.pipeline_layout.get();
  pipe_desc.vertexShader = s.copy_vs.get();
  pipe_desc.pixelShader = s.copy_color_ps.get();
  pipe_desc.depthFunction = plume::RenderComparisonFunction::ALWAYS;
  pipe_desc.depthEnabled = false;
  pipe_desc.depthWriteEnabled = false;
  pipe_desc.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  pipe_desc.cullMode = plume::RenderCullMode::NONE;
  pipe_desc.renderTargetCount = 1;
  pipe_desc.renderTargetFormat[0] = plume::RenderFormat::B8G8R8A8_UNORM;
  pipe_desc.renderTargetBlend[0] = plume::RenderBlendDesc::Copy();
  pipe_desc.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  s.copy_color_pipeline =
      CreateHostGraphicsPipeline(s.device.get(), pipe_desc, "copy-color");
  if (!s.copy_color_pipeline) {
    REXBD_ERROR("Plume createGraphicsPipeline for copy_color failed");
    return false;
  }

  // Present-time gamma pass: copy_color plus a pow(color, Gamma) PS.
  pipe_desc.pixelShader = s.gamma_correction_ps.get();
  s.gamma_correction_pipeline =
      CreateHostGraphicsPipeline(s.device.get(), pipe_desc, "gamma-correction");
  if (!s.gamma_correction_pipeline) {
    REXBD_ERROR("Plume createGraphicsPipeline for gamma_correction failed");
    return false;
  }

  // Depth resolve PSOs are built lazily per dst depth format by
  // GetOrCreateCopyDepthPipeline: D3D12 requires the PSO depthStencilFormat to
  // match the bound DSV exactly, so one init-time PSO cannot cover all formats.
  return true;
}

plume::RenderPipeline *GetOrCreateCopyDepthPipeline(VideoState &s,
                                                    plume::RenderFormat fmt) {
  if (!IsDepthFormat(fmt))
    return nullptr;
  auto it = s.copy_depth_pipelines_by_format.find(fmt);
  if (it != s.copy_depth_pipelines_by_format.end())
    return it->second.get();
  plume::RenderGraphicsPipelineDesc desc;
  desc.pipelineLayout = s.pipeline_layout.get();
  desc.vertexShader = s.copy_vs.get();
  desc.pixelShader = s.copy_depth_ps.get();
  desc.depthFunction = plume::RenderComparisonFunction::ALWAYS;
  desc.depthEnabled = true;
  desc.depthWriteEnabled = true;
  desc.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  desc.cullMode = plume::RenderCullMode::NONE;
  desc.renderTargetCount = 0;
  desc.depthTargetFormat = fmt;
  auto pso = CreateHostGraphicsPipeline(s.device.get(), desc, "copy-depth");
  if (!pso) {
    REXBD_ERROR("Plume createGraphicsPipeline for copy_depth (fmt={}) failed",
             static_cast<int>(fmt));
    return nullptr;
  }
  auto *raw = pso.get();
  s.copy_depth_pipelines_by_format.emplace(fmt, std::move(pso));
  return raw;
}

// Get-or-create a copy color pipeline whose RT format matches 'format'.
// D3DDevice_Resolve targets can be any format, and one pipeline cannot cover
// all of them, so the pipelines are cached per format.
plume::RenderPipeline *GetOrCreateResolvePipeline(VideoState &s,
                                                  plume::RenderFormat format) {
  auto it = s.resolve_pipelines_by_format.find(format);
  if (it != s.resolve_pipelines_by_format.end())
    return it->second.get();
  plume::RenderGraphicsPipelineDesc desc;
  desc.pipelineLayout = s.pipeline_layout.get();
  desc.vertexShader = s.copy_vs.get();
  desc.pixelShader = s.copy_color_ps.get();
  desc.depthFunction = plume::RenderComparisonFunction::ALWAYS;
  desc.depthEnabled = false;
  desc.depthWriteEnabled = false;
  desc.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  desc.cullMode = plume::RenderCullMode::NONE;
  desc.renderTargetCount = 1;
  desc.renderTargetFormat[0] = format;
  desc.renderTargetBlend[0] = plume::RenderBlendDesc::Copy();
  desc.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  auto pipeline = CreateHostGraphicsPipeline(s.device.get(), desc, "resolve");
  if (!pipeline)
    return nullptr;
  auto *raw = pipeline.get();
  s.resolve_pipelines_by_format.emplace(format, std::move(pipeline));
  return raw;
}

// Sample count (a power-of-two bit) -> resolve shader tier index, or -1.
namespace {
int MsaaTierIndex(plume::RenderSampleCounts count) {
  switch (count) {
  case plume::RenderSampleCount::COUNT_2:
    return 0;
  case plume::RenderSampleCount::COUNT_4:
    return 1;
  case plume::RenderSampleCount::COUNT_8:
    return 2;
  default:
    return -1;
  }
}
} // namespace

// Get-or-create the MSAA resolve pipeline for a (dst format, source sample
// count, depth?) combination. Renders single-sample (the resolve output is
// not multisampled), reading the MS source as a Texture2DMS SRV.
plume::RenderPipeline *
GetOrCreateResolveMSAAPipeline(VideoState &s, plume::RenderFormat dst_format,
                               plume::RenderSampleCounts src_samples,
                               bool depth) {
  const int tier = MsaaTierIndex(src_samples);
  if (tier < 0)
    return nullptr;
  const u64 key = (static_cast<u64>(dst_format) << 8) |
                  (static_cast<u64>(tier) << 1) | (depth ? 1u : 0u);
  auto it = s.resolve_msaa_pipelines.find(key);
  if (it != s.resolve_msaa_pipelines.end())
    return it->second.get();

  plume::RenderGraphicsPipelineDesc desc;
  desc.pipelineLayout = s.pipeline_layout.get();
  desc.vertexShader = s.copy_vs.get();
  desc.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  desc.cullMode = plume::RenderCullMode::NONE;
  if (depth) {
    desc.pixelShader = s.resolve_msaa_depth_ps[tier].get();
    desc.depthFunction = plume::RenderComparisonFunction::ALWAYS;
    desc.depthEnabled = true;
    desc.depthWriteEnabled = true;
    desc.renderTargetCount = 0;
    desc.depthTargetFormat = dst_format;
  } else {
    desc.pixelShader = s.resolve_msaa_color_ps[tier].get();
    desc.depthFunction = plume::RenderComparisonFunction::ALWAYS;
    desc.depthEnabled = false;
    desc.depthWriteEnabled = false;
    desc.renderTargetCount = 1;
    desc.renderTargetFormat[0] = dst_format;
    desc.renderTargetBlend[0] = plume::RenderBlendDesc::Copy();
    desc.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  }
  auto pipeline =
      CreateHostGraphicsPipeline(s.device.get(), desc, "resolve-msaa");
  if (!pipeline)
    return nullptr;
  auto *raw = pipeline.get();
  s.resolve_msaa_pipelines.emplace(key, std::move(pipeline));
  return raw;
}

} // namespace bd::gpu
