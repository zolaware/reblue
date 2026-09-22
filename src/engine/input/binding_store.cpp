#include "engine/input/binding_store.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/ui/keybinds.h>

#include "core/logging.h"
#include "engine/input/button_map.h"
#include "platform/platform.h"

REXCVAR_DECLARE(i32, bd_opt_ctl_normal_type);

namespace bd::engine {

namespace {

constexpr const char *kControlTypeCvar = "bd_opt_ctl_normal_type";

struct PadKeybind {
  u16 code;
  const char *cvar;
};

constexpr PadKeybind kPadKeybinds[] = {
    {0, "keybind_dpad_up"},          {1, "keybind_dpad_down"},
    {2, "keybind_dpad_left"},        {3, "keybind_dpad_right"},
    {4, "keybind_start"},            {5, "keybind_back"},
    {6, "keybind_lstick_press"},     {7, "keybind_rstick_press"},
    {8, "keybind_a"},                {9, "keybind_b"},
    {10, "keybind_x"},               {11, "keybind_y"},
    {12, "keybind_left_trigger"},    {13, "keybind_right_trigger"},
    {14, "keybind_left_shoulder"},   {15, "keybind_right_shoulder"},
    {16, "keybind_lstick_up"},       {17, "keybind_lstick_down"},
    {18, "keybind_lstick_right"},    {19, "keybind_lstick_left"},
    {20, "keybind_rstick_up"},       {21, "keybind_rstick_down"},
    {22, "keybind_rstick_right"},    {23, "keybind_rstick_left"},
};

std::string OldKeys(int padButton) {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (static_cast<int>(bind.code) == padButton)
      return rex::cvar::GetFlagByName(bind.cvar);
  }
  return {};
}

bool StoredKeys(int padButton) {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (static_cast<int>(bind.code) == padButton)
      return rex::cvar::HasNonDefaultValue(bind.cvar);
  }
  return false;
}

bool AnyStoredKeys() {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (rex::cvar::HasNonDefaultValue(bind.cvar))
      return true;
  }
  return false;
}

bool MouseSource(const Source &source) {
  if (source.kind == SourceKind::MouseButton ||
      source.kind == SourceKind::MouseWheel)
    return true;
  if (source.kind != SourceKind::Key)
    return false;
  const auto vk = static_cast<rex::ui::VirtualKey>(source.code);
  return vk == rex::ui::VirtualKey::kLButton ||
         vk == rex::ui::VirtualKey::kRButton ||
         vk == rex::ui::VirtualKey::kMButton ||
         vk == rex::ui::VirtualKey::kXButton1 ||
         vk == rex::ui::VirtualKey::kXButton2;
}

int DefaultPadButton(const std::vector<Source> &sources) {
  for (const Source &source : sources) {
    if (source.kind == SourceKind::PadButton)
      return static_cast<int>(source.code);
  }
  return -1;
}

struct NamedCode {
  const char *name;
  SourceKind kind;
  u16 code;
  i8 sign;
};

constexpr NamedCode kNamed[] = {
    {"WheelUp", SourceKind::MouseWheel, 0, 1},
    {"WheelDown", SourceKind::MouseWheel, 0, -1},
    {"MouseXY", SourceKind::MouseAxes, u16(AxisPair::Left), 0},
    {"LStick", SourceKind::PadAxes, u16(AxisPair::Left), 0},
    {"RStick", SourceKind::PadAxes, u16(AxisPair::Right), 0},
    {"PadDUp", SourceKind::PadButton, 0, 0},
    {"PadDDown", SourceKind::PadButton, 1, 0},
    {"PadDLeft", SourceKind::PadButton, 2, 0},
    {"PadDRight", SourceKind::PadButton, 3, 0},
    {"PadStart", SourceKind::PadButton, 4, 0},
    {"PadBack", SourceKind::PadButton, 5, 0},
    {"PadLS", SourceKind::PadButton, 6, 0},
    {"PadRS", SourceKind::PadButton, 7, 0},
    {"PadA", SourceKind::PadButton, 8, 0},
    {"PadB", SourceKind::PadButton, 9, 0},
    {"PadX", SourceKind::PadButton, 10, 0},
    {"PadY", SourceKind::PadButton, 11, 0},
    {"PadLT", SourceKind::PadButton, 12, 0},
    {"PadRT", SourceKind::PadButton, 13, 0},
    {"PadLB", SourceKind::PadButton, 14, 0},
    {"PadRB", SourceKind::PadButton, 15, 0},
    {"LStickUp", SourceKind::PadButton, 16, 0},
    {"LStickDown", SourceKind::PadButton, 17, 0},
    {"LStickRight", SourceKind::PadButton, 18, 0},
    {"LStickLeft", SourceKind::PadButton, 19, 0},
    {"RStickUp", SourceKind::PadButton, 20, 0},
    {"RStickDown", SourceKind::PadButton, 21, 0},
    {"RStickRight", SourceKind::PadButton, 22, 0},
    {"RStickLeft", SourceKind::PadButton, 23, 0},
};

