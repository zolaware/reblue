/**
 * @file    gpu/imgui_overlay_drawer.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/imgui_overlay_drawer.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <rex/math.h>

#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>

#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/constant_buffers.h"
#include "gpu/device.h"

#if defined(REBLUE_D3D12)
#include "src/gpu/shaders/hlsl/imgui_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/imgui_vs.hlsl.dxil.h"
#else
#include "src/gpu/shaders/hlsl/imgui_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/imgui_vs.hlsl.spirv.h"
#endif

namespace bd::gpu {

namespace {
// Boundless D3D12 ranges become UINT_MAX in the root sig, so only consistency
// with reblue's shared heaps matters, not the exact value.
constexpr u32 kSharedTextureSlots = 65536;
constexpr u32 kSharedSamplerSlots = 1024;

const plume::RenderInputSlot
    kImGuiInputSlot(0, sizeof(rex::gpu::OverlayVertex),
                    plume::RenderInputSlotClassification::PER_VERTEX_DATA);
} // namespace

PlumeImmediateTexture::PlumeImmediateTexture(
    u32 w, u32 h, u32 tex_slot, u32 sampler_slot,
    std::unique_ptr<plume::RenderTexture> texture,
    std::unique_ptr<plume::RenderTextureView> view)
    : rex::gpu::OverlayTexture(w, h), tex_slot_(tex_slot),
      sampler_slot_(sampler_slot), texture_(std::move(texture)),
      view_(std::move(view)) {}

PlumeImmediateTexture::~PlumeImmediateTexture() {
  // Runs on the UI thread while Present is parked (same contract as the
  // allocation in UploadRGBA8Texture), and sampler_slot_ is the shared cache's.
  // The slot's null rewrite is fence-deferred, so the image/view must stay
  // alive until the same boundary: park them, don't free.
  Video::FreeBindlessTextureSlot(tex_slot_);
  Video::ParkTextureUntilFence(std::move(texture_));
  Video::ParkTextureUntilFence(std::move(view_));
}

ImGuiOverlayDrawer::ImGuiOverlayDrawer() = default;
ImGuiOverlayDrawer::~ImGuiOverlayDrawer() = default;

bool ImGuiOverlayDrawer::UploadRGBA8Texture(
    u32 w, u32 h, const u8 *rgba,
    std::unique_ptr<plume::RenderTexture> &out_tex,
    std::unique_ptr<plume::RenderTextureView> &out_view, u32 &out_slot) {
  plume::RenderDevice *device = Video::HostDevice();
  plume::RenderCommandList *cmd = Video::CommandList();
  if (!device || !cmd)
    return false;

  plume::RenderTextureDesc desc;
  desc.dimension = plume::RenderTextureDimension::TEXTURE_2D;
  desc.width = w;
  desc.height = h;
  desc.depth = 1;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = plume::RenderFormat::R8G8B8A8_UNORM;
  desc.flags = plume::RenderTextureFlag::NONE;
  desc.multisampling.sampleCount = plume::RenderSampleCount::COUNT_1;
  std::unique_ptr<plume::RenderTexture> tex =
      CreateHostTexture(device, desc, "imgui-overlay");
  if (!tex) {
    REXLOG_WARN(bd, "ImGui overlay: createTexture failed ({}x{})", w, h);
    return false;
  }

  plume::RenderTextureViewDesc view_desc;
  view_desc.format = plume::RenderFormat::R8G8B8A8_UNORM;
  view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_2D;
  view_desc.mipLevels = 1;
  std::unique_ptr<plume::RenderTextureView> view =
      tex->createTextureView(view_desc);
  if (!view) {
    REXLOG_WARN(bd, "ImGui overlay: createTextureView failed ({}x{})", w, h);
    return false;
  }

  // Upload before allocating the bindless slot so a failure can't leak a slot.
  // D3D12 placed footprint row pitch must be 256-aligned, so pad short rows up.
  constexpr u32 kRowPitchAlign = 256; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
  const u32 bytes_per_row = w * 4;
  const u32 row_pitch =
      (bytes_per_row + (kRowPitchAlign - 1)) & ~(kRowPitchAlign - 1);
  ConstantAllocation up;
  if (row_pitch == bytes_per_row) {
    up = UploadHostBytes(rgba, row_pitch * h, 0x200);
  } else {
    std::vector<u8> padded(static_cast<size_t>(row_pitch) * h, 0);
    for (u32 y = 0; y < h; ++y) {
      std::memcpy(padded.data() + static_cast<size_t>(y) * row_pitch,
                  rgba + static_cast<size_t>(y) * bytes_per_row, bytes_per_row);
    }
    up = UploadHostBytes(padded.data(), row_pitch * h, 0x200);
  }
  if (!up.memory) {
    REXLOG_WARN(bd, "ImGui overlay: pixel upload allocation failed ({}x{})", w, h);
    return false;
  }

  const u32 slot = Video::AllocateBindlessTextureSlot();
  if (slot == UINT32_MAX) {
    REXLOG_ERROR(bd, "ImGui overlay: bindless texture slot pool exhausted");
    return false;
  }
  bd::gpu::state().texture_descriptor_set->setTexture(
      slot, tex.get(), plume::RenderTextureLayout::SHADER_READ, view.get());

  plume::RenderTextureBarrier pre(tex.get(),
                                  plume::RenderTextureLayout::COPY_DEST);
  cmd->barriers(plume::RenderBarrierStage::COPY, &pre, 1);

  // PlacedFootprint row width is in TEXELS, and row_pitch/4 = the byte pitch.
  cmd->copyTextureRegion(
      plume::RenderTextureCopyLocation::Subresource(tex.get(), 0),
      plume::RenderTextureCopyLocation::PlacedFootprint(
          up.ref.ref, plume::RenderFormat::R8G8B8A8_UNORM, w, h, 1,
          row_pitch / 4, up.ref.offset));

  plume::RenderTextureBarrier post(tex.get(),
                                   plume::RenderTextureLayout::SHADER_READ);
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS, &post, 1);

  out_tex = std::move(tex);
  out_view = std::move(view);
  out_slot = slot;
  return true;
}

bool ImGuiOverlayDrawer::TryInitDeviceResources() {
  if (resources_ready_)
    return true;
  plume::RenderDevice *device = Video::HostDevice();
  if (!device)
    return false;

  vs_ = device->createShader(REBLUE_SHADER_BLOB(imgui_vs), "main",
                             kHostShaderFormat);
  ps_ = device->createShader(REBLUE_SHADER_BLOB(imgui_ps), "main",
                             kHostShaderFormat);
  if (!vs_ || !ps_) {
    REXLOG_ERROR(bd, "ImGui overlay: createShader failed");
    return false;
  }

  // Own layout (reblue's shared layout lacks the VERTEX ortho push constant
  // b0,space2). Never .create() the set builders: that exhausts plume's single
  // global heap.
  plume::RenderDescriptorSetBuilder tex_b;
  tex_b.begin();
  tex_b.addTexture(0, kSharedTextureSlots);
  tex_b.end(true, kSharedTextureSlots);

  plume::RenderDescriptorSetBuilder samp_b;
  samp_b.begin();
  samp_b.addSampler(0, kSharedSamplerSlots);
  samp_b.end(true, kSharedSamplerSlots);

  plume::RenderPipelineLayoutBuilder lb;
  lb.begin(false, true);
  lb.addDescriptorSet(tex_b);
  lb.addDescriptorSet(samp_b);
#if defined(REBLUE_D3D12)
  // D3D12: two independent root constant parameters (D3D12CommandList::set*
  // PushConstants asserts range.offset == 0 for every range, so neither can
  // take a nonzero offset here).
  lb.addPushConstant(0, 2, sizeof(float) * 4,
                     plume::RenderShaderStageFlag::VERTEX); // Ortho b0,space2
  lb.addPushConstant(1, 2, sizeof(u32) * 2,
                     plume::RenderShaderStageFlag::PIXEL); // Slots b1,space2
#else
  // Vulkan: both ImGui HLSL cbuffers compile to push_constant blocks, which
  // share one address space per pipeline.
  // Ortho takes [0,16) and Slots [16,24), and the HLSL side matches via
  // [[vk::offset(16)]] on Slots' first member.
  lb.addPushConstant(0, 2, sizeof(float) * 4,
                     plume::RenderShaderStageFlag::VERTEX,
                     /*offset=*/0); // Ortho b0,space2, bytes [0,16)
  lb.addPushConstant(1, 2, sizeof(u32) * 2, plume::RenderShaderStageFlag::PIXEL,
                     /*offset=*/sizeof(float) *
                         4); // Slots b1,space2, bytes [16,24)
