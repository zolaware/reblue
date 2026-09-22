/**
 * @file    engine/field_camera.cpp
 * @brief   Raw mouse look on the field camera.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <mutex>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/types.h>

#include "engine/config.h"
#include "engine/field_camera.h"
#include "engine/frame_clock.h"
#include "engine/frame_interp.h"
#include "engine/game.h"
#include "engine/input/action_state.h"
#include "engine/iss_event.h"
#include "engine/object.h"
#include "engine/script.h"
#include "engine/script_man_task.h"
#include "engine/settings.h"
#include "platform/platform.h"

REX_EXTERN(__imp__bdFieldCameraStickUpdate);

namespace bd::engine {
namespace {

struct GameCamera_t {
  u8 _pad000[0x130];
  be_f32 yawMode;
  u8 _pad134[0x14C - 0x134];
  be_f32 pitchLimitPos;
  be_f32 pitchLimitNeg;
  u8 _pad154[0x190 - 0x154];
  be_u32 realignTick;
  u8 _pad194[0x1AC - 0x194];
  be_u32 scriptedMoveFlag;
  u8 _pad1B0[0x1EC - 0x1B0];
  be_u32 stickDisabled;
  u8 _pad1F0[0x218 - 0x1F0];
  be_f32 eye[3];
  u8 _pad224[0x254 - 0x224];
  be_f32 pitch;
  be_f32 yaw;
  u8 _pad25C[0x26C - 0x25C];
  be_f32 pitchSpeed;
  be_f32 yawSpeed;
  u8 _pad274[0x284 - 0x274];
  be_u32 stickActivity;
  u8 _pad288[0x294 - 0x288];
  be_f32 wallYaw;
  be_f32 wallYawTarget;
  be_u32 steerLinger;
  be_u32 steering;
  be_f32 lookTarget[3];
  be_u32 scriptedMoveCancelable;
  u8 _pad2B4[0x424 - 0x2B4];
  be_u32 scriptedMove;
  be_u32 locked;
};
static_assert(offsetof(GameCamera_t, yawMode) == 0x130);
static_assert(offsetof(GameCamera_t, pitchLimitPos) == 0x14C);
static_assert(offsetof(GameCamera_t, pitchLimitNeg) == 0x150);
static_assert(offsetof(GameCamera_t, realignTick) == 0x190);
static_assert(offsetof(GameCamera_t, scriptedMoveFlag) == 0x1AC);
static_assert(offsetof(GameCamera_t, stickDisabled) == 0x1EC);
static_assert(offsetof(GameCamera_t, eye) == 0x218);
static_assert(offsetof(GameCamera_t, pitch) == 0x254);
static_assert(offsetof(GameCamera_t, yaw) == 0x258);
static_assert(offsetof(GameCamera_t, pitchSpeed) == 0x26C);
static_assert(offsetof(GameCamera_t, yawSpeed) == 0x270);
static_assert(offsetof(GameCamera_t, stickActivity) == 0x284);
static_assert(offsetof(GameCamera_t, wallYaw) == 0x294);
static_assert(offsetof(GameCamera_t, wallYawTarget) == 0x298);
static_assert(offsetof(GameCamera_t, steerLinger) == 0x29C);
static_assert(offsetof(GameCamera_t, steering) == 0x2A0);
static_assert(offsetof(GameCamera_t, lookTarget) == 0x2A4);
static_assert(offsetof(GameCamera_t, scriptedMoveCancelable) == 0x2B0);
static_assert(offsetof(GameCamera_t, scriptedMove) == 0x424);
static_assert(offsetof(GameCamera_t, locked) == 0x428);

constexpr u32 kSteerLingerTicks = 4;
constexpr u32 kStickActivityMax = 15;

constexpr f32 kRadiansPerCount = 0.0004f;

constexpr auto kStaleLook = std::chrono::milliseconds(250);

f32 RadiansPerCount() {
  const f64 sensitivity = REXCVAR_QUERY(f64, mnk_sensitivity);
  return sensitivity > 0.0 ? kRadiansPerCount * static_cast<f32>(sensitivity)
                           : kRadiansPerCount;
}

bool ScriptBusy() {
  const engine::ScriptManTask man = Game::Get().ScriptManTask();
  if (!man)
    return false;
  const engine::Script script = man.Script();
  return script && script.Busy();
}

struct LookTurn {
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
  f32 yawPerCount = 0.0f;
  f32 pitchPerCount = 0.0f;
  f32 pitchTarget = 0.0f;
  f32 pitchLow = 0.0f;
  f32 pitchHigh = 0.0f;
  f32 pivot[3] = {};
};

struct TurnRecord {
  u64 tick = 0;
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
};
constexpr int kTurnHistory = 8;

struct SharedLook {
  std::mutex mutex;
  bool live = false;
  u64 tick = 0;
  LookTurn latest;
  TurnRecord history[kTurnHistory];
  int next = 0;
};
SharedLook g_shared;

void PublishLook(bool live, const LookTurn &turn) {
  const u64 tick = TickCount();
  std::lock_guard lock(g_shared.mutex);
  g_shared.live = live;
  if (!live)
    return;
  g_shared.latest = turn;
  const int last = (g_shared.next + kTurnHistory - 1) % kTurnHistory;
  TurnRecord &prev = g_shared.history[last];
  if (prev.tick == tick) {
    prev.yaw += turn.yaw;
    prev.pitch += turn.pitch;
  } else {
    g_shared.history[g_shared.next] = {tick, turn.yaw, turn.pitch};
    g_shared.next = (g_shared.next + 1) % kTurnHistory;
  }
  g_shared.tick = tick;
}

void TurnEye(GameCamera_t &cam, f32 yawTurn, f32 pitchTurn) {
  constexpr f32 kMaxElevation = 1.55f;
  f32 pivot[3];
  f32 off[3];
  for (int i = 0; i < 3; ++i) {
    pivot[i] = cam.lookTarget[i];
    off[i] = static_cast<f32>(cam.eye[i]) - pivot[i];
  }
  const f32 flat = std::sqrt(off[0] * off[0] + off[2] * off[2]);
  const f32 dist = std::sqrt(flat * flat + off[1] * off[1]);
  if (!(flat > 1e-3f))
    return;
  const f32 c = std::cos(yawTurn);
  const f32 sn = std::sin(yawTurn);
  const f32 dirX = (off[0] * c + off[2] * sn) / flat;
  const f32 dirZ = (off[2] * c - off[0] * sn) / flat;
  const f32 elevation =
      std::clamp(std::atan2(off[1], flat) + pitchTurn, -kMaxElevation,
                 kMaxElevation);
  const f32 newFlat = dist * std::cos(elevation);
  cam.eye[0] = pivot[0] + dirX * newFlat;
  cam.eye[1] = pivot[1] + dist * std::sin(elevation);
  cam.eye[2] = pivot[2] + dirZ * newFlat;
}

class FieldCamera : public Object {
public:
  explicit FieldCamera(u32 address) : Object(address) {}

  u32 StickActivity() const {
    const auto *self = Self<GameCamera_t>();
    return self ? static_cast<u32>(self->stickActivity) : 0;
  }

  bool Look(f32 dx, f32 dy, u32 activityBefore, bool pitchHeld,
            LookTurn &turn) const {
    auto *self = Self<GameCamera_t>();
    if (!self || static_cast<u32>(self->locked) != 0 ||
        static_cast<u32>(self->stickDisabled) != 0)
      return false;

    const bool moved = dx != 0.0f || dy != 0.0f;
    if (moved) {
      if (static_cast<u32>(self->scriptedMoveCancelable) != 0) {
        self->scriptedMoveCancelable = 0u;
        self->scriptedMove = 0u;
        self->scriptedMoveFlag = 0u;
      }
      self->stickActivity = std::min(activityBefore + 1, kStickActivityMax);
    }
    if (static_cast<u32>(self->scriptedMove) != 0)
      return false;

    const f32 gain = RadiansPerCount();
    const f32 invert = Config::Get().CamRollInv() ? -1.0f : 1.0f;
    turn.yawPerCount = -gain * invert;
    turn.pitchPerCount = gain * invert;

    const f32 pitch = self->pitch;
    turn.pitchLow = std::min(-static_cast<f32>(self->pitchLimitNeg), pitch);
    turn.pitchHigh = std::max(static_cast<f32>(self->pitchLimitPos), pitch);
    if (dy != 0.0f || pitchHeld) {
      const f32 speed =
          pitchHeld ? 0.0f : static_cast<f32>(self->pitchSpeed);
      const f32 target = std::clamp(pitch + speed + dy * turn.pitchPerCount,
                                    turn.pitchLow, turn.pitchHigh);
      self->pitchSpeed = target - pitch;
      turn.pitch = target - pitch - speed;
    }
    turn.pitchTarget = pitch + static_cast<f32>(self->pitchSpeed);
    if (dx != 0.0f) {
      turn.yaw = dx * turn.yawPerCount;
      self->yawSpeed = static_cast<f32>(self->yawSpeed) + turn.yaw;
    }

    if (moved) {
      TurnEye(*self, turn.yaw, turn.pitch);
      self->steerLinger = kSteerLingerTicks;
      self->steering = 1u;
      MarkCameraSteered();
    }
    for (int i = 0; i < 3; ++i)
      turn.pivot[i] = self->lookTarget[i];
    return true;
  }
};

bool g_pitchHeld = false;
std::chrono::steady_clock::time_point g_lastLook;

void MouseLook(u32 camera, u32 activityBefore) {
  const auto now = std::chrono::steady_clock::now();
  const bool stale = now - g_lastLook > kStaleLook;
  g_lastLook = now;

  f32 dx = 0.0f;
  f32 dy = 0.0f;
  if (!platform::Mouse().TakeLookDelta(dx, dy) || stale) {
    dx = 0.0f;
    dy = 0.0f;
  }

  const bool enabled = Settings::Get().MouseInput() && !ScriptBusy() &&
                       !IssEvent::AnyPlaying();
  const InputActions &actions = InputActions::Get();
  if (!enabled || actions.Axis(AxisPair::Right, 1) != 0.0f ||
      actions.Pressed(Action::ResetCamera))
    g_pitchHeld = false;
  if (!enabled) {
    PublishLook(false, {});
    return;
  }

  const FieldCamera cam(camera);
  LookTurn turn;
  PublishLook(cam.Look(dx, dy, activityBefore, g_pitchHeld, turn), turn);
  if (dy != 0.0f)
    g_pitchHeld = true;
}

} // namespace

bool PendingMouseLook(u64 tick, f32 alpha, PendingLook &out) {
  f32 dx = 0.0f;
  f32 dy = 0.0f;
  platform::Mouse().PeekLookDelta(dx, dy);
  std::lock_guard lock(g_shared.mutex);
  if (!g_shared.live || g_shared.tick + 1 < tick)
    return false;
  const LookTurn &latest = g_shared.latest;
  out.yaw = dx * latest.yawPerCount;
  out.pitch = std::clamp(latest.pitchTarget + dy * latest.pitchPerCount,
                         latest.pitchLow, latest.pitchHigh) -
              latest.pitchTarget;
  for (const TurnRecord &r : g_shared.history) {
    if (r.tick < tick)
      continue;
    const f32 unshown = r.tick == tick ? 1.0f - alpha : 1.0f;
    out.yaw += r.yaw * unshown;
    out.pitch += r.pitch * unshown;
  }
  for (int i = 0; i < 3; ++i)
    out.pivot[i] = latest.pivot[i];
  return out.yaw != 0.0f || out.pitch != 0.0f;
}

} // namespace bd::engine

REX_HOOK_RAW(bdFieldCameraStickUpdate) {
  const u32 camera = ctx.r3.u32;
  const u32 activityBefore =
      bd::engine::FieldCamera(camera).StickActivity();
  __imp__bdFieldCameraStickUpdate(ctx, base);
  bd::engine::MouseLook(camera, activityBefore);
}
