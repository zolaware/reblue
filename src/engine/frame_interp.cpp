/**
 * @file    engine/frame_interp.cpp
 * @brief   Render interpolation between the 30Hz logic ticks of the fps unlock.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/frame_interp.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/battle.h"
#include "engine/cutscene.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/d2anime/d2anime_types.h"
#include "engine/frame_clock.h"
#include "engine/guest_prim.h"
#include "engine/glyph_set.h"
#include "engine/menus/camp_settings.h"
#include "engine/menus/local_map.h"
#include "engine/mouse_cursor.h"
#include "engine/settings.h"
#include "engine/state_layout.h"
#include "engine/virtual_buttons.h"
#include "gpu/gpu.h"

namespace {

template <typename T> T *TryStruct(u32 va) {
  auto *p = bd::mem::try_at<T>(va);
  return p && bd::mem::try_at<u8>(va + sizeof(T) - 1) ? p : nullptr;
}

constexpr double kTickSeconds = 1.0 / 30.0;
constexpr double kFastChangeSeconds = kTickSeconds * 0.5;
constexpr double kEventCutSpacing = kTickSeconds * 1.5;
constexpr double kCutRunSpacing = kTickSeconds * 2.5;
constexpr u64 kStaleFrames = 4;
constexpr float kViewCutRotDot = 0.90f;
constexpr float kEventViewCutStep = 2.0f;
constexpr u32 kMaxCutRun = 1;
constexpr float kCutDistance = 128.0f;
constexpr float kCutRatio = 10.0f;
constexpr float kCutFloor = 20.0f;
constexpr float kStepBlend = 0.25f;
constexpr float kObjCutRotDot = 0.25f;
constexpr u16 kTrustedStreak = 8;
constexpr float kSubTickSpacing = float(kTickSeconds * 0.85);
constexpr float kBasisLerpSafe = 0.02f;
constexpr int kRow[3] = {0, 4, 8};

std::mutex g_interpMutex;
u64 g_frame = 0;
std::atomic<u64> g_cutTick{~0ull};

bool CutThisTick() {
  return g_cutTick.load(std::memory_order_relaxed) == bd::engine::TickCount();
}

void ReadFloats(const be_f32 *p, float *out, int n) {
  for (int i = 0; i < n; ++i)
    out[i] = p[i];
}

void WriteFloats(be_f32 *p, const float *in, int n) {
  for (int i = 0; i < n; ++i)
    p[i] = in[i];
}

float EntityAlpha(double lastChange) {
  const double held = bd::engine::FrameTime() - lastChange;
  return float(std::clamp(held / kTickSeconds, 0.0, 1.0));
}

float DistSq(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return dx * dx + dy * dy + dz * dz;
}

u32 GuestScratch(u32 &slot, u32 bytes) {
  if (slot == 0)
    slot = bd::gpu::HostHeap::Get().AllocGuest((bytes + 15u) & ~15u, 16);
  return slot;
}

u32 WriteScratch(u32 &slot, const float *v, int n) {
  if (!GuestScratch(slot, u32(n) * 4))
    return 0;
  WriteFloats(bd::mem::at<be_f32>(slot), v, n);
  return slot;
}

template <typename Map> void PruneStale(Map &m) {
  std::erase_if(m, [](const auto &kv) {
    return g_frame - kv.second.lastSeen > kStaleFrames;
  });
}

float BlendStep(float avgStep, float step) {
  return avgStep > 0.0f ? avgStep + (step - avgStep) * kStepBlend : step;
}

bool StepDiscontinuous(float step, float avgStep) {
  return step > kCutFloor && (avgStep <= 0.0f || step > avgStep * kCutRatio);
}

struct StepGauge {
  float avgStep = 0.0f;
  u32 cutRun = 0;

  bool Roll(float step, double spacing, bool discontinuous, bool hard) {
    discontinuous = discontinuous || StepDiscontinuous(step, avgStep);
    if (spacing > kCutRunSpacing)
      cutRun = 0;
    const bool cut = hard || (discontinuous && cutRun < kMaxCutRun);
    cutRun = discontinuous ? cutRun + 1 : 0;
    if (!cut)
      avgStep = BlendStep(avgStep, step);
    return cut;
  }
};

enum class Roll { Held, Shared, Rolled };

template <int N> struct Track {
  float prev[N] = {};
  float curr[N] = {};
  double lastChange = 0.0;
  double spacing = 0.0;
  u64 changeTick = ~0ull;
  u64 lastSeen = 0;
  bool valid = false;

  Roll Advance(const float live[N], double now) {
    if (valid && std::equal(curr, curr + N, live))
      return Roll::Held;
    if (!valid) {
      std::copy_n(live, N, prev);
      std::copy_n(live, N, curr);
      valid = true;
      lastChange = now;
      return Roll::Held;
    }
    spacing = now - lastChange;
    const bool fast = spacing < kFastChangeSeconds;
    std::copy_n(fast ? live : curr, N, prev);
    std::copy_n(live, N, curr);
    lastChange = now;
    changeTick = bd::engine::TickDue() ? bd::engine::TickCount() : ~0ull;
    return fast ? Roll::Shared : Roll::Rolled;
  }

  float Alpha() const {
    if (changeTick == bd::engine::TickCount())
      return bd::engine::Alpha();
    if (changeTick == ~0ull)
      return EntityAlpha(lastChange);
    return 1.0f;
  }
};

bool DecomposeBasis(const float *m, float quat[4], float scale[3]) {
  float r[3][3];
  for (int i = 0; i < 3; ++i) {
    const float *row = m + kRow[i];
    scale[i] = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    if (!(scale[i] > 1e-6f))
      return false;
    for (int j = 0; j < 3; ++j)
      r[i][j] = row[j] / scale[i];
  }
  const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                    r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                    r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
  if (det <= 0.0f)
    return false;
  const float trace = r[0][0] + r[1][1] + r[2][2];
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    quat[0] = 0.25f * s;
    quat[1] = (r[1][2] - r[2][1]) / s;
    quat[2] = (r[2][0] - r[0][2]) / s;
    quat[3] = (r[0][1] - r[1][0]) / s;
  } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
    quat[0] = (r[1][2] - r[2][1]) / s;
    quat[1] = 0.25f * s;
    quat[2] = (r[0][1] + r[1][0]) / s;
    quat[3] = (r[0][2] + r[2][0]) / s;
  } else if (r[1][1] > r[2][2]) {
    const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
    quat[0] = (r[2][0] - r[0][2]) / s;
    quat[1] = (r[0][1] + r[1][0]) / s;
    quat[2] = 0.25f * s;
    quat[3] = (r[1][2] + r[2][1]) / s;
  } else {
    const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
    quat[0] = (r[0][1] - r[1][0]) / s;
    quat[1] = (r[0][2] + r[2][0]) / s;
    quat[2] = (r[1][2] + r[2][1]) / s;
    quat[3] = 0.25f * s;
  }
  return true;
}

void ComposeBasis(const float quat[4], const float scale[3], float *m) {
  const float w = quat[0], x = quat[1], y = quat[2], z = quat[3];
  const float r[3][3] = {
      {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w),
       2.0f * (x * z - y * w)},
      {2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z),
       2.0f * (y * z + x * w)},
      {2.0f * (x * z + y * w), 2.0f * (y * z - x * w),
       1.0f - 2.0f * (x * x + y * y)},
  };
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      m[kRow[i] + j] = r[i][j] * scale[i];
}

void LerpElements(const float *a, const float *b, float t, float *out,
                  int floats) {
  for (int i = 0; i < floats; ++i)
    out[i] = a[i] + (b[i] - a[i]) * t;
}

void LerpMatrix(const float a[16], const float b[16], float t, float out[16]) {
  bool turning = false;
  for (int i = 0; i < 3 && !turning; ++i)
    for (int j = 0; j < 3; ++j)
      if (std::fabs(a[kRow[i] + j] - b[kRow[i] + j]) > kBasisLerpSafe) {
        turning = true;
        break;
      }
  if (!turning) {
    LerpElements(a, b, t, out, 16);
    return;
  }
  float qa[4], qb[4], sa[3], sb[3];
  if (!DecomposeBasis(a, qa, sa) || !DecomposeBasis(b, qb, sb)) {
    LerpElements(a, b, t, out, 16);
    return;
  }
  const float dot =
      qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
  const float sign = dot < 0.0f ? -1.0f : 1.0f;
  float q[4];
  float len = 0.0f;
  for (int i = 0; i < 4; ++i) {
    q[i] = qa[i] + (sign * qb[i] - qa[i]) * t;
    len += q[i] * q[i];
  }
  len = std::sqrt(len);
  if (!(len > 1e-6f)) {
    LerpElements(a, b, t, out, 16);
    return;
  }
  for (int i = 0; i < 4; ++i)
    q[i] /= len;
  float scale[3];
  for (int i = 0; i < 3; ++i)
    scale[i] = sa[i] + (sb[i] - sa[i]) * t;
  ComposeBasis(q, scale, out);
  static constexpr int kPassthrough[] = {3, 7, 11, 12, 13, 14, 15};
  for (const int i : kPassthrough)
    out[i] = a[i] + (b[i] - a[i]) * t;
}

void EyeFromView(const float v[16], float eye[3]) {
  const float tx = v[12], ty = v[13], tz = v[14];
  eye[0] = -(tx * v[0] + ty * v[1] + tz * v[2]);
  eye[1] = -(tx * v[4] + ty * v[5] + tz * v[6]);
  eye[2] = -(tx * v[8] + ty * v[9] + tz * v[10]);
}

void LerpView(const float a[16], const float b[16], float t, float out[16]) {
  float ea[3], eb[3];
  EyeFromView(a, ea);
  EyeFromView(b, eb);
  LerpMatrix(a, b, t, out);
  float eye[3];
  for (int i = 0; i < 3; ++i)
    eye[i] = ea[i] + (eb[i] - ea[i]) * t;
  for (int j = 0; j < 3; ++j)
    out[12 + j] =
        -(eye[0] * out[j] + eye[1] * out[4 + j] + eye[2] * out[8 + j]);
}

float MinRowDot(const float *cur, const float *prv) {
  float worst = 1.0f;
  for (int r = 0; r < 12; r += 4) {
    float dot = 0.0f, mc = 0.0f, mp = 0.0f;
    for (int i = r; i < r + 3; ++i) {
      const float c = cur[i], p = prv[i];
      dot += c * p;
      mc += c * c;
      mp += p * p;
    }
    const float denom = std::sqrt(mc * mp);
    if (denom > 1e-6f && dot / denom < worst)
      worst = dot / denom;
  }
  return worst;
}

struct MatrixTrack : Track<16> {
  StepGauge gauge;
  bool cut = false;

  void DetectCut();
};

std::unordered_map<u32, MatrixTrack> g_views;
u32 g_viewScratch = 0;
u32 g_projScratch = 0;

struct Vec3Track : Track<3> {
  bool raw = false;
};

enum class Vec3Slot : u32 { MirrorEye, MirrorTarget, ShaderEye, DofFocus, Count };

std::unordered_map<u32, Vec3Track> g_vec3Tracks;
u32 g_vec3Scratch[u32(Vec3Slot::Count)] = {};

constexpr int kWorldFloats = 16;
u32 g_worldScratch = 0;

struct FloatSnapshot {
  std::vector<float> prev;
  std::vector<float> curr;
  StepGauge gauge;
  float avgSpacing = 0.0f;
  float spacing = 0.0f;
  double lastChange = 0.0;
  u64 lastSeen = 0;
  bool valid = false;
  bool cut = false;
  u16 streak = 0;
};

std::unordered_map<u64, FloatSnapshot> g_objSnapshots;
u64 g_worldKey = 0;
u64 g_nodeScope = 0;
u32 g_listObject = 0;
u32 g_listSeq = 0;
std::unordered_map<u32, u64> g_recordKeys;
u32 g_recordSeq = 0;

u64 NodeIdentity(u32 nodeIdx) {
  const u32 vo = bd::mem::try_load<u32>(bd::engine::addr::kCameraRenderVO);
  return vo ? ((u64(nodeIdx) + 1) << 32) | vo : 0;
}

struct DrawRecord_t {
  /* 0x000 */ u8 _pad000[0x10];
  /* 0x010 */ be_f32 worldMatrix[16];
  /* 0x050 */ u8 _pad050[0xFC - 0x50];
  /* 0x0FC */ be_u32 part;
  /* 0x100 */ u8 _pad100[0x110 - 0x100];
  /* 0x110 */ be_u32 visualObject;
  /* 0x114 */ u8 _pad114[0x118 - 0x114];
  /* 0x118 */ be_u16 range[3];

  u64 Key() const {
    u64 h = 1469598103934665603ull;
    const auto mix = [&h](u32 v) { h = (h ^ v) * 1099511628211ull; };
    mix(visualObject);
    mix(part);
    for (const be_u16 &r : range)
      mix(r);
    return (1ull << 61) | (h >> 3);
  }
};
static_assert(offsetof(DrawRecord_t, worldMatrix) == 0x010);
static_assert(offsetof(DrawRecord_t, part) == 0x0FC);
static_assert(offsetof(DrawRecord_t, visualObject) == 0x110);
static_assert(offsetof(DrawRecord_t, range) == 0x118);

