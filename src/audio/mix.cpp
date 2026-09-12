/**
 * @file    audio/mix.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "audio/settings.h"
#include "core/memory_helpers.h"

#include <algorithm>
#include <cmath>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/types.h>

namespace bd::audio {
namespace {

constexpr u32 kX3DVolReverbMatrix = 392;
constexpr u32 kX3DVolReverbFlag = 420;
constexpr u32 kX3DVolReverbManaged = 1;
constexpr u32 kX3DVolChannels = 6;

constexpr u32 kFrameFormat = 0x0C;
constexpr u32 kFrameSamplePtr = 0x14;
constexpr u32 kFrameSampleCount = 256;
constexpr u32 kFrameMaxChannels = 6;
constexpr u32 kBadFramesBeforeMute = 2;
constexpr f32 kUnreadableFrame = -1.0f;

f32 g_gain = 1.0f;
u32 g_badFrames = 0;

void ClearUnmanagedReverbSend(u32 x3dVol) {
  if (!x3dVol ||
      mem::try_load<u32>(x3dVol + kX3DVolReverbFlag) == kX3DVolReverbManaged)
    return;
  for (u32 c = 0; c < kX3DVolChannels; ++c)
    mem::try_store<f32>(x3dVol + kX3DVolReverbMatrix + c * sizeof(f32), 0.0f);
}

f32 FramePeak(u32 samplesVA, u32 channels) {
  f32 peak = 0.0f;
  for (u32 c = 0; c < channels; ++c) {
    const auto *p =
        mem::try_at<const be_f32>(samplesVA + c * kFrameSampleCount * sizeof(f32));
    if (!p)
      return kUnreadableFrame;
    for (u32 i = 0; i < kFrameSampleCount; ++i) {
      const f32 v = p[i];
      if (!std::isfinite(v))
        return INFINITY;
      peak = std::max(peak, std::fabs(v));
    }
  }
  return peak;
}

void ApplyGainRamp(u32 samplesVA, u32 channels, f32 from, f32 to) {
  if (from == 1.0f && to == 1.0f)
    return;
  const f32 step = (to - from) / static_cast<f32>(kFrameSampleCount);
  for (u32 c = 0; c < channels; ++c) {
    auto *p = mem::try_at<be_f32>(samplesVA + c * kFrameSampleCount * sizeof(f32));
    if (!p)
      continue;
    f32 gain = from;
    for (u32 i = 0; i < kFrameSampleCount; ++i, gain += step) {
      const f32 v = p[i];
      p[i] = std::isfinite(v) ? v * gain : 0.0f;
    }
  }
}

void GuardOutputFrame(u32 frameVA) {
  const f64 limit = Settings::Get().OutputLimit();
  if (limit <= 0.0 || !frameVA)
    return;
  u32 channels = mem::try_load<u32>(frameVA + kFrameFormat) >> 16;
  if (channels == 0 || channels > kFrameMaxChannels)
    channels = kFrameMaxChannels;
  const u32 samples = mem::try_load<u32>(frameVA + kFrameSamplePtr);
  if (!samples)
    return;

  const f32 peak = FramePeak(samples, channels);
  if (peak == kUnreadableFrame)
    return;

  const f32 was = g_gain;
  if (!std::isfinite(peak)) {
    g_badFrames = kBadFramesBeforeMute;
    g_gain = 0.0f;
  } else if (peak > static_cast<f32>(limit)) {
    if (++g_badFrames >= kBadFramesBeforeMute)
      g_gain = 0.0f;
  } else {
    g_badFrames = 0;
    g_gain = 1.0f;
  }
  ApplyGainRamp(samples, channels, was, g_gain);
}

} // namespace
} // namespace bd::audio

REX_EXTERN(__imp__X3DVOL_SetX3DVol);
REX_HOOK_RAW(X3DVOL_SetX3DVol) {
  const u32 x3dVol = ctx.r3.u32;
  __imp__X3DVOL_SetX3DVol(ctx, base);
  bd::audio::ClearUnmanagedReverbSend(x3dVol);
}

REX_EXTERN(__imp__CXenonRenderer_Process);
REX_HOOK_RAW(CXenonRenderer_Process) {
  bd::audio::GuardOutputFrame(ctx.r4.u32);
  __imp__CXenonRenderer_Process(ctx, base);
}
