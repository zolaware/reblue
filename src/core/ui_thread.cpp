/**
 * @file    core/ui_thread.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/ui_thread.h"

#include <mutex>

#include "core/logging.h"

namespace bd {
namespace {

std::mutex g_dispatch_mutex;
std::function<bool(std::function<void()>)> g_dispatch;

} // namespace

void SetUIThreadDispatcher(
    std::function<bool(std::function<void()>)> dispatch) {
  std::lock_guard lock(g_dispatch_mutex);
  g_dispatch = std::move(dispatch);
}

bool RunOnUIThread(std::function<void()> fn) {
  if (!fn)
    return false;
  std::function<bool(std::function<void()>)> dispatch;
  {
    std::lock_guard lock(g_dispatch_mutex);
    dispatch = g_dispatch;
  }
  if (!dispatch) {
    BD_WARN("[ui] dropped work: no UI thread dispatcher installed yet");
    return false;
  }
  return dispatch(std::move(fn));
}

} // namespace bd
