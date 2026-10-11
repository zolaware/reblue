/**
 * @file    engine/script_vars.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/script_vars.h"

#include <cstddef>

#include <rex/hook.h>

#include "core/memory_helpers.h"
#include "engine/events.h"
#include "engine/game.h"

namespace bd::engine {

namespace {

constexpr u32 kGlobalCount = 4096;
constexpr u32 kFlagsPerWord = 16;
constexpr u32 kFlagWordCount = ScriptVars::kFlagMax / kFlagsPerWord + 1;
constexpr u32 kVarNothings = 2385; // lifetime Nothings-collected total

struct ScriptVars_t {
  /* 0x0000 */ be_u32 globals[kGlobalCount];
  /* 0x4000 */ be_u32 flags[kFlagWordCount];
};
static_assert(offsetof(ScriptVars_t, globals) == 0x0000);
static_assert(offsetof(ScriptVars_t, flags) == 0x4000);

} // namespace

u32 ScriptVars::Global(u32 index) const {
  const auto *self = Self<ScriptVars_t>();
  if (!self || index >= kGlobalCount)
    return 0;
  return static_cast<u32>(self->globals[index]);
}

u32 ScriptVars::Flag(u32 id) const {
  const auto *self = Self<ScriptVars_t>();
  if (!self || id > kFlagMax)
    return 0;
  const u32 word = static_cast<u32>(self->flags[id / kFlagsPerWord]);
  return (word >> (2u * (id % kFlagsPerWord))) & 3u;
}

u32 ScriptVars::NothingsCollected() const { return Global(kVarNothings); }

} // namespace bd::engine

// Gmk::ReactGim::vf13, the step a search point runs. One case of its collect
// switch is the joke kind that gives nothing, and its increment of the
// lifetime Nothings total is the only one in the title. That case sits inside
// a jump table with no boundary of its own, so the count is read on either
// side of the step rather than off the store.
REX_EXTERN(__imp__Gmk__ReactGim__vf13);
REX_HOOK_RAW(Gmk__ReactGim__vf13) {
  const auto &game = bd::engine::Game::Get();
  const u32 before = game.ScriptManTask().Vars().NothingsCollected();
  __imp__Gmk__ReactGim__vf13(ctx, base);
  const u32 after = game.ScriptManTask().Vars().NothingsCollected();
  if (after > before)
    bd::engine::Events::Publish(bd::engine::NothingCollected{after});
}
