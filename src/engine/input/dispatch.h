#pragma once

#include "engine/input/actions.h"

namespace bd::engine {

using ActionHandler = void (*)();
using ActionAvailable = bool (*)();

void RegisterHandler(Action action, ActionHandler handler,
                     ActionAvailable available);

bool WillDispatch(Action action);

void RunDispatch();

} // namespace bd::engine
