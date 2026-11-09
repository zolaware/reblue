#include "engine/input/binding_registry.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/hid/input_source.h>
#include <rex/hid/virtual_key.h>
#include <rex/string.h>

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
  if (source.kind == SourceKind::kMouseWheel)
    return true;
  if (source.kind != SourceKind::kKey)
    return false;
  const auto vk = static_cast<rex::hid::VirtualKey>(source.code);
  return vk == rex::hid::VirtualKey::kLButton ||
         vk == rex::hid::VirtualKey::kRButton ||
         vk == rex::hid::VirtualKey::kMButton ||
         vk == rex::hid::VirtualKey::kXButton1 ||
         vk == rex::hid::VirtualKey::kXButton2;
}

int DefaultPadButton(std::span<const Source> sources) {
  for (const Source &source : sources) {
    if (source.kind == SourceKind::kPadButton)
      return static_cast<int>(source.code);
  }
  return -1;
}

constexpr std::string_view kRetiredMouseLook = "MouseXY";

std::string CvarName(Action action) {
  return std::string("bind.") + Describe(action).name;
}

rex::hid::PadAxes ToPadAxes(AxisPair axis) {
  switch (axis) {
  case AxisPair::Left:
    return rex::hid::PadAxes::kLeft;
  case AxisPair::Right:
    return rex::hid::PadAxes::kRight;
  case AxisPair::None:
    break;
  }
  return rex::hid::PadAxes::kNone;
}

// A plain list from a retired cvar, with the same folding of repeats the set
// applies to a bind.
std::vector<Source> ParseOldList(Action action, std::string_view value) {
  std::vector<Source> out;
  size_t pos = 0;
  while (pos <= value.size()) {
    const size_t comma = value.find(',', pos);
    const bool last = comma == std::string_view::npos;
    const std::string_view token = rex::Trim(
        value.substr(pos, last ? std::string_view::npos : comma - pos));
    pos = last ? value.size() + 1 : comma + 1;
    if (token.empty() || token == kRetiredMouseLook)
      continue;
    const auto parsed = rex::hid::ParseInputSource(token);
    if (!parsed) {
      REXLOG_WARN(bd, "[input] {} bind token '{}' failed to parse",
                  ToString(action), token);
      continue;
    }
    const bool repeat =
        std::any_of(out.begin(), out.end(), [&](const Source &have) {
          return rex::hid::IsSameInput(have, *parsed);
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
        rex::Trim(std::string_view(value).substr(0, value.find(',')));
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

void Bindings::Init() {
  for (int i = 0; i < kActionCount; ++i) {
    const ActionDesc &desc = Describe(static_cast<Action>(i));
    descriptions_[i] =
        std::string("Inputs bound to the '") + desc.name + "' action";
    descs_[i] = {.name = desc.name,
                 .defaults = desc.defaults,
                 .description = descriptions_[i],
                 .group = static_cast<u32>(desc.context),
                 .axes = ToPadAxes(desc.axis)};
  }
  set_ = std::make_unique<rex::hid::BindingSet>(descs_);
}

void Bindings::Register(rex::cvar::ICvarRegistry &registry) {
  if (auto added = set_->Register(registry); !added)
    REXLOG_WARN(bd, "input binds not registered: {}", added.error().message);
}

void Bindings::Unregister() { set_->Unregister(); }

void Bindings::Migrate() {
  if (migrated_)
    return;

  for (int i = 0; i < kActionCount; ++i) {
    if (CvarIsSet(CvarName(static_cast<Action>(i)))) {
      migrated_ = true;
      return;
    }
  }

  int type = 0;
  bool haveType = CvarIsSet(kControlTypeCvar);
  if (haveType) {
    type = REXCVAR_GET(bd::cvars::opt_ctl_normal_type);
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
      for (const Source &source : set_->GetSources(i)) {
        if (source.kind != SourceKind::kPadAxes)
          continue;
        const std::string token = rex::hid::InputSourceToString(source);
        if (!token.empty())
          value += "," + token;
      }
    } else {
      const int stock = desc.fixedId >= 0
                            ? int(desc.fixedId)
                            : DefaultPadButton(set_->GetSources(i));
      const int chosen = rowId[i] >= 0 ? rowId[i] : stock;
      if (chosen < 0)
        continue;
      if (chosen == stock && !StoredKeys(chosen))
        continue;

      for (const Source &source : ParseOldList(action, OldKeys(chosen))) {
        if (source.kind != SourceKind::kKey || MouseSource(source))
          continue;
        const std::string token = rex::hid::InputSourceToString(source);
        if (token.empty())
          continue;
        if (!value.empty())
          value += ",";
        value += token;
      }
      for (const Source &source : set_->GetSources(i)) {
        if (source.kind == SourceKind::kKey && !MouseSource(source))
          continue;
        Source moved = source;
        if (source.kind == SourceKind::kPadButton && int(source.code) == stock)
          moved.code = static_cast<u16>(chosen);
        const std::string token = rex::hid::InputSourceToString(moved);
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
  REXLOG_INFO(bd, "input profile migrated: {} binds seeded from {}", seeded, from);
}

std::span<const Source> Bindings::Sources(Action action) const {
  const int i = static_cast<int>(action);
  return set_->GetSources((i < 0 || i >= kActionCount) ? 0 : size_t(i));
}

bool Bindings::SetSource(Action action, int slot, const Source &source) {
  if (slot < 0)
    return false;
  const bool ok = set_->SetSource(static_cast<size_t>(action),
                                  static_cast<size_t>(slot), source);
  ReportParseErrors();
  return ok;
}

bool Bindings::SetSources(Action action, std::span<const Source> sources) {
  const bool ok = set_->SetSources(static_cast<size_t>(action), sources);
  ReportParseErrors();
  return ok;
}

bool Bindings::ClearSources(Action action) {
  const bool ok = set_->Clear(static_cast<size_t>(action));
  ReportParseErrors();
  return ok;
}

bool Bindings::Reset(Action action) {
  const bool ok = set_->Reset(static_cast<size_t>(action));
  ReportParseErrors();
  return ok;
}

bool Bindings::ResetContext(ActionContext context) {
  const bool ok = set_->ResetGroup(static_cast<u32>(context));
  ReportParseErrors();
  return ok;
}

std::optional<Action> Bindings::Conflict(Action action,
                                         const Source &source) const {
  if (const auto other =
          set_->FindConflict(static_cast<size_t>(action), source))
    return static_cast<Action>(*other);
  return std::nullopt;
}

// The retired mouse look token is dropped silently, as it always was.
void Bindings::ReportParseErrors() {
  for (const std::string &msg : set_->TakeParseErrors()) {
    if (msg.find(kRetiredMouseLook) != std::string::npos)
      continue;
    REXLOG_WARN(bd, "[input] {}", msg);
  }
}

} // namespace bd::engine
