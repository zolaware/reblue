/**
 * @file    engine/script_man_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/script_man_task.h"

#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_input.h"
#include "engine/events.h"
#include "engine/input/actions.h"

namespace bd::engine {

namespace {

struct ScriptManTask_t {
  /* 0x000 */ u8 _pad000[0x668];
  /* 0x668 */ be_u32 mapId;
  /* 0x66C */ u8 _pad66C[0x6A0 - 0x66C];
  /* 0x6A0 */ be_u32 sceneState;
  /* 0x6A4 */ u8 _pad6A4[0x6F8 - 0x6A4];
  /* 0x6F8 */ be_u32 scriptVars;
  /* 0x6FC */ u8 _pad6FC[0x720 - 0x6FC];
  /* 0x720 */ be_u32 script;
  /* 0x724 */ u8 _pad724[0x77C - 0x724];
  /* 0x77C */ be_u32 miniMapTask;
};
static_assert(offsetof(ScriptManTask_t, mapId) == 0x668);
static_assert(offsetof(ScriptManTask_t, sceneState) == 0x6A0);
static_assert(offsetof(ScriptManTask_t, scriptVars) == 0x6F8);
static_assert(offsetof(ScriptManTask_t, script) == 0x720);
static_assert(offsetof(ScriptManTask_t, miniMapTask) == 0x77C);

} // namespace

u32 ScriptManTask::MapId() const {
  const auto *self = Self<ScriptManTask_t>();
  return self ? static_cast<u32>(self->mapId) : 0;
}

u32 ScriptManTask::SceneState() const {
  const auto *self = Self<ScriptManTask_t>();
  return self ? static_cast<u32>(self->sceneState) : kSceneStateNone;
}

engine::Script ScriptManTask::Script() const {
  const auto *self = Self<ScriptManTask_t>();
  return engine::Script(self ? static_cast<u32>(self->script) : 0);
}

engine::ScriptVars ScriptManTask::Vars() const {
  const auto *self = Self<ScriptManTask_t>();
  return engine::ScriptVars(self ? static_cast<u32>(self->scriptVars) : 0);
}

engine::MiniMapTask ScriptManTask::MiniMap() const {
  const auto *self = Self<ScriptManTask_t>();
  return engine::MiniMapTask(self ? static_cast<u32>(self->miniMapTask) : 0);
}

} // namespace bd::engine

// Slot 1 of ??_7ScriptManTask@@6B@. Its own steps are what put the save block
// into live state: the script variable block first, the inventory and gold two
// steps later, the party after that. The task dispatcher latches a task as
// initialized on the first non-zero answer from slot 1 and never calls it
// again, so the non-zero return is one edge per session, taken by a continue
// and by a new game alike.
REX_EXTERN(__imp__ScriptManTask__Init);
REX_HOOK_RAW(ScriptManTask__Init) {
  __imp__ScriptManTask__Init(ctx, base);
  if (ctx.r3.u32)
    bd::engine::Events::Publish(bd::engine::SaveLoaded{});
}

REX_EXTERN(__imp__bdFieldAreaLoadStep);
REX_HOOK_RAW(bdFieldAreaLoadStep) {
  const u32 script = ctx.r3.u32;
  __imp__bdFieldAreaLoadStep(ctx, base);
  if (ctx.r3.u32)
    bd::engine::Events::Publish(
        bd::engine::StageLoaded{bd::engine::Script(script)});
}

// ScriptManTask::UnloadScript, the only place a Script leaves the chain, rather
// than Script::~Script: publishing ahead of the call still sees the stage's
// entities and resources live. The outgoing Script is the second parameter.
REX_EXTERN(__imp__ScriptManTask__UnloadScript);
REX_HOOK_RAW(ScriptManTask__UnloadScript) {
  const u32 script = ctx.r4.u32;
  if (script)
    bd::engine::Events::Publish(
        bd::engine::StageUnloading{bd::engine::Script(script)});
  __imp__ScriptManTask__UnloadScript(ctx, base);
}

// Script prompts poll the menu confirm alone, but the prompt a player is
// answering is one Interact opened, so a key moved from Confirm onto Interact
// has to answer it too.
void bdScriptConfirmHook(PPCRegister &r3) {
  if (!r3.u32 && bd::engine::CheckAction(bd::engine::Action::Interact))
    r3.u64 = 1;
}