constexpr u32 kRecMatrix = offsetof(DrawRecord_t, worldMatrix);

thread_local bool t_inRecordReplay = false;

bool SubTickWriter(const FloatSnapshot &e) {
  return e.avgSpacing != 0.0f && e.avgSpacing < kSubTickSpacing;
}

bool ProjectorView(u32 va) {
  return va == bd::engine::addr::kCubeShadowLightView ||
         (va >= bd::engine::addr::kProjectorMapInfos &&
          va < bd::engine::addr::kProjectorMapInfosEnd);
}

bool InShadowDepthPass() {
  auto *view = bd::mem::try_at<be_u32>(bd::engine::addr::kRenderView);
  auto *sun = bd::mem::try_at<be_u32>(bd::engine::addr::kShadowLightView);
  auto *cube = bd::mem::try_at<be_u32>(bd::engine::addr::kCubeShadowLightView);
  if (!view)
    return false;
  bool isSun = sun != nullptr;
  bool isCube = cube != nullptr;
  for (int i = 0; i < 16 && (isSun || isCube); ++i) {
    const u32 v = view[i];
    if (isSun && u32(sun[i]) != v)
      isSun = false;
    if (isCube && u32(cube[i]) != v)
      isCube = false;
  }
  return isSun || isCube;
}

constexpr u32 kVOBoneCount = 0x74C;
constexpr u32 kVOCurrBones = 0xA48;
constexpr u32 kMaxBoneMatrices = 1024;
constexpr double kBoneCopyFresh = kTickSeconds * 1.5;
constexpr double kBoneArrayLinger = 2.0;
constexpr float kParentBoneDist = 16.0f;

struct BoneArray {
  std::vector<float> prevPose;
  std::vector<float> tickTarget;
  std::vector<float> lastLive;
  u64 blendTick = ~0ull;
  u64 hideTick = ~0ull;
  u32 count = 0;
  u32 currEA = 0;
  double copyTime = 0.0;
  double blendTime = -1.0;
  u32 blended = 0;
  u32 scratch = 0;
  u32 scratchCount = 0;
  bool newborn = false;
};

std::unordered_map<u32, BoneArray> g_boneArrays;

thread_local bool t_inCameraRender = false;
thread_local bool t_boneWriter = false;

bool BoneArrayOwned(u32 va) {
  for (const auto &[holder, e] : g_boneArrays) {
    if (e.currEA && va - e.currEA < e.count * 64u)
      return true;
    if (e.scratch && va - e.scratch < e.scratchCount * 64u)
      return true;
  }
  return false;
}

void RegisterBoneArray(u32 vo) {
  if (!bd::engine::InterpolationActive() || vo == 0)
    return;
  const u32 count = bd::mem::try_load<u32>(vo + kVOBoneCount);
  if (count == 0 || count > kMaxBoneMatrices)
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  BoneArray &e = g_boneArrays[vo + kVOCurrBones];
  e.count = count;
  e.copyTime = bd::engine::FrameTime();
}

void PruneBoneArrays() {
  const double now = bd::engine::FrameTime();
  for (auto it = g_boneArrays.begin(); it != g_boneArrays.end();) {
    if (now - it->second.copyTime > kBoneArrayLinger) {
      if (it->second.scratch)
        bd::gpu::HostHeap::Get().FreeGuest(it->second.scratch);
      it = g_boneArrays.erase(it);
    } else {
      ++it;
    }
  }
}

void SeedNewbornPose(BoneArray &e, u64 tick) {
  e.prevPose = e.tickTarget;
  const float *at = &e.tickTarget[12];
  float best = kParentBoneDist * kParentBoneDist;
  float delta[3] = {0.0f, 0.0f, 0.0f};
  bool found = false;
  for (auto &[holder, p] : g_boneArrays) {
    const size_t n = size_t(p.count) * 16;
    if (&p == &e || p.count < 2 || p.tickTarget.size() != n ||
        p.prevPose.size() != n)
      continue;
    const float *prv = nullptr;
    const float *cur = nullptr;
    const be_f32 *live = nullptr;
    if (p.blendTick == tick && p.hideTick != tick) {
      cur = p.tickTarget.data();
      prv = p.prevPose.data();
    } else if (p.blendTick + 1 == tick) {
      live = bd::mem::try_at<be_f32>(p.currEA);
      if (!live)
        continue;
      prv = p.tickTarget.data();
    } else {
      continue;
    }
    for (u32 b = 0; b < p.count; ++b) {
      const size_t t = size_t(b) * 16 + 12;
      float pos[3];
      for (int k = 0; k < 3; ++k)
        pos[k] = cur ? cur[t + k] : float(live[t + k]);
      const float d2 = DistSq(pos, at);
      if (d2 >= best)
        continue;
      best = d2;
      found = true;
      for (int k = 0; k < 3; ++k)
        delta[k] = pos[k] - prv[t + k];
    }
  }
  if (!found)
    return;
  for (size_t i = 0; i < e.prevPose.size(); i += 16)
    for (int k = 0; k < 3; ++k)
      e.prevPose[i + 12 + size_t(k)] -= delta[k];
}

bool BoneDegenerate(const float *m) {
  for (int r = 0; r < 12; r += 4) {
    const float len2 = m[r] * m[r] + m[r + 1] * m[r + 1] + m[r + 2] * m[r + 2];
    if (!(len2 > 1e-12f))
      return true;
  }
  return false;
}

enum class BoneStep { Lerp, Snap, Cut };

BoneStep ClassifyBone(const float *cur, const float *prv) {
  if (DistSq(cur + 12, prv + 12) <= kCutDistance * kCutDistance)
    return BoneStep::Lerp;
  if (BoneDegenerate(cur) || BoneDegenerate(prv))
    return BoneStep::Snap;
  return BoneStep::Cut;
}

struct AnimeClock {
  float prev = 0.0f;
  float curr = 0.0f;
  double lastChange = 0.0;
  u64 lastSeen = 0;
  bool valid = false;
};

std::unordered_map<u32, AnimeClock> g_animeClocks;

bool AnimeClockDiscontinuous(float delta, float speed) {
  if (speed == 0.0f)
    return true;
  return delta * speed < 0.0f || std::fabs(delta) > std::fabs(speed) * 1.5f;
}

enum class Snapshot { Missing, Ready, Rolled, First, Shared };

Snapshot AdvanceSnapshot(u64 key, u32 srcVa, int floats, FloatSnapshot *&out) {
  out = nullptr;
  auto *src = bd::mem::try_at<be_f32>(srcVa);
  if (!src)
    return Snapshot::Missing;
  FloatSnapshot &e = g_objSnapshots[key];
  e.lastSeen = g_frame;
  out = &e;
  if (e.curr.size() != size_t(floats)) {
    e.curr.assign(size_t(floats), 0.0f);
    e.prev.assign(size_t(floats), 0.0f);
    e.valid = false;
  }
  const double now = bd::engine::FrameTime();
  if (!e.valid) {
    ReadFloats(src, e.curr.data(), floats);
    e.prev = e.curr;
    e.valid = true;
    e.lastChange = now;
    return Snapshot::First;
  }
  bool changed = false;
  for (int i = 0; i < floats && !changed; ++i)
    changed = e.curr[size_t(i)] != float(src[i]);
  if (!changed)
    return Snapshot::Ready;
  const double spacing = now - e.lastChange;
  const float sample = float(std::min(spacing, kTickSeconds * 4.0));
  e.spacing = float(spacing);
  e.avgSpacing = e.avgSpacing == 0.0f
                     ? sample
                     : e.avgSpacing + (sample - e.avgSpacing) * 0.25f;
  e.prev.swap(e.curr);
  ReadFloats(src, e.curr.data(), floats);
  e.lastChange = now;
  if (spacing < kFastChangeSeconds) {
    e.prev = e.curr;
    return Snapshot::Shared;
  }
  return Snapshot::Rolled;
}

u32 LerpToScratch(const FloatSnapshot &e, float a, u32 &scratch,
                  int scratchFloats) {
  if (!GuestScratch(scratch, u32(scratchFloats) * 4))
    return 0;
  auto *dst = bd::mem::at<be_f32>(scratch);
  const size_t floats = e.curr.size();
  float blended[16];
  size_t i = 0;
  for (; i + 16 <= floats; i += 16) {
    const float *prv = &e.prev[i];
    const float *cur = &e.curr[i];
    if (MinRowDot(cur, prv) < kObjCutRotDot)
      WriteFloats(dst + i, cur, 16);
    else {
      LerpMatrix(prv, cur, a, blended);
      WriteFloats(dst + i, blended, 16);
    }
  }
  for (; i < floats; ++i)
    dst[i] = e.prev[i] + (e.curr[i] - e.prev[i]) * a;
  return scratch;
}

} // namespace

bool bdLogicTickGateHook(PPCRegister &r28) {
  if (bd::engine::TickDue())
    return false;
  r28.u64 = 0xDEAD0000;
  return true;
}

bool bdFrameClockGateHook() { return !bd::engine::TickDue(); }

void bdEffectParticleSpawnHook(PPCRegister &r11) {
  const u32 vo = r11.u32;
  if (!bd::engine::InterpolationActive() || !vo)
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  BoneArray &e = g_boneArrays[vo + kVOCurrBones];
  e.copyTime = bd::engine::FrameTime();
  e.newborn = true;
}

bool bdCharaBoneChainGateHook(PPCRegister &r3) {
  static std::unordered_map<u32, u64> seen;
  if (!bd::engine::InterpolationActive() || r3.u32 == 0)
    return false;
  const u64 tick = bd::engine::TickCount();
  std::lock_guard<std::mutex> lock(g_interpMutex);
  if (seen.size() > 256)
    seen.clear();
  auto [it, inserted] = seen.try_emplace(r3.u32, tick);
  if (inserted)
    return false;
  if (it->second == tick)
    return true;
  it->second = tick;
  return false;
}

void bdActEvClockMismatchHook(PPCRegister &r11) {
  if (!bd::engine::TickDue())
    r11.u32 ^= 1u;
}

bool bdTickGateHook() { return !bd::engine::TickDue(); }

namespace {

double FrameStepRatio() {
  return bd::engine::InterpolationActive()
             ? bd::engine::FrameDelta() / kTickSeconds
             : 1.0;
}

} // namespace

