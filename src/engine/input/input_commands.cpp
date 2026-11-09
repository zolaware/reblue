#include <string>
#include <string_view>

#include "core/cvar_table.h"

#include "core/logging.h"
#include "engine/input/action_state.h"
#include "engine/input/binding_registry.h"
#include "engine/input/button_map.h"
#include "engine/input/input_sources.h"

void bd::commands::InputBinds(std::string_view) {
  auto &b = bd::engine::Bindings::Get();
  for (int i = 0; i < bd::engine::kActionCount; ++i) {
    const auto action = static_cast<bd::engine::Action>(i);
    std::string list;
    std::string formatted;
    for (const auto &s : b.Sources(action)) {
      if (!bd::engine::FormatSource(s, formatted))
        continue;
      if (!list.empty())
        list += ", ";
      list += formatted;
    }
    REXLOG_INFO(bd, "input {} = {}", bd::engine::ToString(action),
            list.empty() ? "unbound" : list);
  }
}


void bd::commands::InputSourcesDump(std::string_view) {
  const auto &s = bd::engine::InputSources::Get();
  REXLOG_INFO(bd, "input pad held=0x{:08X} analog={:.2f},{:.2f},{:.2f},{:.2f}",
          s.PadHeld(), s.PadAnalog(0), s.PadAnalog(1), s.PadAnalog(2),
          s.PadAnalog(3));
}


void bd::commands::InputActionsDump(std::string_view) {
  const auto &a = bd::engine::InputActions::Get();
  for (int i = 0; i < bd::engine::kActionCount; ++i) {
    const auto action = static_cast<bd::engine::Action>(i);
    REXLOG_INFO(bd, "input {} held={} pressed={} repeat={}",
            bd::engine::ToString(action), a.Held(action),
            a.Pressed(action), a.Repeat(action));
  }
  REXLOG_INFO(bd, "input left={:.2f},{:.2f} right={:.2f},{:.2f}",
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
    REXLOG_INFO(bd, "input {} -> id {} mask 0x{:08X}",
            bd::engine::ToString(action), m.Id(action), m.Mask(action));
  }
}

