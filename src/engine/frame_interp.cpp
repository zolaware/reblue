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
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/platform/perf.h>
#include <rex/ppc/hook.h>
#include <rex/kernel/types/memory.h>
#include <rex/memory/mapped_ptr.h>
#include <rex/ppc/call.h>
#include <rex/ppc/context.h>
#include <rex/kernel/memory_view.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include "core/guest_global.h"
#include "core/memory_helpers.h"
#include "engine/camera_view.h"
#include "engine/cutscene_pause.h"
#include "engine/d2anime/anime_data.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/field_camera.h"
#include "engine/frame_clock.h"
#include "engine/game.h"
#include "engine/guest_prim.h"
#include "engine/glyph_set.h"
#include "engine/iss_event.h"
#include "engine/menus/camp_settings.h"
#include "engine/menus/local_map.h"
#include "engine/mouse_cursor.h"
#include "engine/ply_task.h"
#include "engine/settings.h"
#include "engine/state_layout.h"
#include "engine/virtual_buttons.h"
#include "engine/visual_object.h"
#include "gpu/gpu.h"
#include "platform/host_ui.h"

namespace {

constexpr double kTickSeconds = 1.0 / 30.0;
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
constexpr float kBasisLerpSafe = 0.02f;
constexpr int kRow[3] = {0, 4, 8};

std::mutex g_interpMutex;
u64 g_frame = 0;
thread_local u32 t_renderViewObj = 0;
std::atomic<u64> g_cutTick{~0ull};

std::atomic<u64> g_steerTick{~0ull};

bool SteeredThisTick() {
  const u64 steer = g_steerTick.load(std::memory_order_relaxed);
  const u64 tick = bd::engine::TickCount();
  return steer != ~0ull && tick >= steer && tick - steer <= 1;
}

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

u32 EngineScratch(u32 &slot, u32 bytes) {
  if (slot == 0)
    slot = bd::gpu::HostHeap::Get().AllocGuest((bytes + 15u) & ~15u, 16);
  return slot;
}

u32 WriteScratch(u32 &slot, const float *v, int n) {
  if (!EngineScratch(slot, u32(n) * 4))
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

bool RewrittenThisTick(u64 &rewriteTick) {
  const u64 tick = bd::engine::TickCount();
  const bool same = rewriteTick == tick;
  rewriteTick = tick;
  return same;
}

enum class Roll { Held, Shared, Rolled };

template <int N> struct Track {
  float prev[N] = {};
  float curr[N] = {};
  double lastChange = 0.0;
  double spacing = 0.0;
  u64 changeTick = ~0ull;
  u64 rewriteTick = ~0ull;
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
      rewriteTick = bd::engine::TickCount();
      return Roll::Held;
    }
    spacing = now - lastChange;
    const bool fast = RewrittenThisTick(rewriteTick);
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

void QuatFromRotation(const float r[3][3], float quat[4]) {
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
}

void RotationFromQuat(const float quat[4], float r[3][3]) {
  const float w = quat[0], x = quat[1], y = quat[2], z = quat[3];
  r[0][0] = 1.0f - 2.0f * (y * y + z * z);
  r[0][1] = 2.0f * (x * y + z * w);
  r[0][2] = 2.0f * (x * z - y * w);
  r[1][0] = 2.0f * (x * y - z * w);
  r[1][1] = 1.0f - 2.0f * (x * x + z * z);
  r[1][2] = 2.0f * (y * z + x * w);
  r[2][0] = 2.0f * (x * z + y * w);
  r[2][1] = 2.0f * (y * z - x * w);
  r[2][2] = 1.0f - 2.0f * (x * x + y * y);
}

void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

// The closest rotation to a basis, by Newton polar iteration: averaging a
// matrix with its own inverse transpose converges on the orthogonal factor.
bool PolarRotation(const float basis[3][3], float r[3][3]) {
  float c0[3];
  Cross(basis[1], basis[2], c0);
  const float det0 =
      basis[0][0] * c0[0] + basis[0][1] * c0[1] + basis[0][2] * c0[2];
  if (!(std::fabs(det0) > 1e-12f))
    return false;
  // Each pass halves the distance to unit singular values, so a basis far from
  // unit scale would spend most of its passes just getting near one. Scaling a
  // basis does not move its polar factor, so normalize the determinant first
  // and every bone converges in the same few passes whatever its scale.
  const float norm = 1.0f / std::cbrt(std::fabs(det0));
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      r[i][j] = basis[i][j] * norm;
  for (int iter = 0; iter < 8; ++iter) {
    float c[3][3];
    Cross(r[1], r[2], c[0]);
    Cross(r[2], r[0], c[1]);
    Cross(r[0], r[1], c[2]);
    const float det = r[0][0] * c[0][0] + r[0][1] * c[0][1] + r[0][2] * c[0][2];
    if (!(std::fabs(det) > 1e-12f))
      return false;
    float worst = 0.0f;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        const float next = 0.5f * (r[i][j] + c[i][j] / det);
        worst = std::max(worst, std::fabs(next - r[i][j]));
        r[i][j] = next;
      }
    if (worst < 1e-6f)
      break;
  }
  return true;
}

// A bone basis is a rotation times a stretch, not a rotation times three
// per-axis scales. BD skeletons carry shear, and normalizing the rows to pull a
// rotation out drops it, so recomposing hands back a different basis than it
// was given. Keeping the whole remainder makes both endpoints exact.
bool DecomposeAffine(const float *m, float quat[4], float stretch[3][3]) {
  float basis[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      basis[i][j] = m[kRow[i] + j];
  float r[3][3];
  if (!PolarRotation(basis, r))
    return false;
  float c[3];
  Cross(r[1], r[2], c);
  if (r[0][0] * c[0] + r[0][1] * c[1] + r[0][2] * c[2] < 0.0f)
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        r[i][j] = -r[i][j];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k)
        sum += basis[i][k] * r[j][k];
      stretch[i][j] = sum;
    }
  QuatFromRotation(r, quat);
  return true;
}

void ComposeAffine(const float quat[4], const float stretch[3][3], float *m) {
  float r[3][3];
  RotationFromQuat(quat, r);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k)
        sum += stretch[i][k] * r[k][j];
      m[kRow[i] + j] = sum;
    }
}

void LerpElements(const float *a, const float *b, float t, float *out,
                  int floats) {
  for (int i = 0; i < floats; ++i)
    out[i] = a[i] + (b[i] - a[i]) * t;
}

// The parts of a matrix blend that depend only on its endpoints, so a pair
// blended on many frames decomposes once.
struct MatrixBlend {
  bool affine = false;
  float qa[4] = {};
  float qb[4] = {};
  float stretchA[3][3] = {};
  float stretchB[3][3] = {};
};

MatrixBlend PrepareMatrixBlend(const float a[16], const float b[16]) {
  MatrixBlend p;
  bool turning = false;
  for (int i = 0; i < 3 && !turning; ++i)
    for (int j = 0; j < 3; ++j)
      if (std::fabs(a[kRow[i] + j] - b[kRow[i] + j]) > kBasisLerpSafe) {
        turning = true;
        break;
      }
  if (!turning || !DecomposeAffine(a, p.qa, p.stretchA) ||
      !DecomposeAffine(b, p.qb, p.stretchB))
    return p;
  const float dot = p.qa[0] * p.qb[0] + p.qa[1] * p.qb[1] +
                    p.qa[2] * p.qb[2] + p.qa[3] * p.qb[3];
  if (dot < 0.0f)
    for (float &v : p.qb)
      v = -v;
  p.affine = true;
  return p;
}