void bdFrameStepScaleHook(PPCRegister &step) { step.f64 *= FrameStepRatio(); }

void bdFrameStepSumHook(PPCRegister &sum, PPCRegister &step) {
  sum.f64 -= step.f64 * (1.0 - FrameStepRatio());
}

void bdFrameFactorScaleHook(PPCRegister &factor) {
  if (factor.f64 > 0.0)
    factor.f64 = std::pow(factor.f64, FrameStepRatio());
}

void bdPopUpAgeLerpHook(PPCRegister &age) {
  if (bd::engine::InterpolationActive())
    age.f64 += bd::engine::Alpha();
}

namespace {

constexpr u32 kMaxChainEntries = 4096;

} // namespace

void bdAnimeChainEnableActiveHook(PPCRegister &r28) {
  using namespace bd::engine;
  auto *anime = bd::mem::try_at<AnimeData_t>(r28.u32);
  if (!anime || anime->activeChain.size() > kMaxChainEntries)
    return;
  const f32 clock = anime->frame;
  for (u32 i = 0; i < anime->activeChain.size(); ++i) {
    auto *node = anime->activeChain[i].get();
    if (!node || node->elements.size() > kMaxChainEntries)
      continue;
    for (u32 j = 0; j < node->elements.size(); ++j) {
      auto *element = node->elements[j].get();
      if (!element)
        continue;
      element->timer = clock;
      if (auto *child = element->childAnime.get())
        child->childEnabled = 1;
    }
  }
}

namespace {

struct PlyTask_t {
  /* 0x0000 */ u8 _pad0000[0xBC4];
  /* 0x0BC4 */ be_f32 ambient[3];
  /* 0x0BD0 */ u8 _pad0BD0[0xC40 - 0xBD0];
  /* 0x0C40 */ be_f32 alpha;
  /* 0x0C44 */ u8 _pad0C44[0xC54 - 0xC44];
  /* 0x0C54 */ be_u32 alphaDirty;
  /* 0x0C58 */ u8 _pad0C58[0x28C0 - 0xC58];
  /* 0x28C0 */ be_u32 blinkArm;
};
static_assert(offsetof(PlyTask_t, ambient) == 0xBC4);
static_assert(offsetof(PlyTask_t, alpha) == 0xC40);
static_assert(offsetof(PlyTask_t, alphaDirty) == 0xC54);
static_assert(offsetof(PlyTask_t, blinkArm) == 0x28C0);

} // namespace

bool bdPlayerAmbientRampGateHook(PPCRegister &r31) {
  if (!bd::engine::InterpolationActive())
    return false;
  auto *task = TryStruct<PlyTask_t>(r31.u32);
  if (!task)
    return true;
  const float green = task->ambient[1];
  if (green >= 1.0f)
    return true;
  const float next =
      green + 0.1f * float(bd::engine::FrameDelta() / kTickSeconds);
  if (next >= 1.0f) {
    task->ambient[0] = 1.0f;
    task->ambient[1] = 1.0f;
    task->ambient[2] = 1.0f;
  } else {
    task->ambient[1] = next;
    task->ambient[2] = next;
  }
  return true;
}

namespace {

constexpr u32 kEvtPlaying = 2;
constexpr int kMaxEventTasks = 8;
constexpr int kMaxEventChildren = 256;

struct IssEvent_t {
  /* 0x000 */ u8 _pad000[0x68];
  /* 0x068 */ be_f32 frame;
  /* 0x06C */ u8 _pad06C[0x88 - 0x6C];
  /* 0x088 */ be_u32 state;
  /* 0x08C */ u8 _pad08C[0x3EC - 0x8C];
  /* 0x3EC */ be_f32 speed;
  /* 0x3F0 */ be_u32 childList;
};
static_assert(offsetof(IssEvent_t, frame) == 0x68);
static_assert(offsetof(IssEvent_t, state) == 0x88);
static_assert(offsetof(IssEvent_t, speed) == 0x3EC);
static_assert(offsetof(IssEvent_t, childList) == 0x3F0);

struct IssChild_t {
  /* 0x00 */ be_u32 vtable;
  /* 0x04 */ u8 _pad004[0x80 - 0x04];
  /* 0x80 */ be_u32 parent;
  /* 0x84 */ be_u32 next;
};
static_assert(offsetof(IssChild_t, parent) == 0x80);
static_assert(offsetof(IssChild_t, next) == 0x84);

struct IssVtable_t {
  /* 0x00 */ u8 _pad000[0x08];
  /* 0x08 */ be_u32 update;
};
static_assert(offsetof(IssVtable_t, update) == 0x08);

struct IssActor_t {
  /* 0x000 */ u8 _pad000[0x150];
  /* 0x150 */ be_u32 hidePending;
  /* 0x154 */ u8 _pad154[0x238 - 0x154];
  /* 0x238 */ be_f32 motionCursor;
  /* 0x23C */ be_f32 motionEnd;
  /* 0x240 */ u8 _pad240[0x2CC - 0x240];
  /* 0x2CC */ be_u32 guided;
};
static_assert(offsetof(IssActor_t, hidePending) == 0x150);
static_assert(offsetof(IssActor_t, motionCursor) == 0x238);
static_assert(offsetof(IssActor_t, motionEnd) == 0x23C);
static_assert(offsetof(IssActor_t, guided) == 0x2CC);

struct IssObject_t {
  /* 0x000 */ u8 _pad000[0x9E0];
  /* 0x9E0 */ be_u32 visible;
  /* 0x9E4 */ be_u32 hidePending;
  /* 0x9E8 */ u8 _pad9E8[0xA5C - 0x9E8];
  /* 0xA5C */ be_f32 motionCursor;
  /* 0xA60 */ be_f32 motionEnd;
};
static_assert(offsetof(IssObject_t, visible) == 0x9E0);
static_assert(offsetof(IssObject_t, hidePending) == 0x9E4);
static_assert(offsetof(IssObject_t, motionCursor) == 0xA5C);
static_assert(offsetof(IssObject_t, motionEnd) == 0xA60);

struct AnimClip_t {
  /* 0x00 */ u8 _pad00[0x04];
  /* 0x04 */ be_u16 length;
};
static_assert(offsetof(AnimClip_t, length) == 0x04);

struct AnimSlot_t {
  /* 0x00 */ u8 _pad00[0x0C];
  /* 0x0C */ be_u32 clip;
};
static_assert(offsetof(AnimSlot_t, clip) == 0x0C);

struct CharaAnim_t {
  /* 0x000 */ u8 _pad000[0x754];
  /* 0x754 */ be_u32 loopFlag;
  /* 0x758 */ u8 _pad758[0x768 - 0x758];
  /* 0x768 */ be_f32 cursor;
  /* 0x76C */ be_f32 cursorRate;
  /* 0x770 */ u8 _pad770[0x780 - 0x770];
  /* 0x780 */ be_u32 anim;
};
static_assert(offsetof(CharaAnim_t, loopFlag) == 0x754);
static_assert(offsetof(CharaAnim_t, cursor) == 0x768);
static_assert(offsetof(CharaAnim_t, cursorRate) == 0x76C);
static_assert(offsetof(CharaAnim_t, anim) == 0x780);

constexpr u32 kSceneSpeedMulEA = 0x82DDA880;
constexpr f32 kWindowClosingMargin = 1.5f;

constexpr u32 kIssCameraUpdateEA = 0x824046C0;
constexpr u32 kIssObjectUpdateEA = 0x82406688;
constexpr u32 kIssMapUpdateEA = 0x823F4F40;
constexpr u32 kIssEffectVf02EA = 0x82411610;
constexpr u32 kIssSpriteVf02EA = 0x82412688;
constexpr u32 kIssLightVf02EA = 0x82416DD0;

constexpr u32 kEvtMovementUpdates[] = {
    kIssObjectUpdateEA, kIssCameraUpdateEA, kIssMapUpdateEA,
    kIssEffectVf02EA,   kIssSpriteVf02EA,   kIssLightVf02EA,
};

struct EvtDriveState {
  u32 frameBits = 0;
  bool framesSeen = false;
  bool advancing = false;
  float lastAlpha = 0.0f;
  float driven = 0.0f;
  bool drove = false;
};
std::unordered_map<u32, EvtDriveState> g_evtDrive;
std::unordered_map<u32, f32> g_evtTickAdvanced;
std::unordered_set<u32> g_evtTickScaled;

std::atomic<double> g_evtEngagedUntil{0.0};
std::atomic<bool> g_evtEngaged{false};

void UpdateEventEngagement() {
  g_evtEngaged.store(
      bd::engine::EventScenePlaying() &&
          bd::engine::FrameTime() <
              g_evtEngagedUntil.load(std::memory_order_relaxed) &&
          !bd::engine::Battle{}.IsActive(),
      std::memory_order_relaxed);
}

bool EventSceneEngaged() {
  return g_evtEngaged.load(std::memory_order_relaxed);
}

bool g_hostEvtDrive = false;
bool g_evtWindowClosing = false;

bool IsMovementUpdate(u32 fn) {
  for (const u32 v : kEvtMovementUpdates)
    if (v == fn)
      return true;
  return false;
}

IssEvent_t *DrivenEvent(u32 childEA) {
  auto *child = TryStruct<IssChild_t>(childEA);
  if (!child)
    return nullptr;
  const u32 parent = child->parent;
  if (!g_hostEvtDrive &&
      g_evtTickAdvanced.find(parent) == g_evtTickAdvanced.end())
    return nullptr;
  return TryStruct<IssEvent_t>(parent);
}

template <typename F> void ForEachEventChild(const IssEvent_t &evt, F &&visit) {
  u32 childEA = evt.childList;
  for (int guard = 0; childEA != 0 && guard < kMaxEventChildren; ++guard) {
    auto *child = TryStruct<IssChild_t>(childEA);
    if (!child)
      break;
    visit(childEA, *child);
    childEA = child->next;
  }
}

bool MotionWindowClosing(f32 cursor, f32 end, f32 speed) {
  return end > 0.0f && cursor < end &&
         end - cursor <= speed * kWindowClosingMargin;
}

constexpr int kCamOutputFloats = 64;
constexpr int kCamSampleFloats = 16;
constexpr u32 kCamPosGlobalEA = 0x82DDA8D4;
constexpr u32 kCamDirGlobalEA = 0x82DDA8E0;

struct IssCamera_t {
  /* 0x0000 */ u8 _pad0000[0x9C];
  /* 0x009C */ be_f32 output[kCamOutputFloats];
  /* 0x019C */ u8 _pad019C[0x314 - 0x19C];
  /* 0x0314 */ be_f32 dir[2];
  /* 0x031C */ u8 _pad031C[0x11B8 - 0x31C];
  /* 0x11B8 */ be_f32 sample[kCamSampleFloats];
};
static_assert(offsetof(IssCamera_t, output) == 0x9C);
static_assert(offsetof(IssCamera_t, dir) == 0x314);
static_assert(offsetof(IssCamera_t, sample) == 0x11B8);

struct CamSave {
  f32 output[kCamOutputFloats];
  f32 dir[2];
  f32 sample[kCamSampleFloats];
  f32 posGlobal[3];
  f32 dirGlobal[3];
  bool valid = false;
};
std::unordered_map<u32, CamSave> g_camSaves;
std::unordered_set<u32> g_evtCameras;

bool CameraOutputFinite(const IssCamera_t &cam) {
  for (int i = 0; i < kCamOutputFloats; ++i)
    if (std::isnan(f32(cam.output[i])))
      return false;
  for (int i = 0; i < kCamSampleFloats; ++i)
    if (std::isnan(f32(cam.sample[i])))
      return false;
  return true;
}

void CameraGuardCapture(u32 camEA) {
  auto *cam = TryStruct<IssCamera_t>(camEA);
  if (!cam || !CameraOutputFinite(*cam))
    return;
  CamSave &s = g_camSaves[camEA];
  ReadFloats(cam->output, s.output, kCamOutputFloats);
  ReadFloats(cam->dir, s.dir, 2);
  ReadFloats(cam->sample, s.sample, kCamSampleFloats);
  ReadFloats(bd::mem::at<be_f32>(kCamPosGlobalEA), s.posGlobal, 3);
  ReadFloats(bd::mem::at<be_f32>(kCamDirGlobalEA), s.dirGlobal, 3);
  s.valid = true;
}

void CameraGuardRepair(u32 camEA) {
  auto *cam = TryStruct<IssCamera_t>(camEA);
  if (!cam || CameraOutputFinite(*cam))
    return;
  auto it = g_camSaves.find(camEA);
  if (it == g_camSaves.end() || !it->second.valid)
    return;
  const CamSave &s = it->second;
  WriteFloats(cam->output, s.output, kCamOutputFloats);
  WriteFloats(cam->dir, s.dir, 2);
  WriteFloats(cam->sample, s.sample, kCamSampleFloats);
  WriteFloats(bd::mem::at<be_f32>(kCamPosGlobalEA), s.posGlobal, 3);
  WriteFloats(bd::mem::at<be_f32>(kCamDirGlobalEA), s.dirGlobal, 3);
}

f32 DriveEventChildren(IssEvent_t &evt, f32 frac) {
  const f32 speed = evt.speed;
  if (speed == 0.0f)
    return 0.0f;
  auto *dispatcher = REX_KERNEL_STATE()->function_dispatcher();
  evt.speed = speed * frac;
  g_hostEvtDrive = true;
  g_evtWindowClosing = false;
  ForEachEventChild(evt, [&](u32 childEA, const IssChild_t &child) {
    const u32 vtableEA = child.vtable;
    const u32 fn =
        vtableEA ? u32(bd::mem::at<IssVtable_t>(vtableEA)->update) : 0;
    if (!IsMovementUpdate(fn))
      return;
    if (fn == kIssCameraUpdateEA)
      g_evtCameras.insert(childEA);
    else if (auto *host = dispatcher->GetFunction(fn))
      rex::ppc::GuestToHostFunction<void>(host, childEA);
  });
  g_hostEvtDrive = false;
  evt.speed = speed;
  return speed * frac;
}

void StepEventScenes() {
  g_evtTickAdvanced.clear();
  const bool tick = bd::engine::TickDue();
  if (tick)
    g_evtTickScaled.clear();
  if (!bd::engine::InterpolationActive() || !bd::engine::EventScenePlaying()) {
    g_evtDrive.clear();
    g_camSaves.clear();
    g_evtCameras.clear();
    return;
  }
  u32 live[kMaxEventTasks];
  const int n = bd::engine::Cutscene().Tasks(live, kMaxEventTasks);
  std::erase_if(g_evtDrive, [&](const auto &kv) {
    return std::find(live, live + n, kv.first) == live + n;
  });
  for (int i = 0; i < n; ++i) {
    const u32 evtEA = live[i];
    auto *evt = TryStruct<IssEvent_t>(evtEA);
    if (!evt || u32(evt->state) != kEvtPlaying)
      continue;
    EvtDriveState &st = g_evtDrive[evtEA];
    if (tick) {
      const u32 bits = std::bit_cast<u32>(evt->frame.value);
      st.advancing = st.framesSeen && bits != st.frameBits;
      st.frameBits = bits;
      st.framesSeen = true;
      if (st.drove)
        g_evtTickAdvanced[evtEA] = st.driven;
      st.lastAlpha = 0.0f;
      st.driven = 0.0f;
      st.drove = false;
    } else if (st.advancing) {
      const f32 a = bd::engine::Alpha();
      if (a > st.lastAlpha) {
        st.driven += DriveEventChildren(*evt, a - st.lastAlpha);
        st.lastAlpha = a;
        st.drove = true;
      }
    }
  }
}

struct EvtSpeedRemainder {
  IssEvent_t *evt = nullptr;
  f32 speed = 0.0f;

