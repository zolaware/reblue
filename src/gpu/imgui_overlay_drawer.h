/**
 * @file    gpu/imgui_overlay_drawer.h
 * @brief   Plume/D3D12-backed IOverlayDrawer for detached overlay rendering.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <memory>
#include <span>
#include <rex/result.h>
#include <rex/types.h>

#include <rex/gpu/overlay.h>

// Plume declares these as 'struct'. Match the tag or clang-cl flags a mismatch.
namespace plume {
struct RenderCommandList;
struct RenderFramebuffer;
struct RenderTexture;
struct RenderTextureView;
struct RenderPipelineLayout;
struct RenderPipeline;
struct RenderShader;
} // namespace plume

namespace bd::gpu {

class ReblueUIDrawContext final : public rex::gpu::OverlayDrawContext {
public:
  ReblueUIDrawContext(u32 rt_w, u32 rt_h, plume::RenderCommandList *cmd,
                      plume::RenderFramebuffer *fb)
      : rex::gpu::OverlayDrawContext(nullptr, rt_w, rt_h), cmd_(cmd), fb_(fb) {}

  plume::RenderCommandList *command_list() const { return cmd_; }
  plume::RenderFramebuffer *framebuffer() const { return fb_; }

private:
  plume::RenderCommandList *cmd_;
  plume::RenderFramebuffer *fb_;
};

class PlumeImmediateTexture final : public rex::gpu::OverlayTexture {
public:
  PlumeImmediateTexture(u32 w, u32 h, u32 tex_slot, u32 sampler_slot,
                        std::unique_ptr<plume::RenderTexture> texture,
                        std::unique_ptr<plume::RenderTextureView> view);
  ~PlumeImmediateTexture() override;

  u32 tex_slot() const { return tex_slot_; }
  u32 sampler_slot() const { return sampler_slot_; }

private:
  u32 tex_slot_;
  u32 sampler_slot_;
  std::unique_ptr<plume::RenderTexture> texture_;
  std::unique_ptr<plume::RenderTextureView> view_;
};

// Detached drawer (config.graphics == nullptr): binds the device lazily because
// it exists before reblue's device.
class ImGuiOverlayDrawer final : public rex::gpu::IOverlayDrawer {
public:
  ImGuiOverlayDrawer();
  ~ImGuiOverlayDrawer() override;

  [[nodiscard]] rex::Result<std::unique_ptr<rex::gpu::OverlayTexture>>
  CreateTexture(u32 width, u32 height, rex::gpu::OverlayTextureFilter filter,
                bool repeat, std::span<const u8> rgba8) override;
  void Begin(rex::gpu::OverlayDrawContext &context, float width,
             float height) override;
  void BeginBatch(const rex::gpu::OverlayBatch &batch) override;
  void Draw(const rex::gpu::OverlayDraw &draw) override;
  void EndBatch() override;
  void End() override;

private:
  bool TryInitDeviceResources(); // lazy GPU init, false if device not ready

  // Clamps the draw scissor to the render target. False when it is empty.
  bool Scissor(const rex::gpu::OverlayDraw &draw, rex::u32 &l, rex::u32 &t,
               rex::u32 &w, rex::u32 &h) const;

  // Requires an open Present command list.
  bool UploadRGBA8Texture(u32 w, u32 h, const u8 *rgba,
                          std::unique_ptr<plume::RenderTexture> &out_tex,
                          std::unique_ptr<plume::RenderTextureView> &out_view,
                          u32 &out_slot);

  bool resources_ready_ = false;
  bool batch_open_ = false;
  float coord_w_ = 0.0f;
  float coord_h_ = 0.0f;
  rex::u32 rt_w_ = 0;
  rex::u32 rt_h_ = 0;
  plume::RenderCommandList *cmd_ = nullptr; // valid only between Begin/End

  std::unique_ptr<plume::RenderPipelineLayout> layout_;
  std::unique_ptr<plume::RenderPipeline> pipeline_;
  std::unique_ptr<plume::RenderShader> vs_;
  std::unique_ptr<plume::RenderShader> ps_;
  std::unique_ptr<plume::RenderTexture> white_texture_;
  std::unique_ptr<plume::RenderTextureView> white_view_;
  u32 white_slot_ = 0;
};

} // namespace bd::gpu
