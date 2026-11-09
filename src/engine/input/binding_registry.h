#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hid/bindings.h>
#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

using Source = rex::hid::InputSource;
using SourceKind = rex::hid::InputSourceKind;

class Bindings {
public:
  static Bindings &Get();

  void Init();

  // Adds the bind.* cvars to the registry and watches them. The registry keeps
  // the values a config file or the command line carried for them until then,
  // and the set reads them here.
  void Register(rex::cvar::ICvarRegistry &registry);
  void Unregister();

  // Seeds bind.* from the retired hid.keybind_* cvars and the controller type.
  // It reads the shipped button rows out of guest memory, so it runs once the
  // title is up, after Register.
  void Migrate();

  std::span<const Source> Sources(Action action) const;

  bool SetSource(Action action, int slot, const Source &source);
  bool SetSources(Action action, std::span<const Source> sources);
  bool ClearSources(Action action);
  bool Reset(Action action);
  bool ResetContext(ActionContext context);

  std::optional<Action> Conflict(Action action, const Source &source) const;

  u32 Generation() const { return set_ ? set_->GetGeneration() : 0; }

  void ReportParseErrors();

private:
  Bindings() = default;

  std::array<rex::hid::BindingDesc, kActionCount> descs_;
  std::array<std::string, kActionCount> descriptions_;
  std::unique_ptr<rex::hid::BindingSet> set_;
  bool migrated_ = false;
};

} // namespace bd::engine
