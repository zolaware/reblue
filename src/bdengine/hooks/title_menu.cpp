/**
 * @file    bdengine/hooks/title_menu.cpp
 * @brief   Title screen hooks - add "config" entry and wire to ConfigMenu.
 *
 * Four hooks integrate the config menu into the title screen:
 *   1. bdTitleNavBoundsHook  (midasm)       - extend menu to 4 entries
 *   2. REX_HOOK_RAW(TitleTask_Draw)         - render "config" text entry
 *   3. bdTitleModsDispatchHook (midasm)      - intercept A on "config"
 *   4. REX_HOOK_RAW(TitleTask_Update)        - deferred child creation
 *
 * ConfigMenu is used directly - no D2AnimeScreen, no FindScreen.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/config/config_menu.h"
#include "bdengine/config/config_menu_data.h"
#include "bdengine/common/logging.h"

#include <cstring>

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

REX_IMPORT(__imp__bdColor4fToARGB, Color4fToARGB, u32(u32));
REX_IMPORT(__imp__bdTextCalcWidth, TextCalcWidth, f64(f64, u32, u32, u32, u32));
REX_EXTERN(__imp__Visual__method_7E60);
REX_EXTERN(__imp__TitleTask_Update);
REX_EXTERN(__imp__TitleTask_Draw);
REX_EXTERN(__imp__TitleTask_OnChildComplete);

static constexpr u32 kVisualRenderAddr = 0x82DC9848;

static constexpr u32 kTitleState = 0x90;
static constexpr u32 kTitleCursor = 0x94;
static constexpr u32 kTitleHasSaveData = 0x98;
static constexpr u32 kTitleChildPtr = 0xA0;
static constexpr u32 kTitleIsXboxLive = 0x104;

static constexpr float kCursorBaseY = 528.0f;
static constexpr float kEntrySpacing = 32.0f;
static constexpr float kTextCursorOffset = 16.0f;

namespace {

bd::ConfigMenu s_config_menu;
bool s_create_config = false;
bool s_config_closing = false;
u32 s_title_task_addr = 0;

/**
 * @brief Compute the cursor index for the "config" entry (always last).
 */
u32 ConfigIndex(u8 *base, u32 titleTask) {
  u32 idx = 1;
  if (load_and_swap<u32>(base + titleTask + kTitleHasSaveData))
    idx++;
  if (load_and_swap<u32>(base + titleTask + kTitleIsXboxLive) &&
      load_and_swap<u32>(base + titleTask + kTitleHasSaveData))
    idx++;
  return idx;
}

/**
 * @brief Compute the Y position for the "config" text entry.
 *
 *        The cursor strip sits at kCursorBaseY + index * kEntrySpacing. The
 *        text top-left is kTextCursorOffset above the cursor center, matching
 *        the offset used by the disc/voice select entries.
 */
float ConfigTextY(u8 *base, u32 titleTask) {
  float cursorY = kCursorBaseY + ConfigIndex(base, titleTask) * kEntrySpacing;
  return cursorY - kTextCursorOffset;
}

} // namespace

/**
 * @brief Navigation bounds hook (midasm at 0x820CA654).
 *
 *        Forces New Game+ on (+0x104 = 1) and sets maxItems = 4. Only fires
 *        when hasSaveData != 0.
 */
void bdTitleNavBoundsHook(PPCRegister &r30, PPCRegister &r31) {
  r30.u64 += 1;
}

/**
 * @brief Draw the "config" text entry on top of the original TitleTask draw.
 *
 *        Runs after the original draw in state 2. Text rendering reuses the
 *        disc/voice select pattern: bdColor4fToARGB -> bdTextCalcWidth ->
 *        Visual__method_7E60.
 */
