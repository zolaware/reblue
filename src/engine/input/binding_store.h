#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/types.h>

#include "engine/input/actions.h"

namespace bd::engine {

enum class SourceKind : u8 {
  Key,
  MouseButton,
  MouseWheel,
  MouseAxes,
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
  u32 generation_ = 0;
  bool migrated_ = false;
  std::vector<std::string> parseErrors_;
};

} // namespace bd::engine