#endif
  lb.end();
  layout_ = lb.create(device);
  if (!layout_) {
    REXLOG_ERROR(bd, "ImGui overlay: pipeline layout create failed");
    return false;
  }

  plume::RenderGraphicsPipelineDesc pd;
  pd.pipelineLayout = layout_.get();
  pd.vertexShader = vs_.get();
  pd.pixelShader = ps_.get();
  pd.depthEnabled = false;
  pd.depthWriteEnabled = false;
  pd.primitiveTopology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
  pd.cullMode = plume::RenderCullMode::NONE;
  pd.renderTargetCount = 1;
  pd.renderTargetFormat[0] = plume::RenderFormat::B8G8R8A8_UNORM;
  pd.renderTargetBlend[0] = plume::RenderBlendDesc::AlphaBlend();
  pd.depthTargetFormat = plume::RenderFormat::UNKNOWN;
  // Vertex input: pos.f2@0, uv.f2@8, color RGBA8@16, stride 20, one slot.
  const plume::RenderInputElement elems[] = {
      plume::RenderInputElement("POSITION", 0, 0,
                                plume::RenderFormat::R32G32_FLOAT, 0, 0),
      plume::RenderInputElement("TEXCOORD", 0, 1,
                                plume::RenderFormat::R32G32_FLOAT, 0, 8),
      plume::RenderInputElement("COLOR", 0, 2,
                                plume::RenderFormat::R8G8B8A8_UNORM, 0, 16),
  };
  pd.inputSlots = &kImGuiInputSlot;
  pd.inputSlotsCount = 1;
  pd.inputElements = elems;
  pd.inputElementsCount = 3;
  pipeline_ = CreateHostGraphicsPipeline(device, pd, "imgui-overlay");
  if (!pipeline_) {
    REXLOG_ERROR(bd, "ImGui overlay: pipeline create failed");
    return false;
  }

  // 1x1 white for null texture (solid color) draws.
  static const u8 kWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (!UploadRGBA8Texture(1, 1, kWhite, white_texture_, white_view_,
                          white_slot_)) {
    REXLOG_ERROR(bd, "ImGui overlay: white texture upload failed");
    return false;
  }

  resources_ready_ = true;
  return true;
}

