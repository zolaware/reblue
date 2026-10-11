#pragma once

#include <rex/types.h>

namespace bd::engine {

enum class ActionContext : u8 { Field, Menu, Mechat, System };

enum class ActionKind : u8 { Mapped, Dispatched };

enum class AxisPair : i8 { None = -1, Left = 0, Right = 1 };

enum class Action : int {
  Confirm,
  Cancel,
  NavUp,
  NavDown,
  NavLeft,
  NavRight,

  Interact,
  Attack,
  MainMenu,
  FieldMenu,
  WorldMap,
  FieldSkill1,
  FieldSkill2,
  ResetCamera,
  Move,
  View,

  MachineGun,
  Missile,
  TurnLeft,
  TurnRight,
  Aim,

  Back,
  StickPressLeft,
  StickPressRight,

  Pause,
  Mindows,

  Count,
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);

struct ActionDesc {
  const char *name;
  const char *labelKey;
  ActionContext context;
  ActionKind kind;
  i8 slot;
  i8 fixedId;
  AxisPair axis;
  const char *defaults;
};

const ActionDesc &Describe(Action action);
const char *ToString(Action action);

} // namespace bd::engine
