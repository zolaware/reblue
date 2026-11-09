/**
 * @file    gpu/imgui_overlay_drawer.h
 * @brief   Plume-backed IOverlayRenderer for the overlays and dialogs.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <memory>
#include <rex/result.h>
#include <rex/types.h>

#include <rex/overlay_renderer.h>

struct ImDrawData;
struct ImTextureData;

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

class PlumeImmediateTexture final {
public:
  PlumeImmediateTexture(u32 tex_slot,
                        std::unique_ptr<plume::RenderTexture> texture,
                        std::unique_ptr<plume::RenderTextureView> view);
  ~PlumeImmediateTexture();

  u32 tex_slot() const { return tex_slot_; }

  // Parks the image, view and slot without taking the video state lock. Only
  // for the overlay pass, where Present is parked holding it. The destructor
  // does the same through the locking path when Retire never ran.
  void Retire();

private:
  u32 tex_slot_;
  std::unique_ptr<plume::RenderTexture> texture_;
  std::unique_ptr<plume::RenderTextureView> view_;
};

// Binds the device lazily because it exists before reblue's device.
class ImGuiOverlayDrawer final : public rex::IOverlayRenderer {
public:
  ImGuiOverlayDrawer();
  ~ImGuiOverlayDrawer() override;

  // The command list and framebuffer the next RenderOverlays draws into.
  void SetTarget(plume::RenderCommandList *cmd, plume::RenderFramebuffer *fb,
                 u32 width, u32 height);

  void RenderOverlays(ImDrawData &draw_data) override;

private:
  bool TryInitDeviceResources(); // lazy GPU init, false if device not ready

  void UpdateTexture(ImTextureData &texture);

  // Only valid inside the overlay pass, see PlumeImmediateTexture::Retire.
  void RetireTexture(ImTextureData &texture);

  // Requires an open Present command list.
  bool UploadRGBA8Texture(u32 w, u32 h, const u8 *rgba,
                          std::unique_ptr<plume::RenderTexture> &out_tex,
                          std::unique_ptr<plume::RenderTextureView> &out_view,
                          u32 &out_slot);

  bool resources_ready_ = false;
  u32 rt_w_ = 0;
  u32 rt_h_ = 0;
  plume::RenderCommandList *cmd_ = nullptr;
  plume::RenderFramebuffer *fb_ = nullptr;

  std::unique_ptr<plume::RenderPipelineLayout> layout_;
  std::unique_ptr<plume::RenderPipeline> pipeline_;
  std::unique_ptr<plume::RenderShader> vs_;
  std::unique_ptr<plume::RenderShader> ps_;
  std::unique_ptr<plume::RenderTexture> white_texture_;
  std::unique_ptr<plume::RenderTextureView> white_view_;
  u32 white_slot_ = 0;
};

} // namespace bd::gpu