  explicit EvtSpeedRemainder(u32 childEA) {
    if (g_evtTickAdvanced.empty() || g_hostEvtDrive)
      return;
    auto *child = TryStruct<IssChild_t>(childEA);
    if (!child)
      return;
    const u32 parent = child->parent;
    const auto it = g_evtTickAdvanced.find(parent);
    if (it == g_evtTickAdvanced.end())
      return;
    evt = TryStruct<IssEvent_t>(parent);
    if (!evt)
      return;
    if (!g_evtTickScaled.insert(childEA).second) {
      evt = nullptr;
      return;
    }
    speed = evt->speed;
    evt->speed = std::max(speed - it->second, 0.0f);
  }

  ~EvtSpeedRemainder() {
    if (evt)
      evt->speed = speed;
  }
};

constexpr u32 kIssActorApplySpecialModelFlagsEA = 0x82410A18;
std::unordered_set<u32> g_evtObjectHides;
std::unordered_set<u32> g_evtActorHides;

void FlushEvtHidePending() {
  if (g_evtObjectHides.empty() && g_evtActorHides.empty())
    return;
  std::unordered_set<u32> live;
  u32 evts[kMaxEventTasks];
  const int n = bd::engine::Cutscene().Tasks(evts, kMaxEventTasks);
  for (int i = 0; i < n; ++i)
    if (auto *evt = TryStruct<IssEvent_t>(evts[i]))
      ForEachEventChild(*evt, [&](u32 childEA, const IssChild_t &) {
        live.insert(childEA);
      });
  for (const u32 ea : g_evtObjectHides) {
    auto *obj = live.count(ea) ? TryStruct<IssObject_t>(ea) : nullptr;
    if (obj && u32(obj->hidePending) != 0)
      obj->visible = 0u;
  }
  auto *apply = REX_KERNEL_STATE()->function_dispatcher()->GetFunction(
      kIssActorApplySpecialModelFlagsEA);
  for (const u32 ea : g_evtActorHides) {
    auto *actor = live.count(ea) ? TryStruct<IssActor_t>(ea) : nullptr;
    if (apply && actor && u32(actor->hidePending) != 0)
      rex::ppc::GuestToHostFunction<void>(apply, ea);
  }
  g_evtObjectHides.clear();
  g_evtActorHides.clear();
}

} // namespace

bool bdEvtObjectHideDeferHook(PPCRegister &r31) {
  if (!bd::engine::InterpolationActive())
    return false;
  g_evtObjectHides.insert(r31.u32);
  return true;
}

bool bdEvtActorHideDeferHook(PPCRegister &r31) {
  if (!bd::engine::InterpolationActive())
    return false;
  g_evtActorHides.insert(r31.u32);
  return true;
}

namespace {

std::atomic<u32> g_nodeDraws{0};
u32 g_tickNodeDraws = 0;
bool g_drawTickDue = true;
bool g_swapTickDue = true;
constexpr u32 kSparseMinNodes = 8;

constexpr u32 kLipPlaying = 1;

struct LipRecord_t {
  /* 0x00 */ u8 _pad000[0x08];
  /* 0x08 */ be_i32 level;
  /* 0x0C */ u8 _pad00C[0x04];
};
static_assert(offsetof(LipRecord_t, level) == 0x08);
static_assert(sizeof(LipRecord_t) == 0x10);

struct LipClip_t {
  /* 0x00 */ be_u32 records;
  /* 0x04 */ u8 _pad004[0x44 - 0x04];
  /* 0x44 */ be_u32 recordCount;
};
static_assert(offsetof(LipClip_t, recordCount) == 0x44);

struct LipPlayer_t {
  /* 0x00 */ be_u32 state;
  /* 0x04 */ be_u32 clip;
  /* 0x08 */ be_f32 cursor;
  /* 0x0C */ u8 _pad00C[0x30 - 0x0C];
  /* 0x30 */ u8 viseme;
  /* 0x31 */ u8 prevViseme;
  /* 0x32 */ u8 _pad032[0x34 - 0x32];
  /* 0x34 */ be_f32 amp;
  /* 0x38 */ u8 _pad038[0x50 - 0x38];
  /* 0x50 */ be_u32 owner;
};
static_assert(offsetof(LipPlayer_t, cursor) == 0x08);
static_assert(offsetof(LipPlayer_t, viseme) == 0x30);
static_assert(offsetof(LipPlayer_t, prevViseme) == 0x31);
static_assert(offsetof(LipPlayer_t, amp) == 0x34);
static_assert(offsetof(LipPlayer_t, owner) == 0x50);

struct LipOwner_t {
  /* 0x000 */ u8 _pad000[0x83C];
  /* 0x83C */ be_f32 blend;
};
static_assert(offsetof(LipOwner_t, blend) == 0x83C);

f64 LipStaircase(f64 db) {
  if (db >= -15.0)
    return 1.0;
  if (db < -40.0)
    return 0.4;
  return 1.0 + std::floor((db + 15.0) / 5.0) * 0.1;
}

f64 LipContinuous(f64 db) {
  return std::clamp(0.4 + (db + 45.0) * (0.6 / 30.0), 0.4, 1.0);
}

} // namespace

REX_EXTERN(__imp__LipPlayerApply);
REX_HOOK_RAW(LipPlayerApply) {
  auto *player = TryStruct<LipPlayer_t>(ctx.r3.u32);
  auto *owner = player ? TryStruct<LipOwner_t>(u32(player->owner)) : nullptr;
  const u32 prevVis = player ? player->prevViseme : 0;
  const f32 blendBefore = owner ? f32(owner->blend) : 0.0f;
  __imp__LipPlayerApply(ctx, base);
  if (!owner || !bd::engine::InterpolationActive())
    return;
  const u32 vis = player->viseme;
  if (vis != prevVis && vis != 0 && prevVis != 0)
    owner->blend = blendBefore;
}

REX_EXTERN(__imp__LipPlayerSample);
REX_HOOK_RAW(LipPlayerSample) {
  auto *player = TryStruct<LipPlayer_t>(ctx.r3.u32);
  __imp__LipPlayerSample(ctx, base);
  if (!player || !bd::engine::InterpolationActive() ||
      !bd::engine::EventScenePlaying())
    return;
  if (u32(player->state) != kLipPlaying)
    return;
  auto *clip = TryStruct<LipClip_t>(u32(player->clip));
  if (!clip)
    return;
  const f32 cursor = player->cursor;
  const u32 count = clip->recordCount;
  const u32 idx = u32(cursor) * 2;
  const u32 records = clip->records;
  if (records == 0 || idx >= count)
    return;
  const u32 next = idx + 2 < count ? idx + 2 : idx;
  constexpr u32 kStride = u32(sizeof(LipRecord_t));
  auto *rec0 = TryStruct<LipRecord_t>(records + idx * kStride);
  auto *rec1 = TryStruct<LipRecord_t>(records + next * kStride);
  if (!rec0 || !rec1)
    return;
  const f64 db0 = i32(rec0->level);
  const f64 db1 = i32(rec1->level);
  const f64 frac = cursor - std::floor(cursor);
  const f64 cont =
      LipContinuous(db0) + (LipContinuous(db1) - LipContinuous(db0)) * frac;
  player->amp = f32(cont * (f32(player->amp) / LipStaircase(db0)));
}

REX_EXTERN(__imp__issEvent__Update);
REX_HOOK_RAW(issEvent__Update) {
  const u32 evt = ctx.r3.u32;
  __imp__issEvent__Update(ctx, base);
  auto *evtTask = TryStruct<IssEvent_t>(evt);
  if (evtTask && u32(evtTask->state) == kEvtPlaying) {
    g_evtEngagedUntil.store(bd::engine::FrameTime() + kTickSeconds * 2.0,
                            std::memory_order_relaxed);
  }
}

