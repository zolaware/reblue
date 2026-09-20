#include "engine/input/actions.h"

namespace bd::engine {

namespace {

constexpr ActionDesc kDescs[kActionCount] = {
    {"confirm", "settings.action.confirm", ActionContext::Menu,
     ActionKind::Mapped, 0, -1, AxisPair::None, "LMB,Ctrl+Space,PadA"},
    {"cancel", "settings.action.cancel", ActionContext::Menu,
     ActionKind::Mapped, 1, -1, AxisPair::None, "RMB,Escape,PadB"},
    {"nav_up", "settings.action.nav_up", ActionContext::Menu,
     ActionKind::Mapped, 10, -1, AxisPair::None, "Shift+Up,PadDUp,LStickUp"},
    {"nav_down", "settings.action.nav_down", ActionContext::Menu,
     ActionKind::Mapped, 11, -1, AxisPair::None,
     "Shift+Down,PadDDown,LStickDown"},
    {"nav_left", "settings.action.nav_left", ActionContext::Menu,
     ActionKind::Mapped, 12, -1, AxisPair::None,
     "Shift+Left,PadDLeft,LStickLeft"},
    {"nav_right", "settings.action.nav_right", ActionContext::Menu,
     ActionKind::Mapped, 13, -1, AxisPair::None,
     "Shift+Right,PadDRight,LStickRight"},

    {"interact", "settings.pad.interact", ActionContext::Field,
     ActionKind::Mapped, 4, -1, AxisPair::None, "LMB,Ctrl+Space,PadA"},
    {"attack", "settings.pad.dash_attack", ActionContext::Field,
     ActionKind::Mapped, 5, -1, AxisPair::None, "Space,PadX"},
    {"main_menu", "settings.pad.main_menu", ActionContext::Field,
     ActionKind::Mapped, 2, -1, AxisPair::None, "C,PadY"},
    {"field_menu", "settings.pad.field_menu", ActionContext::Field,
     ActionKind::Mapped, 3, -1, AxisPair::None, "F,PadRT"},
    {"world_map", "settings.pad.world_map", ActionContext::Field,
     ActionKind::Mapped, 6, -1, AxisPair::None, "M,PadStart"},
    {"field_skill_1", "settings.pad.field_skill_1", ActionContext::Field,
     ActionKind::Mapped, 7, -1, AxisPair::None, "Q,PadRB"},
    {"field_skill_2", "settings.pad.field_skill_2", ActionContext::Field,
     ActionKind::Mapped, 8, -1, AxisPair::None, "E,PadLB"},
    {"reset_camera", "settings.pad.reset_camera", ActionContext::Field,
     ActionKind::Mapped, 9, -1, AxisPair::None, "Z,PadLT"},
    {"move", "settings.pad.move", ActionContext::Field, ActionKind::Mapped, -1,
     -1, AxisPair::Left, "W,S,A,D,LStick"},
    {"view", "settings.pad.view", ActionContext::Field, ActionKind::Mapped, -1,
     -1, AxisPair::Right, "Up,Down,Left,Right,RStick"},

    {"machine_gun", "settings.pad.machine_gun", ActionContext::Mechat,
     ActionKind::Mapped, 0, -1, AxisPair::None, "LMB,Ctrl+Space,PadA"},
    {"missile", "settings.pad.missile", ActionContext::Mechat,
     ActionKind::Mapped, 1, -1, AxisPair::None, "RMB,Escape,PadB"},
    {"turn_left", "settings.pad.turn_left", ActionContext::Mechat,
     ActionKind::Mapped, 3, -1, AxisPair::None, "E,PadLB,PadLT"},
    {"turn_right", "settings.pad.turn_right", ActionContext::Mechat,
     ActionKind::Mapped, 4, -1, AxisPair::None, "Q,PadRB,PadRT"},
    {"aim", "settings.pad.aim", ActionContext::Mechat, ActionKind::Mapped, -1,
     -1, AxisPair::Left, "W,S,A,D,LStick"},

    {"back", "settings.action.back", ActionContext::Menu, ActionKind::Mapped,
     -1, 5, AxisPair::None, "Tab,PadBack"},
    {"stick_press_left", "settings.action.stick_press_left",
     ActionContext::Field, ActionKind::Mapped, -1, 6, AxisPair::None,
     "K,PadLS"},
    {"stick_press_right", "settings.action.stick_press_right",
     ActionContext::Field, ActionKind::Mapped, -1, 7, AxisPair::None,
     "L,PadRS"},

    {"area_map", "settings.action.area_map", ActionContext::Field,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},
    {"skip_cutscene", "settings.action.skip_cutscene", ActionContext::Field,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},

    {"overlay", "settings.action.overlay", ActionContext::System,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},
    {"mindows", "settings.action.mindows", ActionContext::System,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},
    {"screenshot", "settings.action.screenshot", ActionContext::System,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},
    {"fullscreen", "settings.action.fullscreen", ActionContext::System,
     ActionKind::Dispatched, -1, -1, AxisPair::None, ""},
};

static_assert(kDescs[static_cast<int>(Action::Fullscreen)].context ==
              ActionContext::System);
static_assert(kDescs[static_cast<int>(Action::Aim)].axis == AxisPair::Left);

} // namespace

const ActionDesc &Describe(Action action) {
  const int i = static_cast<int>(action);
  return kDescs[(i < 0 || i >= kActionCount) ? 0 : i];
}

const char *ToString(Action action) { return Describe(action).name; }

} // namespace bd::engine
