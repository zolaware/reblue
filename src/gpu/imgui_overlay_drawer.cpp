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

#include <imgui.h>

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
    kImGuiInputSlot(0, sizeof(ImDrawVert),
                    plume::RenderInputSlotClassification::PER_VERTEX_DATA);
} // namespace

PlumeImmediateTexture::PlumeImmediateTexture(
    u32 tex_slot, std::unique_ptr<plume::RenderTexture> texture,
    std::unique_ptr<plume::RenderTextureView> view)
    : tex_slot_(tex_slot), texture_(std::move(texture)),
      view_(std::move(view)) {}

PlumeImmediateTexture::~PlumeImmediateTexture() {
  // Outside the overlay pass nothing is parked holding the video state lock, so
  // the locking park is safe. The slot's null rewrite is fence-deferred, so the
  // image and view must stay alive until the same boundary: park them, don't
  // free.
  if (tex_slot_ == UINT32_MAX)
    return;
  Video::FreeBindlessTextureSlot(tex_slot_);
  Video::ParkTextureUntilFence(std::move(texture_));
  Video::ParkTextureUntilFence(std::move(view_));
}

void PlumeImmediateTexture::Retire() {
  // The parked Present holds the video state lock for the UI thread, so the
  // locking park would deadlock against it.
  const u32 slot = tex_slot_;
  tex_slot_ = UINT32_MAX;
  Video::FreeBindlessTextureSlotLocked(slot);
  Video::ParkTextureUntilFenceLocked(std::move(texture_));
  Video::ParkTextureUntilFenceLocked(std::move(view_));
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
    REXBD_WARN("ImGui overlay: createTexture failed ({}x{})", w, h);
    return false;
  }

  plume::RenderTextureViewDesc view_desc;
  view_desc.format = plume::RenderFormat::R8G8B8A8_UNORM;
  view_desc.dimension = plume::RenderTextureViewDimension::TEXTURE_2D;
  view_desc.mipLevels = 1;
  std::unique_ptr<plume::RenderTextureView> view =
      tex->createTextureView(view_desc);
  if (!view) {
    REXBD_WARN("ImGui overlay: createTextureView failed ({}x{})", w, h);
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
    REXBD_WARN("ImGui overlay: pixel upload allocation failed ({}x{})", w, h);
    return false;
  }

  const u32 slot = Video::AllocateBindlessTextureSlot();
  if (slot == UINT32_MAX) {
    REXBD_ERROR("ImGui overlay: bindless texture slot pool exhausted");
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
    REXBD_ERROR("ImGui overlay: createShader failed");
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
    REXBD_ERROR("ImGui overlay: pipeline layout create failed");
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
    REXBD_ERROR("ImGui overlay: pipeline create failed");
    return false;
  }

  // 1x1 white for null texture (solid color) draws.
  static const u8 kWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  if (!UploadRGBA8Texture(1, 1, kWhite, white_texture_, white_view_,
                          white_slot_)) {
    REXBD_ERROR("ImGui overlay: white texture upload failed");
    return false;
  }

  resources_ready_ = true;
  return true;
}

void ImGuiOverlayDrawer::SetTarget(plume::RenderCommandList *cmd,
                                   plume::RenderFramebuffer *fb, u32 width,
                                   u32 height) {
  cmd_ = cmd;
  fb_ = fb;
  rt_w_ = width;
  rt_h_ = height;
}

void ImGuiOverlayDrawer::RetireTexture(ImTextureData &texture) {
  auto *old = static_cast<PlumeImmediateTexture *>(texture.BackendUserData);
  if (!old)
    return;
  old->Retire();
  delete old;
}

// An atlas update replaces the whole image. The font atlas changes rarely, so a
// partial upload is not worth the copy region bookkeeping.
void ImGuiOverlayDrawer::UpdateTexture(ImTextureData &texture) {
  if (texture.Status == ImTextureStatus_WantDestroy) {
    RetireTexture(texture);
    texture.BackendUserData = nullptr;
    texture.SetTexID(ImTextureID_Invalid);
    texture.SetStatus(ImTextureStatus_Destroyed);
    return;
  }
  if (texture.Status != ImTextureStatus_WantCreate &&
      texture.Status != ImTextureStatus_WantUpdates)
    return;

  const u32 w = static_cast<u32>(texture.Width);
  const u32 h = static_cast<u32>(texture.Height);
  std::vector<u8> rgba(static_cast<size_t>(w) * h * 4);
  const u8 *src = static_cast<const u8 *>(texture.GetPixels());
  if (texture.Format == ImTextureFormat_Alpha8) {
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      rgba[i * 4 + 0] = 0xFF;
      rgba[i * 4 + 1] = 0xFF;
      rgba[i * 4 + 2] = 0xFF;
      rgba[i * 4 + 3] = src[i];
    }
  } else {
    std::memcpy(rgba.data(), src, rgba.size());
  }

  std::unique_ptr<plume::RenderTexture> tex;
  std::unique_ptr<plume::RenderTextureView> view;
  u32 slot = 0;
  if (!UploadRGBA8Texture(w, h, rgba.data(), tex, view, slot)) {
    REXBD_WARN("ImGui overlay: texture upload failed ({}x{})", w, h);
    return;
  }
  RetireTexture(texture);
  texture.BackendUserData =
      new PlumeImmediateTexture(slot, std::move(tex), std::move(view));
  texture.SetTexID(static_cast<ImTextureID>(slot));
  texture.SetStatus(ImTextureStatus_OK);
}