u8 TakeModifiers(std::string_view &token) {
  u8 mods = 0;
  for (;;) {
    if (token.starts_with("Shift+")) {
      mods |= platform::KeyboardInput::kModShift;
      token.remove_prefix(6);
    } else if (token.starts_with("Ctrl+")) {
      mods |= platform::KeyboardInput::kModCtrl;
      token.remove_prefix(5);
    } else if (token.starts_with("Alt+")) {
      mods |= platform::KeyboardInput::kModAlt;
      token.remove_prefix(4);
    } else {
      return mods;
    }
  }
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

std::string CvarName(Action action) {
  return std::string("bind_") + Describe(action).name;
}

bool SameSource(const Source &a, const Source &b) {
  return a.kind == b.kind && a.code == b.code && a.mods == b.mods &&
         a.sign == b.sign;
}

void ApplyList(std::vector<Source> &out, std::vector<std::string> &errors,
                Action action, std::string_view value) {
  out.clear();
  const ActionDesc &desc = Describe(action);
  u8 buttonSlot = 0;
  size_t pos = 0;
  while (pos <= value.size()) {
    const size_t comma = value.find(',', pos);
    const std::string_view raw = value.substr(
        pos,
        comma == std::string_view::npos ? std::string_view::npos : comma - pos);
    const std::string_view token = Trim(raw);
    if (token.empty()) {
      if (comma == std::string_view::npos)
        break;
      if (desc.axis != AxisPair::None && buttonSlot < kMaxAxisButtons)
        ++buttonSlot;
      pos = comma + 1;
      continue;
    }

    Source source;
    if (!ParseSource(token, source)) {
      errors.push_back(std::string("[input] ") + ToString(action) +
                        " bind token '" + std::string(token) +
                        "' failed to parse");
      if (comma == std::string_view::npos)
        break;
      pos = comma + 1;
      continue;
    }

    const bool buttonShaped = source.kind != SourceKind::MouseAxes &&
                               source.kind != SourceKind::PadAxes;
    if (desc.axis != AxisPair::None && buttonShaped) {
      if (buttonSlot >= kMaxAxisButtons) {
        errors.push_back(std::string("[input] ") + ToString(action) +
                          " drops '" + std::string(token) +
                          "', its axis row already has " +
                          std::to_string(kMaxAxisButtons) + " button halves");
        if (comma == std::string_view::npos)
          break;
        pos = comma + 1;
        continue;
      }
      source.axisSlot = ++buttonSlot;
    }
    out.push_back(source);

    if (comma == std::string_view::npos)
      break;
    pos = comma + 1;
  }
}

constexpr int kAxisKeyButtons[2][4] = {
    {16, 17, 19, 18},
    {20, 21, 23, 22},
};

bool AxisKeys(AxisPair pair, std::string &out) {
  const int which = static_cast<int>(pair);
  if (which < 0 || which > 1)
    return false;
  out.clear();
  bool stored = false;
  for (const int button : kAxisKeyButtons[which]) {
    const std::string value = OldKeys(button);
    const std::string_view token =
        Trim(std::string_view(value).substr(0, value.find(',')));
    if (token.empty())
      return false;
    stored = stored || StoredKeys(button);
    if (!out.empty())
      out += ",";
    out += token;
  }
  return stored;
}

std::string FormatList(const std::vector<Source> &sources) {
  std::string halves[kMaxAxisButtons];
  int lastHalf = 0;
  std::string formatted;
  for (const auto &source : sources) {
    if (source.axisSlot == 0 || source.axisSlot > kMaxAxisButtons ||
        !FormatSource(source, formatted))
      continue;
    halves[source.axisSlot - 1] = formatted;
    lastHalf = std::max(lastHalf, int(source.axisSlot));
  }

  std::string out;
  bool first = true;
  const auto append = [&](const std::string &token) {
    if (!first)
      out += ",";
    out += token;
    first = false;
  };
  for (int i = 0; i < lastHalf; ++i)
    append(halves[i]);
  for (const auto &source : sources) {
    if (source.axisSlot == 0 && FormatSource(source, formatted))
      append(formatted);
  }
  return out;
}

} // namespace

