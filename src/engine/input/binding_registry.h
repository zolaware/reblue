#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

enum class SourceKind : u8 {
  Key,
  MouseButton,
  MouseWheel,
  PadButton,
  PadAxes,
};

struct Source {
  SourceKind kind = SourceKind::Key;
  u16 code = 0;
  u8 mods = 0;
  i8 sign = 0;
  u8 axisSlot = 0;

  bool operator==(const Source &o) const {
    return kind == o.kind && code == o.code && mods == o.mods &&
           sign == o.sign && axisSlot == o.axisSlot;
  }
};

inline constexpr int kAxisDirections = 4;
inline constexpr int kAxisKeysPerDirection = 2;
inline constexpr int kMaxAxisButtons = kAxisDirections * kAxisKeysPerDirection;

constexpr u8 AxisSlot(int direction, int alternate) {
  return static_cast<u8>(alternate * kAxisDirections + direction + 1);
}

constexpr int AxisDirection(u8 axisSlot) {
  return (axisSlot - 1) % kAxisDirections;
}

bool ParseSource(std::string_view token, Source &out);

bool FormatSource(const Source &source, std::string &out);

class Bindings {
public:
  static Bindings &Get();

  void Init();

  // Adds the bind.* cvars to the registry and watches them. The registry keeps
  // the values a config file or the command line carried for them until then.
  void Register(rex::cvar::ICvarRegistry &registry);
  void Unregister(rex::cvar::ICvarRegistry &registry);

  void Migrate();

  const std::vector<Source> &Sources(Action action) const;

  bool SetSource(Action action, int slot, const Source &source);
  bool SetSources(Action action, const std::vector<Source> &sources);
  bool ClearSources(Action action);
  bool Reset(Action action);
  bool ResetContext(ActionContext context);

  std::optional<Action> Conflict(Action action, const Source &source) const;

  u32 Generation() const { return generation_; }

  void ReportParseErrors();

private:
  Bindings() = default;

  std::vector<Source> sources_[kActionCount];
  std::string names_[kActionCount];
  std::string descriptions_[kActionCount];
  std::unique_ptr<rex::cvar::Var<std::string>> vars_[kActionCount];
  std::vector<const rex::cvar::VarBase *> table_;
  std::vector<rex::cvar::CallbackHandle> callbacks_;
  u32 generation_ = 0;
  bool migrated_ = false;
  std::vector<std::string> parseErrors_;
};

} // namespace bd::engine
