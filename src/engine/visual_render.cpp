/**
 * @file    engine/visual_render.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/visual_render.h"

#include <cstddef>

#include "core/memory_helpers.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

struct VisualRender_t {
  /* 0x0000 */ u8 _pad0000[0x1A38];
  /* 0x1A38 */ be_f32 screenW;
  /* 0x1A3C */ be_f32 screenH;
  /* 0x1A40 */ u8 _pad1A40[0x1B2C - 0x1A40];
  /* 0x1B2C */ be_f32 brightness[VisualRender::kBrightnessChannels];
  /* 0x1B38 */ u8 _pad1B38[0x1B44 - 0x1B38];
  /* 0x1B44 */ be_f32 screenPosX;
  /* 0x1B48 */ be_f32 screenPosY;
  /* 0x1B4C */ u8 _pad1B4C[0x1B58 - 0x1B4C];
  /* 0x1B58 */ be_f32 safeRate;
  /* 0x1B5C */ u8 _pad1B5C[0x1BC4 - 0x1B5C];
  /* 0x1BC4 */ be_f32 renderRate;
  /* 0x1BC8 */ be_u32 fsaa;
};
static_assert(offsetof(VisualRender_t, screenW) == 0x1A38);
static_assert(offsetof(VisualRender_t, screenH) == 0x1A3C);
static_assert(offsetof(VisualRender_t, brightness) == 0x1B2C);
static_assert(offsetof(VisualRender_t, screenPosX) == 0x1B44);
static_assert(offsetof(VisualRender_t, screenPosY) == 0x1B48);
static_assert(offsetof(VisualRender_t, safeRate) == 0x1B58);
static_assert(offsetof(VisualRender_t, renderRate) == 0x1BC4);
static_assert(offsetof(VisualRender_t, fsaa) == 0x1BC8);

} // namespace

VisualRender VisualRender::Get() {
  return VisualRender(mem::try_load<u32>(addr::kVisualRender));
}

f32 VisualRender::ScreenW() const {
  const auto *self = Self<VisualRender_t>();
  return self ? static_cast<f32>(self->screenW) : 0.0f;
}

bool VisualRender::SetScreenW(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->screenW = v;
  return true;
}

bool VisualRender::SetScreenH(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->screenH = v;
  return true;
}

bool VisualRender::SetBrightness(u32 channel, f32 v) {
  if (channel >= kBrightnessChannels)
    return false;
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->brightness[channel] = v;
  return true;
}

bool VisualRender::SetScreenPosX(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->screenPosX = v;
  return true;
}

bool VisualRender::SetScreenPosY(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->screenPosY = v;
  return true;
}

bool VisualRender::SetSafeRate(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->safeRate = v;
  return true;
}

f32 VisualRender::RenderRate() const {
  const auto *self = Self<VisualRender_t>();
  return self ? static_cast<f32>(self->renderRate) : 0.0f;
}

bool VisualRender::SetRenderRate(f32 v) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->renderRate = v;
  return true;
}

bool VisualRender::SetFSAA(bool on) {
  auto *self = Self<VisualRender_t>();
  if (!self)
    return false;
  self->fsaa = on ? 1u : 0u;
  return true;
}

} // namespace bd::engine