REX_EXTERN(__imp__issObject__Update);
REX_HOOK_RAW(issObject__Update) {
  const u32 objectEA = ctx.r3.u32;
  EvtSpeedRemainder z(objectEA);
  auto *evt = DrivenEvent(objectEA);
  if (auto *object = evt ? TryStruct<IssObject_t>(objectEA) : nullptr) {
    g_evtWindowClosing = MotionWindowClosing(
        object->motionCursor, object->motionEnd, evt->speed);
  }
  __imp__issObject__Update(ctx, base);
  g_evtWindowClosing = false;
}

REX_EXTERN(__imp__issCamera__Update);
REX_HOOK_RAW(issCamera__Update) {
  const u32 cam = ctx.r3.u32;
  const bool guard = bd::engine::InterpolationActive() &&
                     g_evtCameras.find(cam) != g_evtCameras.end();
  if (guard)
    CameraGuardCapture(cam);
  __imp__issCamera__Update(ctx, base);
  if (guard)
    CameraGuardRepair(cam);
}

REX_EXTERN(__imp__issMap__Update);
REX_HOOK_RAW(issMap__Update) {
  EvtSpeedRemainder z(ctx.r3.u32);
  __imp__issMap__Update(ctx, base);
}

REX_EXTERN(__imp__issEffect__Update);
REX_HOOK_RAW(issEffect__Update) {
  EvtSpeedRemainder z(ctx.r3.u32);
  __imp__issEffect__Update(ctx, base);
}

REX_EXTERN(__imp__issSprite__Update);
REX_HOOK_RAW(issSprite__Update) {
  EvtSpeedRemainder z(ctx.r3.u32);
  __imp__issSprite__Update(ctx, base);
}

REX_EXTERN(__imp__issLight__Update);
REX_HOOK_RAW(issLight__Update) {
  EvtSpeedRemainder z(ctx.r3.u32);
  __imp__issLight__Update(ctx, base);
}

REX_EXTERN(__imp__issActor__Update);
REX_HOOK_RAW(issActor__Update) {
  const u32 actorEA = ctx.r3.u32;
  auto *evt = DrivenEvent(actorEA);
  if (auto *actor = evt ? TryStruct<IssActor_t>(actorEA) : nullptr) {
    g_evtWindowClosing =
        u32(actor->guided) == 0 &&
        MotionWindowClosing(actor->motionCursor, actor->motionEnd, evt->speed);
  }
  __imp__issActor__Update(ctx, base);
  g_evtWindowClosing = false;
}

REX_EXTERN(__imp__bdAnimationUpdate);
REX_HOOK_RAW(bdAnimationUpdate) {
  auto *vo = g_evtWindowClosing ? TryStruct<CharaAnim_t>(ctx.r3.u32) : nullptr;
  bool hold = false;
  f32 length = 0.0f;
  if (vo && u32(vo->loopFlag) != 0) {
    auto *slot = TryStruct<AnimSlot_t>(u32(vo->anim));
    auto *clip = slot ? TryStruct<AnimClip_t>(u32(slot->clip)) : nullptr;
    if (clip) {
      length = f32(u16(clip->length));
      const f32 cursor = vo->cursor;
      const f32 step =
          f32(vo->cursorRate) * bd::mem::load<f32>(kSceneSpeedMulEA);
      hold = length > 0.0f && cursor <= length && cursor + step > length;
    }
  }
  f32 cursorRate = 0.0f;
  if (hold) {
    cursorRate = vo->cursorRate;
    vo->cursor = length;
    vo->cursorRate = 0.0f;
  }
  __imp__bdAnimationUpdate(ctx, base);
  if (hold)
    vo->cursorRate = cursorRate;
}

namespace {

constexpr u32 kRotKeyStride = 8;
constexpr i32 kHalfTurn = 32768;
constexpr i32 kFullTurn = 65536;
constexpr f32 kTurnUnitToRadians = 0.000095873802f;

struct RotKey {
  i32 t = 0;
  i32 x = 0;
  i32 y = 0;
  i32 z = 0;
};

RotKey LoadRotKey(u32 keysEA, i32 index) {
  auto *p = bd::mem::at<be_i16>(keysEA + u32(index) * kRotKeyStride);
  return {i32(i16(p[0])), i32(i16(p[1])), i32(i16(p[2])), i32(i16(p[3]))};
}

i32 WrapTurn(i32 v) { return i32(i16(v)); }

i32 ShortestArc(i32 from, i32 to) {
  i32 d = to - from;
  if (d < -kHalfTurn)
    d += kFullTurn;
  else if (d > kHalfTurn)
    d -= kFullTurn;
  return d;
}

RotKey MirroredRepresentation(const RotKey &k) {
  return {k.t, WrapTurn(k.x + kHalfTurn), WrapTurn(kHalfTurn - k.y),
          WrapTurn(k.z + kHalfTurn)};
}

i32 ArcDistance(const RotKey &a, const RotKey &b) {
  return std::abs(ShortestArc(a.x, b.x)) + std::abs(ShortestArc(a.y, b.y)) +
         std::abs(ShortestArc(a.z, b.z));
}

void StoreEuler(u32 outEA, f32 x, f32 y, f32 z) {
  auto *out = bd::mem::at<be_f32>(outEA);
  out[0] = x * kTurnUnitToRadians;
  out[1] = y * kTurnUnitToRadians;
  out[2] = z * kTurnUnitToRadians;
}

} // namespace