void ImGuiOverlayDrawer::RenderOverlays(ImDrawData &draw_data) {
  // The device does not exist yet, or the frame has no open command list.
  // Textures stay queued in ImGui and go up on a later frame.
  if (!cmd_ || !TryInitDeviceResources())
    return;

  if (draw_data.Textures) {
    for (ImTextureData *texture : *draw_data.Textures)
      UpdateTexture(*texture);
  }

  const float disp_w = draw_data.DisplaySize.x;
  const float disp_h = draw_data.DisplaySize.y;
  if (!(disp_w > 0.0f) || !(disp_h > 0.0f) || !rt_w_ || !rt_h_)
    return;

  cmd_->setFramebuffer(fb_);
  plume::RenderViewport vp(0.0f, 0.0f, static_cast<float>(rt_w_),
                           static_cast<float>(rt_h_), 0.0f, 1.0f);
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
  ortho.scale[0] = 2.0f / disp_w;
  ortho.scale[1] = -2.0f / disp_h;
  ortho.translate[0] = -1.0f - draw_data.DisplayPos.x * ortho.scale[0];
  ortho.translate[1] = 1.0f - draw_data.DisplayPos.y * ortho.scale[1];
  cmd_->setGraphicsPushConstants(0, &ortho, 0, sizeof(ortho));

  const float scale_x = static_cast<float>(rt_w_) / disp_w;
  const float scale_y = static_cast<float>(rt_h_) / disp_h;

  for (const ImDrawList *list : draw_data.CmdLists) {
    const u32 vbytes = u32(sizeof(ImDrawVert)) * u32(list->VtxBuffer.Size);
    const u32 ibytes = u32(sizeof(ImDrawIdx)) * u32(list->IdxBuffer.Size);
    if (!vbytes || !ibytes)
      continue;
    ConstantAllocation va = UploadHostBytes(list->VtxBuffer.Data, vbytes, 16);
    ConstantAllocation ia = UploadHostBytes(list->IdxBuffer.Data, ibytes, 4);
    if (!va.memory || !ia.memory)
      continue;
    const plume::RenderVertexBufferView vbv(va.ref, va.size);
    cmd_->setVertexBuffers(0, &vbv, 1, &kImGuiInputSlot);
    const plume::RenderIndexBufferView ibv(
        ia.ref, ia.size,
        sizeof(ImDrawIdx) == 2 ? plume::RenderFormat::R16_UINT
                               : plume::RenderFormat::R32_UINT);
    cmd_->setIndexBuffer(&ibv);

    for (const ImDrawCmd &pcmd : list->CmdBuffer) {
      if (pcmd.UserCallback || pcmd.ElemCount == 0)
        continue;
      const float x0 = rex::clamp_float(
          (pcmd.ClipRect.x - draw_data.DisplayPos.x) * scale_x, 0.0f,
          static_cast<float>(rt_w_));
      const float y0 = rex::clamp_float(
          (pcmd.ClipRect.y - draw_data.DisplayPos.y) * scale_y, 0.0f,
          static_cast<float>(rt_h_));
      const float x1 = rex::clamp_float(
          (pcmd.ClipRect.z - draw_data.DisplayPos.x) * scale_x, x0,
          static_cast<float>(rt_w_));
      const float y1 = rex::clamp_float(
          (pcmd.ClipRect.w - draw_data.DisplayPos.y) * scale_y, y0,
          static_cast<float>(rt_h_));
      if (x1 <= x0 || y1 <= y0)
        continue;
      plume::RenderRect rc(i32(x0), i32(y0), i32(std::ceil(x1)),
                           i32(std::ceil(y1)));
      cmd_->setScissors(&rc, 1);

      u32 slots[2] = {white_slot_, 0};
      if (const ImTextureID id = pcmd.GetTexID())
        slots[0] = static_cast<u32>(id);
      cmd_->setGraphicsPushConstants(1, slots, 0, sizeof(slots));

      cmd_->drawIndexedInstanced(pcmd.ElemCount, 1, pcmd.IdxOffset,
                                 static_cast<i32>(pcmd.VtxOffset), 0);
    }
  }
  cmd_ = nullptr;
}

} // namespace bd::gpu
