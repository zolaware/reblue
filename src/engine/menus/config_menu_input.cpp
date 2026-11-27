/**
 * @file    engine/menus/config_menu_input.cpp
 * @brief   ConfigMenu per-state input handling.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_menu.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings_model.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime.h"
#include "engine/sfx.h"
#include "engine/game_options.h"
#include "engine/glyph_set.h"
#include "engine/input/binding_registry.h"
#include "engine/input/input_sources.h"
#include "engine/menus/config_layout.h"
#include "engine/menus/config_menu_data.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include <rex/types.h>

namespace bd::engine {

namespace {

int SelectableChip(const BindEntry &entry, int chip, int step) {
  for (int c = chip; c >= 0 && c < kBindChipCount; c += step) {
    if (!BindChipFixed(entry, c))
      return c;
  }
  return -1;
}

bool LeftButtonDown() {
  Source button;
  button.type = SourceKind::kKey;
  button.code = static_cast<u16>(rex::VirtualKey::kLButton);
  return InputSources::Get().Active(button);
}

int SettleChip(const BindEntry &entry, int chip) {
  if (const int down = SelectableChip(entry, chip, -1); down >= 0)
    return down;
  const int up = SelectableChip(entry, chip, 1);
  return up >= 0 ? up : 0;
}

} // namespace

// The pointer, not the cursor, says which list a click is meant for. Without
// this the sidebar and the list beside it disagree the moment the mouse crosses
// between them, and a click hits whichever row the other one was holding.
bool ConfigMenu::PointerHop() {
  // A slider being swept off the left of its row is still that row's drag,
  // not a reach for the sidebar.
  if (drag_row_ >= 0 || !MenuMouse::Get().MouseHasCursor()) {
    hop_blocked_ = false;
    return false;
  }

  int row = -1;
  f32 x = 0.0f;
  switch (state_) {
  case State::SECTION: {
    AnimeMenu *content = ContentMenu();
    if (!content || !content->PointerRowX(row, x)) {
      hop_blocked_ = false;
      return false;
    }
    if (hop_blocked_)
      return false;
    Transition(ContentState());
    return true;
  }
  case State::SETTINGS:
  case State::MODLIST:
  case State::DLCLIST:
  case State::LANGLIST:
  case State::ACHVLIST:
    if (!section_menu_.PointerRowX(row, x))
      return false;
    // Straight onto the row under the pointer, so the preview it opens is the
    // one being pointed at rather than the one the sidebar last held.
    section_menu_.SetCursorIndex(row);
    Transition(State::SECTION);
    return true;
  default:
    // A carried mod and a capture are holding the frame for something else.
    return false;
  }
}

void ConfigMenu::HandleSection() {
  if (CheckAction(Action::Confirm)) {
    const State next = SectionState(section_menu_.CursorIndex());
    if (next != State::SECTION)
      Transition(next);
    return;
  }

  if (CheckAction(Action::Cancel)) {
    if (DlcChanged() || LanguagesChanged() || settings_restart_dirty_)
      Transition(State::CONFIRM_REBOOT);
    else
      Transition(State::CLOSING);
  }
}

// Read-only, so B is the only input the list takes.
void ConfigMenu::HandleAchvlist() {
  if (CheckAction(Action::Cancel))
    Transition(State::SECTION);
}

// A bar row sweeps while a direction is held. bdInputCheckButton is edge-gated,
// so without this a long press moves one step and stops.
int ConfigMenu::HeldStep(int cursor) {
  const int held = ButtonHeld(Button::Left)    ? -1
                   : ButtonHeld(Button::Right) ? 1
                                               : 0;
  if (held != held_dir_) {
    held_dir_ = held;
    held_frames_ = 0;
    return 0;
  }
  if (!held)
    return 0;

  ++held_frames_;
  const auto ui = SettingsRowUi(settings_page_, cursor);
  if (ui != RowUi::Slider && ui != RowUi::SliderSteps)
    return 0;
  if (held_frames_ < kHeldRepeatDelay)
    return 0;
  return (held_frames_ - kHeldRepeatDelay) % kHeldRepeatInterval == 0 ? held
                                                                     : 0;
}

// The pointer names a value rather than a direction: an option button sets the
// option it is, and a point along a slider track sets the value it stands for.
bool ConfigMenu::SetRowFromPointer(int row, f32 x, bool dragging) {
    const auto page = settings_page_;
    if (row < 0 || row >= static_cast<int>(SettingsCount(page)))
        return false;

    if (SettingsDisabled(page, row)) {
        if (!dragging)
            sfx::Play(sfx::kDisabled);
        return false;
    }

    const auto ui = SettingsRowUi(page, row);
    bool changed = false;
    bool playSound = false;

    if (ui == RowUi::Slider || ui == RowUi::SliderSteps) {
        double fraction = 0.0;
        if (!SettingItemTemplate::SliderFractionAt(x, dragging, fraction))
            return false;

        if (ui == RowUi::Slider) {
            const double oldFrac = SettingsSliderFraction(page, row);
            const double lo = SettingsSliderMin(page, row);
            const double hi = SettingsSliderMax(page, row);
            changed = SetSliderValue(page, row, lo + fraction * (hi - lo));

            if (changed) {
                // Quantize continuous slider into 10 discrete audio steps (10% increments)
                constexpr int kAudioSteps = 10;
                const int oldStep = static_cast<int>(oldFrac * kAudioSteps + 0.5);
                const int newStep = static_cast<int>(fraction * kAudioSteps + 0.5);
                if (oldStep != newStep) {
                    playSound = true;
                }
            }
        }
        else {
            // Stepped slider
            const int count = SettingsOptionCount(page, row);
            const double oldFrac = SettingsSliderFraction(page, row);
            const int oldOption = static_cast<int>(oldFrac * (count - 1) + 0.5);
            const int targetOption = static_cast<int>(fraction * (count - 1) + 0.5);

            changed = count > 1 && SetSelectedOption(page, row, targetOption);
            if (changed && targetOption != oldOption) {
                playSound = true;
            }
        }
    }
    else if (ui == RowUi::Buttons) {
        if (dragging)
            return false;
        const int option =
            SettingItemTemplate::ButtonAt(x, SettingsOptionCount(page, row));
        if (option < 0)
            return false;
        changed = SetSelectedOption(page, row, option);
        if (changed) {
            playSound = true;
        }
    }
    else {
        return false;
    }

    if (changed) {
        if (playSound)
            sfx::Play(sfx::kCursor);

        settings_dirty_ = true;
        if (SettingsRestartBound(page, row))
            settings_restart_dirty_ = true;
    }
    return true;
}

void ConfigMenu::HandleSettings() {
  const auto page = settings_page_;
  const int slot = CurrentSettingsList().CursorIndex();

  // Section titles are entries like any other, since the engine bounds the
  // cursor by the entry count alone, so step over one in the direction the
  // cursor was traveling. The pointer stands down, as it does on the keybind
  // screen's spacer band: hover parks wherever the mouse is.
  if (SettingsSlotToRow(page, slot) < 0) {
    // A pointer parked on a title leaves the cursor there, so the way back out
    // has to be answered before the nudge returns.
    if (CheckAction(Action::Cancel)) {
      Transition(State::SECTION);
      return;
    }
    if (!MenuMouse::Get().MouseHasCursor()) {
      const int slots = static_cast<int>(SettingsSlotCount(page));
      const int to = last_settings_slot_ <= slot ? slot + 1 : slot - 1;
      if (to >= 0 && to < slots)
        CurrentSettingsList().SetCursorIndex(to);
    }
    return;
  }
  last_settings_slot_ = slot;

  const int cursor = SettingsSlotToRow(page, slot);

  int dir = 0;
  if (CheckButton(Button::Left))
    dir = -1;
  else if (CheckButton(Button::Right))
    dir = 1;

  const int repeat = HeldStep(cursor);
  if (dir == 0)
    dir = repeat;

  if (dir != 0) {
    if (SettingsDisabled(page, cursor)) {
        sfx::Play(sfx::kDisabled);
    }

    if (CycleSetting(page, cursor, dir)) {
      sfx::Play(sfx::kCursor);
      settings_dirty_ = true;
      if (SettingsRestartBound(page, cursor))
        settings_restart_dirty_ = true;
    }
    return;
  }

  // A latched slider follows the pointer for as long as confirm is held, so a
  // bar is dragged rather than clicked one position at a time. It keeps the
  // row it started on: the bands are 34px and a drag along one would otherwise
  // fall off it.
  const bool pointer = MenuMouse::Get().MouseHasCursor();
  const bool confirmDown = CheckAction(Action::Confirm);
  if (drag_row_ >= 0) {
    f32 x = 0.0f;
    if (pointer && ActionHeld(Action::Confirm) &&
        CurrentSettingsList().RowPointerX(drag_row_, x)) {
      SetRowFromPointer(SettingsSlotToRow(page, drag_row_), x, true);
      return;
    }
    drag_row_ = -1;
  }

  if (confirmDown) {
    // The row under the pointer, not the one the cursor holds: hover keeps the
    // two together a frame later than the click, and a click off every row is
    // spent on nothing.
    int hitSlot = slot;
    f32 x = 0.0f;
    if (pointer && !CurrentSettingsList().PointerRowX(hitSlot, x))
      return;
    const int row = SettingsSlotToRow(page, hitSlot);
    if (row < 0)
      return;

    if (SettingsRowUi(page, row) == RowUi::Action) {
      if (SettingsDisabled(page, row)) {
          sfx::Play(sfx::kDisabled);
          return;
      }
      if (SettingsRowAction(page, row) == SettingAction::Keybinds) {
        bind_chip_ = Glyphs::Get().Resolved() == GlyphSet::Keyboard
                         ? 0
                         : kBindPadChip;
        Transition(State::KEYBINDS);
      }
      return;
    }

    // A click acts on the control under it and on nothing else, so missing one
    // does nothing at all. A press that reports no pointer is a pad press, and
    // steps the row forward the way Right does.
    if (pointer) {
      if (SetRowFromPointer(row, x, false)) {
        const auto ui = SettingsRowUi(page, row);
        if (ui == RowUi::Slider || ui == RowUi::SliderSteps)
          drag_row_ = hitSlot;
      }
      return;
    }

    // Guard: Do not step or cycle when pressing 'A' on sliders
    const auto ui = SettingsRowUi(page, row);
    if (ui == RowUi::Slider || ui == RowUi::SliderSteps) {
        return;
    }

    if (SettingsDisabled(page, row)) {
        sfx::Play(sfx::kDisabled);
        return;
    }

    if (CycleSetting(page, row, 1)) {
      sfx::Play(sfx::kOpen);
      settings_dirty_ = true;
      if (SettingsRestartBound(page, row))
        settings_restart_dirty_ = true;
    }
    return;
  }

  if (CheckAction(Action::Cancel))
    Transition(State::SECTION);
}

int ConfigMenu::FirstBindSlot() const {
  constexpr int kCols = 2;
  const int slots = BindGridRows() * kCols;
  for (int col = 0; col < kCols; ++col)
    for (int slot = col; slot < slots; slot += kCols)
      if (BindAt(slot).Bindable())
        return slot;
  return 0;
}

void ConfigMenu::ShowBindPage(int page) {
  if (page == bind_page_ || page < 0 || page >= kBindPageCount)
    return;
  bind_page_ = page;
  bind_menu_.SetCursorIndex(FirstBindSlot());
  last_bind_slot_ = bind_menu_.CursorIndex();
  conflict_shown_ = false;
  sfx::Play(sfx::kCursor);
  RefreshKeybindVisuals();
  SetKeybindChrome(nullptr);
  GetLayout().SyncVars(task_.AnimeData());
}

bool ConfigMenu::HandleBindTabs() {
  if (CheckAction(Action::PrevPage)) {
    ShowBindPage((bind_page_ + kBindPageCount - 1) % kBindPageCount);
    return true;
  }
  if (CheckAction(Action::NextPage)) {
    ShowBindPage((bind_page_ + 1) % kBindPageCount);
    return true;
  }
  f32 x = 0.0f, y = 0.0f;
  if (!MenuMouse::Get().MouseHasCursor() || !CursorInMenuSpace(x, y))
    return false;
  const int tab = ConfigLayout::BindTabAt(x, y);
  if (tab < 0 || !CheckAction(Action::Confirm))
    return false;
  ShowBindPage(tab);
  return true;
}

bool ConfigMenu::SkipBindSpacer() {
  const int slot = bind_menu_.CursorIndex();
  if (BindAt(slot).Bindable())
    return false;

  constexpr int kCols = 2;
  const int slots = BindGridRows() * kCols;
  const int delta = slot - last_bind_slot_;
  int to = -1;
  if (delta != 0 && delta % kCols == 0) {
    int step = delta > 0 ? kCols : -kCols;
    if (std::abs(delta) > kCols)
      step = -step;
    for (int s = slot + step; s >= 0 && s < slots && to < 0; s += step)
      if (BindAt(s).Bindable())
        to = s;
  } else {
    for (int d = kCols; d < slots && to < 0; d += kCols) {
      if (slot - d >= 0 && BindAt(slot - d).Bindable())
        to = slot - d;
      else if (slot + d < slots && BindAt(slot + d).Bindable())
        to = slot + d;
    }
  }
  if (to < 0)
    to = last_bind_slot_;
  bind_menu_.SetCursorIndex(to);
  last_bind_slot_ = to;
  return true;
}

void ConfigMenu::HandleKeybinds() {
  AnimeMenu &list = bind_menu_;
  const int step = CheckAction(Action::NavRight)  ? 1
                   : CheckAction(Action::NavLeft) ? -1
                                                  : 0;
  if (step != 0)
    MenuMouse::Get().SetMouseHasCursor(false);
  const bool pointer = MenuMouse::Get().MouseHasCursor();

  int cursor = list.CursorIndex();
  const BindEntry was = BindAt(last_bind_slot_);
  if (step != 0 && !pointer && was.Bindable()) {
    const int chip = SelectableChip(was, bind_chip_ + step, step);
    if (chip >= 0) {
      bind_chip_ = chip;
      if (cursor != last_bind_slot_) {
        list.SetCursorIndex(last_bind_slot_);
        cursor = last_bind_slot_;
      }
    } else if (cursor != last_bind_slot_) {
      bind_chip_ = step > 0 ? 0 : kBindPadChip;
    }
  }

  const BindEntry current = BindAt(cursor);
  if (current.Bindable()) {
    last_bind_slot_ = cursor;
    bind_chip_ = SettleChip(current, bind_chip_);
  } else if (!pointer && SkipBindSpacer()) {
    return;
  }

  int hoverRow = -1, hoverChip = -1;
  f32 hoverX = 0.0f;
  if (pointer && list.PointerRowX(hoverRow, hoverX))
    hoverChip = KeybindItemTemplate::ChipAt(hoverX);
  const BindEntry hovered = BindAt(hoverRow);
  const bool hovering = hovered.Bindable() && hoverChip >= 0;
  if (hovering && !BindChipFixed(hovered, hoverChip))
    bind_chip_ = hoverChip;

  if (HandleBindTabs())
    return;

  if (CheckAction(Action::Confirm)) {
    if (pointer && !hovering && LeftButtonDown())
      return;
    const int slot = hovering ? hoverRow : cursor;
    const int chip = hovering ? hoverChip : bind_chip_;
    const BindEntry entry = BindAt(slot);
    if (!entry.Bindable())
      return;
    if (BindChipFixed(entry, chip)) {
      sfx::Play(sfx::kDisabled);
      return;
    }
    capture_slot_ = slot;
    capture_chip_ = chip;
    conflict_shown_ = false;
    bd::platform::BeginKeyCapture();
    Transition(State::KEYBIND_CAPTURE);
    return;
  }

  if (CheckAction(Action::ClearBinding)) {
    const BindEntry &entry = hovering ? hovered : current;
    const int chip = hovering ? hoverChip : bind_chip_;
    if (entry.Bindable() && ClearBindChip(entry, chip))
      settings_dirty_ = true;
    return;
  }

  if (CheckAction(Action::ResetDefaults)) {
    Transition(State::CONFIRM_RESET_BINDS);
    return;
  }

  if (CheckAction(Action::Cancel))
    Transition(State::SETTINGS);
}

void ConfigMenu::HandleModlist() {
  if (CheckAction(Action::Confirm)) {
    Transition(State::REORDER);
    return;
  }

  if (CheckAction(Action::ToggleContent)) {
    const int cursor = modlist_menu_.CursorIndex();
    FlipMod(cursor);
    dirty_ = true;
    REXBD_DEBUG("config toggled mod[{}]", cursor);
    return;
  }

  if (CheckAction(Action::DeleteContent) && ModCount() > 0) {
    delete_index_ = modlist_menu_.CursorIndex();
    delete_kind_ = DeleteKind::Mod;
    Transition(State::CONFIRM_DELETE);
    return;
  }

  if (CheckAction(Action::InstallContent)) {
    if (InstallMod()) {
      // Same as DLC install: list structure is baked into the generated CSV,
      // so a count change needs a menu restart. Destroy() saves and reloads.
      dirty_ = true;
      resume_state_ = State::MODLIST;
      wants_restart_ = true;
      Transition(State::CLOSING);
    }
    return;
  }

  if (CheckAction(Action::Cancel))
    Transition(State::SECTION);
}

void ConfigMenu::HandleDLCList() {
  if (CheckAction(Action::ToggleContent) && DlcCount() > 0) {
    const int cursor = dlclist_menu_.CursorIndex();
    ToggleDLC(cursor);
    RefreshDLCVisuals();
    REXBD_DEBUG("config toggled dlc[{}]", cursor);
    return;
  }

  if (CheckAction(Action::DeleteContent) && DlcCount() > 0) {
    delete_index_ = dlclist_menu_.CursorIndex();
    delete_kind_ = DeleteKind::DLC;
    Transition(State::CONFIRM_DELETE);
    return;
  }

  if (CheckAction(Action::InstallContent)) {
    if (InstallDLC()) {
      // Row count and item template are baked into the generated CSV at task
      // load, so a count change requires a menu restart to pick them up.
      resume_state_ = State::DLCLIST;
      wants_restart_ = true;
      Transition(State::CLOSING);
    }
    return;
  }

  if (CheckAction(Action::Cancel))
    Transition(State::SECTION);
}

void ConfigMenu::HandleLangList() {
#ifdef REBLUE_BUILD_INSTALLER
  if (CheckAction(Action::DeleteContent)) {
    const int cursor = langlist_menu_.CursorIndex();
    if (cursor >= 0 && cursor < static_cast<int>(LanguageCount()) &&
        LanguageRemovable(cursor)) {
      delete_index_ = cursor;
      delete_kind_ = DeleteKind::Language;
      Transition(State::CONFIRM_DELETE);
    }
    return;
  }

  if (CheckAction(Action::InstallContent)) {
    lang_prompt_.clear();
    Transition(State::LANGADD);
    return;
  }
#endif

  if (CheckAction(Action::Cancel))
    Transition(State::SECTION);
}

void ConfigMenu::HandleLangAdd() {
  if (!confirm_popup_.Poll())
    return;

  if (!confirm_popup_.Confirmed()) {
    confirm_popup_.Kill();
    ClearLanguageSources();
    Transition(State::LANGLIST);
    return;
  }

  confirm_popup_.Kill();

  std::string detail;
  switch (AddLanguageSources(detail)) {
  case LanguageAddResult::Picked:
    lang_pick_ = 0;
    lang_accept_.assign(LanguageOfferCount(), false);
    Transition(State::LANGPICK);
    break;
  case LanguageAddResult::Missing:
    lang_prompt_ = detail;
    Transition(State::LANGADD);
    break;
  case LanguageAddResult::Canceled:
    Transition(State::LANGLIST);
    break;
  case LanguageAddResult::NothingNew:
    lang_notice_ = i18n::Text("menu.language.nothing_new");
    Transition(State::LANGNOTICE);
    break;
  case LanguageAddResult::Failed:
    lang_notice_ = i18n::Fmt("menu.language.failed", detail);
    Transition(State::LANGNOTICE);
    break;
  }
}

void ConfigMenu::HandleLangPick() {
  if (!confirm_popup_.Poll())
    return;

  const bool yes = confirm_popup_.Confirmed();
  const bool canceled = confirm_popup_.Canceled();
  confirm_popup_.Kill();

  if (canceled) {
    ClearLanguageSources();
    REXBD_DEBUG("config language add canceled at question {}", lang_pick_);
    Transition(State::LANGLIST);
    return;
  }

  const int offers = static_cast<int>(LanguageOfferCount());
  if (lang_pick_ < offers) {
    lang_accept_[static_cast<size_t>(lang_pick_)] = yes;
    ++lang_pick_;
    Transition(State::LANGPICK);
    return;
  }

  bool any = yes;
  for (bool on : lang_accept_)
    any = any || on;

  if (!any) {
    ClearLanguageSources();
    lang_notice_ = i18n::Text("menu.language.nothing_new");
    Transition(State::LANGNOTICE);
    return;
  }

  std::string detail;
  if (!StartLanguageJob(lang_accept_, yes, detail)) {
    lang_notice_ = i18n::Fmt("menu.language.failed", detail);
    Transition(State::LANGNOTICE);
    return;
  }

  Transition(State::LANGJOB);
}

void ConfigMenu::HandleLangJob() {
  std::string error;
  switch (PollLanguageJob(error)) {
  case LanguageJobOutcome::Running:
    if (CheckAction(Action::Cancel) && LanguageJobCancelable())
      RequestLanguageJobCancel();
    break;
  case LanguageJobOutcome::Canceled:
    Transition(State::LANGLIST);
    break;
  case LanguageJobOutcome::Failed:
    lang_notice_ = i18n::Fmt("menu.language.failed", error);
    Transition(State::LANGNOTICE);
    break;
  case LanguageJobOutcome::Done:
    resume_state_ = State::LANGLIST;
    wants_restart_ = true;
    Transition(State::CLOSING);
    break;
  }
}

void ConfigMenu::HandleLangNotice() {
  if (CheckAction(Action::Confirm) || CheckAction(Action::Cancel))
    Transition(State::LANGLIST);
}

void ConfigMenu::HandleKeybindCapture() {
  const std::string token = bd::platform::PollKeyCapture();
  if (!token.empty()) {
    const BindEntry entry = BindAt(capture_slot_);
    std::string conflict;
    if (!BindChipAccepts(capture_chip_, token)) {
      sfx::Play(sfx::kDisabled);
    } else if (SetBindChip(entry, capture_chip_, token, &conflict)) {
      settings_dirty_ = true;
      conflict_shown_ = !conflict.empty();
      conflict_label_ = std::move(conflict);
    }
    capture_slot_ = -1;
    capture_chip_ = -1;
    Transition(State::KEYBINDS);
    return;
  }

  if (bd::platform::KeyCaptureCanceled()) {
    capture_slot_ = -1;
    capture_chip_ = -1;
    Transition(State::KEYBINDS);
    REXBD_DEBUG("config rebind canceled");
  }
}

void ConfigMenu::HandleReorder() {
  const int cursor = modlist_menu_.CursorIndex();

  if (cursor != reorder_origin_) {
    ReorderMod(reorder_origin_, cursor);
    reorder_origin_ = cursor;
    dirty_ = true;
    REXBD_DEBUG("config reorder: swapped to position {}", cursor);
  }

  if (CheckAction(Action::Confirm)) {
    Transition(State::MODLIST);
    REXBD_DEBUG("config reorder confirmed at position {}", cursor);
    return;
  }

  if (CheckAction(Action::Cancel)) {
    Transition(State::MODLIST);
    REXBD_DEBUG("config reorder canceled");
  }
}

void ConfigMenu::HandleConfirmDelete() {
  if (!confirm_popup_.Poll())
    return;

  const State list = delete_kind_ == DeleteKind::DLC        ? State::DLCLIST
                     : delete_kind_ == DeleteKind::Language ? State::LANGLIST
                                                            : State::MODLIST;

  if (!confirm_popup_.Confirmed()) {
    confirm_popup_.Kill();
    REXBD_DEBUG("config delete canceled");
    Transition(list);
    return;
  }

  confirm_popup_.Kill();

  if (delete_kind_ == DeleteKind::Language) {
    if (!RemoveLanguage(delete_index_)) {
      REXBD_ERROR("config language remove failed for [{}]", delete_index_);
      Transition(State::LANGLIST);
      return;
    }
    Transition(State::LANGJOB);
    return;
  }

  const bool ok = delete_kind_ == DeleteKind::DLC ? DeleteDLC(delete_index_)
                                                  : RemoveMod(delete_index_);
  if (!ok) {
    REXBD_ERROR("config {} delete failed for [{}]", DeleteKindName(delete_kind_),
             delete_index_);
    Transition(list);
    return;
  }

  REXBD_DEBUG("config deleted {}[{}]", DeleteKindName(delete_kind_),
           delete_index_);
  wants_restart_ = true;
  resume_state_ = list;
  Transition(State::CLOSING);
}

void ConfigMenu::HandleConfirmReboot() {
  if (!confirm_popup_.Poll())
    return;

  const bool confirmed = confirm_popup_.Confirmed();
  confirm_popup_.Kill();

  if (confirmed) {
    // Persist mod changes before the relaunch. RequestWarmReboot saves the
    // cvars, and DLC is already on disk.
    if (dirty_) {
      SaveAndReload();
      dirty_ = false;
    }
    bd::platform::RequestWarmReboot();
    REXBD_DEBUG("config reboot confirmed");
  } else {
    REXBD_DEBUG("config reboot declined");
  }

  // CLOSING is a no-op once the relaunch proceeds. It is the path taken when
  // the reboot was declined or could not start.
  Transition(State::CLOSING);
}

void ConfigMenu::HandleConfirmResetBinds() {
  if (!confirm_popup_.Poll())
    return;

  const bool confirmed = confirm_popup_.Confirmed();
  confirm_popup_.Kill();

  if (confirmed) {
    if (ResetAllKeybinds())
      settings_dirty_ = true;
    REXBD_DEBUG("config binds reset to defaults");
  } else {
    REXBD_DEBUG("config bind reset declined");
  }

  Transition(State::KEYBINDS);
}

} // namespace bd::engine
