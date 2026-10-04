/**
 * @file    core/ui_thread.h
 * @brief   Marshals work onto the UI thread for callers that run somewhere
 *          else and touch state the UI thread owns.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <functional>

namespace bd {

void SetUIThreadDispatcher(std::function<bool(std::function<void()>)> dispatch);

bool RunOnUIThread(std::function<void()> fn);

} // namespace bd
