/**
 * @file    engine/battle_target.cpp
 * @brief   Point at the enemy you mean during the battle's target step.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <algorithm>
#include <cmath>
#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/battle_task.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/visual_render.h"
#include "reblue_init.h"

REX_IMPORT(__imp__bdBattleTargetIsSelectable, TargetIsSelectable,
           u32(u32, u32, u32));
REX_IMPORT(__imp__bdBattleTargetRefreshHighlights, TargetRefreshHighlights,
           u32(u32, u32));
REX_IMPORT(__imp__bdWorldToScreenPos3, WorldToScreenPos3,
           void(u32, u32, u32, u32, u32));
REX_EXTERN(__imp__bdBattleTargetSelectInput);

namespace bd::engine {

namespace {

constexpr i32 kPartBody = -1;

constexpr f32 kBodyRise = 60.0f;
constexpr f32 kPickRadius = 140.0f;
constexpr f32 kFrontOfCamera = 1.0f;

struct Vec3_t {
  be_f32 x;
  be_f32 y;
  be_f32 z;
};
static_assert(sizeof(Vec3_t) == 0x0C);

struct BattleActor_t {
  /* 0x000 */ u8 _pad000[0x128];
  /* 0x128 */ mem::GuestVec<mem::GuestPtr<BattleActor_t>> parts;
  /* 0x134 */ u8 _pad134[0x1B8 - 0x134];
  /* 0x1B8 */ Vec3_t worldPos;
};
static_assert(offsetof(BattleActor_t, parts) == 0x128);
static_assert(offsetof(BattleActor_t, worldPos) == 0x1B8);

// The engine's selectable test is handed a patched copy of the live block, so
// this is a byte copy and four field writes rather than a read.
struct BattleSelection_t {
  /* 0x00 */ be_u32 actor;
  /* 0x04 */ be_u32 side;
  /* 0x08 */ be_u32 commandSlot;
  /* 0x0C */ be_u32 group;
  /* 0x10 */ be_u32 member;
  /* 0x14 */ be_i32 part;
  /* 0x18 */ be_u32 flags;
};
static_assert(sizeof(BattleSelection_t) == 0x1C);

constexpr u32 kScratchBytes = 0x60;
constexpr u32 kScratch_Sel = 0x20;
constexpr u32 kScratch_Screen = 0x40;
constexpr u32 kScratch_World = 0x50;

struct Candidate {
  u32 actor = 0;
  u32 group = 0;
  u32 member = 0;
  i32 part = kPartBody;
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
  f32 distance = 0.0f;
};

// True while the pad, rather than the pointer, is moving the target: the same
// four buttons bdBattleTargetSelectInput takes first out of its own poll.
bool PadMovedTarget() {
  return CheckButton(Button::Up) || CheckButton(Button::Down) ||
         CheckButton(Button::Left) || CheckButton(Button::Right);
}