bool ParseSource(std::string_view token, Source &out) {
  out = Source{};
  out.mods = TakeModifiers(token);
  if (token.empty())
    return false;

  for (const NamedCode &n : kNamed) {
    if (token == n.name) {
      out.kind = n.kind;
      out.code = n.code;
      out.sign = n.sign;
      return true;
    }
  }

  const auto vk = rex::ui::ParseVirtualKey(token);
  if (vk == rex::ui::VirtualKey::kNone)
    return false;
  out.kind = SourceKind::Key;
  out.code = static_cast<u16>(vk);
  return true;
}

bool FormatSource(const Source &source, std::string &out) {
  out.clear();

  std::string prefix;
  if (source.mods & platform::KeyboardInput::kModCtrl)
    prefix += "Ctrl+";
  if (source.mods & platform::KeyboardInput::kModAlt)
    prefix += "Alt+";
  if (source.mods & platform::KeyboardInput::kModShift)
    prefix += "Shift+";

  for (const NamedCode &n : kNamed) {
    if (n.kind != source.kind || n.code != source.code)
      continue;
    if (n.kind == SourceKind::MouseWheel && n.sign != source.sign)
      continue;
    out = prefix + n.name;
    return true;
  }

  if (source.kind == SourceKind::Key) {
    const auto vk = static_cast<rex::ui::VirtualKey>(source.code);
    const std::string key = rex::ui::VirtualKeyToString(vk);
    if (key.empty())
      return false;
    out = prefix + key;
    return true;
  }

  return false;
}

Bindings &Bindings::Get() {
  static Bindings instance;
  return instance;
}

void Bindings::Init() {
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);

    ApplyList(sources_[i], parseErrors_, action, desc.defaults);

    rex::cvar::RegisterFlag(rex::cvar::FlagEntry{
        .name = CvarName(action),
        .type = rex::cvar::FlagType::String,
        .category = "Input/Binds",
        .description =
            std::string("Inputs bound to the '") + desc.name + "' action",
        .setter =
            [this, i](std::string_view v) {
              ApplyList(sources_[i], parseErrors_, static_cast<Action>(i), v);
              ++generation_;
              return true;
            },
        .getter = [this, i]() { return FormatList(sources_[i]); },
        .default_value = desc.defaults,
    });
  }
  ++generation_;
}