rex::Result<std::unique_ptr<rex::gpu::OverlayTexture>>
ImGuiOverlayDrawer::CreateTexture(u32 width, u32 height,
                                  rex::gpu::OverlayTextureFilter filter,
                                  bool repeat, std::span<const u8> rgba8) {
  // Error until the device exists. The caller retries on a later frame.
  if (!TryInitDeviceResources())
    return rex::Err<std::unique_ptr<rex::gpu::OverlayTexture>>(
        rex::ErrorCategory::kRuntime, "Plume device not ready");
  if (rgba8.empty() || width == 0 || height == 0)
    return rex::Err<std::unique_ptr<rex::gpu::OverlayTexture>>(
        rex::ErrorCategory::kValidation, "ImGui overlay: empty texture");

  std::unique_ptr<plume::RenderTexture> tex;
  std::unique_ptr<plume::RenderTextureView> view;
  u32 slot = 0;
  if (!UploadRGBA8Texture(width, height, rgba8.data(), tex, view, slot)) {
    REXLOG_WARN(bd, "ImGui overlay: CreateTexture upload failed ({}x{})", width,
            height);
    return rex::Err<std::unique_ptr<rex::gpu::OverlayTexture>>(
        rex::ErrorCategory::kRuntime, "ImGui overlay: texture upload failed");
  }

  // Shared sampler slot 0 (linear-clamp) is what the font atlas wants.
  (void)filter;
  (void)repeat;
  return std::unique_ptr<rex::gpu::OverlayTexture>(
      std::make_unique<PlumeImmediateTexture>(
          width, height, slot,
          /*sampler_slot=*/0u, std::move(tex), std::move(view)));
}

bool ImGuiOverlayDrawer::Scissor(const rex::gpu::OverlayDraw &draw, u32 &l,
                                 u32 &t, u32 &w, u32 &h) const {
  if (!draw.scissor) {
    l = 0;
    t = 0;
    w = rt_w_;
    h = rt_h_;
    return rt_w_ && rt_h_;
  }
  const rex::gpu::OverlayRect &sc = *draw.scissor;
  const float rt_wf = float(rt_w_);
  const float rt_hf = float(rt_h_);
  const float scale_x = rt_wf / coord_w_;
  const float scale_y = rt_hf / coord_h_;
  const float x0f = rex::clamp_float(sc.left * scale_x, 0.0f, rt_wf);
  const float y0f = rex::clamp_float(sc.top * scale_y, 0.0f, rt_hf);
  const float x1f = rex::clamp_float(sc.right * scale_x, x0f, rt_wf);
  const float y1f = rex::clamp_float(sc.bottom * scale_y, y0f, rt_hf);
  // 16p8 fixed point, top-left includes .5 and bottom-right excludes it.
  const i32 x0 = (i32(std::nearbyint(x0f * 256.0f)) + 127) >> 8;
  const i32 y0 = (i32(std::nearbyint(y0f * 256.0f)) + 127) >> 8;
  const i32 x1 = (i32(std::nearbyint(x1f * 256.0f)) + 127) >> 8;
  const i32 y1 = (i32(std::nearbyint(y1f * 256.0f)) + 127) >> 8;
  l = u32(x0);
  t = u32(y0);
  w = u32(x1 - x0);
  h = u32(y1 - y0);
  return x1 > x0 && y1 > y0;
}

