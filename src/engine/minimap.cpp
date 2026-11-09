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
#include <rex/ppc/detour.h>
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
REX_DETOUR(bdMinimapLoad, u32, (u32 db, u32 name, u32 dbName)) {
  if (g_redirect.active) {
    RewriteName(name, "MM_");
    RewriteName(dbName, "db_");
  }
  return original(db, name, dbName);
}

// MiniMapTask_LoadAreaFloors(task, category, areaHi, areaLo).
REX_IMPORT(__imp__MiniMapTask_LoadAreaFloors, guest_MiniMapTask_LoadAreaFloors,
           u32(u32, i32, i32, i32));
u32 MiniMapTaskLoadAreaFloors_hook(u32 taskVA, i32 category, i32 areaHi,
                                   i32 areaLo) {
  const u32 cat = static_cast<u32>(category);
  const u32 hi = static_cast<u32>(areaHi);
  const u32 lo = static_cast<u32>(areaLo);

  const auto area = static_cast<bd::engine::AreaCategory>(cat);
  bd::engine::MiniMapTask task(taskVA);
  if (!task || (area != bd::engine::AreaCategory::Bg &&
                area != bd::engine::AreaCategory::Bi)) {
    return guest_MiniMapTask_LoadAreaFloors(taskVA, category, areaHi, areaLo);
  }

  // The body's own identity check compares against the forced category, so
  // perform it here against the true one.
  if (task.Floor() && task.Category() == cat && task.AreaHi() == hi &&
      task.AreaLo() == lo) {
    return 1;
  }

  bd::engine::Script::BuildName(g_redirect.stem, sizeof(g_redirect.stem), cat,
                                hi * 100 + lo);
  g_redirect.active = true;
  const u32 result = guest_MiniMapTask_LoadAreaFloors(
      taskVA, static_cast<i32>(bd::engine::AreaCategory::Dg), areaHi, areaLo);
  g_redirect.active = false;

  // Restore the true category so the next dungeon carrying these numbers is
  // not taken for this stage.
  task.SetCategory(cat);
  return result;
}
REX_HOOK(MiniMapTask_LoadAreaFloors, MiniMapTaskLoadAreaFloors_hook);