void Bindings::Migrate() {
  if (migrated_)
    return;

  for (int i = 0; i < kActionCount; ++i) {
    if (rex::cvar::HasNonDefaultValue(CvarName(static_cast<Action>(i)))) {
      migrated_ = true;
      return;
    }
  }

  int type = 0;
  bool haveType = rex::cvar::HasNonDefaultValue(kControlTypeCvar);
  if (haveType) {
    type = REXCVAR_GET(bd_opt_ctl_normal_type);
    haveType = type > 0 && type < ButtonMap::kControlTypes;
  }
  const bool haveKeys = AnyStoredKeys();
  if (!haveType && !haveKeys) {
    migrated_ = true;
    return;
  }

  int rowId[kActionCount];
  bool rowRead = false;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    rowId[i] = -1;
    if (!haveType || desc.context == ActionContext::Mechat || desc.slot < 0 ||
        desc.fixedId >= 0)
      continue;
    rowId[i] = ButtonMap::ShippedId(action, type);
    rowRead = rowRead || rowId[i] >= 0;
  }
  if (haveType && !rowRead)
    return;
  migrated_ = true;

  int seeded = 0;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    if (desc.kind != ActionKind::Mapped)
      continue;

    std::string value;
    if (desc.axis != AxisPair::None) {
      if (!AxisKeys(desc.axis, value))
        continue;
      for (const Source &source : sources_[i]) {
        if (source.kind != SourceKind::PadAxes &&
            source.kind != SourceKind::MouseAxes)
          continue;
        std::string token;
        if (FormatSource(source, token))
          value += "," + token;
      }
    } else {
      const int stock = desc.fixedId >= 0 ? int(desc.fixedId)
                                          : DefaultPadButton(sources_[i]);
      const int chosen = rowId[i] >= 0 ? rowId[i] : stock;
      if (chosen < 0)
        continue;
      if (chosen == stock && !StoredKeys(chosen))
        continue;

      std::vector<Source> keys;
      ApplyList(keys, parseErrors_, action, OldKeys(chosen));
      for (const Source &source : keys) {
        if (source.kind != SourceKind::Key || MouseSource(source))
          continue;
        std::string token;
        if (!FormatSource(source, token))
          continue;
        if (!value.empty())
          value += ",";
        value += token;
      }
      for (const Source &source : sources_[i]) {
        if (source.kind == SourceKind::Key && !MouseSource(source))
          continue;
        Source moved = source;
        if (source.kind == SourceKind::PadButton && int(source.code) == stock)
          moved.code = static_cast<u16>(chosen);
        std::string token;
        if (!FormatSource(moved, token))
          continue;
        if (!value.empty())
          value += ",";
        value += token;
      }
    }
    rex::cvar::SetFlagByName(CvarName(action), value);
    ++seeded;
  }
  ReportParseErrors();
  if (!seeded)
    return;

  const std::string from =
      haveType ? "controller type " + std::to_string(type) + " and its keys"
               : "the keys it had bound";
  BD_INFO("[input] profile migrated: {} binds seeded from {}", seeded, from);
}

const std::vector<Source> &Bindings::Sources(Action action) const {
  const int i = static_cast<int>(action);
  return sources_[(i < 0 || i >= kActionCount) ? 0 : i];
}

bool Bindings::SetSource(Action action, int slot, const Source &source) {
  if (slot < 0)
    return false;

  std::string formatted;
  if (!FormatSource(source, formatted))
    return false;

  std::vector<Source> updated = sources_[static_cast<int>(action)];
  if (static_cast<size_t>(slot) < updated.size())
    updated[static_cast<size_t>(slot)] = source;
  else
    updated.push_back(source);
  return SetSources(action, updated);
}

bool Bindings::SetSources(Action action, const std::vector<Source> &sources) {
  const bool ok =
      rex::cvar::SetFlagByName(CvarName(action), FormatList(sources));
  ReportParseErrors();
  return ok;
}

bool Bindings::ClearSources(Action action) {
  const bool ok = rex::cvar::SetFlagByName(CvarName(action), "");
  ReportParseErrors();
  return ok;
}

bool Bindings::Reset(Action action) {
  rex::cvar::ResetToDefault(CvarName(action));
  ReportParseErrors();
  return true;
}

bool Bindings::ResetContext(ActionContext context) {
  bool any = false;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (Describe(action).context != context)
      continue;
    rex::cvar::ResetToDefault(CvarName(action));
    any = true;
  }
  ReportParseErrors();
  return any;
}

std::optional<Action> Bindings::Conflict(Action action,
                                         const Source &source) const {
  const ActionContext context = Describe(action).context;
  for (int i = 0; i < kActionCount; ++i) {
    const auto other = static_cast<Action>(i);
    if (other == action || Describe(other).context != context)
      continue;
    for (const auto &s : sources_[i]) {
      if (SameSource(s, source))
        return other;
    }
  }
  return std::nullopt;
}

void Bindings::ReportParseErrors() {
  for (const auto &msg : parseErrors_)
    BD_WARN("{}", msg);
  parseErrors_.clear();
}

} // namespace bd::engine
