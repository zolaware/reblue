/**
 * @file    engine/minimap.cpp
 * @brief   Minimaps in towns and indoor maps, which the engine loads only for
 *          dungeons: bg/bi areas go through the dg path under their own stem.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include <cstdio>
#include <cstring>

#include <rex/hook.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/mini_map_task.h"
#include "engine/script.h"

namespace {

// LoadAreaFloors calls bdMinimapLoad inline on the same thread, so a plain flag
// carries the stem between the two hooks.
struct StemRedirect {
  bool active = false;
  char stem[16] = {};
} g_redirect;

constexpr u32 kNameCap = 32;

// Past the prefix a base name carries one '_' and a sub-floor probe two, so
// the second one opens the "_NN" to keep: "MM_dg300_01_01" -> "MM_bi03a01_01".
void RewriteName(u32 va, const char *prefix) {
  char *out = bd::mem::try_at<char>(va);
  if (!out)
    return;
  const size_t prefixLen = std::strlen(prefix);
  if (std::strncmp(out, prefix, prefixLen) != 0)
    return;
  const char *old = out + prefixLen;
  const char *suffix = "";
  u32 underscores = 0;
  for (const char *p = old; *p; ++p)
    if (*p == '_' && ++underscores == 2)
      suffix = p;
  // suffix points into out, which snprintf may not overlap.
  char keep[8] = {};
  std::snprintf(keep, sizeof(keep), "%s", suffix);
  std::snprintf(out, kNameCap, "%s%s%s", prefix, g_redirect.stem, keep);
}

} // namespace

// bdMinimapLoad(db, nameVA, dbNameVA).
REX_EXTERN(__imp__bdMinimapLoad);
REX_HOOK_RAW(bdMinimapLoad) {
  if (g_redirect.active) {
    RewriteName(ctx.r4.u32, "MM_");
    RewriteName(ctx.r5.u32, "db_");
  }
  __imp__bdMinimapLoad(ctx, base);
}

// MiniMapTask_LoadAreaFloors(task, category, areaHi, areaLo).
REX_EXTERN(__imp__MiniMapTask_LoadAreaFloors);
REX_HOOK_RAW(MiniMapTask_LoadAreaFloors) {
  const u32 cat = ctx.r4.u32;
  const u32 hi = ctx.r5.u32;
  const u32 lo = ctx.r6.u32;

  const auto area = static_cast<bd::engine::AreaCategory>(cat);
  bd::engine::MiniMapTask task(ctx.r3.u32);
  if (!task || (area != bd::engine::AreaCategory::Bg &&
                area != bd::engine::AreaCategory::Bi)) {
    __imp__MiniMapTask_LoadAreaFloors(ctx, base);
    return;
  }

  // The body's own identity check compares against the forced category, so
  // perform it here against the true one.
  if (task.Floor() && task.Category() == cat && task.AreaHi() == hi &&
      task.AreaLo() == lo) {
    ctx.r3.u64 = 1;
    return;
  }

  bd::engine::Script::BuildName(g_redirect.stem, sizeof(g_redirect.stem), cat,
                                hi * 100 + lo);
  g_redirect.active = true;
  ctx.r4.u32 = static_cast<u32>(bd::engine::AreaCategory::Dg);
  __imp__MiniMapTask_LoadAreaFloors(ctx, base);
  g_redirect.active = false;

  // Restore the true category so the next dungeon carrying these numbers is
  // not taken for this stage.
  task.SetCategory(cat);
}