void ApplyMatrixBlend(const MatrixBlend &p, const float a[16],
                      const float b[16], float t, float out[16]) {
  if (!p.affine) {
    LerpElements(a, b, t, out, 16);
    return;
  }
  float q[4];
  float len = 0.0f;
  for (int i = 0; i < 4; ++i) {
    q[i] = p.qa[i] + (p.qb[i] - p.qa[i]) * t;
    len += q[i] * q[i];
  }
  len = std::sqrt(len);
  if (!(len > 1e-6f)) {
    LerpElements(a, b, t, out, 16);
    return;
  }
  for (int i = 0; i < 4; ++i)
    q[i] /= len;
  float stretch[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      stretch[i][j] =
          p.stretchA[i][j] + (p.stretchB[i][j] - p.stretchA[i][j]) * t;
  ComposeAffine(q, stretch, out);
  static constexpr int kPassthrough[] = {3, 7, 11, 12, 13, 14, 15};
  for (const int i : kPassthrough)
    out[i] = a[i] + (b[i] - a[i]) * t;
}

void LerpMatrix(const float a[16], const float b[16], float t, float out[16]) {
  ApplyMatrixBlend(PrepareMatrixBlend(a, b), a, b, t, out);
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

void TurnView(float v[16], const bd::engine::PendingLook &look) {
  float eye[3];
  EyeFromView(v, eye);
  const float *p = look.pivot;
  const float cy = std::cos(look.yaw);
  const float sy = std::sin(look.yaw);
  const auto yaw = [&](const float x[3], float out[3]) {
    out[0] = x[0] * cy + x[2] * sy;
    out[1] = x[1];
    out[2] = x[2] * cy - x[0] * sy;
  };
  float off[3] = {eye[0] - p[0], eye[1] - p[1], eye[2] - p[2]};
  float turned[3];
  yaw(off, turned);
  const float flat = std::sqrt(turned[0] * turned[0] + turned[2] * turned[2]);
  float axis[3] = {0.0f, 0.0f, 0.0f};
  const bool pitched = look.pitch != 0.0f && flat > 1e-3f;
  if (pitched) {
    axis[0] = -turned[2] / flat;
    axis[2] = turned[0] / flat;
  }
  const float cp = std::cos(look.pitch);
  const float sp = std::sin(look.pitch);
  const auto turn = [&](const float x[3], float out[3]) {
    float y[3];
    yaw(x, y);
    if (!pitched) {
      std::copy_n(y, 3, out);
      return;
    }
    const float cross[3] = {axis[1] * y[2] - axis[2] * y[1],
                            axis[2] * y[0] - axis[0] * y[2],
                            axis[0] * y[1] - axis[1] * y[0]};
    const float along = axis[0] * y[0] + axis[1] * y[1] + axis[2] * y[2];
    for (int i = 0; i < 3; ++i)
      out[i] = y[i] * cp + cross[i] * sp + axis[i] * along * (1.0f - cp);
  };

  float rm[3][3];
  for (int i = 0; i < 3; ++i) {
    const float basis[3] = {i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f,
                            i == 2 ? 1.0f : 0.0f};
    turn(basis, rm[i]);
  }
  float r[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      r[i][j] = v[4 * i + j];
  float q[3];
  for (int i = 0; i < 3; ++i)
    q[i] = p[i] - (p[0] * rm[i][0] + p[1] * rm[i][1] + p[2] * rm[i][2]);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      v[4 * i + j] =
          rm[0][i] * r[0][j] + rm[1][i] * r[1][j] + rm[2][i] * r[2][j];
  for (int j = 0; j < 3; ++j)
    v[12 + j] += q[0] * r[0][j] + q[1] * r[1][j] + q[2] * r[2][j];
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
  bool derived = false;

  void DetectCut();
};

std::unordered_map<u32, MatrixTrack> g_views;
u32 g_viewScratch = 0;

struct FrameLook {
  double time = -1.0;
  u32 view = 0;
  bool turned = false;
  bd::engine::PendingLook pending;
};
FrameLook g_frameLook;

struct GlobalViewTrack : MatrixTrack {
  float written[16] = {};
  bool hasWritten = false;
};

std::unordered_map<u64, GlobalViewTrack> g_globalViews;
u32 g_projScratch = 0;

struct Vec3Track : Track<3> {
  bool raw = false;
};

enum class Vec3Slot : u32 { DofFocus, Count };

std::unordered_map<u32, Vec3Track> g_vec3Tracks;
u32 g_vec3Scratch[u32(Vec3Slot::Count)] = {};

constexpr int kCameraPointFloats = 6;
constexpr u32 kViewObjTargetGap = 12;

struct CameraPointTrack : Track<kCameraPointFloats> {
  bool raw = false;
};

std::unordered_map<u32, CameraPointTrack> g_cameraPoints;
u32 g_cameraPointScratch = 0;

constexpr int kWorldFloats = 16;
u32 g_worldScratch = 0;

struct FloatSnapshot {
  std::vector<float> prev;
  std::vector<float> curr;
  StepGauge gauge;
  float spacing = 0.0f;
  double lastChange = 0.0;
  u64 rewriteTick = ~0ull;
  u64 lastSeen = 0;
  bool valid = false;
  bool cut = false;
  bool subTick = false;
  bool subTickWriter = false;
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
  const u32 vo = bd::mem::global_load(bd::engine::addr::kCameraRenderVO);
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

bool ProjectorView(u32 va) {
  return va == bd::engine::addr::kCubeShadowLightView ||
         (va >= bd::engine::addr::kProjectorMapInfos &&
          va < bd::engine::addr::kProjectorMapInfosEnd);
}

// Rebuilt from the tick camera once per view submitted, into one global, so a
// track keyed by the address alone lerps between two views' matrices.
bool GlobalLightView(u32 va) {
  return va == bd::engine::addr::kShadowLightView ||
         va == bd::engine::addr::kWaterBottomLightView;
}

// Built from camera points this layer already served, so tracking it again
// interpolates an interpolated value.
bool ReflectSlotView(u32 va) {
  return va >= bd::engine::addr::kReflectSlots &&
         va < bd::engine::addr::kReflectSlotsEnd;
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

constexpr u32 kMaxBoneMatrices = 1024;
constexpr u32 kParticleModelVOSize = 5052;
constexpr double kBoneCopyFresh = kTickSeconds * 1.5;
constexpr double kBoneArrayLinger = 2.0;

u32 BoneCount(u32 vo) {
  const auto *object = bd::mem::try_at<const bd::engine::VisualObject_t>(vo);
  return object ? u32(object->boneCount) : 0;
}

constexpr u32 BoneHolder(u32 vo) {
  return vo + offsetof(bd::engine::VisualObject_t, boneBuffer);
}

struct BoneBlend {
  bool snap = false;
  MatrixBlend matrix;
};

struct BoneArray {
  std::vector<float> prevPose;
  std::vector<float> tickTarget;
  // Raw big-endian words, so the per-acquire check is one memcmp.
  std::vector<u32> lastLive;
  std::vector<BoneBlend> blends;
  bool blendsStale = true;
  // The camera pass that last found the live bones unchanged.
  u64 verifiedPass = 0;
  u64 blendTick = ~0ull;
  u32 count = 0;
  u32 currEA = 0;
  double copyTime = 0.0;
  double blendTime = -1.0;
  u32 blended = 0;
  u32 scratch = 0;
  u32 scratchCount = 0;
};

std::unordered_map<u32, BoneArray> g_boneArrays;
std::map<u32, u32> g_boneRanges;

thread_local bool t_inCameraRender = false;
thread_local bool t_boneWriter = false;

// A pass id unique across threads, renewed when a nested bdCameraRender
// returns, since the nested pass may have written bones.
std::atomic<u64> g_cameraPassSeq{0};
thread_local u64 t_cameraPass = 0;

u64 NextCameraPass() {
  return g_cameraPassSeq.fetch_add(1, std::memory_order_relaxed) + 1;
}

void UnmapBoneRange(u32 &slot) {
  if (slot)
    g_boneRanges.erase(slot);
  slot = 0;
}

void MapBoneRange(u32 &slot, u32 start, u32 count) {
  if (slot != start)
    UnmapBoneRange(slot);
  slot = start;
  if (start)
    g_boneRanges[start] = start + count * 64u;
}

bool BoneArrayOwned(u32 va) {
  auto it = g_boneRanges.upper_bound(va);
  return it != g_boneRanges.begin() && va < (--it)->second;
}

bool ParticleModelPoolVO(u32 vo) {
  const u32 base =
      bd::mem::global_load_fast(bd::engine::addr::kParticleModelPool);
  const u32 count =
      bd::mem::global_load_fast(bd::engine::addr::kParticleModelPoolCount);
  return base != 0 && vo - base < count * kParticleModelVOSize;
}

bool BonePaletteResolves(u32 holder, u32 count) {
  const u32 span = count * 64u;
  for (const u32 slot : {holder + 8u, holder + 20u}) {
    const u32 ptr = bd::mem::try_load<u32>(slot);
    if (!ptr)
      return false;
    const u32 buf = bd::mem::try_load<u32>(ptr);
    if (!buf || !bd::mem::try_at<be_f32>(buf) ||
        !bd::mem::try_at<be_f32>(buf + span - 4u))
      return false;
  }
  return true;
}

void RegisterBoneArray(u32 vo) {
  if (!bd::engine::InterpolationActive() || vo == 0)
    return;
  const u32 count = BoneCount(vo);
  if (count == 0 || count > kMaxBoneMatrices)
    return;
  const u32 holder = BoneHolder(vo);
  if (!BonePaletteResolves(holder, count))
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  BoneArray &e = g_boneArrays[holder];
  e.count = count;
  MapBoneRange(e.currEA, e.currEA, count);
  e.copyTime = bd::engine::FrameTime();
}

void PruneBoneArrays() {
  const double now = bd::engine::FrameTime();
  for (auto it = g_boneArrays.begin(); it != g_boneArrays.end();) {
    if (now - it->second.copyTime > kBoneArrayLinger) {
      if (it->second.scratch)
        bd::gpu::HostHeap::Get().FreeGuest(it->second.scratch);
      UnmapBoneRange(it->second.scratch);
      UnmapBoneRange(it->second.currEA);
      it = g_boneArrays.erase(it);
    } else {
      ++it;
    }
  }
}

enum class BoneStep { Lerp, Snap };

BoneStep ClassifyBone(const float *cur, const float *prv) {
  return DistSq(cur + 12, prv + 12) <= kCutDistance * kCutDistance
             ? BoneStep::Lerp
             : BoneStep::Snap;
}

struct AnimeClock {
  float prev = 0.0f;
  float curr = 0.0f;
  double lastChange = 0.0;
  u64 rewriteTick = ~0ull;
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
    e.rewriteTick = bd::engine::TickCount();
    e.subTick = false;
    return Snapshot::First;
  }
  bool changed = false;
  for (int i = 0; i < floats && !changed; ++i)
    changed = e.curr[size_t(i)] != float(src[i]);
  if (!changed)
    return Snapshot::Ready;
  e.spacing = float(now - e.lastChange);
  e.prev.swap(e.curr);
  ReadFloats(src, e.curr.data(), floats);
  e.lastChange = now;
  if (RewrittenThisTick(e.rewriteTick)) {
    e.subTick = true;
    e.prev = e.curr;
    return Snapshot::Shared;
  }
  e.subTickWriter = e.subTick;
  e.subTick = false;
  return Snapshot::Rolled;
}

u32 LerpToScratch(const FloatSnapshot &e, float a, u32 &scratch,
                  int scratchFloats) {
  if (!EngineScratch(scratch, u32(scratchFloats) * 4))
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

REX_MIDASM_BRANCH(bdLogicTickGateHook, (PPCRegister &r28)) {
  if (bd::engine::TickDue())
    return false;
  r28.u64 = 0xDEAD0000;
  return true;
}
REX_MIDASM_AT(bdLogicTickGateHook, 0x82126B78, before, 0x82126C20, r28);

REX_MIDASM_BRANCH(bdFrameClockGateHook, ()) { return !bd::engine::TickDue(); }
REX_MIDASM_AT(bdFrameClockGateHook, 0x82134DB4, before, 0x82134DB8);

REX_MIDASM_BRANCH(bdCharaBoneChainGateHook, (PPCRegister &)) {
  return !bd::engine::TickDue() &&
         (bd::engine::InterpolationActive() ||
          bd::engine::CutscenePause::Get().Frozen());
}
REX_MIDASM_AT(bdCharaBoneChainGateHook, 0x821681D4, before, 0x821681D8, r3);

REX_MIDASM_BRANCH(bdTickGateHook, ()) { return !bd::engine::TickDue(); }
REX_MIDASM_AT(bdTickGateHook, 0x82433570, before, 0x82433574);
REX_MIDASM_AT(bdTickGateHook, 0x8239CC18, before, 0x8239CC1C);
REX_MIDASM_AT(bdTickGateHook, 0x8212957C, before, 0x821295A0);
REX_MIDASM_AT(bdTickGateHook, 0x82129590, before, 0x821295A0);
REX_MIDASM_AT(bdTickGateHook, 0x821295C0, before, 0x821295D0);
REX_MIDASM_AT(bdTickGateHook, 0x823E9388, before, 0x823E938C);
REX_MIDASM_AT(bdTickGateHook, 0x82381030, before, 0x82381034);
REX_MIDASM_AT(bdTickGateHook, 0x82380610, before, 0x82380614);
REX_MIDASM_AT(bdTickGateHook, 0x82129448, before, 0x82129450);
REX_MIDASM_AT(bdTickGateHook, 0x821294C4, before, 0x821294C8);
REX_MIDASM_AT(bdTickGateHook, 0x821C2E00, before, 0x821C2E08);
REX_MIDASM_AT(bdTickGateHook, 0x821C2E20, before, 0x821C2E54);
REX_MIDASM_AT(bdTickGateHook, 0x82355000, before, 0x82355018);
REX_MIDASM_AT(bdTickGateHook, 0x8216FAF8, before, 0x8216FB00);
REX_MIDASM_AT(bdTickGateHook, 0x82440AC4, before, 0x82440AD8);

namespace {

double FrameStepRatio() {
  return bd::engine::InterpolationActive() ||
                 bd::engine::CutscenePause::Get().Frozen()
             ? bd::engine::FrameDelta() / kTickSeconds
             : 1.0;
}

struct TickCounter {
  double prev = 0.0;
  double curr = 0.0;
  u64 tick = 0;
  u64 lastSeen = 0;
};

std::unordered_map<u32, TickCounter> g_tickCounters;

constexpr u32 kUVAnimOffsetU = 0x1C;
constexpr u32 kUVAnimOffsetV = 0x20;

void LerpTickCounter(u32 key, PPCRegister &value) {
  if (!bd::engine::InterpolationActive())
    return;
  const double live = value.f64;
  const u64 tick = bd::engine::TickCount();
  std::lock_guard<std::mutex> lock(g_interpMutex);
  auto [it, inserted] = g_tickCounters.try_emplace(key);
  TickCounter &c = it->second;
  c.lastSeen = g_frame;
  if (inserted) {
    c.prev = live;
    c.curr = live;
    c.tick = tick;
    return;
  }
  if (live != c.curr) {
    c.prev = c.curr;
    c.curr = live;
    c.tick = tick;
  }
  if (c.tick == tick)
    value.f64 = c.prev + (c.curr - c.prev) * bd::engine::Alpha();
}

} // namespace

REX_MIDASM_HOOK(bdFrameStepScaleHook, (PPCRegister &step)) { step.f64 *= FrameStepRatio(); }
REX_MIDASM_AT(bdFrameStepScaleHook, 0x824543D0, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x82455588, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x82453A94, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5A0, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5A4, before, f13);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5B4, before, f12);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5B8, before, f11);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5DC, before, f13);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5E8, before, f13);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E5F4, before, f12);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8221E600, before, f11);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8239CBBC, before, f10);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x821295DC, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8212964C, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x823E9760, before, f14);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x821C2A00, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x821C2A34, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x822062B4, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x822062E0, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x821A1384, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x82463FFC, before, f0);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x82321350, before, f13);
REX_MIDASM_AT(bdFrameStepScaleHook, 0x8232135C, before, f0);

REX_MIDASM_HOOK(bdFrameStepSumHook, (PPCRegister &sum, PPCRegister &step)) {
  sum.f64 -= step.f64 * (1.0 - FrameStepRatio());
}
REX_MIDASM_AT(bdFrameStepSumHook, 0x821A13B0, before, f0, f30);

REX_MIDASM_HOOK(bdFrameFactorScaleHook, (PPCRegister &factor)) {
  if (factor.f64 > 0.0)
    factor.f64 = std::pow(factor.f64, FrameStepRatio());
}
REX_MIDASM_AT(bdFrameFactorScaleHook, 0x82151268, before, f13);
REX_MIDASM_AT(bdFrameFactorScaleHook, 0x82151284, before, f0);

REX_MIDASM_HOOK(bdPopUpAgeLerpHook, (PPCRegister &age)) {
  if (bd::engine::InterpolationActive())
    age.f64 += bd::engine::Alpha();
}
REX_MIDASM_AT(bdPopUpAgeLerpHook, 0x823EFFE0, before, f13);
REX_MIDASM_AT(bdPopUpAgeLerpHook, 0x823F028C, before, f13);
REX_MIDASM_AT(bdPopUpAgeLerpHook, 0x823F048C, before, f12);
REX_MIDASM_AT(bdPopUpAgeLerpHook, 0x823F05B8, before, f0);

REX_MIDASM_HOOK(bdTickCounterLerpHook, (PPCRegister &value, PPCRegister &owner)) {
  LerpTickCounter(owner.u32, value);
}
REX_MIDASM_AT(bdTickCounterLerpHook, 0x82359564, after, f12, r3);
REX_MIDASM_AT(bdTickCounterLerpHook, 0x82386F28, after, f12, r3);
REX_MIDASM_AT(bdTickCounterLerpHook, 0x82369688, after, f12, r31);
REX_MIDASM_AT(bdTickCounterLerpHook, 0x8238108C, after, f0, r31);
REX_MIDASM_AT(bdTickCounterLerpHook, 0x8238066C, after, f0, r31);

REX_MIDASM_HOOK(bdUVAnimOffsetULerpHook, (PPCRegister &value, PPCRegister &entry)) {
  LerpTickCounter(entry.u32 + kUVAnimOffsetU, value);
}
REX_MIDASM_AT(bdUVAnimOffsetULerpHook, 0x82281804, after, f0, r11);
REX_MIDASM_AT(bdUVAnimOffsetULerpHook, 0x82281824, after, f0, r11);

REX_MIDASM_HOOK(bdUVAnimOffsetVLerpHook, (PPCRegister &value, PPCRegister &entry)) {
  LerpTickCounter(entry.u32 + kUVAnimOffsetV, value);
}
REX_MIDASM_AT(bdUVAnimOffsetVLerpHook, 0x82281810, after, f0, r11);
REX_MIDASM_AT(bdUVAnimOffsetVLerpHook, 0x82281830, after, f0, r11);