REX_EXTERN(__imp__bdAnimRotationKeysSample);
REX_HOOK_RAW(bdAnimRotationKeysSample) {
  const u32 outEA = ctx.r3.u32;
  const u32 keysEA = ctx.r4.u32;
  const i32 last = i32(ctx.r6.u32);
  const f32 time = f32(ctx.f1.f64);
  if (keysEA == 0 || last < 0 || !bd::mem::try_at<be_i16>(keysEA)) {
    __imp__bdAnimRotationKeysSample(ctx, base);
    return;
  }
  const i32 whole = i32(time);
  i32 lo = 0;
  i32 hi = last;
  i32 mid = 0;
  while (lo <= hi) {
    mid = (lo + hi) >> 1;
    const i32 t = LoadRotKey(keysEA, mid).t;
    if (t == whole)
      break;
    if (t <= whole)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  RotKey k = LoadRotKey(keysEA, mid);
  if (f32(k.t) < time && mid != last)
    k = LoadRotKey(keysEA, ++mid);
  if (f32(k.t) <= time || mid == 0) {
    StoreEuler(outEA, f32(k.x), f32(k.y), f32(k.z));
    return;
  }
  RotKey p = LoadRotKey(keysEA, mid - 1);
  const RotKey mirrored = MirroredRepresentation(p);
  if (ArcDistance(mirrored, k) < ArcDistance(p, k))
    p = mirrored;
  const f32 span = f32(k.t) - f32(p.t);
  const f32 w = span != 0.0f ? (time - f32(p.t)) / span : 0.0f;
  StoreEuler(outEA, f32(p.x) + f32(ShortestArc(p.x, k.x)) * w,
             f32(p.y) + f32(ShortestArc(p.y, k.y)) * w,
             f32(p.z) + f32(ShortestArc(p.z, k.z)) * w);
}

namespace {

constexpr f32 kPlyBlinkAlpha = 0.5f;
constexpr u64 kPlyBlinkWindowTicks = 2;

struct PlyBlinkState {
  u64 tick = 0;
  f32 alpha = 1.0f;
  u32 dirty = 0;
  bool held = false;
};
std::unordered_map<u32, PlyBlinkState> g_plyBlinkWindow;

void FlushPlyBlinkWindow() {
  const u64 tick = bd::engine::TickCount();
  for (auto it = g_plyBlinkWindow.begin(); it != g_plyBlinkWindow.end();) {
    if (it->second.held) {
      if (auto *task = TryStruct<PlyTask_t>(it->first)) {
        task->alpha = it->second.alpha;
        task->alphaDirty = it->second.dirty;
      }
      it->second.held = false;
    }
    if (tick - it->second.tick > kPlyBlinkWindowTicks)
      it = g_plyBlinkWindow.erase(it);
    else
      ++it;
  }
}

} // namespace

bool bdCompassBlinkHoldHook(PPCRegister &r11) {
  static bool blinkActive = false;
  if (r11.u32 != 0) {
    blinkActive = true;
    return false;
  }
  if (bd::engine::TickDue()) {
    blinkActive = false;
    return false;
  }
  return blinkActive;
}

namespace {

struct FrostPrimCapture {
  u64 tick = ~0ull;
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  u32 color = 0;
  u32 texObjEA = 0;
};
FrostPrimCapture g_frostPrim;

} // namespace

void bdFaceFrostCaptureHook(PPCRegister &f1, PPCRegister &f2, PPCRegister &f4,
                            PPCRegister &f5, PPCRegister &r8,
                            PPCRegister &r30) {
  g_frostPrim.tick = bd::engine::TickCount();
  g_frostPrim.x = f1.f64;
  g_frostPrim.y = f2.f64;
  g_frostPrim.w = f4.f64;
  g_frostPrim.h = f5.f64;
  g_frostPrim.color = r8.u32;
  g_frostPrim.texObjEA = r30.u32;
}

REX_IMPORT(__imp__Visual__method_7E60, ItemDropPushText,
           void(f64, f64, f64, f64, f64, u32, u32, u32, u32, u32, u32, u32, u32,
                u32));

namespace {

constexpr u32 kItemDropTextChars = 96;
constexpr u32 kItemDropTextPrims = 2;

struct ItemDropTextPrim {
  f64 x = 0.0, y = 0.0, z = 0.0, w = 0.0, h = 0.0;
  u32 color = 0;
  u32 mode = 0;
};

struct ItemDropTextCapture {
  u64 tick = ~0ull;
  u32 count = 0;
  u32 textEA = 0;
  double time = 0.0;
  bool prevValid = false;
  ItemDropTextPrim prims[kItemDropTextPrims];
  ItemDropTextPrim prevPrims[kItemDropTextPrims];
};

ItemDropTextCapture g_itemDropText;

bool CopyGuestWideString(u32 srcVa, u32 dstVa) {
  auto *src = bd::mem::try_at<const be_u16>(srcVa);
  auto *dst = bd::mem::at<be_u16>(dstVa);
  if (!src || !dst)
    return false;
  for (u32 i = 0; i + 1 < kItemDropTextChars; ++i) {
    const u16 c = src[i];
    dst[i] = c;
    if (c == 0)
      return i > 0;
  }
  dst[kItemDropTextChars - 1] = 0;
  return true;
}

} // namespace

bool bdItemDropTextCaptureHook(PPCRegister &f1, PPCRegister &f2,
                               PPCRegister &f3, PPCRegister &f4,
                               PPCRegister &f5, PPCRegister &r8,
                               PPCRegister &r9, PPCRegister &r10) {
  if (!bd::engine::InterpolationActive())
    return false;
  auto &cap = g_itemDropText;
  const u64 tick = bd::engine::TickCount();
  if (cap.tick != tick) {
    cap.prevValid = tick == cap.tick + 1 && cap.count == kItemDropTextPrims;
    if (cap.prevValid)
      std::copy(cap.prims, cap.prims + kItemDropTextPrims, cap.prevPrims);
    cap.tick = tick;
    cap.count = 0;
    cap.time = bd::engine::FrameTime();
  }
  if (cap.count >= kItemDropTextPrims)
    return false;
  if (!GuestScratch(cap.textEA, kItemDropTextChars * sizeof(be_u16)))
    return false;
  if (cap.count == 0 && !CopyGuestWideString(r8.u32, cap.textEA)) {
    cap.tick = ~0ull;
    return false;
  }
  auto &prim = cap.prims[cap.count++];
  prim.x = f1.f64;
  prim.y = f2.f64;
  prim.z = f3.f64;
  prim.w = f4.f64;
  prim.h = f5.f64;
  prim.color = r9.u32;
  prim.mode = r10.u32;
  return true;
}

void bdItemDropTextReplayHook() {
  if (!bd::engine::InterpolationActive())
    return;
  const auto &cap = g_itemDropText;
  if (cap.tick != bd::engine::TickCount() || !cap.textEA || cap.count == 0)
    return;
  const f64 a = cap.prevValid ? EntityAlpha(cap.time) : 1.0;
  for (u32 i = 0; i < cap.count; ++i) {
    const auto &curr = cap.prims[i];
    const auto &prev = cap.prevValid ? cap.prevPrims[i] : curr;
    const f64 currA = curr.color >> 24;
    const f64 prevA = cap.prevValid ? f64(prev.color >> 24) : currA;
    const u32 alpha =
        static_cast<u32>(std::clamp(prevA + (currA - prevA) * a, 0.0, 255.0));
    const u32 color = (alpha << 24) | (curr.color & 0x00FFFFFF);
    PrimSelectTexture(0, 0);
    ItemDropPushText(prev.x + (curr.x - prev.x) * a,
                     prev.y + (curr.y - prev.y) * a, curr.z, curr.w, curr.h, 0,
                     0, 0, 0, 0, cap.textEA, color, curr.mode, 0);
  }
}

REX_EXTERN(__imp__FreeDfsTask__Draw);
REX_HOOK_RAW(FreeDfsTask__Draw) {
  __imp__FreeDfsTask__Draw(ctx, base);
  if (!bd::engine::InterpolationActive() || bd::engine::TickDue())
    return;
  if (g_frostPrim.tick != bd::engine::TickCount() || !g_frostPrim.texObjEA)
    return;
  PrimSelectTexture(0, g_frostPrim.texObjEA);
  PrimDrawRect2D(g_frostPrim.x, g_frostPrim.y, 1.0, g_frostPrim.w,
                 g_frostPrim.h, 0, 0, 0, 0, 0, g_frostPrim.color);
}

namespace {

constexpr u32 kLightEntriesEA = 0x82E18694;
constexpr u32 kLightChangedListEA = 0x82E1DFA8;
constexpr u32 kLightChangedCountEA = 0x82E1E458;
constexpr u32 kLightChangedFlag = 0x40;
constexpr u32 kLightMaxEntries = 300;

struct LightEntry_t {
  /* 0x00 */ be_u32 flags;
  /* 0x04 */ u8 _pad004[0x4C - 0x04];
};
static_assert(sizeof(LightEntry_t) == 0x4C);

void SetChangedFlags(u32 count, bool set) {
  auto *list = bd::mem::at<be_u32>(kLightChangedListEA);
  if (!list)
    return;
  for (u32 i = 0; i < count; ++i) {
    const u32 entry = static_cast<u32>(list[i]);
    if (entry < kLightEntriesEA ||
        entry >= kLightEntriesEA + kLightMaxEntries * sizeof(LightEntry_t))
      continue;
    if (auto *e = bd::mem::at<LightEntry_t>(entry)) {
      const u32 v = e->flags;
      e->flags = set ? (v | kLightChangedFlag) : (v & ~kLightChangedFlag);
    }
  }
}

void ClearLightChangedList() {
  u32 count = bd::mem::load<u32>(kLightChangedCountEA);
  if (count > kLightMaxEntries)
    count = kLightMaxEntries;
  SetChangedFlags(count, false);
  bd::mem::store<u32>(kLightChangedCountEA, 0u);
}

void MatrixTrack::DetectCut() {
  float pe[3], ce[3];
  EyeFromView(prev, pe);
  EyeFromView(curr, ce);
  const float step = std::sqrt(DistSq(pe, ce));
  const bool discontinuous =
      (EventSceneEngaged() && step > kEventViewCutStep &&
       spacing > kEventCutSpacing) ||
      MinRowDot(curr, prev) < kViewCutRotDot;
  cut = gauge.Roll(step, spacing, discontinuous, false);
  if (cut)
    g_cutTick.store(bd::engine::TickCount(), std::memory_order_relaxed);
}

MatrixTrack &ViewTrack(u32 va) {
  MatrixTrack &e = g_views[va];
  e.lastSeen = g_frame;
  return e;
}

u32 ServeVec3(u32 va, Vec3Slot slot) {
  auto *src = bd::mem::try_at<be_f32>(va);
  if (!src)
    return 0;
  float live[3];
  ReadFloats(src, live, 3);
  float out[3];
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    Vec3Track &t = g_vec3Tracks[va];
    t.lastSeen = g_frame;
    switch (t.Advance(live, bd::engine::FrameTime())) {
    case Roll::Shared:
      t.raw = true;
      break;
    case Roll::Rolled:
      t.raw = DistSq(t.prev, t.curr) > kCutDistance * kCutDistance;
      break;
    case Roll::Held:
      break;
    }
    if (t.raw || CutThisTick())
      return 0;
    LerpElements(t.prev, t.curr, t.Alpha(), out, 3);
  }
  return WriteScratch(g_vec3Scratch[u32(slot)], out, 3);
}

bool ServeView(MatrixTrack &e, const float live[16], double now,
               float out[16]) {
  const Roll roll = e.Advance(live, now);
  if (roll == Roll::Shared) {
    std::copy_n(live, 16, out);
    return true;
  }
  if (roll == Roll::Rolled)
    e.DetectCut();
  if (CutThisTick())
    std::copy_n(e.curr, 16, out);
  else
    LerpView(e.prev, e.curr, e.Alpha(), out);
  return false;
}

bool ServeProj(MatrixTrack &e, const float live[16], double now,
               float out[16]) {
  if (e.Advance(live, now) == Roll::Shared) {
    std::copy_n(live, 16, out);
    return true;
  }
  if (CutThisTick())
    std::copy_n(e.curr, 16, out);
  else
    LerpElements(e.prev, e.curr, e.Alpha(), out, 16);
  return false;
}

void ServeSunLightView(be_f32 *global, u32 va) {
  static float lastWritten[16];
  float liveView[16];
  ReadFloats(global, liveView, 16);
  float view[16];
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    if (std::equal(liveView, liveView + 16, lastWritten))
      return;
    if (ServeView(ViewTrack(va), liveView, bd::engine::FrameTime(), view))
      return;
  }
  WriteFloats(global, view, 16);
  std::copy_n(view, 16, lastWritten);
}

struct HUDAnchor : Track<3> {
  u32 va = 0;
  StepGauge gauge;
  bool cut = false;
};

constexpr float kAnchorMatchDistance = 8.0f;

class HUDAnchorTable {
public:
  HUDAnchor &ForCall(u32 anchorVa, const float live[3]) {
    HUDAnchor *best = nullptr;
    float bestDistSq = 0.0f;
    for (HUDAnchor &a : anchors_) {
      if (a.va != anchorVa || a.lastSeen == g_frame)
        continue;
      const float reach =
          std::max(kAnchorMatchDistance, a.gauge.avgStep * kCutRatio);
      const float distSq = DistSq(a.curr, live);
      if (distSq > reach * reach || (best && distSq >= bestDistSq))
        continue;
      best = &a;
      bestDistSq = distSq;
    }
    if (!best) {
      best = &anchors_.emplace_back();
      best->va = anchorVa;
    }
    best->lastSeen = g_frame;
    return *best;
  }

  void BeginFrame() {
    std::erase_if(anchors_, [](const HUDAnchor &a) {
      return g_frame - a.lastSeen > kStaleFrames;
    });
  }

private:
  std::vector<HUDAnchor> anchors_;
};

HUDAnchorTable g_hudAnchors;
MatrixTrack g_hudView;
MatrixTrack g_hudProj;
double g_hudLastCall = 0.0;
constexpr double kHudGapSeconds = kTickSeconds * 1.5;
u32 g_hudScratch = 0;

constexpr u32 kCameraView_Id = 0x00;
constexpr u32 kCameraView_Next = 0x1C;
constexpr u32 kCameraView_View = 0xB8;
constexpr u32 kCameraView_Proj = 0xF8;

bool MainCameraMatrices(float view[16], float proj[16]) {
  u32 v = bd::mem::try_load<u32>(bd::engine::addr::kCameraViewList);
  while (v && bd::mem::try_load<u32>(v + kCameraView_Id) != 0)
    v = bd::mem::try_load<u32>(v + kCameraView_Next);
  auto *mv = v ? bd::mem::try_at<be_f32>(v + kCameraView_View) : nullptr;
  auto *mp = v ? bd::mem::try_at<be_f32>(v + kCameraView_Proj) : nullptr;
  if (!mv || !mp)
    return false;
  ReadFloats(mv, view, 16);
  ReadFloats(mp, proj, 16);
  return true;
}

void ProjectBlended(PPCContext &ctx, bool radius) {
  if (!bd::engine::InterpolationActive())
    return;
  const u32 anchorVa = ctx.r6.u32;
  auto *in = bd::mem::try_at<be_f32>(anchorVa);
  float liveView[16];
  float liveProj[16];
  if (!in || !MainCameraMatrices(liveView, liveProj))
    return;
  float live[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  ReadFloats(in, live, radius ? 4 : 3);
  std::lock_guard<std::mutex> lock(g_interpMutex);
  const double now = bd::engine::FrameTime();
  if (now - g_hudLastCall > kHudGapSeconds) {
    g_hudView = MatrixTrack{};
    g_hudProj = MatrixTrack{};
  }
  g_hudLastCall = now;
  HUDAnchor &a = g_hudAnchors.ForCall(anchorVa, live);
  float view[16];
  if (ServeView(g_hudView, liveView, now, view) || g_hudView.cut ||
      CutThisTick())
    return;
  float proj[16];
  ServeProj(g_hudProj, liveProj, now, proj);
  const float sx =
      std::fabs(liveProj[0]) > 1e-6f ? proj[0] / liveProj[0] : 1.0f;
  const float sy =
      std::fabs(liveProj[5]) > 1e-6f ? proj[5] / liveProj[5] : 1.0f;
  switch (a.Advance(live, now)) {
  case Roll::Rolled: {
    const float step = std::sqrt(DistSq(a.prev, a.curr));
    a.cut = a.gauge.Roll(step, a.spacing, false, step > kCutDistance);
    break;
  }
  case Roll::Shared:
    a.cut = false;
    break;
  case Roll::Held:
    break;
  }
  float pos[3];
  if (a.cut)
    std::copy_n(a.curr, 3, pos);
  else
    LerpElements(a.prev, a.curr, a.Alpha(), pos, 3);
  float q[3];
  for (int j = 0; j < 3; ++j)
    q[j] = pos[0] * view[j] + pos[1] * view[4 + j] + pos[2] * view[8 + j] +
           view[12 + j] - liveView[12 + j];
  const float scale[3] = {sx, sy, 1.0f};
  for (int j = 0; j < 3; ++j)
    q[j] = (q[j] + liveView[12 + j]) * scale[j] - liveView[12 + j];
  float out[4];
  for (int i = 0; i < 3; ++i)
    out[i] = q[0] * liveView[4 * i] + q[1] * liveView[4 * i + 1] +
             q[2] * liveView[4 * i + 2];
  out[3] = live[3];
  if (const u32 scratch = WriteScratch(g_hudScratch, out, 4))
    ctx.r6.u32 = scratch;
}

} // namespace

REX_EXTERN(__imp__bdLightListUpdateSnapshot);
REX_HOOK_RAW(bdLightListUpdateSnapshot) {
  u32 held = bd::engine::InterpolationActive()
                 ? bd::mem::load<u32>(kLightChangedCountEA)
                 : 0;
  if (held > kLightMaxEntries)
    held = 0;
  __imp__bdLightListUpdateSnapshot(ctx, base);
  if (held == 0)
    return;
  SetChangedFlags(held, true);
  bd::mem::store<u32>(kLightChangedCountEA, held);
}

