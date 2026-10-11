#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "core/logging.h"
#include "engine/input/action_state.h"
#include "engine/input/binding_store.h"
#include "engine/input/button_map.h"
#include "engine/input/input_sources.h"

REXCVAR_DEFINE_COMMAND_ARGS(
    input_binds,
    [](std::string_view) {
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
        BD_INFO("[input] {} = {}", bd::engine::ToString(action),
                list.empty() ? "unbound" : list);
      }
    },
    "Input", "Dump every action and the inputs bound to it");

REXCVAR_DEFINE_COMMAND_ARGS(
    input_sources,
    [](std::string_view) {
      const auto &s = bd::engine::InputSources::Get();
      BD_INFO("[input] pad held=0x{:08X} analog={:.2f},{:.2f},{:.2f},{:.2f}",
              s.PadHeld(), s.PadAnalog(0), s.PadAnalog(1), s.PadAnalog(2),
              s.PadAnalog(3));
    },
    "Input", "Dump the sampled physical input state");

REXCVAR_DEFINE_COMMAND_ARGS(
    input_actions,
    [](std::string_view) {
      const auto &a = bd::engine::InputActions::Get();
      for (int i = 0; i < bd::engine::kActionCount; ++i) {
        const auto action = static_cast<bd::engine::Action>(i);
        BD_INFO("[input] {} held={} pressed={} repeat={}",
                bd::engine::ToString(action), a.Held(action),
                a.Pressed(action), a.Repeat(action));
      }
      BD_INFO("[input] left={:.2f},{:.2f} right={:.2f},{:.2f}",
              a.Axis(bd::engine::AxisPair::Left, 0),
              a.Axis(bd::engine::AxisPair::Left, 1),
              a.Axis(bd::engine::AxisPair::Right, 0),
              a.Axis(bd::engine::AxisPair::Right, 1));
    },
    "Input", "Dump the resolved action state");

REXCVAR_DEFINE_COMMAND_ARGS(
    input_map,
    [](std::string_view) {
      auto &m = bd::engine::ButtonMap::Get();
      m.Rebuild();
      for (int i = 0; i < bd::engine::kActionCount; ++i) {
        const auto action = static_cast<bd::engine::Action>(i);
        if (bd::engine::Describe(action).kind !=
            bd::engine::ActionKind::Mapped)
          continue;
        BD_INFO("[input] {} -> id {} mask 0x{:08X}",
                bd::engine::ToString(action), m.Id(action), m.Mask(action));
      }
    },
    "Input", "Dump the derived button map");