REX_HOOK_RAW(TitleTask_Draw) {
  u32 titleTask = ctx.r3.u32;
  __imp__TitleTask_Draw(ctx, base);

  u32 state = load_and_swap<u32>(base + titleTask + kTitleState);
  if (state != 2)
    return;

  u32 hasSaveData =
      load_and_swap<u32>(base + titleTask + kTitleHasSaveData);
  if (!hasSaveData)
    return;

  float yPos = ConfigTextY(base, titleTask);

  rex::ppc::stack_guard guard(ctx);

  // bdColor4fToARGB takes floats in {r, g, b, a} order (big-endian).
  u32 be_color[4];
  {
    float vals[] = {0.7f, 0.7f, 0.7f, 1.0f};
    for (int i = 0; i < 4; i++) {
      u32 bits;
      std::memcpy(&bits, &vals[i], 4);
      be_color[i] = __builtin_bswap32(bits);
    }
  }
  u32 colorAddr = rex::ppc::stack_push(ctx, base, be_color, sizeof(be_color));
  u32 color = Color4fToARGB(colorAddr);

  static const u16 kConfigStr[] = {0x0063, 0x006F, 0x006E, 0x0066,
                                        0x0069, 0x0067, 0x0000};
  u16 be_str[7];
  for (int i = 0; i < 7; ++i)
    be_str[i] = __builtin_bswap16(kConfigStr[i]);
  u32 strAddr = rex::ppc::stack_push(ctx, base, be_str, sizeof(be_str));

  float textW = TextCalcWidth(24.0, color, strAddr, 1, -1);

  u32 visualRender = load_and_swap<u32>(base + kVisualRenderAddr);
  float screenW = load_and_swap<float>(base + visualRender + 0x1A38);
  float xPos = (screenW * 0.5f) - (textW * 0.5f);

  {
    rex::CallFrame cf(ctx);
    cf.ctx.f1.f64 = (double)xPos;
    cf.ctx.f2.f64 = (double)yPos;
    cf.ctx.f3.f64 = 0.0;
    cf.ctx.f4.f64 = 24.0;
    cf.ctx.f5.f64 = 24.0;
    cf.ctx.r9.u32 = color;
    cf.ctx.r10.u32 = 1;
    // Zero all stack params: the callee reads ~18 u32s from the stack frame.
    for (int i = 0; i < 20; ++i)
      store_and_swap<u32>(base + cf.ctx.r1.u32 + 0x08 + i * 4, 0);
    // Rewrite the string AFTER zeroing: the stack param region can overlap.
    for (int i = 0; i < 7; ++i)
      store_and_swap<u16>(base + strAddr + i * 2, kConfigStr[i]);
    cf.ctx.r8.u32 = strAddr;
    __imp__Visual__method_7E60(cf, base);
  }
}

/**
 * @brief A-button dispatch hook (midasm at 0x820CA6C4).
 *
 *        Intercepts when the cursor is on the config entry. Sets the deferred
 *        creation flag and moves TitleTask into state 4 (CHILD_RUNNING).
 */
bool bdTitleModsDispatchHook(PPCRegister &r31, PPCRegister &r11) {
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 titleTask = r31.u32;
  u32 cursor = load_and_swap<u32>(base + titleTask + kTitleCursor);

  if (cursor != ConfigIndex(base, titleTask))
    return false;

  s_create_config = true;
  s_title_task_addr = titleTask;
  store_and_swap<u32>(base + titleTask + kTitleState, 4);

  BD_INFO("[config] A on config entry, creating child task");
  return true;
}

/**
 * @brief TitleTask_OnChildComplete hook; cleans up after the config child.
 */
REX_HOOK_RAW(TitleTask_OnChildComplete) {
  u32 titleTask = ctx.r3.u32;
  u32 childTask = ctx.r4.u32;

  if (s_config_menu.IsActive() && childTask == s_config_menu.TaskAddr()) {
    memcpy(base + titleTask + 0xA4, "Title\0", 6);
    store_and_swap<u32>(base + titleTask + kTitleChildPtr, 0);

    s_config_menu.Destroy();
    bd::config::UnregisterVFS();

    BD_INFO("[config] OnChildComplete: wrote 'Title' to nameBuffer");
    // Skip the original: it would copy garbage from +0x78.
    return;
  }

  __imp__TitleTask_OnChildComplete(ctx, base);
}

/**
 * @brief Deferred child creation on TitleTask_Update.
 *
 *        Handles pending close first, then creation, then delegates update
 *        to ConfigMenu when it is active. ConfigMenu::Create links the
 *        child; the title state is forced to 4 (CHILD_RUNNING) afterwards.
 */
REX_HOOK_RAW(TitleTask_Update) {
  u32 titleTask = ctx.r3.u32;

  // Handle pending close first so a reinstall can register fresh VFS state.
  if (s_config_closing) {
    s_config_closing = false;

    bool wantsRestart = s_config_menu.WantsRestart();
    s_config_menu.Destroy();

    store_and_swap<u32>(base + titleTask + kTitleChildPtr, 0);
    store_and_swap<u32>(base + titleTask + kTitleState, 2);
    memcpy(base + titleTask + 0xA4, "Title\0", 6);

    bd::config::UnregisterVFS();

    BD_INFO("[config] child closing, restored state 2");

    if (wantsRestart)
      s_create_config = true;
  }

  if (s_create_config) {
    s_create_config = false;

    bd::config::RegisterVFS();
    s_config_menu.Create(titleTask);

    if (!s_config_menu.IsActive()) {
      BD_ERROR("[config] Create failed, restoring state 2");
      store_and_swap<u32>(base + titleTask + kTitleState, 2);
      bd::config::UnregisterVFS();
    } else {
      store_and_swap<u32>(base + titleTask + kTitleState, 4);
      BD_INFO("[config] config menu active, task at 0x{:08X}",
              s_config_menu.TaskAddr());
    }
  }

  if (s_config_menu.IsActive()) {
    s_config_menu.Update(ctx, base);
    if (s_config_menu.IsClosing())
      s_config_closing = true;
  } else {
    __imp__TitleTask_Update(ctx, base);
  }
}
