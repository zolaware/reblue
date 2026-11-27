#include "engine/input/binding_registry.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/input_keys.h>
#include <rex/settings_registry.h>
#include <rex/string/text.h>

#include "core/cvar_table.h"
#include "core/logging.h"
#include "engine/input/button_map.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

constexpr const char *kControlTypeCvar = "bd.opt_ctl_normal_type";

struct PadKeybind {
  u16 code;
  const char *cvar;
};

constexpr PadKeybind kPadKeybinds[] = {
    {0, "hid.keybind_dpad_up"},          {1, "hid.keybind_dpad_down"},
    {2, "hid.keybind_dpad_left"},        {3, "hid.keybind_dpad_right"},
    {4, "hid.keybind_start"},            {5, "hid.keybind_back"},
    {6, "hid.keybind_lstick_press"},     {7, "hid.keybind_rstick_press"},
    {8, "hid.keybind_a"},                {9, "hid.keybind_b"},
    {10, "hid.keybind_x"},               {11, "hid.keybind_y"},
    {12, "hid.keybind_left_trigger"},    {13, "hid.keybind_right_trigger"},
    {14, "hid.keybind_left_shoulder"},   {15, "hid.keybind_right_shoulder"},
    {16, "hid.keybind_lstick_up"},       {17, "hid.keybind_lstick_down"},
    {18, "hid.keybind_lstick_right"},    {19, "hid.keybind_lstick_left"},
    {20, "hid.keybind_rstick_up"},       {21, "hid.keybind_rstick_down"},
    {22, "hid.keybind_rstick_right"},    {23, "hid.keybind_rstick_left"},
};

std::string OldKeys(int padButton) {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (static_cast<int>(bind.code) == padButton)
      return CvarGet(bind.cvar);
  }
  return {};
}

bool StoredKeys(int padButton) {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (static_cast<int>(bind.code) == padButton)
      return CvarIsSet(bind.cvar);
  }
  return false;
}

bool AnyStoredKeys() {
  for (const PadKeybind &bind : kPadKeybinds) {
    if (CvarIsSet(bind.cvar))
      return true;
  }
  return false;
}

bool MouseSource(const Source &source) {
  if (source.type == SourceKind::kMouseWheel)
    return true;
  if (source.type != SourceKind::kKey)
    return false;
  const auto vk = static_cast<rex::VirtualKey>(source.code);
  return vk == rex::VirtualKey::kLButton ||
         vk == rex::VirtualKey::kRButton ||
         vk == rex::VirtualKey::kMButton ||
         vk == rex::VirtualKey::kXButton1 ||
         vk == rex::VirtualKey::kXButton2;
}

int DefaultPadButton(std::span<const Source> sources) {
  for (const Source &source : sources) {
    if (source.type == SourceKind::kPadButton)
      return static_cast<int>(source.code);
  }
  return -1;
}

constexpr std::string_view kRetiredMouseLook = "MouseXY";

bool CanConflict(Action a, Action b) {
  return (Describe(a).owners & Describe(b).owners) != 0 &&
         !SharesByDesign(a, b);
}

std::string CvarName(Action action) {
  return std::string("bind.") + Describe(action).name;
}

rex::PadAxes ToPadAxes(AxisPair axis) {
  switch (axis) {
  case AxisPair::Left:
    return rex::PadAxes::kLeft;
  case AxisPair::Right:
    return rex::PadAxes::kRight;
  case AxisPair::None:
    break;
  }
  return rex::PadAxes::kNone;
}

// A plain list from a retired cvar, with the same folding of repeats the set
// applies to a bind.
std::vector<Source> ParseOldList(Action action, std::string_view value) {
  std::vector<Source> out;
  size_t pos = 0;
  while (pos <= value.size()) {
    const size_t comma = value.find(',', pos);
    const bool last = comma == std::string_view::npos;
    const std::string_view token = rex::string::Trim(
        value.substr(pos, last ? std::string_view::npos : comma - pos));
    pos = last ? value.size() + 1 : comma + 1;
    if (token.empty() || token == kRetiredMouseLook)
      continue;
    const auto parsed = bd::platform::HID().ParseInputSource(token);
    if (!parsed) {
      REXBD_WARN("[input] {} bind token '{}' failed to parse",
                  ToString(action), token);
      continue;
    }
    const bool repeat =
        std::any_of(out.begin(), out.end(), [&](const Source &have) {
          return rex::IsSameInput(have, *parsed);
        });
    if (!repeat)
      out.push_back(*parsed);
  }
  return out;
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
        rex::string::Trim(std::string_view(value).substr(0, value.find(',')));
    if (token.empty())
      return false;
    stored = stored || StoredKeys(button);
    if (!out.empty())
      out += ",";
    out += token;
  }
  return stored;
}

} // namespace

Bindings &Bindings::Get() {
  static Bindings instance;
  return instance;
}

void Bindings::Init(rex::hid::IInputSystem &input) {
  input_ = &input;
  for (int i = 0; i < kActionCount; ++i)
    descriptions_[i] = std::string("Inputs bound to the '") +
                       Describe(static_cast<Action>(i)).name + "' action";
}

void Bindings::Register() {
  if (!input_)
    return;
  rex::SettingsRegistry &registry = rex::GetSettingsRegistry();
  size_t count = 0;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const std::string name = CvarName(action);
    slot_[i] = -1;
    if (registry.IsAlias(name) || registry.Find(name)) {
      REXBD_ERROR("[input] {} not registered, the name is already taken",
                  name);
      continue;
    }
    const ActionDesc &desc = Describe(action);
    slot_[i] = static_cast<int>(count);
    descs_[count++] = {.name = desc.name,
                       .defaults = desc.defaults,
                       .description = descriptions_[i],
                       .group = static_cast<u32>(desc.context),
                       .axes = ToPadAxes(desc.axis)};
  }
  set_ = input_->CreateBindingSet(
      std::span<const rex::hid::BindingConfig>(descs_.data(), count), "bind");
  if (auto added = set_->Register(registry); !added) {
    REXBD_ERROR("[input] binds not registered: {}", added.error().message);
    set_.reset();
  }
}

