/**
 * @file    core/guest_crt.cpp
 * @brief   Guest CRT entry points the host answers directly rather than
 *          running the recompiled PPC.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/encoding.h"
#include "core/logging.h"
#include "core/settings.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include <rex/hook.h>
#include <rex/kernel/xboxkrnl/format.h>
#include <rex/ppc.h>
#include <rex/ppc/function.h>
#include <rex/runtime.h>
#include <rex/runtime/thread_state.h>
#include <rex/types.h>
#include "core/memory_helpers.h"

namespace {

double GuestAtan2(double y, double x) { return std::atan2(y, x); }
double GuestAtof(const char *s) { return std::atof(s); }

} // namespace

REX_HOOK(rex_atan2, GuestAtan2);
REX_HOOK(rex_atof, GuestAtof);

// The retail build stubs DbgPrint_v and also installs it as the default no-op
// callback in vtables and data tables, so the format pointer is not always a
// string.
u32 bdDebugPrintHook(MappedString fmt) {
  if (!bd::Settings::Get().DbgPrint())
    return 1;

  const u32 fmt_addr = fmt.GetGuestAddress();
  if (fmt_addr < 0x82000000u || fmt_addr >= 0xC0000000u)
    return 1;

  auto &ctx = *rex::runtime::ThreadState::Get()->GetContext();
  auto *base = bd::mem::view()->GetVirtualMembase();

  rex::runtime::StackArgList args(ctx, base, 1);
  rex::runtime::StringFormatData data(
      reinterpret_cast<const u8 *>(fmt.GetHostAddress()));

  if (rex::runtime::FormatCore(base, data, args, /*wide=*/false) <= 0)
    return 1;

  auto str = data.GetOutput();
  while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())))
    str.pop_back();
  if (str.empty())
    return 1;

  REXLOG_INFO(bd, "dbg {}", bd::SjisToUtf8(str));
  return 1;
}
REX_HOOK(rex_DebugPrint, bdDebugPrintHook);