namespace bd::engine {

void OnGuestGameStep() {
  UpdateEventEngagement();
  Advance();
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    ++g_frame;
    PruneStale(g_objSnapshots);
    PruneStale(g_views);
    PruneStale(g_vec3Tracks);
    PruneStale(g_animeClocks);
    g_hudAnchors.BeginFrame();
    g_recordKeys.clear();
    PruneBoneArrays();
  }
  if (TickDue())
    FlushEvtHidePending();
  StepEventScenes();
  if (InterpolationActive() && TickDue()) {
    FlushPlyBlinkWindow();
    ClearLightChangedList();
  }
}

bool SparseFrame() {
  const u32 nodes = g_nodeDraws.exchange(0, std::memory_order_relaxed);
  if (!InterpolationActive() || g_swapTickDue) {
    g_tickNodeDraws = nodes;
    return false;
  }
  return EventSceneEngaged() && g_tickNodeDraws >= kSparseMinNodes &&
         nodes * 2 < g_tickNodeDraws;
}

} // namespace bd::engine

namespace {

struct NodeDrawScope {
  explicit NodeDrawScope(u32 nodeIdx) {
    g_nodeDraws.fetch_add(1, std::memory_order_relaxed);
    if (bd::engine::InterpolationActive()) {
      g_worldKey = g_nodeScope = NodeIdentity(nodeIdx);
      g_recordSeq = 0;
    }
  }
  ~NodeDrawScope() {
    g_worldKey = 0;
    g_nodeScope = 0;
  }
};

} // namespace

REX_EXTERN(__imp__bdSceneNodeProcessRenderCmds);
REX_HOOK_RAW(bdSceneNodeProcessRenderCmds) {
  NodeDrawScope scope(ctx.r4.u32);
  __imp__bdSceneNodeProcessRenderCmds(ctx, base);
}

REX_EXTERN(__imp__bdSceneNodeDrawSingle);
REX_HOOK_RAW(bdSceneNodeDrawSingle) {
  NodeDrawScope scope(ctx.r4.u32);
  __imp__bdSceneNodeDrawSingle(ctx, base);
}

REX_EXTERN(__imp__bdBuildMirrorViewProjection);
REX_HOOK_RAW(bdBuildMirrorViewProjection) {
  if (bd::engine::InterpolationActive()) {
    if (const u32 eye = ServeVec3(ctx.r4.u32, Vec3Slot::MirrorEye))
      ctx.r4.u32 = eye;
    if (const u32 target = ServeVec3(ctx.r5.u32, Vec3Slot::MirrorTarget))
      ctx.r5.u32 = target;
  }
  __imp__bdBuildMirrorViewProjection(ctx, base);
}

namespace {

thread_local u32 t_renderViewObj = 0;
constexpr u32 kRenderViewObj = 0x08;
constexpr u32 kViewObjEye = 0x120;

void ServeShaderEye() {
  if (!bd::engine::InterpolationActive() || !t_renderViewObj)
    return;
  const u32 eyeVa = t_renderViewObj + kViewObjEye;
  auto *staged = bd::mem::try_at<be_f32>(bd::engine::addr::kShaderEye);
  auto *raw = bd::mem::try_at<be_f32>(eyeVa);
  if (!staged || !raw)
    return;
  for (int i = 0; i < 3; ++i)
    if (float(staged[i]) != float(raw[i]))
      return;
  const u32 scratch = ServeVec3(eyeVa, Vec3Slot::ShaderEye);
  if (!scratch)
    return;
  float eye[3];
  ReadFloats(bd::mem::at<be_f32>(scratch), eye, 3);
  WriteFloats(staged, eye, 3);
}

} // namespace

REX_EXTERN(__imp__bdRenderViewSubmit);
REX_HOOK_RAW(bdRenderViewSubmit) {
  t_renderViewObj = bd::mem::try_load<u32>(ctx.r3.u32 + kRenderViewObj);
  __imp__bdRenderViewSubmit(ctx, base);
  t_renderViewObj = 0;
}

void bdDofFocusLerpHook(PPCRegister &r11) {
  if (!bd::engine::InterpolationActive())
    return;
  const u32 slot = r11.u32;
  const u32 lerped = ServeVec3(slot, Vec3Slot::DofFocus);
  if (!lerped)
    return;
  float focus[3];
  ReadFloats(bd::mem::at<be_f32>(lerped), focus, 3);
  WriteFloats(bd::mem::at<be_f32>(slot), focus, 3);
}

REX_EXTERN(__imp__Visual__RenderInfo__vf04);
REX_HOOK_RAW(Visual__RenderInfo__vf04) {
  ServeShaderEye();
  __imp__Visual__RenderInfo__vf04(ctx, base);
}

namespace {

u64 WorldMatrixKey(u32 va) {
  u64 key = 0;
  if (g_worldKey) {
    key = (1ull << 63) | g_worldKey;
  } else if (auto it = g_recordKeys.find(va - kRecMatrix);
             it != g_recordKeys.end()) {
    key = (1ull << 63) | it->second;
  } else if (auto *rec = t_inRecordReplay
                             ? bd::mem::try_at<const DrawRecord_t>(va - kRecMatrix)
                             : nullptr) {
    key = rec->Key();
  } else if (g_listObject) {
    key = (1ull << 62) | (u64(g_listSeq++) << 32) | g_listObject;
  } else {
    key = u64(va);
  }
  g_worldKey = 0;
  return key;
}

void ServeWorldMatrix(PPCContext &ctx) {
  const u32 va = ctx.r3.u32;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  const u64 key = WorldMatrixKey(va);
  if (BoneArrayOwned(va))
    return;
  FloatSnapshot *e = nullptr;
  switch (AdvanceSnapshot(key, va, kWorldFloats, e)) {
  case Snapshot::Rolled: {
    const float step = std::sqrt(DistSq(&e->curr[12], &e->prev[12]));
    const bool hard = step > kCutDistance ||
                      MinRowDot(e->curr.data(), e->prev.data()) < kObjCutRotDot ||
                      SubTickWriter(*e);
    e->cut = e->gauge.Roll(step, e->spacing, false, hard);
    if (e->cut)
      e->streak = 0;
    else if (e->streak < 0xFFFF)
      ++e->streak;
  }
    [[fallthrough]];
  case Snapshot::Ready:
    if (!e->cut && !CutThisTick() &&
        (!InShadowDepthPass() || e->streak >= kTrustedStreak)) {
      if (const u32 scratch = LerpToScratch(*e, EntityAlpha(e->lastChange),
                                            g_worldScratch, kWorldFloats))
        ctx.r3.u32 = scratch;
    }
    break;
  case Snapshot::Shared:
    e->streak = 0;
    break;
  case Snapshot::First:
  case Snapshot::Missing:
    break;
  }
}

void ServeCameraMatrices(PPCContext &ctx) {
  const u32 viewVa = ctx.r4.u32;
  auto *live = bd::mem::try_at<be_f32>(viewVa);
  if (!live)
    return;
  if (ProjectorView(viewVa)) {
    if (viewVa == bd::engine::addr::kShadowLightView)
      ServeSunLightView(live, viewVa);
    return;
  }
  const double now = bd::engine::FrameTime();
  if (auto *liveProj =
          ctx.r5.u32 ? bd::mem::try_at<be_f32>(ctx.r5.u32) : nullptr) {
    float liveP[16];
    ReadFloats(liveProj, liveP, 16);
    float proj[16];
    bool shared = false;
    {
      std::lock_guard<std::mutex> lock(g_interpMutex);
      shared = ServeProj(ViewTrack(ctx.r5.u32), liveP, now, proj);
    }
    if (!shared)
      if (const u32 scratch = WriteScratch(g_projScratch, proj, 16))
        ctx.r5.u32 = scratch;
  }
  float liveView[16];
  ReadFloats(live, liveView, 16);
  float view[16];
  bool shared = false;
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    shared = ServeView(ViewTrack(viewVa), liveView, now, view);
  }
  if (shared)
    return;
  if (const u32 scratch = WriteScratch(g_viewScratch, view, 16))
    ctx.r4.u32 = scratch;
}

} // namespace

REX_EXTERN(__imp__bdBuildViewMatrix);
REX_HOOK_RAW(bdBuildViewMatrix) {
  if (bd::engine::InterpolationActive()) {
    if (ctx.r3.u32 && !ctx.r4.u32 && !ctx.r5.u32)
      ServeWorldMatrix(ctx);
    else if (ctx.r4.u32)
      ServeCameraMatrices(ctx);
  }
  __imp__bdBuildViewMatrix(ctx, base);
}

REX_EXTERN(__imp__bdWorldToScreenPos3);
REX_HOOK_RAW(bdWorldToScreenPos3) {
  ProjectBlended(ctx, false);
  __imp__bdWorldToScreenPos3(ctx, base);
}

REX_EXTERN(__imp__bdWorldToScreenPos4);
REX_HOOK_RAW(bdWorldToScreenPos4) {
  ProjectBlended(ctx, true);
  __imp__bdWorldToScreenPos4(ctx, base);
}

REX_EXTERN(__imp__PlyTask__Draw);
REX_HOOK_RAW(PlyTask__Draw) {
  auto *task = bd::engine::InterpolationActive()
                   ? TryStruct<PlyTask_t>(ctx.r3.u32)
                   : nullptr;
  if (task) {
    const u32 arm = task->blinkArm;
    if (arm == 1) {
      task->blinkArm = 0u;
      g_plyBlinkWindow[ctx.r3.u32].tick = bd::engine::TickCount();
    }
    if (arm <= 1) {
      auto it = g_plyBlinkWindow.find(ctx.r3.u32);
      if (it != g_plyBlinkWindow.end() && !it->second.held) {
        const f32 cur = task->alpha;
        if (cur > kPlyBlinkAlpha) {
          it->second.alpha = cur;
          it->second.dirty = task->alphaDirty;
          it->second.held = true;
          task->alpha = kPlyBlinkAlpha;
          task->alphaDirty = 1u;
        }
      }
    }
  }
  __imp__PlyTask__Draw(ctx, base);
}

REX_IMPORT(__imp__AnimeVarTrack_Apply, AnimeVarTrack_Apply, void(u32, f64));
REX_IMPORT(__imp__AnimeData_SyncDerivedVars, AnimeData_SyncDerivedVars,
           void(u32));

namespace {

constexpr u32 kAnimeVarTrackCap = 512;
constexpr float kAnimeFirstFrame = 1.0f;

float LerpedAnimeFrame(u32 taskEA, const bd::engine::D2AnimeTask_t &task) {
  const float live = static_cast<float>(task.animeData.frame);
  std::lock_guard<std::mutex> lock(g_interpMutex);
  AnimeClock &c = g_animeClocks[taskEA];
  c.lastSeen = g_frame;
  const double now = bd::engine::FrameTime();
  if (!c.valid) {
    c.prev = c.curr = live;
    c.valid = true;
    c.lastChange = now;
  } else if (c.curr != live) {
    const bool cut = now - c.lastChange < kFastChangeSeconds ||
                     AnimeClockDiscontinuous(
                         live - c.curr, static_cast<float>(task.animeData.speed));
    c.prev = cut ? live : c.curr;
    c.curr = live;
    c.lastChange = now;
  }
  const float alpha = EntityAlpha(c.lastChange);
  if (c.prev == c.curr || alpha >= 1.0f)
    c.prev = c.curr;
  if (c.prev == c.curr)
    return live;
  return std::max(c.prev + (c.curr - c.prev) * alpha, kAnimeFirstFrame);
}

} // namespace

