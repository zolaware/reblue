#include <string>
#include <string_view>

#include "core/cvar_table.h"

#include "core/logging.h"
#include "core/settings_model.h"
#include "engine/input/action_state.h"
#include "engine/input/binding_registry.h"
#include "engine/input/button_map.h"
#include "platform/hid.h"
#include "engine/input/input_sources.h"
#include "platform/platform.h"

void bd::commands::InputBinds(std::string_view) {
  auto &b = bd::engine::Bindings::Get();
  for (int i = 0; i < bd::engine::kActionCount; ++i) {
    const auto action = static_cast<bd::engine::Action>(i);
    std::string list;
    for (const auto &s : b.Sources(action)) {
      const std::string formatted = bd::platform::HID().InputSourceToString(s);
      if (formatted.empty())
        continue;
      if (!list.empty())
        list += ", ";
      list += formatted;
    }
    REXBD_INFO("input {} = {}", bd::engine::ToString(action),
            list.empty() ? "unbound" : list);
  }
}


void bd::commands::InputResetBinds(std::string_view args) {
  constexpr std::string_view kSpace = " 	";
  const size_t first = args.find_first_not_of(kSpace);
  const std::string_view name =
      first == std::string_view::npos
          ? std::string_view()
          : args.substr(first, args.find_last_not_of(kSpace) - first + 1);

  bool changed = false;
  if (name.empty()) {
    changed = bd::ResetAllKeybinds();
  } else {
    int found = -1;
    for (int i = 0; i < bd::engine::kActionCount; ++i) {
      if (name == bd::engine::ToString(static_cast<bd::engine::Action>(i)))
        found = i;
    }
    if (found < 0) {
      REXBD_ERROR("input no action named '{}'", name);
      return;
    }
    changed = bd::engine::Bindings::Get().Reset(
        static_cast<bd::engine::Action>(found));
  }

  const std::string_view what = name.empty() ? "every bind" : name;
  if (!changed) {
    REXBD_INFO("input {} already at its default", what);
    return;
  }
  if (auto saved = bd::SaveConfig(bd::platform::ConfigFilePath()); !saved) {
    REXBD_WARN("input {} reset but the config save failed: {}", what,
               saved.error().message);
    return;
  }
  REXBD_INFO("input {} reset to its default", what);
}

void bd::commands::InputSourcesDump(std::string_view) {
  const auto &s = bd::engine::InputSources::Get();
  REXBD_INFO("input pad held=0x{:08X} analog={:.2f},{:.2f},{:.2f},{:.2f}",
          s.PadHeld(), s.PadAnalog(0), s.PadAnalog(1), s.PadAnalog(2),
          s.PadAnalog(3));
}


void bd::commands::InputActionsDump(std::string_view) {
  const auto &a = bd::engine::InputActions::Get();
  for (int i = 0; i < bd::engine::kActionCount; ++i) {
    const auto action = static_cast<bd::engine::Action>(i);
    REXBD_INFO("input {} held={} pressed={} repeat={}",
            bd::engine::ToString(action), a.Held(action),
            a.Pressed(action), a.Repeat(action));
  }
  REXBD_INFO("input left={:.2f},{:.2f} right={:.2f},{:.2f}",
          a.Axis(bd::engine::AxisPair::Left, 0),
          a.Axis(bd::engine::AxisPair::Left, 1),
          a.Axis(bd::engine::AxisPair::Right, 0),
          a.Axis(bd::engine::AxisPair::Right, 1));
}


void bd::commands::InputMap(std::string_view) {
  auto &m = bd::engine::ButtonMap::Get();
  m.Rebuild();
  for (int i = 0; i < bd::engine::kActionCount; ++i) {
    const auto action = static_cast<bd::engine::Action>(i);
    if (bd::engine::Describe(action).kind !=
        bd::engine::ActionKind::Mapped)
      continue;
    REXBD_INFO("input {} -> id {} mask 0x{:08X}",
            bd::engine::ToString(action), m.Id(action), m.Mask(action));
  }
}