// Runs ahead of the engine's own target input, so a click in the same frame
// confirms whoever the pointer had just moved to.
void UpdateBattleTargetHover(PPCContext &ctx, u8 *base, u32 taskVA) {
  // Reaching this function at all means the player is being asked who to hit,
  // and a cancel from here has somewhere to go, so escape and right-click have
  // to read as one and the arrow keys have to reach the pad. Ahead of every
  // gate below, which are about the pointer rather than about the keyboard, and
  // ahead of the region test, since an action that covers the whole field still
  // takes a confirm and a cancel.
  MenuMouse::Get().MarkInputOwned();

  const BattleTask task(taskVA);
  const auto *selection =
      mem::try_at<const BattleSelection_t>(task.SelectionAddress());
  if (!selection)
    return;
  const BattleShape shape = task.TargetShape();
  if (!shape || !shape.Enabled())
    return;
  const u32 mode = shape.Mode();
  if (mode == 0 || mode == kShapeEverything)
    return;

  if (PadMovedTarget()) {
    MenuMouse::Get().SetMouseHasCursor(false);
    return;
  }
  if (!MenuMouse::Get().PointerActive())
    return;

  f32 pointerX = 0.0f;
  f32 pointerY = 0.0f;
  if (!CursorInMenuSpace(pointerX, pointerY))
    return;

  const u32 slot = selection->commandSlot;
  if (slot >= task.CommandCount())
    return;
  const BattleCommand command = task.CommandAt(slot);
  if (!command || command.GroupCount() == 0)
    return;

  const u32 currentActor = task.CurrentActor().Address();
  const u32 visualRender = VisualRender::Get().Address();
  if (!currentActor || !visualRender)
    return;

  // Every engine call below runs on a frame of its own, so the register state
  // the original is about to read is left exactly as it arrived.
  rex::CallFrame frame(ctx);
  rex::ppc::stack_guard guard(frame.ctx);
  alignas(8) u8 zeroed[kScratchBytes]{};
  const u32 scratch =
      rex::ppc::stack_push(frame.ctx, base, zeroed, kScratchBytes);

  auto *scratchSel = mem::try_at<BattleSelection_t>(scratch + kScratch_Sel);
  auto *world = mem::try_at<Vec3_t>(scratch + kScratch_World);
  const auto *screen = mem::try_at<const Vec3_t>(scratch + kScratch_Screen);
  if (!scratchSel || !world || !screen)
    return;

  const auto accepts = [&](u32 actor, u32 g, u32 m, i32 part) {
    *scratchSel = *selection;
    scratchSel->actor = actor;
    scratchSel->group = g;
    scratchSel->member = m;
    scratchSel->part = part;
    return TargetIsSelectable(frame, base, currentActor, actor,
                              scratch + kScratch_Sel) != 0 &&
           i32(scratchSel->part) == part;
  };

  Candidate best{};
  bool found = false;

  const u32 groups = u32(command.GroupCount());
  for (u32 g = 0; g < groups; ++g) {
    const BattleGroup group = command.GroupAt(g);
    if (!group)
      continue;
    const u32 members = u32(group.MemberCount());
    for (u32 m = 0; m < members; ++m) {
      const BattleMember member = group.MemberAt(m);
      if (!member)
        continue;
      const u32 actorVA = member.ActorAddress();
      const auto *actor = mem::try_at<const BattleActor_t>(actorVA);
      if (!actor)
        continue;

      const i32 partCount = i32(actor->parts.size());
      for (i32 p = kPartBody; p < partCount; ++p) {
        if (!accepts(actorVA, g, m, p))
          continue;

        const auto *object = p == kPartBody ? actor : actor->parts[p].get();
        if (!object)
          continue;
        *world = object->worldPos;
        WorldToScreenPos3(frame, base, visualRender, 0,
                          scratch + kScratch_Screen, scratch + kScratch_World,
                          0);

        Candidate c{};
        c.actor = actorVA;
        c.group = g;
        c.member = m;
        c.part = p;
        c.x = screen->x;
        c.y = screen->y;
        c.z = screen->z;
        const f32 rise = p == kPartBody ? kBodyRise : 0.0f;
        const f32 dx = c.x - pointerX;
        const f32 dy = (c.y - rise) - pointerY;
        c.distance = std::sqrt(dx * dx + dy * dy);

        if (c.z >= kFrontOfCamera || c.distance > kPickRadius)
          continue;
        if (!found || c.distance < best.distance) {
          best = c;
          found = true;
        }
      }
    }
  }

  if (!found)
    return;
  if (best.actor == u32(selection->actor) && best.part == i32(selection->part))
    return;

  if (!accepts(best.actor, best.group, best.member, best.part))
    return;

  auto *live = mem::try_at<BattleSelection_t>(task.SelectionAddress());
  if (!live)
    return;
  *live = *scratchSel;

  // The highlight list is rebuilt only where the engine's own movement
  // succeeds, so a pointer move has to ask for it or the marks stay on the
  // previous target. Zero clears the list first, the value the pad passes.
  TargetRefreshHighlights(frame, base, taskVA, 0);
}

} // namespace

} // namespace bd::engine

// The battle's target step, called once per frame from
// bdBattleSceneActionDispatch for as long as the player is choosing who to hit.
// It polls buttons 0 through 23 itself, so running ahead of it leaves the
// selection already moved when it reads the confirm button. r3 is the
// BattleTask.
REX_HOOK_RAW(bdBattleTargetSelectInput) {
  bd::engine::UpdateBattleTargetHover(ctx, base, ctx.r3.u32);
  __imp__bdBattleTargetSelectInput(ctx, base);
}
