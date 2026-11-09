/**
 * @file    audio/mix.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "audio/settings.h"
#include "core/memory_helpers.h"
#include "core/settings.h"
#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ppc/detour.h>
#include <rex/types.h>

namespace bd::audio {
namespace {

constexpr u32 kX3DVolReverbManaged = 1;

// The game's X3DVOL wrapper around the X3DAudio emitter and listener. The
// matrix is the per-channel reverb send row X3DVOL::SetX3DVol fills when the
// flag says the send is managed.
struct X3DVol_t {
  /* 0x000 */ u8 _pad000[0x188];
  /* 0x188 */ be_f32 reverbMatrix[6];
  /* 0x1A0 */ u8 _pad1A0[0x1A4 - 0x1A0];
  /* 0x1A4 */ be_u32 reverbFlag;
};
static_assert(offsetof(X3DVol_t, reverbMatrix) == 0x188);
static_assert(offsetof(X3DVol_t, reverbFlag) == 0x1A4);

constexpr u32 kFrameFormat = 0x0C;
constexpr u32 kFrameSamplePtr = 0x14;
constexpr u32 kFrameSampleCount = 256;
constexpr u32 kFrameMaxChannels = 6;
constexpr u32 kBadFramesBeforeMute = 2;
constexpr f32 kUnreadableFrame = -1.0f;

f32 g_gain = 1.0f;
f32 g_limitGain = 1.0f;
u32 g_badFrames = 0;

void ClearUnmanagedReverbSend(u32 x3dVol) {
  auto *vol = mem::try_at<X3DVol_t>(x3dVol);
  if (!vol || u32(vol->reverbFlag) == kX3DVolReverbManaged)
    return;
  for (be_f32 &send : vol->reverbMatrix)
    send = 0.0f;
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

bool MuteWanted() { return !bd::platform::HID().IsFocused(); }

void GuardOutputFrame(u32 frameVA) {
  if (!frameVA)
    return;
  const f64 limit = Settings::Get().OutputLimit();
  const bool muted = MuteWanted();
  if (limit <= 0.0 && !muted && g_gain == 1.0f)
    return;
  u32 channels = mem::try_load<u32>(frameVA + kFrameFormat) >> 16;
  if (channels == 0 || channels > kFrameMaxChannels)
    channels = kFrameMaxChannels;
  const u32 samples = mem::try_load<u32>(frameVA + kFrameSamplePtr);
  if (!samples)
    return;

  if (limit <= 0.0) {
    g_limitGain = 1.0f;
    g_badFrames = 0;
  } else {
    const f32 peak = FramePeak(samples, channels);
    if (peak == kUnreadableFrame)
      return;
    if (!std::isfinite(peak)) {
      g_badFrames = kBadFramesBeforeMute;
      g_limitGain = 0.0f;
    } else if (peak > static_cast<f32>(limit)) {
      if (++g_badFrames >= kBadFramesBeforeMute)
        g_limitGain = 0.0f;
    } else {
      g_badFrames = 0;
      g_limitGain = 1.0f;
    }
  }

  const f32 was = g_gain;
  g_gain = muted ? 0.0f : g_limitGain;
  ApplyGainRamp(samples, channels, was, g_gain);
}

} // namespace
} // namespace bd::audio

REX_DETOUR(X3DVOL_SetX3DVol, u32, (u32 x3dVol)) {
  const u32 result = original(x3dVol);
  bd::audio::ClearUnmanagedReverbSend(x3dVol);
  return result;
}

REX_DETOUR(CXenonRenderer_Process, void, (u32 self, u32 frame)) {
  bd::audio::GuardOutputFrame(frame);
  original(self, frame);
}
