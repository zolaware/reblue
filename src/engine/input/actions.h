#pragma once

#include <rex/types.h>

namespace bd::engine {

enum class ActionContext : u8 {
  Field,
  General,
  CampMenu,
  MapMenu,
  Battle,
  Misc,
  Config,
  Mechat,
  System,
};

enum class ActionKind : u8 { Mapped, Dispatched, Host };

enum class InputOwner : u8 {
  Field,
  Menu,
  CampTop,
  Encounter,
  BattleTarget,
  Battle,
  Map,
  Event,
  Minigame,
  Mechat,
  ConfigBinds,
  ConfigMods,
  ConfigDLC,
  ConfigLanguages,
  Config,
  ScriptWindow,
  MessWin,
  Capture,

  Count,
};

using OwnerMask = u32;

constexpr OwnerMask OwnerBit(InputOwner owner) {
  return static_cast<OwnerMask>(1u << static_cast<u32>(owner));
}

template <typename... Owners> constexpr OwnerMask LiveIn(Owners... owners) {
  return static_cast<OwnerMask>((OwnerBit(owners) | ...));
}

static_assert(static_cast<u32>(InputOwner::Count) <= 8 * sizeof(OwnerMask));

inline constexpr OwnerMask kAllOwners = static_cast<OwnerMask>(
    ((1u << static_cast<u32>(InputOwner::Count)) - 1u) &
    ~u32(OwnerBit(InputOwner::Capture)));

enum class AxisPair : i8 { None = -1, Left = 0, Right = 1 };

enum class Action : int {
  Confirm,
  Cancel,
  NavUp,
  NavDown,
  NavLeft,
  NavRight,
  NextPage,
  PrevPage,
  FirstItem,
  LastItem,
  NextLeader,
  PrevLeader,
  QuitMenu,

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

  SwitchSide,
  ShowStatus,

  Zoom,
  AreaMap,
  ZoomOut,
  ZoomIn,

  SkipScene,
  MinigamePause,
  StartBattle,
  QteAction1,
  QteAction2,
  VoiceLanguage,

  ClearBinding,
  ResetDefaults,
  DeleteContent,
  ToggleContent,
  InstallContent,

  MachineGun,
  Missile,
  TurnLeft,
  TurnRight,
  Aim,

  Pause,
  Overlay,
  Mindows,

  Count,
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);

inline bool InRange(Action action) {
  const int i = static_cast<int>(action);
  return i >= 0 && i < kActionCount;
}

struct ActionDesc {
  const char *name;
  const char *labelKey;
  ActionContext context;
  OwnerMask owners;
  ActionKind kind;
  i8 slot;
  i8 fixedId;
  AxisPair axis;
  const char *defaults;
  bool fixedPad = false;
};

const ActionDesc &Describe(Action action);
const char *ToString(Action action);

bool SharesByDesign(Action a, Action b);

} // namespace bd::engine
