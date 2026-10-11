/**
 * @file    engine/cheats_hooks.cpp
 * @brief   The stat-recompute hook the attack and defence multipliers ride on.
 *
 * @license BSD 3-Clause License
 */
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/types.h>

#include "engine/cheats.h"

REX_EXTERN(__imp__Player_CalcBattleParams);

// r4 is the destination CharaBattleParams_t, not the chara: the body holds it
// in r23 and every one of its nine stores targets that register, while r3 (the
// chara, held in r29) is only ever read from. Scaling r3's own params block
// instead is wrong wherever the guest was handed a different destination --
// the accessory screen previews an equip by computing into a scratch block,
// and a hook that scaled the live one would multiply it again on every cursor
// move, with nothing to rewrite it in between.
//
// Read before the call: a guest function is free to clobber its argument
// registers, and this one does. Scaling after it returns cannot compound,
// because the call rebuilds every field of the destination first.
REX_HOOK_RAW(Player_CalcBattleParams) {
  const u32 params = ctx.r4.u32;
  __imp__Player_CalcBattleParams(ctx, base);
  bd::engine::Cheats::Get().ScaleBattleParams(params);
}