REX_MIDASM_HOOK(bdAnimeChainEnableActiveHook, (PPCRegister &r28)) {
  bd::engine::AnimeData(r28.u32).SyncActiveChain();
}
REX_MIDASM_AT(bdAnimeChainEnableActiveHook, 0x82153D34, before, r28);

REX_MIDASM_BRANCH(bdPlayerAmbientRampGateHook, (PPCRegister &r31)) {
  if (!bd::engine::InterpolationActive())
    return false;
  bd::engine::Player chara = bd::engine::PlyTask(r31.u32).Chara();
  if (!chara)
    return true;
  const f32 green = chara.Ambient(1);
  if (green >= 1.0f)
    return true;
  const f32 next = green + 0.1f * f32(bd::engine::FrameDelta() / kTickSeconds);
  if (next >= 1.0f) {
    chara.SetAmbient(0, 1.0f);
    chara.SetAmbient(1, 1.0f);
    chara.SetAmbient(2, 1.0f);
  } else {
    chara.SetAmbient(1, next);
    chara.SetAmbient(2, next);
  }
  return true;
}
REX_MIDASM_AT(bdPlayerAmbientRampGateHook, 0x823B4FB4, before, 0x823B4FDC, r31);

namespace {

constexpr u32 kEvtPlaying = 2;
constexpr int kMaxEventTasks = 8;
constexpr int kMaxEventChildren = 256;

struct IssChild_t;

struct IssEvent_t {
  /* 0x000 */ u8 _pad000[0x68];
  /* 0x068 */ be_f32 frame;
  /* 0x06C */ u8 _pad06C[0x88 - 0x6C];
  /* 0x088 */ be_u32 state;
  /* 0x08C */ u8 _pad08C[0x3EC - 0x8C];
  /* 0x3EC */ be_f32 speed;
  /* 0x3F0 */ rex::kernel::be_ptr<IssChild_t> childList;
};
static_assert(offsetof(IssEvent_t, frame) == 0x68);
static_assert(offsetof(IssEvent_t, state) == 0x88);
static_assert(offsetof(IssEvent_t, speed) == 0x3EC);
static_assert(offsetof(IssEvent_t, childList) == 0x3F0);

struct IssChild_t {
  /* 0x00 */ be_u32 vtable;
  /* 0x04 */ u8 _pad004[0x80 - 0x04];
  /* 0x80 */ rex::kernel::be_ptr<IssEvent_t> parent;
  /* 0x84 */ rex::kernel::be_ptr<IssChild_t> next;
};
static_assert(offsetof(IssChild_t, parent) == 0x80);
static_assert(offsetof(IssChild_t, next) == 0x84);

struct IssVtable_t {
  /* 0x00 */ u8 _pad000[0x08];
  /* 0x08 */ be_u32 update;
};
static_assert(offsetof(IssVtable_t, update) == 0x08);

struct IssActor_t {
  /* 0x000 */ u8 _pad000[0x94];
  /* 0x094 */ be_u32 visualObject;
  /* 0x098 */ u8 _pad098[0x04];
  /* 0x09C */ char modelName[0x150 - 0x9C];
  /* 0x150 */ be_u32 hidePending;
  /* 0x154 */ u8 _pad154[0x238 - 0x154];
  /* 0x238 */ be_f32 motionCursor;
  /* 0x23C */ be_f32 motionEnd;
  /* 0x240 */ u8 _pad240[0x2CC - 0x240];
  /* 0x2CC */ be_u32 guided;
};
static_assert(offsetof(IssActor_t, visualObject) == 0x94);
static_assert(offsetof(IssActor_t, modelName) == 0x9C);
static_assert(offsetof(IssActor_t, hidePending) == 0x150);
static_assert(offsetof(IssActor_t, motionCursor) == 0x238);
static_assert(offsetof(IssActor_t, motionEnd) == 0x23C);
static_assert(offsetof(IssActor_t, guided) == 0x2CC);

struct IssObject_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x38 - 0x04];
  /* 0x038 */ be_u32 firstChild;
  /* 0x03C */ be_u32 nextSibling;
  /* 0x040 */ be_u32 parentTask;
  /* 0x044 */ be_u32 rootTask;
  /* 0x048 */ u8 _pad048[0x58 - 0x48];
  /* 0x058 */ be_u32 taskFlags;
  /* 0x05C */ u8 _pad05C[0x68 - 0x5C];
  /* 0x068 */ be_f32 time;
  /* 0x06C */ u8 _pad06C[0x7C - 0x6C];
  /* 0x07C */ be_u32 loadState;
  /* 0x080 */ be_u32 event;
  /* 0x084 */ be_u32 next;
  /* 0x088 */ be_u32 visualObject;
  /* 0x08C */ u8 modelLoad[0x7C8 - 0x8C];
  /* 0x7C8 */ u8 _pad7C8[0x7D0 - 0x7C8];
  /* 0x7D0 */ u8 isItem;
  /* 0x7D1 */ u8 itemPacked;
  /* 0x7D2 */ char eventDir[0x8D6 - 0x7D2];
  /* 0x8D6 */ char modelPath[0x9DC - 0x8D6];
  /* 0x9DC */ be_u32 track;
  /* 0x9E0 */ be_u32 visible;
  /* 0x9E4 */ be_u32 hidePending;
  /* 0x9E8 */ be_f32 pos[3];
  /* 0x9F4 */ be_f32 rot[3];
  /* 0xA00 */ be_f32 scale[3];
  /* 0xA0C */ be_f32 color[4];
  /* 0xA1C */ u8 _padA1C[0xA5C - 0xA1C];
  /* 0xA5C */ be_f32 motionCursor;
  /* 0xA60 */ be_f32 motionEnd;
  /* 0xA64 */ be_f32 posCursor;
  /* 0xA68 */ be_f32 posDuration;
  /* 0xA6C */ be_f32 posFrom[3];
  /* 0xA78 */ be_f32 posTo[3];
  /* 0xA84 */ be_f32 rotCursor;
  /* 0xA88 */ be_f32 rotDuration;
  /* 0xA8C */ be_f32 rotFrom[3];
  /* 0xA98 */ be_f32 rotTo[3];
  /* 0xAA4 */ be_f32 scaleCursor;
  /* 0xAA8 */ be_f32 scaleDuration;
  /* 0xAAC */ be_f32 scaleFrom[3];
  /* 0xAB8 */ be_f32 scaleTo[3];
  /* 0xAC4 */ be_f32 colorCursor;
  /* 0xAC8 */ be_f32 colorDuration;
  /* 0xACC */ be_f32 colorFrom[4];
  /* 0xADC */ be_f32 colorTo[4];
  /* 0xAEC */ be_u32 guideActive;
  /* 0xAF0 */ be_u32 guide;
  /* 0xAF4 */ be_u32 moveActive;
  /* 0xAF8 */ be_u32 move;
  /* 0xAFC */ be_f32 attachCursor;
  /* 0xB00 */ be_f32 attachDuration;
  /* 0xB04 */ be_u32 attachActorName;
  /* 0xB08 */ be_u32 attachBoneName;
  /* 0xB0C */ be_u32 attachFlags;
  /* 0xB10 */ be_u32 attachRotateOffset;
  /* 0xB14 */ be_f32 attachPosOffset[3];
  /* 0xB20 */ be_f32 attachRotOffset[3];
  /* 0xB2C */ u8 _padB2C[0xB38 - 0xB2C];
  /* 0xB38 */ be_f32 materialCursor;
  /* 0xB3C */ be_f32 materialDuration;
  /* 0xB40 */ be_f32 materialFrom[16];
  /* 0xB80 */ be_f32 materialTo[16];
  /* 0xBC0 */ be_f32 baseColor[4];
  /* 0xBD0 */ u8 _padBD0[0xBD4 - 0xBD0];
  /* 0xBD4 */ be_f32 initPos[3];
  /* 0xBE0 */ be_f32 initPosTarget[3];
  /* 0xBEC */ be_f32 initRot[3];
  /* 0xBF8 */ be_f32 initRotTarget[3];
  /* 0xC04 */ be_f32 initDuration;
  /* 0xC08 */ be_f32 initCursor;
  /* 0xC0C */ be_u32 initVisible;
  /* 0xC10 */ be_f32 initPosCursor;
  /* 0xC14 */ be_f32 initPosDuration;
  /* 0xC18 */ be_f32 initPosFrom[3];
  /* 0xC24 */ be_f32 initPosTo[3];
  /* 0xC30 */ be_f32 initRotCursor;
  /* 0xC34 */ be_f32 initRotDuration;
  /* 0xC38 */ be_f32 initRotFrom[3];
  /* 0xC44 */ be_f32 initRotTo[3];
  /* 0xC50 */ be_u32 itemPackRequest;
  /* 0xC54 */ be_i32 itemPackSlot;
  /* 0xC58 */ be_u32 itemPackHandle;
  /* 0xC5C */ u8 _padC5C[0xC60 - 0xC5C];
};
static_assert(offsetof(IssObject_t, firstChild) == 0x38);
static_assert(offsetof(IssObject_t, taskFlags) == 0x58);
static_assert(offsetof(IssObject_t, time) == 0x68);
static_assert(offsetof(IssObject_t, loadState) == 0x7C);
static_assert(offsetof(IssObject_t, event) == 0x80);
static_assert(offsetof(IssObject_t, next) == 0x84);
static_assert(offsetof(IssObject_t, visualObject) == 0x88);
static_assert(offsetof(IssObject_t, isItem) == 0x7D0);
static_assert(offsetof(IssObject_t, eventDir) == 0x7D2);
static_assert(offsetof(IssObject_t, modelPath) == 0x8D6);
static_assert(offsetof(IssObject_t, track) == 0x9DC);
static_assert(offsetof(IssObject_t, visible) == 0x9E0);
static_assert(offsetof(IssObject_t, hidePending) == 0x9E4);
static_assert(offsetof(IssObject_t, pos) == 0x9E8);
static_assert(offsetof(IssObject_t, color) == 0xA0C);
static_assert(offsetof(IssObject_t, motionCursor) == 0xA5C);
static_assert(offsetof(IssObject_t, motionEnd) == 0xA60);
static_assert(offsetof(IssObject_t, posCursor) == 0xA64);
static_assert(offsetof(IssObject_t, rotCursor) == 0xA84);
static_assert(offsetof(IssObject_t, scaleCursor) == 0xAA4);
static_assert(offsetof(IssObject_t, colorCursor) == 0xAC4);
static_assert(offsetof(IssObject_t, guideActive) == 0xAEC);
static_assert(offsetof(IssObject_t, move) == 0xAF8);
static_assert(offsetof(IssObject_t, attachCursor) == 0xAFC);
static_assert(offsetof(IssObject_t, attachActorName) == 0xB04);
static_assert(offsetof(IssObject_t, attachBoneName) == 0xB08);
static_assert(offsetof(IssObject_t, attachPosOffset) == 0xB14);
static_assert(offsetof(IssObject_t, attachRotOffset) == 0xB20);
static_assert(offsetof(IssObject_t, materialCursor) == 0xB38);
static_assert(offsetof(IssObject_t, materialFrom) == 0xB40);
static_assert(offsetof(IssObject_t, materialTo) == 0xB80);
static_assert(offsetof(IssObject_t, baseColor) == 0xBC0);
static_assert(offsetof(IssObject_t, initPos) == 0xBD4);
static_assert(offsetof(IssObject_t, initDuration) == 0xC04);
static_assert(offsetof(IssObject_t, initPosCursor) == 0xC10);
static_assert(offsetof(IssObject_t, initRotCursor) == 0xC30);
static_assert(offsetof(IssObject_t, itemPackRequest) == 0xC50);
static_assert(sizeof(IssObject_t) == 0xC60);

struct AnimClip_t {
  /* 0x00 */ u8 _pad00[0x04];
  /* 0x04 */ be_u16 length;
};
static_assert(offsetof(AnimClip_t, length) == 0x04);

struct AnimSlot_t {
  /* 0x00 */ u8 _pad00[0x0C];
  /* 0x0C */ rex::kernel::be_ptr<AnimClip_t> clip;
};
static_assert(offsetof(AnimSlot_t, clip) == 0x0C);

struct CharaAnim_t {
  /* 0x000 */ u8 _pad000[0x754];
  /* 0x754 */ be_u32 loopFlag;
  /* 0x758 */ u8 _pad758[0x768 - 0x758];
  /* 0x768 */ be_f32 cursor;
  /* 0x76C */ be_f32 cursorRate;
  /* 0x770 */ u8 _pad770[0x780 - 0x770];
  /* 0x780 */ rex::kernel::be_ptr<AnimSlot_t> anim;
};
static_assert(offsetof(CharaAnim_t, loopFlag) == 0x754);
static_assert(offsetof(CharaAnim_t, cursor) == 0x768);
static_assert(offsetof(CharaAnim_t, cursorRate) == 0x76C);
static_assert(offsetof(CharaAnim_t, anim) == 0x780);

REX_GUEST_GLOBAL(be_f32, kSceneSpeedMul, 0x82DDA880);
constexpr f32 kWindowClosingMargin = 1.5f;

