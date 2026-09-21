#include "engine/input/dispatch.h"

#include "core/settings.h"
#include "core/ui_thread.h"
#include "engine/game.h"
#include "engine/input/action_state.h"
#include "ui/ui.h"

namespace bd::engine {

namespace {

struct Entry {
  ActionHandler handler;
  ActionAvailable available;
  bool fired;
};

Entry g_entries[kActionCount] = {};

bool DevmodeOn() { return bd::Settings::Get().Devmode(); }

void CycleOverlay() {
  bd::RunOnUIThread([] {
    const auto stage =
        bd::ui::NextOverlayStage(static_cast<bd::ui::OverlayStage>(
            bd::ui::Settings::Get().PerfOverlay()));
    bd::ui::Settings::Get().SetPerfOverlay(static_cast<i32>(stage));
  });
}

void ToggleDebugOverlay() { Game::Get().ToggleMindows(); }

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
    RegisterDefault(Action::Overlay, CycleOverlay, DevmodeOn);
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
