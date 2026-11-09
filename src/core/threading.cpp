/**
 * @file    core/threading.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "core/threading.h"

#include "core/logging.h"
#include "platform/host_ui.h"

#include <rex/platform/thread.h>
#include <rex/ppc/hook.h>
#include <rex/kernel/title_runtime.h>
#include <rex/kernel/thread_registry.h>
#include <rex/types.h>

namespace bd {

void DemoteThreadToBackground() {
  rex::platform::SetCurrentThreadPriority(
      rex::platform::ThreadPriority::kLowest);
}

} // namespace bd

// PPC kernel bypass for ResumeThread.
u32 ResumeThread_hook(u32 handle) {
  auto thread = bd::platform::Title()->GetThreadRegistry().FindByHandle(handle);
  if (thread)
    thread->Resume();
  return 0;
}
REX_HOOK(rex_ResumeThread, ResumeThread_hook);