constexpr u32 kIssCameraUpdateEA = 0x824046C0;
constexpr u32 kIssObjectUpdateEA = 0x82406688;
constexpr u32 kIssMapUpdateEA = 0x823F4F40;
constexpr u32 kIssSpriteVf02EA = 0x82412688;

constexpr u32 kEvtMovementUpdates[] = {
    kIssObjectUpdateEA,
    kIssCameraUpdateEA,
    kIssMapUpdateEA,
    kIssSpriteVf02EA,
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
std::unordered_set<u32> g_evtRestartedVO;

std::atomic<double> g_evtEngagedUntil{0.0};
std::atomic<bool> g_evtEngaged{false};

void UpdateEventEngagement() {
  g_evtEngaged.store(
      bd::engine::IssEvent::LiveCount() > 0 &&
          bd::engine::FrameTime() <
              g_evtEngagedUntil.load(std::memory_order_relaxed) &&
          !bd::engine::Game::Get().BattleCameraTask(),
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
  auto *child = bd::mem::at<IssChild_t>(childEA);
  if (!child)
    return nullptr;
  if (!g_hostEvtDrive && g_evtTickAdvanced.find(child->parent.GetAddress()) ==
                             g_evtTickAdvanced.end())
    return nullptr;
  return child->parent.GetHost();
}

template <typename F> void ForEachEventChild(const IssEvent_t &evt, F &&visit) {
  u32 childEA = evt.childList.GetAddress();
  auto *child = evt.childList.GetHost();
  for (int guard = 0; child && guard < kMaxEventChildren; ++guard) {
    visit(childEA, *child);
    childEA = child->next.GetAddress();
    child = child->next.GetHost();
  }
}

bool MotionWindowClosing(f32 cursor, f32 end, f32 speed) {
  return end > 0.0f && cursor < end &&
         end - cursor <= speed * kWindowClosingMargin;
}

constexpr int kCamOutputFloats = 64;
constexpr int kCamSampleFloats = 16;
struct CamVec3_t {
  /* 0x00 */ be_f32 v[3];
};
static_assert(sizeof(CamVec3_t) == 0x0C);
REX_GUEST_GLOBAL(CamVec3_t, kCamPosGlobal, 0x82DDA8D4);
REX_GUEST_GLOBAL(CamVec3_t, kCamDirGlobal, 0x82DDA8E0);

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

// The camera path stepper clears phase when it advances a segment and counts
// segmentIndex up.
struct CamPath_t {
  /* 0x00 */ be_u32 phase;
  /* 0x04 */ u8 _pad004[0x0C - 0x04];
  /* 0x0C */ be_u32 segmentIndex;
};
static_assert(offsetof(CamPath_t, phase) == 0x00);
static_assert(offsetof(CamPath_t, segmentIndex) == 0x0C);

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

void CameraGuardCapture(rex::memory::MappedPtr<IssCamera_t> cam) {
  if (!cam || !CameraOutputFinite(*cam))
    return;
  CamSave &s = g_camSaves[cam.GetGuestAddress()];
  ReadFloats(cam->output, s.output, kCamOutputFloats);
  ReadFloats(cam->dir, s.dir, 2);
  ReadFloats(cam->sample, s.sample, kCamSampleFloats);
  if (const auto view = bd::mem::view()) {
    ReadFloats(kCamPosGlobal.GetHost(*view)->v, s.posGlobal, 3);
    ReadFloats(kCamDirGlobal.GetHost(*view)->v, s.dirGlobal, 3);
  }
  s.valid = true;
}

void CameraGuardRepair(rex::memory::MappedPtr<IssCamera_t> cam) {
  if (!cam || CameraOutputFinite(*cam))
    return;
  auto it = g_camSaves.find(cam.GetGuestAddress());
  if (it == g_camSaves.end() || !it->second.valid)
    return;
  const CamSave &s = it->second;
  WriteFloats(cam->output, s.output, kCamOutputFloats);
  WriteFloats(cam->dir, s.dir, 2);
  WriteFloats(cam->sample, s.sample, kCamSampleFloats);
  if (const auto view = bd::mem::view()) {
    WriteFloats(kCamPosGlobal.GetHost(*view)->v, s.posGlobal, 3);
    WriteFloats(kCamDirGlobal.GetHost(*view)->v, s.dirGlobal, 3);
  }
}

f32 DriveEventChildren(IssEvent_t &evt, f32 frac) {
  const f32 speed = evt.speed;
  if (speed == 0.0f)
    return 0.0f;
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
    else if (auto *host = bd::platform::FindTitleFunction(fn))
      rex::ppc::HostToGuestCall<void>(host, childEA);
  });
  g_hostEvtDrive = false;
  evt.speed = speed;
  return speed * frac;
}

bool EvtClipRestarted(u32 childEA) {
  auto *object = bd::mem::at<IssObject_t>(childEA);
  return object && g_evtRestartedVO.count(u32(object->visualObject)) != 0;
}

int LiveEventAddresses(u32 *out) {
  int n = 0;
  const size_t count = bd::engine::IssEvent::LiveCount();
  for (size_t i = 0; i < count; ++i) {
    const u32 ea = bd::engine::IssEvent::LiveAt(i).Address();
    if (ea)
      out[n++] = ea;
  }
  return n;
}

void StepEventScenes() {
  g_evtTickAdvanced.clear();
  g_evtRestartedVO.clear();
  const bool tick = bd::engine::TickDue();
  if (tick)
    g_evtTickScaled.clear();
  if (!bd::engine::InterpolationActive() ||
      bd::engine::IssEvent::LiveCount() == 0) {
    g_evtDrive.clear();
    g_camSaves.clear();
    g_evtCameras.clear();
    return;
  }
  u32 live[kMaxEventTasks];
  const int n = LiveEventAddresses(live);
  std::erase_if(g_evtDrive, [&](const auto &kv) {
    return std::find(live, live + n, kv.first) == live + n;
  });
  for (int i = 0; i < n; ++i) {
    const u32 evtEA = live[i];
    auto *evt = bd::mem::at<IssEvent_t>(evtEA);
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
    auto *child = bd::mem::at<IssChild_t>(childEA);
    if (!child)
      return;
    const auto it = g_evtTickAdvanced.find(child->parent.GetAddress());
    if (it == g_evtTickAdvanced.end() || EvtClipRestarted(childEA))
      return;
    evt = child->parent.GetHost();
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
  const int n = LiveEventAddresses(evts);
  for (int i = 0; i < n; ++i)
    if (auto *evt = bd::mem::at<IssEvent_t>(evts[i]))
      ForEachEventChild(*evt, [&](u32 childEA, const IssChild_t &) {
        live.insert(childEA);
      });
  for (const u32 ea : g_evtObjectHides) {
    auto *obj = live.count(ea) ? bd::mem::try_at<IssObject_t>(ea) : nullptr;
    if (obj && u32(obj->hidePending) != 0)
      obj->visible = 0u;
  }
  auto *apply =
      bd::platform::FindTitleFunction(kIssActorApplySpecialModelFlagsEA);
  for (const u32 ea : g_evtActorHides) {
    auto *actor = live.count(ea) ? bd::mem::try_at<IssActor_t>(ea) : nullptr;
    if (apply && actor && u32(actor->hidePending) != 0)
      rex::ppc::HostToGuestCall<void>(apply, ea);
  }
  g_evtObjectHides.clear();
  g_evtActorHides.clear();
}

} // namespace

REX_MIDASM_BRANCH(bdEvtObjectHideDeferHook, (PPCRegister &r31)) {
  if (!bd::engine::InterpolationActive())
    return false;
  g_evtObjectHides.insert(r31.u32);
  return true;
}
REX_MIDASM_AT(bdEvtObjectHideDeferHook, 0x82406EEC, before, 0x82406EF4, r31);

REX_MIDASM_BRANCH(bdEvtActorHideDeferHook, (PPCRegister &r31)) {
  if (!bd::engine::InterpolationActive())
    return false;
  g_evtActorHides.insert(r31.u32);
  return true;
}
REX_MIDASM_AT(bdEvtActorHideDeferHook, 0x824107D4, before, 0x824107DC, r31);

REX_MIDASM_BRANCH(bdEvtMapHideDeferHook, ()) { return g_hostEvtDrive; }
REX_MIDASM_AT(bdEvtMapHideDeferHook, 0x823F4FEC, before, 0x823F5050);

REX_MIDASM_BRANCH(bdEvtCameraPathEndHook, (PPCRegister &r31)) {
  const auto *path = bd::mem::try_at<const CamPath_t>(r31.u32);
  return path && u32(path->phase) == 0 && u32(path->segmentIndex) != 0;
}
REX_MIDASM_AT(bdEvtCameraPathEndHook, 0x8241AF90, before, 0x8241B014, r31);

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
  /* 0x00 */ rex::kernel::be_ptr<LipRecord_t> records;
  /* 0x04 */ u8 _pad004[0x44 - 0x04];
  /* 0x44 */ be_u32 recordCount;
};
static_assert(offsetof(LipClip_t, recordCount) == 0x44);

struct LipOwner_t;

