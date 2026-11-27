#include "engine/input/actions.h"

#include <span>
#include <string_view>

namespace bd::engine {

namespace {

using O = InputOwner;

constexpr OwnerMask kInField = LiveIn(O::Field);
constexpr OwnerMask kInMenu = LiveIn(O::Menu);
constexpr OwnerMask kInMenus =
    LiveIn(O::Menu, O::Config, O::ConfigBinds, O::ConfigMods, O::ConfigDLC,
           O::ConfigLanguages);
constexpr OwnerMask kInConfigBinds = LiveIn(O::ConfigBinds);
constexpr OwnerMask kInContentLists =
    LiveIn(O::ConfigMods, O::ConfigDLC, O::ConfigLanguages);
constexpr OwnerMask kInToggleLists = LiveIn(O::ConfigMods, O::ConfigDLC);
constexpr OwnerMask kInCampTop = LiveIn(O::CampTop);
constexpr OwnerMask kInMap = LiveIn(O::Map);
constexpr OwnerMask kInMinigame = LiveIn(O::Minigame);
constexpr OwnerMask kInMechat = LiveIn(O::Mechat);
constexpr OwnerMask kInMoving = LiveIn(O::Field, O::Minigame);
constexpr OwnerMask kInLooking = LiveIn(O::Field, O::Map);
constexpr OwnerMask kInConfirm = kAllOwners & ~LiveIn(O::Field, O::Mechat);
constexpr OwnerMask kInCancel = kInConfirm & ~LiveIn(O::ScriptWindow);
constexpr OwnerMask kInSelect =
    kInCancel & ~LiveIn(O::MessWin, O::Battle, O::Map);
constexpr OwnerMask kInEvent = LiveIn(O::Event);

constexpr ActionDesc kDescs[kActionCount] = {
    {"confirm", "settings.action.confirm", ActionContext::General,
     kInConfirm,
     ActionKind::Mapped, 0, -1, AxisPair::None, "Space,LMB,PadA"},
    {"cancel", "settings.action.cancel", ActionContext::General,
     kInCancel,
     ActionKind::Mapped, 1, -1, AxisPair::None, "Escape,RMB,PadB"},
    {"nav_up", "settings.action.nav_up", ActionContext::General,
     kInSelect,
     ActionKind::Mapped, 10, -1, AxisPair::None, "Up,PadDUp,LStickUp"},
    {"nav_down", "settings.action.nav_down", ActionContext::General,
     kInSelect,
     ActionKind::Mapped, 11, -1, AxisPair::None,
     "Down,PadDDown,LStickDown"},
    {"nav_left", "settings.action.nav_left", ActionContext::General,
     kInSelect,
     ActionKind::Mapped, 12, -1, AxisPair::None,
     "Left,PadDLeft,LStickLeft"},
    {"nav_right", "settings.action.nav_right", ActionContext::General,
     kInSelect,
     ActionKind::Mapped, 13, -1, AxisPair::None,
     "Right,PadDRight,LStickRight"},
    {"next_page", "settings.action.next_page", ActionContext::General,
     kInMenus, ActionKind::Mapped, -1, 15, AxisPair::None, "Q,PadRB"},
    {"prev_page", "settings.action.prev_page", ActionContext::General,
     kInMenus, ActionKind::Mapped, -1, 14, AxisPair::None, "E,PadLB"},
    {"first_item", "settings.action.first_item", ActionContext::General,
     kInMenus, ActionKind::Mapped, -1, 12, AxisPair::None, "Z,Home,PadLT"},
    {"last_item", "settings.action.last_item", ActionContext::General,
     kInMenus, ActionKind::Mapped, -1, 13, AxisPair::None, "F,End,PadRT"},
    {"start", "settings.action.start", ActionContext::General, kInMenu,
     ActionKind::Mapped, -1, 4, AxisPair::None, "Return,PadStart"},
    {"next_leader", "settings.action.next_leader", ActionContext::CampMenu,
     kInCampTop, ActionKind::Mapped, -1, 15, AxisPair::None, "Q,PadRB"},
    {"prev_leader", "settings.action.prev_leader", ActionContext::CampMenu,
     kInCampTop, ActionKind::Mapped, -1, 14, AxisPair::None, "E,PadLB"},
    {"quit_menu", "settings.action.quit_menu", ActionContext::CampMenu,
     kInCampTop, ActionKind::Mapped, -1, 5, AxisPair::None, "Tab,PadBack"},

    {"interact", "settings.pad.interact", ActionContext::Field, kInField,
     ActionKind::Mapped, 4, -1, AxisPair::None, "Return,LMB,PadA"},
    {"attack", "settings.pad.dash_attack", ActionContext::Field, kInField,
     ActionKind::Mapped, 5, -1, AxisPair::None, "Space,RMB,PadX"},
    {"main_menu", "settings.action.camp_menu", ActionContext::Field,
     LiveIn(O::Field, O::CampTop, O::Menu), ActionKind::Mapped, 2, -1,
     AxisPair::None, "C,PadY"},
    {"field_menu", "settings.pad.field_menu", ActionContext::Field,
     LiveIn(O::Field, O::Encounter), ActionKind::Mapped, 3, -1, AxisPair::None,
     "F,PadRT"},
    {"world_map", "settings.pad.world_map", ActionContext::Field,
     LiveIn(O::Field, O::Map), ActionKind::Mapped, 6, -1, AxisPair::None,
     "M,PadStart"},
    {"field_skill_1", "settings.pad.field_skill_1", ActionContext::Field,
     kInField, ActionKind::Mapped, 7, -1, AxisPair::None, "Q,PadRB"},
    {"field_skill_2", "settings.pad.field_skill_2", ActionContext::Field,
     kInField, ActionKind::Mapped, 8, -1, AxisPair::None, "E,PadLB"},
    {"reset_camera", "settings.pad.reset_camera", ActionContext::Field,
     kInField, ActionKind::Mapped, 9, -1, AxisPair::None, "Z,PadLT"},
    {"move", "settings.pad.move", ActionContext::Field, kInMoving,
     ActionKind::Mapped, -1, -1, AxisPair::Left, "W,S,A,D,LStick"},
    {"view", "settings.action.look", ActionContext::Field, kInLooking,
     ActionKind::Mapped, -1, -1, AxisPair::Right, "Up,Down,Left,Right,RStick"},

    {"switch_side", "settings.action.switch_side", ActionContext::Battle,
     LiveIn(O::BattleTarget), ActionKind::Mapped, -1, 11, AxisPair::None,
     "C,PadY"},
    {"show_status", "settings.action.show_status", ActionContext::Battle,
     LiveIn(O::Battle, O::BattleTarget), ActionKind::Mapped, -1, 14,
     AxisPair::None, "E,PadLB"},

    {"zoom", "settings.action.zoom", ActionContext::MapMenu, kInMap,
     ActionKind::Mapped, -1, 14, AxisPair::None, "E,WheelDown,PadLB"},
    {"area_map", "settings.action.area_map", ActionContext::MapMenu, kInMap,
     ActionKind::Host, -1, -1, AxisPair::None, "F,PadRT"},
    {"zoom_out", "settings.action.zoom_out", ActionContext::MapMenu, kInMap,
     ActionKind::Host, -1, -1, AxisPair::None, "E,WheelDown,PadLB"},
    {"zoom_in", "settings.action.zoom_in", ActionContext::MapMenu, kInMap,
     ActionKind::Host, -1, -1, AxisPair::None, "Q,WheelUp,PadRB"},

    {"skip_scene", "settings.action.skip_scene", ActionContext::Misc,
     kInEvent, ActionKind::Mapped, -1, 4, AxisPair::None,
     "M,PadStart"},
    {"minigame_pause", "settings.action.minigame_pause", ActionContext::Misc,
     kInMinigame, ActionKind::Mapped, -1, 4, AxisPair::None, "M,PadStart"},
    {"start_battle", "settings.action.start_battle", ActionContext::Misc,
     LiveIn(O::Encounter), ActionKind::Mapped, -1, 4, AxisPair::None,
     "M,PadStart"},
    {"qte_action_1", "settings.action.qte_action_1", ActionContext::Misc,
     kInMinigame, ActionKind::Mapped, -1, 10, AxisPair::None, "Q,PadX",
     true},
    {"qte_action_2", "settings.action.qte_action_2", ActionContext::Misc,
     kInMinigame, ActionKind::Mapped, -1, 11, AxisPair::None, "C,PadY", true},
    {"voice_language", "settings.action.voice_language", ActionContext::Misc,
     kInMenu, ActionKind::Mapped, -1, 10, AxisPair::None, "Tab,PadX"},

    {"clear_binding", "settings.action.clear_binding", ActionContext::Config,
     kInConfigBinds, ActionKind::Host, -1, -1, AxisPair::None, "Delete,PadX"},
    {"reset_defaults", "settings.action.reset_defaults", ActionContext::Config,
     kInConfigBinds, ActionKind::Host, -1, -1, AxisPair::None, "Tab,PadBack"},
    {"delete_content", "settings.action.delete_content",
     ActionContext::Config, kInContentLists, ActionKind::Host, -1, -1,
     AxisPair::None, "Delete,PadX"},
    {"toggle_content", "settings.action.toggle_content",
     ActionContext::Config, kInToggleLists, ActionKind::Host, -1, -1,
     AxisPair::None, "C,PadY"},
    {"install_content", "settings.action.install_content",
     ActionContext::Config, kInContentLists, ActionKind::Host, -1, -1,
     AxisPair::None, "Tab,PadBack"},

    {"machine_gun", "settings.pad.machine_gun", ActionContext::Mechat,
     kInMechat, ActionKind::Mapped, 0, -1, AxisPair::None,
     "Space,LMB,PadA"},
    {"missile", "settings.pad.missile", ActionContext::Mechat, kInMechat,
     ActionKind::Mapped, 1, -1, AxisPair::None, "F,RMB,PadB"},
    {"turn_left", "settings.pad.turn_left", ActionContext::Mechat, kInMechat,
     ActionKind::Mapped, 3, -1, AxisPair::None, "E,PadLB,PadLT"},
    {"turn_right", "settings.pad.turn_right", ActionContext::Mechat,
     kInMechat, ActionKind::Mapped, 4, -1, AxisPair::None, "Q,PadRB,PadRT"},
    {"aim", "settings.pad.aim", ActionContext::Mechat, kInMechat,
     ActionKind::Mapped, -1, -1, AxisPair::Left, "W,S,A,D,LStick"},

    {"pause", "settings.action.pause", ActionContext::Misc, kInEvent,
     ActionKind::Dispatched, -1, -1, AxisPair::None, "P,PadX"},
    {"overlay", "settings.action.overlay", ActionContext::System, kAllOwners,
     ActionKind::Dispatched, -1, -1, AxisPair::None, "F3"},
    {"mindows", "settings.action.mindows", ActionContext::System, kAllOwners,
     ActionKind::Dispatched, -1, -1, AxisPair::None, "Slash,Ctrl+Alt+M"},
};

constexpr bool HostActionsTakeNoId() {
  for (const ActionDesc &desc : kDescs) {
    if (desc.kind == ActionKind::Host && (desc.slot >= 0 || desc.fixedId >= 0))
      return false;
  }
  return true;
}

constexpr bool SharedFixedIdsNeverLiveTogether() {
  for (int a = 0; a < kActionCount; ++a) {
    for (int b = a + 1; b < kActionCount; ++b) {
      const ActionDesc &x = kDescs[a];
      const ActionDesc &y = kDescs[b];
      if (x.fixedId >= 0 && x.fixedId == y.fixedId &&
          (x.owners & y.owners) != 0)
        return false;
    }
  }
  return true;
}

constexpr bool FixedPadsHaveFixedIds() {
  for (const ActionDesc &desc : kDescs) {
    if (desc.fixedPad && desc.fixedId < 0)
      return false;
  }
  return true;
}

constexpr bool NothingLiveInCapture() {
  for (const ActionDesc &desc : kDescs) {
    if (desc.owners & OwnerBit(InputOwner::Capture))
      return false;
  }
  return true;
}

constexpr bool MixedSection(ActionContext context) {
  return context == ActionContext::Misc || context == ActionContext::Config;
}

constexpr bool SectionsShareOwners() {
  for (int a = 0; a < kActionCount; ++a) {
    for (int b = a + 1; b < kActionCount; ++b) {
      const ActionDesc &x = kDescs[a];
      const ActionDesc &y = kDescs[b];
      if (x.context == y.context && !MixedSection(x.context) &&
          (x.owners & y.owners) == 0)
        return false;
    }
  }
  return true;
}

constexpr bool MenuSection(ActionContext context) {
  return context == ActionContext::General ||
         context == ActionContext::CampMenu ||
         context == ActionContext::MapMenu || context == ActionContext::Config;
}

constexpr bool GeneralReachesMenus() {
  for (const ActionDesc &desc : kDescs) {
    if (MenuSection(desc.context) && (desc.owners & kInCancel) == 0)
      return false;
  }
  return true;
}

struct SharedPair {
  Action a;
  Action b;
};

constexpr SharedPair kSharedByDesign[] = {
    {Action::Zoom, Action::ZoomOut},
};

constexpr std::span<const SharedPair> kMayShare = {};

constexpr std::string_view NextToken(std::string_view &list) {
  const size_t comma = list.find(',');
  const std::string_view token = list.substr(0, comma);
  list = comma == std::string_view::npos ? std::string_view()
                                         : list.substr(comma + 1);
  return token;
}

constexpr bool DefaultsShare(std::string_view x, std::string_view y) {
  while (!x.empty()) {
    const std::string_view token = NextToken(x);
    std::string_view rest = y;
    while (!rest.empty()) {
      if (NextToken(rest) == token && !token.empty())
        return true;
    }
  }
  return false;
}

constexpr bool Listed(std::span<const SharedPair> list, Action a, Action b) {
  for (const SharedPair &pair : list) {
    if ((pair.a == a && pair.b == b) || (pair.a == b && pair.b == a))
      return true;
  }
  return false;
}

constexpr bool SharedPairsMatchDefaults() {
  for (int a = 0; a < kActionCount; ++a) {
    for (int b = a + 1; b < kActionCount; ++b) {
      const ActionDesc &x = kDescs[a];
      const ActionDesc &y = kDescs[b];
      const bool shares = (x.owners & y.owners) != 0 &&
                          DefaultsShare(x.defaults, y.defaults);
      if (shares != Listed(kSharedByDesign, static_cast<Action>(a),
                           static_cast<Action>(b)))
        return false;
    }
  }
  return true;
}

constexpr bool MaySharePairsAreOptIn() {
  for (const SharedPair &pair : kMayShare) {
    const ActionDesc &x = kDescs[static_cast<int>(pair.a)];
    const ActionDesc &y = kDescs[static_cast<int>(pair.b)];
    if ((x.owners & y.owners) == 0 || DefaultsShare(x.defaults, y.defaults) ||
        Listed(kSharedByDesign, pair.a, pair.b))
      return false;
  }
  return true;
}

static_assert(kDescs[static_cast<int>(Action::Mindows)].context ==
              ActionContext::System);
static_assert(kDescs[static_cast<int>(Action::Aim)].axis == AxisPair::Left);
static_assert(kDescs[static_cast<int>(Action::QteAction2)].fixedPad);
static_assert(HostActionsTakeNoId());
static_assert(SharedFixedIdsNeverLiveTogether());
static_assert(FixedPadsHaveFixedIds());
static_assert(NothingLiveInCapture());
static_assert(SharedPairsMatchDefaults());
static_assert(MaySharePairsAreOptIn());
static_assert(SectionsShareOwners());
static_assert(GeneralReachesMenus());

} // namespace

const ActionDesc &Describe(Action action) {
  const int i = static_cast<int>(action);
  return kDescs[(i < 0 || i >= kActionCount) ? 0 : i];
}

const char *ToString(Action action) { return Describe(action).name; }

bool SharesByDesign(Action a, Action b) {
  return Listed(kSharedByDesign, a, b) || Listed(kMayShare, a, b);
}

} // namespace bd::engine
