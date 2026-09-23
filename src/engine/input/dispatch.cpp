#include "engine/input/dispatch.h"

#include "core/settings.h"
#include "engine/cutscene_pause.h"
#include "engine/game.h"
#include "engine/input/action_state.h"

namespace bd::engine {

namespace {

struct Entry {
  ActionHandler handler;
  ActionAvailable available;
  bool fired;
};

Entry g_entries[kActionCount] = {};

bool DevmodeOn() { return bd::Settings::Get().Devmode(); }

void ToggleDebugOverlay() { Game::Get().ToggleMindows(); }

void TogglePause() { CutscenePause::Get().Toggle(); }

bool PauseAvailable() { return CutscenePause::Get().Available(); }

bool InRange(Action action) {
  const int i = static_cast<int>(action);
  return i >= 0 && i < kActionCount;
}

void RegisterDefault(Action action, ActionHandler handler,
                     ActionAvailable available) {
  if (!InRange(action) || g_entries[static_cast<int>(action)].handler)
    return;
  RegisterHandler(action, handler, available);
}

void PrepareDefaults() {
  static const bool once = [] {
    RegisterDefault(Action::Pause, TogglePause, PauseAvailable);
    RegisterDefault(Action::Mindows, ToggleDebugOverlay, DevmodeOn);
    return true;
  }();
  (void)once;
}

} // namespace

void RegisterHandler(Action action, ActionHandler handler,
                     ActionAvailable available) {
  if (!InRange(action))
    return;
  g_entries[static_cast<int>(action)] = {handler, available, false};
}

bool WillDispatch(Action action) {
  PrepareDefaults();
  if (!InRange(action))
    return false;
  const Entry &entry = g_entries[static_cast<int>(action)];
  return entry.handler != nullptr &&
         (entry.available == nullptr || entry.available());
}

void RunDispatch() {
  PrepareDefaults();
  const InputActions &actions = InputActions::Get();
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (Describe(action).kind != ActionKind::Dispatched)
      continue;
    Entry &entry = g_entries[i];
    if (!WillDispatch(action) || !actions.Pressed(action)) {
      entry.fired = false;
      continue;
    }
    if (entry.fired)
      continue;
    entry.fired = true;
    entry.handler();
  }
}

} // namespace bd::engine