int Bindings::Slot(Action action) const {
  const int i = static_cast<int>(action);
  return set_ && i >= 0 && i < kActionCount ? slot_[i] : -1;
}

void Bindings::Unregister() {
  if (!set_)
    return;
  set_->Unregister();
  // The set must not outlive the input system, and this singleton does.
  set_.reset();
  for (auto &sources : frame_)
    sources.clear();
  frame_generation_ = 0;
}

void Bindings::Migrate() {
  if (migrated_)
    return;

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (Slot(action) >= 0 && CvarIsSet(CvarName(action))) {
      migrated_ = true;
      return;
    }
  }

  int type = 0;
  bool haveType = CvarIsSet(kControlTypeCvar);
  if (haveType) {
    type = bd::cvars::Get().opt_ctl_normal_type.Get();
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
    if (desc.kind == ActionKind::Dispatched || Slot(action) < 0)
      continue;

    std::string value;
    if (desc.axis != AxisPair::None) {
      if (!AxisKeys(desc.axis, value))
        continue;
      for (const Source &source : Sources(action)) {
        if (source.type != SourceKind::kPadAxes)
          continue;
        const std::string token = bd::platform::HID().InputSourceToString(source);
        if (!token.empty())
          value += "," + token;
      }
    } else {
      const int stock = desc.fixedId >= 0
                            ? int(desc.fixedId)
                            : DefaultPadButton(Sources(action));
      const int chosen = rowId[i] >= 0 ? rowId[i] : stock;
      if (chosen < 0)
        continue;
      if (chosen == stock && !StoredKeys(chosen))
        continue;

      for (const Source &source : ParseOldList(action, OldKeys(chosen))) {
        if (source.type != SourceKind::kKey || MouseSource(source))
          continue;
        const std::string token = bd::platform::HID().InputSourceToString(source);
        if (token.empty())
          continue;
        if (!value.empty())
          value += ",";
        value += token;
      }
      for (const Source &source : Sources(action)) {
        if (source.type == SourceKind::kKey && !MouseSource(source))
          continue;
        Source moved = source;
        if (source.type == SourceKind::kPadButton && int(source.code) == stock)
          moved.code = static_cast<u16>(chosen);
        const std::string token = bd::platform::HID().InputSourceToString(moved);
        if (token.empty())
          continue;
        if (!value.empty())
          value += ",";
        value += token;
      }
    }
    CvarSet(CvarName(action), value);
    ++seeded;
  }
  ReportParseErrors();
  if (!seeded)
    return;

  const std::string from =
      haveType ? "controller type " + std::to_string(type) + " and its keys"
               : "the keys it had bound";
  REXBD_INFO("input profile migrated: {} binds seeded from {}", seeded, from);
}

std::vector<Source> Bindings::Sources(Action action) const {
  const int slot = Slot(action);
  return slot < 0 ? std::vector<Source>() : set_->GetSources(size_t(slot));
}

std::span<const Source> Bindings::FrameSources(Action action) const {
  const int i = static_cast<int>(action);
  return frame_[(i < 0 || i >= kActionCount) ? 0 : size_t(i)];
}

void Bindings::Reload() {
  if (!set_)
    return;
  const u32 generation = set_->GetGeneration();
  if (generation == frame_generation_)
    return;
  for (size_t i = 0; i < kActionCount; ++i)
    frame_[i] = Sources(static_cast<Action>(i));
  frame_generation_ = generation;
}

bool Bindings::SetSource(Action action, int slot, const Source &source) {
  const int at = Slot(action);
  if (slot < 0 || at < 0)
    return false;
  const bool ok =
      set_->SetSource(size_t(at), static_cast<size_t>(slot), source);
  ReportParseErrors();
  return ok;
}

bool Bindings::SetSources(Action action, std::span<const Source> sources) {
  const int at = Slot(action);
  if (at < 0)
    return false;
  const bool ok = set_->SetSources(size_t(at), sources);
  ReportParseErrors();
  return ok;
}

bool Bindings::Reset(Action action) {
  const int at = Slot(action);
  if (at < 0)
    return false;
  const bool ok = set_->Reset(size_t(at));
  ReportParseErrors();
  return ok;
}

bool Bindings::ResetContext(ActionContext context) {
  if (!set_)
    return false;
  const bool ok = set_->ResetGroup(static_cast<u32>(context));
  ReportParseErrors();
  return ok;
}

std::vector<Action> Bindings::Conflicts(Action action,
                                        const Source &source) const {
  std::vector<Action> out;
  for (int i = 0; i < kActionCount; ++i) {
    const auto other = static_cast<Action>(i);
    if (other == action || !CanConflict(action, other))
      continue;
    for (const Source &held : Sources(other)) {
      if (rex::IsSameInput(held, source)) {
        out.push_back(other);
        break;
      }
    }
  }
  return out;
}

// The retired mouse look token is dropped silently, as it always was.
void Bindings::ReportParseErrors() {
  if (!set_)
    return;
  for (const std::string &msg : set_->TakeParseErrors()) {
    if (msg.find(kRetiredMouseLook) != std::string::npos)
      continue;
    REXBD_WARN("[input] {}", msg);
  }
}

} // namespace bd::engine