struct LipPlayer_t {
  /* 0x00 */ be_u32 state;
  /* 0x04 */ rex::kernel::be_ptr<LipClip_t> clip;
  /* 0x08 */ be_f32 cursor;
  /* 0x0C */ u8 _pad00C[0x30 - 0x0C];
  /* 0x30 */ u8 viseme;
  /* 0x31 */ u8 prevViseme;
  /* 0x32 */ u8 _pad032[0x34 - 0x32];
  /* 0x34 */ be_f32 amp;
  /* 0x38 */ u8 _pad038[0x50 - 0x38];
  /* 0x50 */ rex::kernel::be_ptr<LipOwner_t> owner;
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

REX_DETOUR(LipPlayerApply, void, (LipPlayer_t *lip, u32 outFlag)) {
  auto *owner = lip ? lip->owner.GetHost() : nullptr;
  const u32 prevVis = lip ? lip->prevViseme : 0;
  const f32 blendBefore = owner ? f32(owner->blend) : 0.0f;
  original(lip, outFlag);
  if (!owner || !bd::engine::InterpolationActive())
    return;
  const u32 vis = lip->viseme;
  if (vis != prevVis && vis != 0 && prevVis != 0)
    owner->blend = blendBefore;
}

REX_DETOUR(LipPlayerSample, void, (LipPlayer_t *lip)) {
  original(lip);
  if (!lip || !bd::engine::InterpolationActive() ||
      bd::engine::IssEvent::LiveCount() == 0)
    return;
  if (u32(lip->state) != kLipPlaying)
    return;
  auto *clip = lip->clip.GetHost();
  if (!clip)
    return;
  const f32 cursor = lip->cursor;
  const u32 count = clip->recordCount;
  const u32 idx = u32(cursor) * 2;
  const auto *records = clip->records.GetHost();
  if (!records || idx >= count)
    return;
  const u32 next = idx + 2 < count ? idx + 2 : idx;
  const f64 db0 = i32(records[idx].level);
  const f64 db1 = i32(records[next].level);
  const f64 frac = cursor - std::floor(cursor);
  const f64 cont =
      LipContinuous(db0) + (LipContinuous(db1) - LipContinuous(db0)) * frac;
  lip->amp = f32(cont * (f32(lip->amp) / LipStaircase(db0)));
}

REX_DETOUR(issEvent__Update, void, (IssEvent_t *evtTask)) {
  original(evtTask);
  if (evtTask && u32(evtTask->state) == kEvtPlaying) {
    g_evtEngagedUntil.store(bd::engine::FrameTime() + kTickSeconds * 2.0,
                            std::memory_order_relaxed);
  }
}

REX_DETOUR(bdVisualObjectSetAnimation, void,
           (rex::memory::MappedPtr<CharaAnim_t> visualObject, u32 slot, u32 clip,
            f32 blend, u32 arg4, u32 arg5)) {
  CharaAnim_t *anim = slot == 0 ? visualObject.GetHostPtr() : nullptr;
  const f32 before = anim ? f32(anim->cursor) : 0.0f;
  const u32 clipBefore = anim ? anim->anim.GetAddress() : 0;
  original(visualObject, slot, clip, blend, arg4, arg5);
  if (anim && f32(anim->cursor) == 0.0f &&
      (before != 0.0f || anim->anim.GetAddress() != clipBefore))
    g_evtRestartedVO.insert(visualObject.GetGuestAddress());
}

REX_DETOUR(issObject__Update, void, (rex::memory::MappedPtr<IssObject_t> object)) {
  EvtSpeedRemainder z(object.GetGuestAddress());
  auto *evt = DrivenEvent(object.GetGuestAddress());
  IssObject_t *obj = evt ? object.GetHostPtr() : nullptr;
  if (obj) {
    g_evtWindowClosing = MotionWindowClosing(
        obj->motionCursor, obj->motionEnd, evt->speed);
  }
  original(object);
  g_evtWindowClosing = false;
}

REX_DETOUR(issCamera__Update, void, (rex::memory::MappedPtr<IssCamera_t> camera)) {
  const bool guard =
      bd::engine::InterpolationActive() &&
      g_evtCameras.find(camera.GetGuestAddress()) != g_evtCameras.end();
  if (guard)
    CameraGuardCapture(camera);
  original(camera);
  if (guard)
    CameraGuardRepair(camera);
}

REX_DETOUR(issMap__Update, void, (u32 map)) {
  EvtSpeedRemainder z(map);
  original(map);
}

REX_DETOUR(issSprite__Update, void, (u32 sprite)) {
  EvtSpeedRemainder z(sprite);
  original(sprite);
}

REX_DETOUR(issActor__Update, void, (rex::memory::MappedPtr<IssActor_t> actor)) {
  auto *evt = DrivenEvent(actor.GetGuestAddress());
  IssActor_t *act = evt ? actor.GetHostPtr() : nullptr;
  if (act) {
    g_evtWindowClosing =
        u32(act->guided) == 0 &&
        MotionWindowClosing(act->motionCursor, act->motionEnd, evt->speed);
  }
  original(actor);
  g_evtWindowClosing = false;
}

REX_DETOUR(bdAnimationUpdate, void,
           (CharaAnim_t *visualObject, i32 mode, u32 slot)) {
  CharaAnim_t *vo = g_evtWindowClosing ? visualObject : nullptr;
  bool hold = false;
  f32 length = 0.0f;
  if (vo && u32(vo->loopFlag) != 0) {
    auto *animSlot = vo->anim.GetHost();
    auto *clip = animSlot ? animSlot->clip.GetHost() : nullptr;
    if (clip) {
      length = f32(u16(clip->length));
      const f32 cursor = vo->cursor;
      const f32 step =
          f32(vo->cursorRate) * bd::mem::global_load_fast(kSceneSpeedMul);
      hold = length > 0.0f && cursor <= length && cursor + step > length;
    }
  }
  f32 cursorRate = 0.0f;
  if (hold) {
    cursorRate = vo->cursorRate;
    vo->cursor = length;
    vo->cursorRate = 0.0f;
  }
  original(visualObject, mode, slot);
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

REX_IMPORT(__imp__bdAnimRotationKeysSample, guest_bdAnimRotationKeysSample,
           void(u32, u32, f32, i32));
void bdAnimRotationKeysSample_hook(u32 outEA, u32 keysEA, f32 time, i32 last) {
  if (keysEA == 0 || last < 0 || !bd::mem::try_at<be_i16>(keysEA)) {
    guest_bdAnimRotationKeysSample(outEA, keysEA, time, last);
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
REX_HOOK(bdAnimRotationKeysSample, bdAnimRotationKeysSample_hook);

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
      if (auto chara = bd::engine::PlyTask(it->first).Chara()) {
        chara.SetAlpha(it->second.alpha);
        chara.SetAlphaDirty(it->second.dirty);
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

namespace {

constexpr u32 kEffEmitterTrail = 0x20000;
constexpr u32 kEffParticleSnapshotted = 0x2;
constexpr u32 kEffRingSlotSize = 16;
constexpr int kMaxEffEmitters = 256;
constexpr int kMaxEffParticles = 8192;
constexpr u64 kEffHistoryLinger = 4;
constexpr u32 kMaxEffTrailPoints = 256;
constexpr float kEffUVWrap = 0.5f;
constexpr u32 kEffEmitterDefSize = 560;
constexpr u32 kEffCurvesPerEmitter = 22;
constexpr u32 kEffCurveUV = 10;
constexpr u32 kEffCurveMaskUV = 0x100;
constexpr u32 kMaxEffCurveKeys = 64;
constexpr float kEffAgeStep = 1.0f;

struct EffEmitter_t;
struct EffEmitterDef_t;
struct EffEffect_t;
struct EffCurve_t;
struct EffParticle_t;

struct EffSystem_t {
  /* 0x00 */ u8 _pad000[0x0C];
  /* 0x0C */ rex::kernel::be_ptr<EffEmitter_t> emitters;
};
static_assert(offsetof(EffSystem_t, emitters) == 0x0C);

struct EffTask_t {
  /* 0x00 */ u8 _pad000[0x14];
  /* 0x14 */ EffSystem_t system;
};
static_assert(offsetof(EffTask_t, system) == 0x14);

struct EffEmitter_t {
  /* 0x000 */ u8 _pad000[0x04];
  /* 0x004 */ rex::kernel::be_ptr<EffEmitter_t> next;
  /* 0x008 */ u8 _pad008[0xB0 - 0x08];
  /* 0x0B0 */ be_f32 worldPos[3];
  /* 0x0BC */ u8 _pad0BC[0xD0 - 0xBC];
  /* 0x0D0 */ rex::kernel::be_ptr<EffEffect_t> effect;
  /* 0x0D4 */ rex::kernel::be_ptr<EffEmitterDef_t> def;
  /* 0x0D8 */ u8 _pad0D8[0xDC - 0xD8];
  /* 0x0DC */ rex::kernel::be_ptr<EffParticle_t> particles;
};
static_assert(offsetof(EffEmitter_t, next) == 0x04);
static_assert(offsetof(EffEmitter_t, worldPos) == 0xB0);
static_assert(offsetof(EffEmitter_t, effect) == 0xD0);
static_assert(offsetof(EffEmitter_t, def) == 0xD4);
static_assert(offsetof(EffEmitter_t, particles) == 0xDC);

struct EffEmitterDef_t {
  /* 0x00 */ u8 _pad000[0x4C];
  /* 0x4C */ be_u32 flags;
};
static_assert(offsetof(EffEmitterDef_t, flags) == 0x4C);

struct EffEffect_t {
  /* 0x00 */ u8 _pad000[0x04];
  /* 0x04 */ be_u32 emitterDefs;
  /* 0x08 */ u8 _pad008[0x1C - 0x08];
  /* 0x1C */ rex::kernel::be_ptr<be_u32> curveMasks;
  /* 0x20 */ rex::kernel::be_ptr<EffCurve_t> curves;
};
static_assert(offsetof(EffEffect_t, emitterDefs) == 0x04);
static_assert(offsetof(EffEffect_t, curveMasks) == 0x1C);
static_assert(offsetof(EffEffect_t, curves) == 0x20);

struct EffCurve_t {
  /* 0x00 */ be_u32 count;
  /* 0x04 */ rex::kernel::be_ptr<be_f32> keys;
};
static_assert(sizeof(EffCurve_t) == 0x08);

struct EffEmitterView {
  u32 ea = 0;
  u32 flags = 0;
  const EffCurve_t *uvCurves = nullptr;
};

struct EffParticle_t {
  /* 0x000 */ u8 _pad000[0x04];
  /* 0x004 */ rex::kernel::be_ptr<EffParticle_t> next;
  /* 0x008 */ u8 _pad008[0x10 - 0x08];
  /* 0x010 */ be_f32 life;
  /* 0x014 */ be_f32 age;
  /* 0x018 */ u8 _pad018[0x80 - 0x18];
  /* 0x080 */ be_f32 anchor[3];
  /* 0x08C */ u8 _pad08C[0xBC - 0x8C];
  /* 0x0BC */ be_f32 size[3];
  /* 0x0C8 */ be_f32 color[4];
  /* 0x0D8 */ u8 _pad0D8[0xE8 - 0xD8];
  /* 0x0E8 */ be_f32 uv[2];
  /* 0x0F0 */ u8 _pad0F0[0x170 - 0xF0];
  /* 0x170 */ be_u32 flags;
  /* 0x174 */ be_f32 trailHead[4];
  /* 0x184 */ be_f32 drawPos[3];
  /* 0x190 */ be_f32 drawSize[3];
  /* 0x19C */ be_f32 drawMatrix[16];
  /* 0x1DC */ be_u32 ring;
  /* 0x1E0 */ be_u32 ringCount;
  /* 0x1E4 */ be_u32 ringHead;
  /* 0x1E8 */ be_u32 ringSize;
};
static_assert(offsetof(EffParticle_t, next) == 0x04);
static_assert(offsetof(EffParticle_t, life) == 0x10);
static_assert(offsetof(EffParticle_t, age) == 0x14);
static_assert(offsetof(EffParticle_t, anchor) == 0x80);
static_assert(offsetof(EffParticle_t, size) == 0xBC);
static_assert(offsetof(EffParticle_t, color) == 0xC8);
static_assert(offsetof(EffParticle_t, uv) == 0xE8);
static_assert(offsetof(EffParticle_t, flags) == 0x170);
static_assert(offsetof(EffParticle_t, trailHead) == 0x174);
static_assert(offsetof(EffParticle_t, drawPos) == 0x184);
static_assert(offsetof(EffParticle_t, drawSize) == 0x190);
static_assert(offsetof(EffParticle_t, drawMatrix) == 0x19C);
static_assert(offsetof(EffParticle_t, ring) == 0x1DC);
static_assert(offsetof(EffParticle_t, ringCount) == 0x1E0);
static_assert(offsetof(EffParticle_t, ringHead) == 0x1E4);
static_assert(offsetof(EffParticle_t, ringSize) == 0x1E8);

struct EffParticlePose {
  float age;
  float size[3];
  float color[4];
  float uv[2];
  float drawPos[3];
  float drawSize[3];
  float drawMatrix[16];
};

struct EffParticleHistory {
  EffParticlePose prev;
  EffParticlePose curr;
  std::vector<float> prevRing;
  std::vector<float> currRing;
  u64 tick = 0;
  u32 spawned = 0;
  u32 captured = 0;
  bool served = false;
};

struct EffEmitterMotion {
  float prev[3];
  float curr[3];
  u64 tick = 0;
};

std::unordered_map<u32, EffParticleHistory> g_effHistory;
std::unordered_map<u32, EffEmitterMotion> g_effEmitterMotion;

template <typename F> void ForEachEffEmitter(const EffSystem_t *sys, F &&fn) {
  if (!sys)
    return;
  u32 emitterEA = sys->emitters.GetAddress();
  auto *emitter = sys->emitters.GetHost();
  for (int e = 0; emitter && e < kMaxEffEmitters; ++e) {
    fn(emitterEA, *emitter);
    emitterEA = emitter->next.GetAddress();
    emitter = emitter->next.GetHost();
  }
}

const EffCurve_t *EffUVCurves(const EffEmitter_t &e) {
  const auto *effect = e.effect.GetHost();
  if (!effect)
    return nullptr;
  const u32 defEA = e.def.GetAddress();
  const u32 base = effect->emitterDefs;
  if (defEA < base)
    return nullptr;
  const u32 index = (defEA - base) / kEffEmitterDefSize;
  // index comes from address math, so a def from another effect lands outside
  // this table and only the checked read keeps it from faulting.
  const auto *mask = effect->curveMasks
                         ? bd::mem::try_at<const be_u32>(
                               effect->curveMasks.GetAddress() + index * 4)
                         : nullptr;
  const auto *curves = effect->curves.GetHost();
  if (!mask || !curves || (u32(*mask) & kEffCurveMaskUV) == 0)
    return nullptr;
  return curves + (kEffCurvesPerEmitter * index + kEffCurveUV);
}

bool EvalEffCurve(const EffCurve_t *curve, float t, float &out) {
  if (!curve)
    return false;
  const u32 count = curve->count;
  if (count < 2 || count > kMaxEffCurveKeys)
    return false;
  const auto *keys = curve->keys.GetHost();
  if (!keys)
    return false;
  out = 0.0f;
  for (u32 i = 0; i + 1 < count; ++i) {
    const float t0 = keys[2 * i];
    const float t1 = keys[2 * i + 2];
    if (t < t0 || t > t1 || t1 <= t0)
      continue;
    const float v0 = keys[2 * i + 1];
    const float v1 = keys[2 * i + 3];
    out = v0 + (t - t0) / (t1 - t0) * (v1 - v0);
    return true;
  }
  return true;
}

template <typename F> void ForEachEffParticle(const EffSystem_t *sys, F &&fn) {
  ForEachEffEmitter(sys, [&](u32 emitterEA, EffEmitter_t &emitter) {
    EffEmitterView em;
    em.ea = emitterEA;
    if (auto *def = emitter.def.GetHost())
      em.flags = def->flags;
    em.uvCurves = EffUVCurves(emitter);
    u32 particleEA = emitter.particles.GetAddress();
    auto *particle = emitter.particles.GetHost();
    for (int p = 0; particle && p < kMaxEffParticles; ++p) {
      fn(em, particleEA, *particle);
      particleEA = particle->next.GetAddress();
      particle = particle->next.GetHost();
    }
  });
}

void ReadEffPose(const EffParticle_t &p, EffParticlePose &o) {
  o.age = p.age;
  ReadFloats(p.size, o.size, 3);
  ReadFloats(p.color, o.color, 4);
  ReadFloats(p.uv, o.uv, 2);
  ReadFloats(p.drawPos, o.drawPos, 3);
  ReadFloats(p.drawSize, o.drawSize, 3);
  ReadFloats(p.drawMatrix, o.drawMatrix, 16);
}

struct EffTrailRing {
  u32 base = 0;
  u32 count = 0;
  u32 head = 0;
  u32 size = 0;

  EffTrailRing(const EffParticle_t &p, u32 flags) {
    if ((flags & kEffEmitterTrail) == 0)
      return;
    const u32 ring = p.ring;
    const u32 live = p.ringCount;
    const u32 capacity = p.ringSize;
    if (ring == 0 || capacity == 0 || capacity > kMaxEffTrailPoints ||
        live > capacity)
      return;
    base = ring;
    count = live;
    head = p.ringHead;
    size = capacity;
  }

  explicit operator bool() const { return base != 0; }

  u32 Slot(u32 i) const {
    return base + ((head + i) % size) * kEffRingSlotSize;
  }
};

bool ReadEffTrail(const EffParticle_t &p, u32 flags, std::vector<float> &out) {
  const EffTrailRing r(p, flags);
  if (!r) {
    out.clear();
    return false;
  }
  out.resize(size_t(r.count) * 4);
  for (u32 i = 0; i < r.count; ++i) {
    auto *slot = bd::mem::try_at<const be_f32>(r.Slot(i));
    if (!slot) {
      out.clear();
      return false;
    }
    ReadFloats(slot, &out[size_t(i) * 4], 4);
  }
  return true;
}

void WriteEffTrail(EffParticle_t &p, u32 flags, const std::vector<float> &pts) {
  const EffTrailRing r(p, flags);
  if (!r)
    return;
  const u32 n = std::min(r.count, u32(pts.size() / 4));
  for (u32 i = 0; i < n; ++i) {
    auto *slot = bd::mem::try_at<be_f32>(r.Slot(i));
    if (!slot)
      return;
    WriteFloats(slot, &pts[size_t(i) * 4], 4);
  }
}

void WriteEffPose(EffParticle_t &p, u32 flags, const EffParticlePose &o) {
  WriteFloats(p.size, o.size, (flags & kEffEmitterTrail) != 0 ? 2 : 3);
  WriteFloats(p.color, o.color, 4);
  WriteFloats(p.uv, o.uv, 2);
  WriteFloats(p.drawPos, o.drawPos, 3);
  WriteFloats(p.drawSize, o.drawSize, 3);
  WriteFloats(p.drawMatrix, o.drawMatrix, 16);
}

void RestoreEffParticles(const EffSystem_t *sys) {
  if (g_effHistory.empty())
    return;
  ForEachEffParticle(sys, [](const EffEmitterView &em, u32 ea,
                               EffParticle_t &p) {
    auto it = g_effHistory.find(ea);
    if (it == g_effHistory.end() || !it->second.served)
      return;
    WriteEffPose(p, em.flags, it->second.curr);
    WriteEffTrail(p, em.flags, it->second.currRing);
    it->second.served = false;
  });
}

void ShiftEffPose(EffParticlePose &o, const float delta[3]) {
  for (int k = 0; k < 3; ++k) {
    o.drawPos[k] -= delta[k];
    o.drawMatrix[12 + k] -= delta[k];
  }
}

void ShiftEffTrail(std::vector<float> &pts, const float delta[3]) {
  for (size_t i = 0; i + 4 <= pts.size(); i += 4)
    for (int k = 0; k < 3; ++k)
      pts[i + size_t(k)] -= delta[k];
}

void CaptureEffParticles(const EffSystem_t *sys) {
  const u64 tick = bd::engine::TickCount();
  ForEachEffEmitter(sys, [tick](u32 ea, EffEmitter_t &e) {
    auto [it, inserted] = g_effEmitterMotion.try_emplace(ea);
    EffEmitterMotion &m = it->second;
    float pos[3];
    ReadFloats(e.worldPos, pos, 3);
    if (inserted || m.tick + 1 != tick)
      std::copy_n(pos, 3, m.prev);
    else
      std::copy_n(m.curr, 3, m.prev);
    std::copy_n(pos, 3, m.curr);
    m.tick = tick;
  });
  ForEachEffParticle(sys, [tick](const EffEmitterView &em, u32 ea,
                                   EffParticle_t &p) {
    if ((u32(p.flags) & kEffParticleSnapshotted) == 0)
      return;
    auto [it, inserted] = g_effHistory.try_emplace(ea);
    EffParticleHistory &h = it->second;
    EffParticlePose live;
    ReadEffPose(p, live);
    if (inserted || h.captured != h.spawned) {
      h.prev = live;
      float anchor[3];
      ReadFloats(p.anchor, anchor, 3);
      float delta[3];
      for (int k = 0; k < 3; ++k)
        delta[k] = live.drawPos[k] - anchor[k];
      const auto motion = g_effEmitterMotion.find(em.ea);
      if (motion != g_effEmitterMotion.end())
        for (int k = 0; k < 3; ++k)
          delta[k] += motion->second.curr[k] - motion->second.prev[k];
      ShiftEffPose(h.prev, delta);
      ReadEffTrail(p, em.flags, h.currRing);
      h.prevRing = h.currRing;
      ShiftEffTrail(h.prevRing, delta);
    } else if (h.tick + 1 == tick) {
      h.prev = h.curr;
      h.prevRing.swap(h.currRing);
      ReadEffTrail(p, em.flags, h.currRing);
    } else {
      h.prev = live;
      ReadEffTrail(p, em.flags, h.currRing);
      h.prevRing = h.currRing;
    }
    h.curr = live;
    h.tick = tick;
    h.captured = h.spawned;
    h.served = false;
  });
  static u64 prunedTick = 0;
  if (prunedTick == tick)
    return;
  prunedTick = tick;
  for (auto it = g_effHistory.begin(); it != g_effHistory.end();) {
    if (tick - it->second.tick > kEffHistoryLinger)
      it = g_effHistory.erase(it);
    else
      ++it;
  }
  for (auto it = g_effEmitterMotion.begin();
       it != g_effEmitterMotion.end();) {
    if (tick - it->second.tick > kEffHistoryLinger)
      it = g_effEmitterMotion.erase(it);
    else
      ++it;
  }
}

void BlendEffUV(const EffEmitterView &em, const EffParticle_t &p,
                const EffParticleHistory &h, float alpha, float uv[2]) {
  const float life = p.life;
  if (em.uvCurves && life != 0.0f) {
    const float t =
        (h.prev.age - kEffAgeStep + (h.curr.age - h.prev.age) * alpha) / life;
    for (int i = 0; i < 2; ++i)
      EvalEffCurve(em.uvCurves + i, t, uv[i]);
    return;
  }
  for (int i = 0; i < 2; ++i)
    if (std::fabs(h.curr.uv[i] - h.prev.uv[i]) <= kEffUVWrap)
      uv[i] = h.prev.uv[i] + (h.curr.uv[i] - h.prev.uv[i]) * alpha;
}

void BlendEffParticles(const EffSystem_t *sys) {
  const u64 tick = bd::engine::TickCount();
  const float alpha = CutThisTick() ? 1.0f : bd::engine::Alpha();
  std::vector<float> ring;
  ForEachEffParticle(sys, [&](const EffEmitterView &em, u32 ea,
                                EffParticle_t &p) {
    auto it = g_effHistory.find(ea);
    if (it == g_effHistory.end() || it->second.tick != tick)
      return;
    EffParticleHistory &h = it->second;
    EffParticlePose out = h.curr;
    LerpElements(h.prev.drawPos, h.curr.drawPos, alpha, out.drawPos, 3);
    LerpMatrix(h.prev.drawMatrix, h.curr.drawMatrix, alpha, out.drawMatrix);
    LerpElements(h.prev.size, h.curr.size, alpha, out.size, 3);
    LerpElements(h.prev.drawSize, h.curr.drawSize, alpha, out.drawSize, 3);
    LerpElements(h.prev.color, h.curr.color, alpha, out.color, 4);
    BlendEffUV(em, p, h, alpha, out.uv);
    WriteEffPose(p, em.flags, out);
    if (!h.currRing.empty()) {
      const int points = int(h.currRing.size() / 4);
      const int older = int(h.prevRing.size() / 4);
      const int lead = older - points;
      ring.resize(h.currRing.size());
      for (int i = 0; i < points; ++i) {
        const float *curr = &h.currRing[size_t(i) * 4];
        const int j = i + lead;
        if (j >= 0 && j < older)
          LerpElements(&h.prevRing[size_t(j) * 4], curr, alpha,
                       &ring[size_t(i) * 4], 4);
        else
          std::copy_n(curr, 4, &ring[size_t(i) * 4]);
      }
      WriteEffTrail(p, em.flags, ring);
    }
    h.served = true;
  });
}

} // namespace

REX_MIDASM_HOOK(bdParticleSpawnHook, (PPCRegister &r31)) {
  if (bd::engine::InterpolationActive())
    ++g_effHistory[r31.u32].spawned;
}
REX_MIDASM_AT(bdParticleSpawnHook, 0x82422404, after, r31);

REX_DETOUR(bdParticleSystemSnapshot, void, (EffSystem_t *system)) {
  const bool tick = bd::engine::TickDue();
  if (tick)
    RestoreEffParticles(system);
  original(system);
  if (!bd::engine::InterpolationActive()) {
    g_effHistory.clear();
    g_effEmitterMotion.clear();
    return;
  }
  if (tick)
    CaptureEffParticles(system);
  BlendEffParticles(system);
}

REX_DETOUR(bdEffectStepUpdate, void, (EffTask_t *effect)) {
  RestoreEffParticles(&effect->system);
  original(effect);
}

REX_MIDASM_BRANCH(bdCompassBlinkHoldHook, (PPCRegister &r11)) {
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
REX_MIDASM_AT(bdCompassBlinkHoldHook, 0x8239CBF4, before, 0x8239CBF8, r11);

namespace {

struct FrostPrimCapture {
  u64 tick = ~0ull;
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  u32 color = 0;
  u32 texObjEA = 0;
};
FrostPrimCapture g_frostPrim;

} // namespace

REX_MIDASM_HOOK(bdFaceFrostCaptureHook, (PPCRegister &f1, PPCRegister &f2, PPCRegister &f4, PPCRegister &f5, PPCRegister &r8, PPCRegister &r30)) {
  g_frostPrim.tick = bd::engine::TickCount();
  g_frostPrim.x = f1.f64;
  g_frostPrim.y = f2.f64;
  g_frostPrim.w = f4.f64;
  g_frostPrim.h = f5.f64;
  g_frostPrim.color = r8.u32;
  g_frostPrim.texObjEA = r30.u32;
}
REX_MIDASM_AT(bdFaceFrostCaptureHook, 0x821AAD20, before, f1, f2, f4, f5, r8, r30);

REX_IMPORT(__imp__Visual__method_7E60, ItemDropPushText,
           void(f64, f64, f64, f64, f64, u32, u32, u32, u32));

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

bool CopyWideString(u32 srcVa, u32 dstVa) {
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

REX_MIDASM_BRANCH(bdItemDropTextCaptureHook, (PPCRegister &f1, PPCRegister &f2, PPCRegister &f3, PPCRegister &f4, PPCRegister &f5, PPCRegister &r8, PPCRegister &r9, PPCRegister &r10)) {
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
  if (!EngineScratch(cap.textEA, kItemDropTextChars * sizeof(be_u16)))
    return false;
  if (cap.count == 0 && !CopyWideString(r8.u32, cap.textEA)) {
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
REX_MIDASM_AT(bdItemDropTextCaptureHook, 0x822FC280, before, 0x822FC284, f1, f2, f3, f4, f5, r8, r9, r10);
REX_MIDASM_AT(bdItemDropTextCaptureHook, 0x822FC2AC, before, 0x822FC2B0, f1, f2, f3, f4, f5, r8, r9, r10);

REX_MIDASM_HOOK(bdItemDropTextReplayHook, ()) {
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
                     prev.y + (curr.y - prev.y) * a, curr.z, curr.w, curr.h,
                     cap.textEA, color, curr.mode, 0);
  }
}
REX_MIDASM_AT(bdItemDropTextReplayHook, 0x82134144, before);

REX_DETOUR(FreeDfsTask__Draw, void, (u32 task)) {
  original(task);
  if (!bd::engine::InterpolationActive() || bd::engine::TickDue())
    return;
  if (g_frostPrim.tick != bd::engine::TickCount() || !g_frostPrim.texObjEA)
    return;
  PrimSelectTexture(0, g_frostPrim.texObjEA);
  PrimDrawRect2D(g_frostPrim.x, g_frostPrim.y, 1.0, g_frostPrim.w,
                 g_frostPrim.h, g_frostPrim.color);
}

namespace {

constexpr u32 kLightEntriesEA = 0x82E18694;
constexpr u32 kLightChangedListEA = 0x82E1DFA8;
constexpr u32 kLightChangedCountEA = 0x82E1E458;
constexpr u32 kLightChangedFlag = 0x40;
constexpr u32 kLightMaxEntries = 300;

struct LightEntry_t {
  /* 0x00 */ be_u32 flags;
  /* 0x04 */ u8 _pad004[0x14 - 0x04];
  /* 0x14 */ be_f32 pos[3];
  /* 0x20 */ u8 _pad020[0x2C - 0x20];
  /* 0x2C */ be_f32 color[3];
  /* 0x38 */ u8 _pad038[0x40 - 0x38];
  /* 0x40 */ be_f32 luminance;
  /* 0x44 */ u8 _pad044[0x48 - 0x44];
  /* 0x48 */ be_f32 intensity;
};
static_assert(offsetof(LightEntry_t, pos) == 0x14);
static_assert(offsetof(LightEntry_t, color) == 0x2C);
static_assert(offsetof(LightEntry_t, luminance) == 0x40);
static_assert(offsetof(LightEntry_t, intensity) == 0x48);
static_assert(sizeof(LightEntry_t) == 0x4C);

// The light table, its changed list and the counts of both. The snapshot half
// is the copy bdLightListUpdateSnapshot takes at the start of each frame.
struct LightHalf_t {
  /* 0x0000 */ LightEntry_t entries[kLightMaxEntries];
  /* 0x5910 */ be_u32 count;
  /* 0x5914 */ be_u32 changed[kLightMaxEntries];
  /* 0x5DC4 */ be_u32 changedCount;
};
static_assert(offsetof(LightHalf_t, count) == 0x5910);
static_assert(offsetof(LightHalf_t, changed) == 0x5914);
static_assert(offsetof(LightHalf_t, changedCount) == 0x5DC4);
static_assert(sizeof(LightHalf_t) == 0x5DC8);

// shadowIndex is a thread double buffer: the game thread reads and writes slot
// 0 and the render thread slot 1.
struct LightSystem_t {
  /* 0x0000 */ u8 _pad0000[0x08];
  /* 0x0008 */ LightHalf_t live;
  /* 0x5DD0 */ LightHalf_t snapshot;
  /* 0xBB98 */ u8 _padBB98[0xBB9C - 0xBB98];
  /* 0xBB9C */ be_i32 shadowIndex[2];
};
static_assert(offsetof(LightSystem_t, live) == 0x08);
static_assert(offsetof(LightSystem_t, snapshot) == 0x5DD0);
static_assert(offsetof(LightSystem_t, shadowIndex) == 0xBB9C);

constexpr u32 kLightSystemEA = kLightEntriesEA - offsetof(LightSystem_t, live);
static_assert(kLightSystemEA + offsetof(LightSystem_t, live) +
                  offsetof(LightHalf_t, changed) ==
              kLightChangedListEA);
static_assert(kLightSystemEA + offsetof(LightSystem_t, live) +
                  offsetof(LightHalf_t, changedCount) ==
              kLightChangedCountEA);

REX_GUEST_GLOBAL(be_u32, kLightChangedCount, kLightChangedCountEA);

constexpr u32 kEffLightFloats = 8;

struct EffLightHistory {
  float prev[kEffLightFloats];
  float curr[kEffLightFloats];
  u64 tick = 0;
};

std::unordered_map<u32, EffLightHistory> g_effLights;

bool LightEntryInTable(u32 entry) {
  return entry >= kLightEntriesEA &&
         entry < kLightEntriesEA + kLightMaxEntries * sizeof(LightEntry_t);
}

void ReadEffLight(const LightEntry_t &e, float *out) {
  ReadFloats(e.pos, out, 3);
  ReadFloats(e.color, out + 3, 3);
  out[6] = e.luminance;
  out[7] = e.intensity;
}

void WriteEffLight(LightEntry_t &e, const float *in) {
  WriteFloats(e.pos, in, 3);
  WriteFloats(e.color, in + 3, 3);
  e.luminance = in[6];
  e.intensity = in[7];
}

void LerpEffLights(u32 lightSystem, std::vector<u32> &lerped) {
  if (lightSystem != kLightSystemEA)
    return;
  const u64 tick = bd::engine::TickCount();
  const auto *system = bd::mem::try_at<const LightSystem_t>(lightSystem);
  const i32 shadowIndex = system ? i32(system->shadowIndex[0]) : -1;
  const float alpha = CutThisTick() ? 1.0f : bd::engine::Alpha();
  for (const auto &[entry, h] : g_effLights) {
    if (h.tick != tick ||
        i32((entry - kLightEntriesEA) / sizeof(LightEntry_t)) == shadowIndex)
      continue;
    auto *e = bd::mem::try_at<LightEntry_t>(entry);
    if (!e)
      continue;
    float out[kEffLightFloats];
    LerpElements(h.prev, h.curr, alpha, out, int(kEffLightFloats));
    WriteEffLight(*e, out);
    lerped.push_back(entry);
  }
}

void RestoreEffLights(const std::vector<u32> &lerped) {
  for (const u32 entry : lerped) {
    const auto it = g_effLights.find(entry);
    auto *e = bd::mem::try_at<LightEntry_t>(entry);
    if (e && it != g_effLights.end())
      WriteEffLight(*e, it->second.curr);
  }
}

void PruneEffLights(u64 tick) {
  static u64 prunedTick = 0;
  if (prunedTick == tick)
    return;
  prunedTick = tick;
  std::erase_if(g_effLights, [tick](const auto &kv) {
    return tick - kv.second.tick > kEffHistoryLinger;
  });
}

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
  u32 count = bd::mem::global_load_fast(kLightChangedCount);
  if (count > kLightMaxEntries)
    count = kLightMaxEntries;
  SetChangedFlags(count, false);
  bd::mem::global_store_fast(kLightChangedCount, 0u);
}

void MatrixTrack::DetectCut() {
  float pe[3], ce[3];
  EyeFromView(prev, pe);
  EyeFromView(curr, ce);
  const float step = std::sqrt(DistSq(pe, ce));
  const bool steered = !derived && SteeredThisTick();
  if (steered) {
    gauge.avgStep = BlendStep(gauge.avgStep, step);
    gauge.cutRun = 0;
    cut = false;
    return;
  }
  const bool turned = MinRowDot(curr, prev) < kViewCutRotDot;
  const bool discontinuous = EventSceneEngaged() && step > kEventViewCutStep &&
                             spacing > kEventCutSpacing;
  cut = gauge.Roll(step, spacing, discontinuous, turned);
  if (cut && !derived)
    g_cutTick.store(bd::engine::TickCount(), std::memory_order_relaxed);
}

MatrixTrack &ViewTrack(u32 va) {
  MatrixTrack &e = g_views[va];
  e.lastSeen = g_frame;
  return e;
}

u32 ServeCameraPoints(u32 eyeVa) {
  auto *src = bd::mem::try_at<be_f32>(eyeVa);
  if (!src)
    return 0;
  float live[kCameraPointFloats];
  ReadFloats(src, live, kCameraPointFloats);
  float out[kCameraPointFloats];
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    CameraPointTrack &t = g_cameraPoints[eyeVa];
    t.lastSeen = g_frame;
    switch (t.Advance(live, bd::engine::FrameTime())) {
    case Roll::Shared:
      t.raw = true;
      break;
    case Roll::Rolled:
      t.raw = DistSq(t.prev, t.curr) > kCutDistance * kCutDistance ||
              DistSq(t.prev + 3, t.curr + 3) > kCutDistance * kCutDistance;
      break;
    case Roll::Held:
      break;
    }
    if (t.raw || CutThisTick())
      return 0;
    LerpElements(t.prev, t.curr, t.Alpha(), out, kCameraPointFloats);
  }
  return WriteScratch(g_cameraPointScratch, out, kCameraPointFloats);
}

u32 ServeVec3(u32 key, u32 va, Vec3Slot slot) {
  auto *src = bd::mem::try_at<be_f32>(va);
  if (!src)
    return 0;
  float live[3];
  ReadFloats(src, live, 3);
  float out[3];
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    Vec3Track &t = g_vec3Tracks[key];
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
  if (CutThisTick() || (e.derived && e.cut))
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

void ServeGlobalView(be_f32 *global, u32 va) {
  float liveView[16];
  ReadFloats(global, liveView, 16);
  float view[16];
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    GlobalViewTrack &e = g_globalViews[(u64(t_renderViewObj) << 32) | va];
    e.lastSeen = g_frame;
    e.derived = true;
    if (e.hasWritten && std::equal(liveView, liveView + 16, e.written))
      return;
    if (ServeView(e, liveView, bd::engine::FrameTime(), view))
      return;
    std::copy_n(view, 16, e.written);
    e.hasWritten = true;
  }
  WriteFloats(global, view, 16);
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

bool MainCameraMatrices(float view[16], float proj[16]) {
  const auto *v = bd::mem::try_at<const bd::engine::CameraView_t>(
      bd::mem::global_load(bd::engine::addr::kCameraViewList));
  while (v && u32(v->id) != 0)
    v = bd::mem::try_at<const bd::engine::CameraView_t>(u32(v->next));
  if (!v)
    return false;
  ReadFloats(v->view, view, 16);
  ReadFloats(v->proj, proj, 16);
  return true;
}

void ProjectBlended(u32 &anchor, bool radius) {
  if (!bd::engine::InterpolationActive())
    return;
  const u32 anchorVa = anchor;
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
    anchor = scratch;
}

} // namespace

REX_DETOUR(bdEffectLightStep, void, (u32 record, f32 step)) {
  original(record, step);
  if (!bd::engine::InterpolationActive())
    return;
  const u32 entry = bd::mem::try_load<u32>(record);
  if (!LightEntryInTable(entry))
    return;
  auto *e = bd::mem::try_at<LightEntry_t>(entry);
  if (!e)
    return;
  const u64 tick = bd::engine::TickCount();
  auto [it, inserted] = g_effLights.try_emplace(entry);
  EffLightHistory &h = it->second;
  float live[kEffLightFloats];
  ReadEffLight(*e, live);
  if (inserted)
    std::copy_n(live, kEffLightFloats, h.prev);
  else if (h.tick != tick)
    std::copy_n(h.curr, kEffLightFloats, h.prev);
  std::copy_n(live, kEffLightFloats, h.curr);
  h.tick = tick;
}

REX_DETOUR(bdLightListUpdateSnapshot, void, (u32 list)) {
  const bool interp = bd::engine::InterpolationActive();
  u32 held = interp ? bd::mem::global_load_fast(kLightChangedCount) : 0;
  if (held > kLightMaxEntries)
    held = 0;
  std::vector<u32> lerped;
  if (interp) {
    LerpEffLights(list, lerped);
    PruneEffLights(bd::engine::TickCount());
  }
  original(list);
  RestoreEffLights(lerped);
  if (held == 0)
    return;
  SetChangedFlags(held, true);
  bd::mem::global_store_fast(kLightChangedCount, held);
}

namespace bd::engine {

void OnGameStep() {
  UpdateEventEngagement();
  Advance();
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    REX_PROFILE_SCOPE("interp frame prune");
    REX_PROFILE_PLOT("obj snapshots", g_objSnapshots.size());
    REX_PROFILE_PLOT("bone arrays", g_boneArrays.size());
    ++g_frame;
    PruneStale(g_objSnapshots);
    PruneStale(g_views);
    PruneStale(g_globalViews);
    PruneStale(g_vec3Tracks);
    PruneStale(g_cameraPoints);
    PruneStale(g_animeClocks);
    PruneStale(g_tickCounters);
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

void MarkCameraSteered() {
  g_steerTick.store(TickCount(), std::memory_order_relaxed);
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

REX_DETOUR(bdSceneNodeProcessRenderCmds, void,
           (u32 arg0, u32 node, u32 arg2, u32 arg3, u32 arg4, u32 arg5)) {
  REX_PROFILE_SCOPE("bdSceneNodeProcessRenderCmds");
  NodeDrawScope scope(node);
  original(arg0, node, arg2, arg3, arg4, arg5);
}

REX_DETOUR(bdSceneNodeDrawSingle, void,
           (u32 arg0, u32 node, u32 arg2, u32 arg3)) {
  REX_PROFILE_SCOPE("bdSceneNodeDrawSingle");
  NodeDrawScope scope(node);
  original(arg0, node, arg2, arg3);
}

REX_DETOUR(bdBuildMirrorViewProjection, void,
           (u32 out, u32 points, u32 target)) {
  if (bd::engine::InterpolationActive() &&
      target == points + kViewObjTargetGap) {
    if (const u32 served = ServeCameraPoints(points)) {
      points = served;
      target = served + kViewObjTargetGap;
    }
  }
  original(out, points, target);
}

namespace {

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
  const u32 scratch = ServeCameraPoints(eyeVa);
  if (!scratch)
    return;
  float eye[3];
  ReadFloats(bd::mem::at<be_f32>(scratch), eye, 3);
  WriteFloats(staged, eye, 3);
}

} // namespace

REX_DETOUR(bdRenderViewSubmit, void, (u32 view)) {
  t_renderViewObj = bd::mem::try_load<u32>(view + kRenderViewObj);
  original(view);
  t_renderViewObj = 0;
}

REX_MIDASM_HOOK(bdDofFocusLerpHook, (PPCRegister &r11)) {
  if (!bd::engine::InterpolationActive())
    return;
  const u32 slot = r11.u32;
  const u32 lerped = ServeVec3(t_renderViewObj ? t_renderViewObj : slot, slot,
                               Vec3Slot::DofFocus);
  if (!lerped)
    return;
  float focus[3];
  ReadFloats(bd::mem::at<be_f32>(lerped), focus, 3);
  WriteFloats(bd::mem::at<be_f32>(slot), focus, 3);
}
REX_MIDASM_AT(bdDofFocusLerpHook, 0x821865B8, before, r11);

REX_DETOUR(Visual__RenderInfo__vf04, void, (u32 info, i32 flag)) {
  ServeShaderEye();
  original(info, flag);
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

void ServeWorldMatrix(u32 &world) {
  REX_PROFILE_SCOPE("ServeWorldMatrix");
  const u32 va = world;
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
                      e->subTickWriter;
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
        world = scratch;
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

void ServeCameraMatrices(u32 &viewPtr, u32 &projPtr) {
  REX_PROFILE_SCOPE("ServeCameraMatrices");
  const u32 viewVa = viewPtr;
  auto *live = bd::mem::try_at<be_f32>(viewVa);
  if (!live)
    return;
  if (ReflectSlotView(viewVa))
    return;
  if (GlobalLightView(viewVa)) {
    ServeGlobalView(live, viewVa);
    return;
  }
  if (ProjectorView(viewVa))
    return;
  const double now = bd::engine::FrameTime();
  if (auto *liveProj =
          projPtr ? bd::mem::try_at<be_f32>(projPtr) : nullptr) {
    float liveP[16];
    ReadFloats(liveProj, liveP, 16);
    float proj[16];
    bool shared = false;
    {
      std::lock_guard<std::mutex> lock(g_interpMutex);
      shared = ServeProj(ViewTrack(projPtr), liveP, now, proj);
    }
    if (!shared)
      if (const u32 scratch = WriteScratch(g_projScratch, proj, 16))
        projPtr = scratch;
  }
  float liveView[16];
  ReadFloats(live, liveView, 16);
  float view[16];
  bool shared = false;
  float alpha = 1.0f;
  {
    std::lock_guard<std::mutex> lock(g_interpMutex);
    MatrixTrack &track = ViewTrack(viewVa);
    shared = ServeView(track, liveView, now, view);
    alpha = track.Alpha();
  }
  if (shared)
    return;
  const bool cut = CutThisTick();
  FrameLook &look = g_frameLook;
  if (look.time != now || look.view != viewVa) {
    look.time = now;
    look.view = viewVa;
    look.turned = !cut && bd::engine::PendingMouseLook(bd::engine::TickCount(),
                                                       alpha, look.pending);
  }
  if (look.turned)
    TurnView(view, look.pending);
  if (const u32 scratch = WriteScratch(g_viewScratch, view, 16))
    viewPtr = scratch;
}

} // namespace

REX_DETOUR(bdBuildViewMatrix, void, (u32 world, u32 view, u32 proj)) {
  REX_PROFILE_SCOPE("bdBuildViewMatrix");
  if (bd::engine::InterpolationActive()) {
    if (world && !view && !proj)
      ServeWorldMatrix(world);
    else if (view)
      ServeCameraMatrices(view, proj);
  }
  original(world, view, proj);
}

REX_DETOUR(bdWorldToScreenPos3, void,
           (u32 arg0, u32 arg1, u32 out, u32 in)) {
  ProjectBlended(in, false);
  original(arg0, arg1, out, in);
}

REX_DETOUR(bdWorldToScreenPos4, void,
           (u32 arg0, u32 arg1, u32 out, u32 in)) {
  ProjectBlended(in, true);
  original(arg0, arg1, out, in);
}

REX_DETOUR(PlyTask__Draw, void, (u32 task)) {
  bd::engine::PlyTask ply(bd::engine::InterpolationActive() ? task : 0);
  if (auto chara = ply.Chara()) {
    const u32 arm = ply.BlinkArm();
    if (arm == 1) {
      ply.SetBlinkArm(0u);
      g_plyBlinkWindow[task].tick = bd::engine::TickCount();
    }
    if (arm <= 1) {
      auto it = g_plyBlinkWindow.find(task);
      if (it != g_plyBlinkWindow.end() && !it->second.held) {
        const f32 cur = chara.Alpha();
        if (cur > kPlyBlinkAlpha) {
          it->second.alpha = cur;
          it->second.dirty = chara.AlphaDirty();
          it->second.held = true;
          chara.SetAlpha(kPlyBlinkAlpha);
          chara.SetAlphaDirty(1u);
        }
      }
    }
  }
  original(task);
}

namespace {

constexpr f32 kAnimeFirstFrame = 1.0f;

f32 LerpedAnimeFrame(u32 taskEA, const bd::engine::AnimeData &data) {
  const f32 live = data.Frame();
  std::lock_guard<std::mutex> lock(g_interpMutex);
  AnimeClock &c = g_animeClocks[taskEA];
  c.lastSeen = g_frame;
  const double now = bd::engine::FrameTime();
  if (!c.valid) {
    c.prev = c.curr = live;
    c.valid = true;
    c.lastChange = now;
    c.rewriteTick = bd::engine::TickCount();
  } else if (c.curr != live) {
    const bool cut = RewrittenThisTick(c.rewriteTick) ||
                     AnimeClockDiscontinuous(live - c.curr, data.Speed());
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

REX_DETOUR(D2AnimeTask_Draw, void, (u32 task)) {
  bd::engine::AnimeData data =
      bd::engine::InterpolationActive()
          ? bd::engine::D2AnimeTask(task).AnimeData()
          : bd::engine::AnimeData();
  const f32 live = data ? data.Frame() : 0.0f;
  const f32 lerped = data ? LerpedAnimeFrame(task, data) : 0.0f;
  if (!data || lerped == live) {
    original(task);
    return;
  }
  data.SetFrame(lerped);
  data.ApplyVarTracks(lerped);
  original(task);
  data.SetFrame(live);
}

REX_MIDASM_HOOK(bdAnimeChildClockFloorHook, (PPCRegister &time)) {
  if (time.f64 < kAnimeFirstFrame)
    time.f64 = kAnimeFirstFrame;
}
REX_MIDASM_AT(bdAnimeChildClockFloorHook, 0x821594FC, before, f1);

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

REX_DETOUR(ScriptWindow__Draw, void, (ScriptWindow_t *window)) {
  ScriptWindow_t *wnd = bd::engine::InterpolationActive() ? window : nullptr;
  const u32 state = wnd ? u32(wnd->state) : 0;
  if (state != kScriptWndOpening && state != kScriptWndClosing) {
    original(window);
    return;
  }
  const float held = wnd->fade;
  const float step = kScriptWndFadeStep * bd::engine::Alpha();
  wnd->fade = state == kScriptWndOpening ? std::min(held + step, 1.0f)
                                         : std::max(held - step, 0.0f);
  original(window);
  wnd->fade = held;
}

REX_MIDASM_RETURN_IF(bdInputSystemUpdateSkip, ()) {
  if (!bd::engine::TickDue())
    return !bd::engine::CutscenePause::Get().Frozen();
  bd::engine::MenuMouse::Get().BeginFrame();
  bd::engine::UpdateMouseLook();
  bd::engine::MouseCursorTick();
  bd::engine::Glyphs::Get().Tick();
  bd::engine::D2AnimeTask::Tick();
  bd::engine::CampSettings::Get().Tick();
  bd::engine::AreaMapTick();
  return false;
}
REX_MIDASM_AT(bdInputSystemUpdateSkip, 0x82131D90, before);

REX_MIDASM_RETURN_IF(PadVibrationCore__DrawSkip, (u32 core)) {
  if (!bd::engine::TickDue())
    return true;
  if (!bd::engine::IsVibrationEnabled()) {
    if (auto *amp = bd::mem::at<be_f32>(core + 0x6C)) {
      amp[0] = 0.0f;
      amp[1] = 0.0f;
    }
  }
  return false;
}
REX_MIDASM_AT(PadVibrationCore__DrawSkip, 0x82190D48, before, r3);

REX_MIDASM_HOOK(bdAlphaPrimCaptureHook, (PPCRegister &r3)) {
  if (g_nodeScope == 0 || r3.u32 == 0)
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  g_recordKeys[r3.u32] = g_nodeScope | (u64(++g_recordSeq) << 48);
}
REX_MIDASM_AT(bdAlphaPrimCaptureHook, 0x8227DBB8, before, r3);

REX_MIDASM_HOOK(bdListObjectBeginHook, (PPCRegister &r3)) {
  g_listObject = r3.u32;
  g_listSeq = 0;
}
REX_MIDASM_AT(bdListObjectBeginHook, 0x821843E4, before, r3);

REX_MIDASM_HOOK(bdListObjectEndHook, ()) { g_listObject = 0; }
REX_MIDASM_AT(bdListObjectEndHook, 0x821843E8, before);

REX_IMPORT(__imp__bdDoubleBufferAcquire, guest_bdDoubleBufferAcquire,
           u32(u32));
REX_DETOUR(bdVisualObjectCopyShadowBones, void, (u32 visualObject)) {
  const u32 vo = visualObject;
  original(visualObject);
  if (!bd::engine::InterpolationActive() || !vo)
    return;
  const u32 count = BoneCount(vo);
  if (count == 0 || count > kMaxBoneMatrices)
    return;
  const u32 currEA = guest_bdDoubleBufferAcquire(BoneHolder(vo));
  if (!bd::mem::try_at<be_f32>(currEA))
    return;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  BoneArray &e = g_boneArrays[BoneHolder(vo)];
  e.count = count;
  MapBoneRange(e.currEA, currEA, count);
  e.copyTime = bd::engine::FrameTime();
}

REX_DETOUR(bdDrawRecordListReplay, void, ()) {
  REX_PROFILE_SCOPE("bdDrawRecordListReplay");
  t_inRecordReplay = true;
  original();
  t_inRecordReplay = false;
}

REX_DETOUR(bdCameraRender, void, (u32 visualObject)) {
  const bool outer = !t_inCameraRender;
  t_inCameraRender = true;
  t_cameraPass = NextCameraPass();
  RegisterBoneArray(visualObject);
  original(visualObject);
  if (outer)
    t_inCameraRender = false;
  t_cameraPass = outer ? 0 : NextCameraPass();
}

REX_DETOUR(bdFrameSubmitAndDebugHUD, void, (u32 arg0)) {
  g_swapTickDue = g_drawTickDue;
  g_drawTickDue = bd::engine::TickDue();
  bd::engine::PublishRenderClock();
  original(arg0);
}

REX_DETOUR(bdRenderStep, void, (u32 arg0)) {
  bd::engine::BindRenderThread();
  original(arg0);
}

REX_DETOUR(bdVisualObjectInitBones, void, (u32 visualObject)) {
  t_boneWriter = true;
  original(visualObject);
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
      e.blendsStale = true;
      return 0;
    }
    // A pass draws an object's nodes back to back, so one check per pass
    // still catches a writer between passes.
    if (t_cameraPass != 0 && e.verifiedPass == t_cameraPass)
      return e.blended;
    auto *live = bd::mem::try_at<const u32>(e.currEA);
    if (live && e.lastLive.size() == size_t(e.count) * 16 &&
        std::memcmp(live, e.lastLive.data(),
                    e.lastLive.size() * sizeof(u32)) != 0)
      return 0;
    e.verifiedPass = t_cameraPass;
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
    UnmapBoneRange(e.scratch);
  }
  if (e.scratch == 0) {
    MapBoneRange(e.scratch,
                 bd::gpu::HostHeap::Get().AllocGuest(u32(floats) * 4, 16),
                 count);
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
    e.blendsStale = true;
  } else if (e.blendTick != tick) {
    e.prevPose = e.tickTarget;
    ReadFloats(cur, e.tickTarget.data(), floats);
    e.blendTick = tick;
    e.blendsStale = true;
  }
  const auto *raw = reinterpret_cast<const u32 *>(cur);
  e.lastLive.assign(raw, raw + floats);
  if (CutThisTick()) {
    e.prevPose = e.tickTarget;
    e.blendsStale = true;
    return 0;
  }
  if (e.blendsStale || e.blends.size() != count) {
    e.blends.resize(count);
    for (u32 b = 0; b < count; ++b) {
      const float *prvM = &e.prevPose[size_t(b) * 16];
      const float *tgtM = &e.tickTarget[size_t(b) * 16];
      BoneBlend &blend = e.blends[b];
      blend.snap = ClassifyBone(tgtM, prvM) == BoneStep::Snap;
      blend.matrix =
          blend.snap ? MatrixBlend{} : PrepareMatrixBlend(prvM, tgtM);
    }
    e.blendsStale = false;
  }
  const float a = bd::engine::Alpha();
  float outM[16];
  for (int i = 0; i < floats; i += 16) {
    const float *prvM = &e.prevPose[size_t(i)];
    const float *tgtM = &e.tickTarget[size_t(i)];
    const BoneBlend &blend = e.blends[size_t(i / 16)];
    if (blend.snap)
      std::copy_n(tgtM, 16, outM);
    else
      ApplyMatrixBlend(blend.matrix, prvM, tgtM, a, outM);
    WriteFloats(dst + i, outM, 16);
  }
  e.blended = e.scratch;
  e.verifiedPass = t_cameraPass;
  return e.scratch;
}

} // namespace

REX_DETOUR(bdDoubleBufferAcquire, u32, (u32 holder)) {
  const u32 result = original(holder);
  if (!bd::engine::InterpolationActive())
    return result;
  std::lock_guard<std::mutex> lock(g_interpMutex);
  auto it = g_boneArrays.find(holder);
  if (it == g_boneArrays.end())
    return result;
  BoneArray &e = it->second;
  if (e.currEA != result)
    MapBoneRange(e.currEA, result, e.count);
  if (t_boneWriter || (!t_inCameraRender && !bd::engine::IsRenderThread()) ||
      ParticleModelPoolVO(holder - BoneHolder(0)))
    return result;
  if (const u32 served = ServeBones(e, bd::engine::FrameTime()))
    return served;
  return result;
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