REX_EXTERN(__imp__D2AnimeTask_Draw);
REX_HOOK_RAW(D2AnimeTask_Draw) {
  auto *task = bd::engine::InterpolationActive()
                   ? bd::mem::try_at<bd::engine::D2AnimeTask_t>(ctx.r3.u32)
                   : nullptr;
  const float live = task ? static_cast<float>(task->animeData.frame) : 0.0f;
  const float lerped = task ? LerpedAnimeFrame(ctx.r3.u32, *task) : 0.0f;
  if (!task || lerped == live) {
    __imp__D2AnimeTask_Draw(ctx, base);
    return;
  }
  task->animeData.frame = lerped;
  const auto &tracks = task->animeData.varTracks;
  const u32 count = std::min<u32>(tracks.size(), kAnimeVarTrackCap);
  for (u32 i = 0; i < count; ++i) {
    if (const u32 track = bd::mem::try_load<u32>(tracks.address(i)))
      AnimeVarTrack_Apply(track, f64(lerped));
  }
  if (count) {
    AnimeData_SyncDerivedVars(ctx.r3.u32 +
                              offsetof(bd::engine::D2AnimeTask_t, animeData));
  }
  __imp__D2AnimeTask_Draw(ctx, base);
  task->animeData.frame = live;
}

void bdAnimeChildClockFloorHook(PPCRegister &time) {
  if (time.f64 < kAnimeFirstFrame)
    time.f64 = kAnimeFirstFrame;
}

namespace {

constexpr u32 kScriptWndOpening = 1;
constexpr u32 kScriptWndClosing = 2;
constexpr float kScriptWndFadeStep = 0.1f;

struct ScriptWindow_t {
  /* 0x000 */ u8 _pad000[0x3EC];
  /* 0x3EC */ be_u32 state;
  /* 0x3F0 */ u8 _pad3F0[0x40C - 0x3F0];
  /* 0x40C */ be_f32 fade;
};
static_assert(offsetof(ScriptWindow_t, state) == 0x3EC);
static_assert(offsetof(ScriptWindow_t, fade) == 0x40C);

} // namespace

REX_EXTERN(__imp__ScriptWindow__Draw);
REX_HOOK_RAW(ScriptWindow__Draw) {
  auto *wnd = bd::engine::InterpolationActive()
                  ? TryStruct<ScriptWindow_t>(ctx.r3.u32)
                  : nullptr;
  const u32 state = wnd ? u32(wnd->state) : 0;
  if (state != kScriptWndOpening && state != kScriptWndClosing) {
    __imp__ScriptWindow__Draw(ctx, base);
    return;
  }
  const float held = wnd->fade;
  const float step = kScriptWndFadeStep * bd::engine::Alpha();
  wnd->fade = state == kScriptWndOpening ? std::min(held + step, 1.0f)
                                         : std::max(held - step, 0.0f);
  __imp__ScriptWindow__Draw(ctx, base);
  wnd->fade = held;
}

REX_EXTERN(__imp__bdInputSystemUpdate);
REX_HOOK_RAW(bdInputSystemUpdate) {
  if (!bd::engine::TickDue())
    return;
  bd::engine::SampleButtonEdges();
  bd::engine::MenuMouse::Get().BeginFrame();
  bd::engine::UpdateMouseLook();
  bd::engine::MouseCursorTick();
  bd::engine::Glyphs::Get().Tick();
  bd::engine::D2AnimeTask::Tick();
  bd::engine::CampSettings::Get().Tick();
  bd::engine::AreaMapTick();
  __imp__bdInputSystemUpdate(ctx, base);
}

REX_EXTERN(__imp__PadVibrationCore__Draw);
REX_HOOK_RAW(PadVibrationCore__Draw) {
  if (!bd::engine::TickDue())
    return;
  if (!bd::engine::Settings::Get().Vibration()) {
    if (auto *amp = bd::mem::at<be_f32>(ctx.r3.u32 + 0x6C)) {
      amp[0] = 0.0f;
      amp[1] = 0.0f;
    }
  }
  __imp__PadVibrationCore__Draw(ctx, base);
}

void bdAlphaPrimCaptureHook(PPCRegister &r3) {
  if (g_nodeScope == 0 || r3.u32 == 0)
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  g_recordKeys[r3.u32] = g_nodeScope | (u64(++g_recordSeq) << 48);
}

void bdListObjectBeginHook(PPCRegister &r3) {
  g_listObject = r3.u32;
  g_listSeq = 0;
}

void bdListObjectEndHook() { g_listObject = 0; }

REX_EXTERN(__imp__bdDoubleBufferAcquire);
REX_EXTERN(__imp__bdVisualObjectCopyShadowBones);
REX_HOOK_RAW(bdVisualObjectCopyShadowBones) {
  const u32 vo = ctx.r3.u32;
  __imp__bdVisualObjectCopyShadowBones(ctx, base);
  if (!bd::engine::InterpolationActive() || !vo)
    return;
  const u32 count = bd::mem::try_load<u32>(vo + kVOBoneCount);
  if (count == 0 || count > kMaxBoneMatrices)
    return;
  const u32 r3Out = ctx.r3.u32;
  ctx.r3.u32 = vo + kVOCurrBones;
  __imp__bdDoubleBufferAcquire(ctx, base);
  const u32 currEA = ctx.r3.u32;
  ctx.r3.u32 = r3Out;
  if (!bd::mem::try_at<be_f32>(currEA))
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  BoneArray &e = g_boneArrays[vo + kVOCurrBones];
  e.count = count;
  e.currEA = currEA;
  e.copyTime = bd::engine::FrameTime();
}

REX_EXTERN(__imp__bdDrawRecordListReplay);
REX_HOOK_RAW(bdDrawRecordListReplay) {
  t_inRecordReplay = true;
  __imp__bdDrawRecordListReplay(ctx, base);
  t_inRecordReplay = false;
}

REX_EXTERN(__imp__bdCameraRender);
REX_HOOK_RAW(bdCameraRender) {
  const u32 vo = ctx.r3.u32;
  const bool outer = !t_inCameraRender;
  t_inCameraRender = true;
  RegisterBoneArray(vo);
  __imp__bdCameraRender(ctx, base);
  if (outer)
    t_inCameraRender = false;
}

REX_EXTERN(__imp__bdFrameSubmitAndDebugHUD);
REX_HOOK_RAW(bdFrameSubmitAndDebugHUD) {
  g_swapTickDue = g_drawTickDue;
  g_drawTickDue = bd::engine::TickDue();
  bd::engine::PublishRenderClock();
  __imp__bdFrameSubmitAndDebugHUD(ctx, base);
}

REX_EXTERN(__imp__bdRenderStep);
REX_HOOK_RAW(bdRenderStep) {
  bd::engine::BindRenderThread();
  __imp__bdRenderStep(ctx, base);
}

REX_EXTERN(__imp__bdVisualObjectInitBones);
REX_HOOK_RAW(bdVisualObjectInitBones) {
  t_boneWriter = true;
  __imp__bdVisualObjectInitBones(ctx, base);
  t_boneWriter = false;
}

namespace {

u32 ServeBones(BoneArray &e, double now) {
  if (now - e.copyTime > kBoneCopyFresh)
    return 0;
  if (e.blendTime == now) {
    if (!e.blended)
      return 0;
    if (CutThisTick()) {
      e.blended = 0;
      e.prevPose = e.tickTarget;
      return 0;
    }
    auto *live = bd::mem::try_at<be_f32>(e.currEA);
    if (live && e.lastLive.size() == size_t(e.count) * 16)
      for (size_t i = 0; i < e.lastLive.size(); ++i)
        if (float(live[i]) != e.lastLive[i])
          return 0;
    return e.blended;
  }
  e.blendTime = now;
  e.blended = 0;
  const u32 count = e.count;
  const int floats = int(count) * 16;
  auto *cur = bd::mem::try_at<be_f32>(e.currEA);
  if (!cur)
    return 0;
  if (e.scratch != 0 && e.scratchCount < count) {
    bd::gpu::HostHeap::Get().FreeGuest(e.scratch);
    e.scratch = 0;
  }
  if (e.scratch == 0) {
    e.scratch = bd::gpu::HostHeap::Get().AllocGuest(u32(floats) * 4, 16);
    e.scratchCount = count;
  }
  if (e.scratch == 0)
    return 0;
  auto *dst = bd::mem::at<be_f32>(e.scratch);
  const u64 tick = bd::engine::TickCount();
  if (e.tickTarget.size() != size_t(floats)) {
    e.tickTarget.resize(size_t(floats));
    ReadFloats(cur, e.tickTarget.data(), floats);
    e.prevPose = e.tickTarget;
    e.blendTick = tick;
  } else if (e.blendTick != tick) {
    e.prevPose = e.tickTarget;
    ReadFloats(cur, e.tickTarget.data(), floats);
    e.blendTick = tick;
  }
  e.lastLive.resize(size_t(floats));
  ReadFloats(cur, e.lastLive.data(), floats);
  if (e.newborn) {
    e.newborn = false;
    SeedNewbornPose(e, tick);
  }
  if (CutThisTick()) {
    e.prevPose = e.tickTarget;
    return 0;
  }
  for (int i = 0; i < floats && e.hideTick != tick; i += 16)
    if (ClassifyBone(&e.tickTarget[size_t(i)], &e.prevPose[size_t(i)]) ==
        BoneStep::Cut)
      e.hideTick = tick;
  const float a = bd::engine::Alpha();
  float outM[16];
  for (int i = 0; i < floats; i += 16) {
    const float *prvM = &e.prevPose[size_t(i)];
    const float *tgtM = &e.tickTarget[size_t(i)];
    if (e.hideTick == tick) {
      std::copy_n(&e.lastLive[size_t(i)], 16, outM);
      for (const int r : kRow)
        outM[r] = outM[r + 1] = outM[r + 2] = 0.0f;
    } else if (ClassifyBone(tgtM, prvM) == BoneStep::Snap) {
      std::copy_n(tgtM, 16, outM);
    } else {
      LerpMatrix(prvM, tgtM, a, outM);
    }
    WriteFloats(dst + i, outM, 16);
  }
  e.blended = e.scratch;
  return e.scratch;
}

} // namespace

REX_HOOK_RAW(bdDoubleBufferAcquire) {
  const u32 holder = ctx.r3.u32;
  __imp__bdDoubleBufferAcquire(ctx, base);
  if (!bd::engine::InterpolationActive())
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  auto it = g_boneArrays.find(holder);
  if (it == g_boneArrays.end())
    return;
  BoneArray &e = it->second;
  e.currEA = ctx.r3.u32;
  if (t_boneWriter || (!t_inCameraRender && !bd::engine::IsRenderThread()))
    return;
  if (const u32 served = ServeBones(e, bd::engine::FrameTime()))
    ctx.r3.u32 = served;
}

u32 rex_QueryPerformanceCounter_hook(u32 lpPerformanceCount) {
  if (lpPerformanceCount) {
    auto *out = bd::mem::at<be_i64>(lpPerformanceCount);
    if (out)
      *out = std::chrono::steady_clock::now().time_since_epoch().count();
  }
  return 1;
}
REX_HOOK(rex_QueryPerformanceCounter, rex_QueryPerformanceCounter_hook);

u32 rex_QueryPerformanceFrequency_hook(u32 lpFrequency) {
  if (lpFrequency) {
    constexpr i64 kFreq = std::chrono::steady_clock::period::den /
                          std::chrono::steady_clock::period::num;
    auto *out = bd::mem::at<be_i64>(lpFrequency);
    if (out)
      *out = kFreq;
  }
  return 1;
}
REX_HOOK(rex_QueryPerformanceFrequency, rex_QueryPerformanceFrequency_hook);