void ImGuiOverlayDrawer::Begin(rex::gpu::OverlayDrawContext &ctx, float width,
                               float height) {
  rt_w_ = ctx.GetRenderTargetWidth();
  rt_h_ = ctx.GetRenderTargetHeight();
  if (!(width > 0.0f) || !(height > 0.0f)) {
    width = float(rt_w_);
    height = float(rt_h_);
  }
  coord_w_ = width;
  coord_h_ = height;
  batch_open_ = false;
  if (!TryInitDeviceResources()) {
    cmd_ = nullptr;
    return;
  }

  auto &rctx = static_cast<ReblueUIDrawContext &>(ctx);
  cmd_ = rctx.command_list();
  if (!cmd_)
    return;

  const float cw = coord_w_;
  const float ch = coord_h_;

  cmd_->setFramebuffer(rctx.framebuffer());
  plume::RenderViewport vp(
      0.0f, 0.0f, static_cast<float>(ctx.GetRenderTargetWidth()),
      static_cast<float>(ctx.GetRenderTargetHeight()), 0.0f, 1.0f);
  cmd_->setViewports(&vp, 1);
  // Bind our layout before the sets: BeginCommandList leaves reblue's main
  // layout active and sets bind against whatever layout is current.
  cmd_->setGraphicsPipelineLayout(layout_.get());
  cmd_->setPipeline(pipeline_.get());
  cmd_->setGraphicsDescriptorSet(bd::gpu::state().texture_descriptor_set.get(),
                                 0);
  cmd_->setGraphicsDescriptorSet(bd::gpu::state().sampler_descriptor_set.get(),
                                 1);

  // Ortho projection push constant (b0,space2, VERTEX range 0), y flipped.
  struct Ortho {
    float scale[2];
    float translate[2];
  } ortho;
  ortho.scale[0] = 2.0f / cw;
  ortho.scale[1] = -2.0f / ch;
  ortho.translate[0] = -1.0f;
  ortho.translate[1] = 1.0f;
  cmd_->setGraphicsPushConstants(0, &ortho, 0, sizeof(ortho));
}

void ImGuiOverlayDrawer::BeginBatch(const rex::gpu::OverlayBatch &b) {
  batch_open_ = false;
  if (!cmd_ || b.vertices.empty())
    return;

  const u32 vbytes =
      u32(sizeof(rex::gpu::OverlayVertex)) * u32(b.vertices.size());
  ConstantAllocation va = UploadHostBytes(b.vertices.data(), vbytes, 16);
  if (!va.memory)
    return;
  const plume::RenderVertexBufferView vbv(va.ref, va.size);
  cmd_->setVertexBuffers(0, &vbv, 1, &kImGuiInputSlot);

  if (!b.indices.empty()) {
    const u32 ibytes = u32(sizeof(u16)) * u32(b.indices.size());
    ConstantAllocation ia = UploadHostBytes(b.indices.data(), ibytes, 4);
    if (!ia.memory)
      return;
    const plume::RenderIndexBufferView ibv(ia.ref, ia.size,
                                           plume::RenderFormat::R16_UINT);
    cmd_->setIndexBuffer(&ibv);
  }
  batch_open_ = true;
}

void ImGuiOverlayDrawer::Draw(const rex::gpu::OverlayDraw &draw) {
  if (!cmd_ || !batch_open_ || draw.count == 0)
    return;

  u32 l, t, w, h;
  if (!Scissor(draw, l, t, w, h))
    return; // empty -> skip
  plume::RenderRect rc(i32(l), i32(t), i32(l + w), i32(t + h));
  cmd_->setScissors(&rc, 1);

  u32 tex_slot = white_slot_;
  u32 samp_slot = 0;
  if (draw.texture) {
    auto *tex = static_cast<PlumeImmediateTexture *>(draw.texture);
    tex_slot = tex->tex_slot();
    samp_slot = tex->sampler_slot();
  }
  u32 slots[2] = {tex_slot, samp_slot};
  cmd_->setGraphicsPushConstants(1, slots, 0, sizeof(slots)); // PIXEL range 1

  cmd_->drawIndexedInstanced(draw.count, 1, draw.index_offset,
                             draw.base_vertex, 0);
}

void ImGuiOverlayDrawer::EndBatch() { batch_open_ = false; }

void ImGuiOverlayDrawer::End() {
  cmd_ = nullptr;
}

} // namespace bd::gpu
