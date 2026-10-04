/**
 * @file    engine/menus/achievements_menu.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/achievements_menu.h"

#include "core/i18n.h"
#include "core/logging.h"
#include "core/text_wrap.h"
#include "engine/achievements/achievement_icon_tex.h"
#include "engine/achievements/achievement_list.h"
#include "engine/menus/achievements_layout.h"
#include "engine/sfx.h"

#include <format>
#include <string>
#include <vector>

#include <rex/hook.h>

namespace bd::engine {

namespace {

constexpr const char *kMenuMount = "ui:achievements-menu";

// The description runs the full width of the list it sits under: 600px at the
// 15px font's ~8px advance. Wrapping is by glyph count, so this is short of
// what fits by enough to absorb a line of unusually wide ones.
constexpr int kDescWrapChars = 72;

// Last resort only: the wait is on the transition's own anime frame, so this
// bounds the case where the anime is not advancing at all, which means it is
// not drawing either and there is nothing to wait for.
constexpr int kIntroFrameBudget = 45;

// How long the right-hand pane takes to arrive once the strip has settled.
// stock record screens fade their own content in over the tail of the
// transition. Ours cannot, because the transition and the list both draw the
// strip and overlapping them would blend it twice.
constexpr float kFadeFrames = 6.0f;

} // namespace

void RegisterAchievementsVFS() {
  // Exactly once per process: FileSystem::Add bumps the overlay revision, and
  // the next engine directory enumeration then rebuilds the whole index (120k
  // keys across the shipped packs), costing a second at the Encyclopedia open.
  // Nothing is lost by registering once, since the providers below run at read
  // time, so each CSV is regenerated from its live layout object every time.
  static bool registered = false;
  if (registered)
    return;

  // One VFS key per icon, so the catalog has to be known here. Two of the three
  // callers reach this before anything reads the list, and an empty catalog
  // means the runtime is not up yet, so leave it unregistered and retry.
  const auto &rows = GetAchievementList().empty() ? RefreshAchievementList()
                                                  : GetAchievementList();
  if (rows.empty()) {
    BD_WARN("[achv] catalog empty, deferring VFS registration");
    return;
  }

  // Served from the stock Encyclopedia directory so the screen's relative
  // references, dia_str_xx.u16 and res\icon_* and the row template, resolve the
  // same way the disc's own sibling CSVs do.
  LayoutMount mount("d2anime\\camp\\dia\\");
  mount.Add("l_dia_ach.csv", &AchievementsLayout::Get())
      .Add("s_dia_top_ach.csv", &AchievementsTransitionLayout::Get())
      .Add("l_dia_ach_row.csv", &AchievementRowTemplate::Screen())
      .Add("l_dia_ach_tab.csv", &AchievementsRowLayout::Get())
      .Add("l_dia_ach_prev.csv", &AchievementsPreviewLayout::Get());

  // Beside every CSV that draws it, since a tex path resolves against the CSV
  // that names it. Decoded on first read, so this costs nothing until a list
  // is opened.
  for (const auto &dir : kAchievementIconDirs)
    mount.AddRaw(AchievementIconAtlasVFSKey(dir), [] {
      const std::vector<u8> *blob = GetAchievementIconAtlas();
      return blob ? *blob : std::vector<u8>();
    });

  mount.Publish(kMenuMount);
  registered = true;
}

void AchievementsMenu::Preload(const Task &parent) {
  if (task_)
    return;

  if (!intro_) {
    // Before the CSVs are generated: the row count is baked into the menu
    // widget, and the transition names our own strip row.
    i18n::SyncLocale();

    const auto &rows = RefreshAchievementList();
    AchievementsLayout::Get().SetRowCount(rows.size());

    auto &intro = AchievementsTransitionLayout::Get();
    intro.title.set(i18n::Text("achv.title"));
    intro.rowLabel.set(i18n::Text("achv.entry"));

    RegisterAchievementsVFS();

    intro_ = D2AnimeTask::Load(parent, "d2anime\\camp\\dia\\s_dia_top_ach.csv",
                               D2AnimeTask::Reveal::Held);
    if (!intro_) {
      BD_WARN("[achv] transition load failed, the screen will pop in");
    } else {
      intro.SyncVars(intro_.AnimeData());
      // The list screen waits for the next call: eleven child anime arrived
      // on this frame already.
      return;
    }
  }

  task_ = D2AnimeTask::Load(parent, "d2anime\\camp\\dia\\l_dia_ach.csv",
                            D2AnimeTask::Reveal::Held);
  if (!task_) {
    BD_ERROR("[achv] LoadAsync failed");
    intro_.Kill();
    intro_ = D2AnimeTask();
    return;
  }
  // A fresh screen needs its menu found and its entries built again.
  list_menu_ = AnimeMenu();

  // Decoded here rather than on the frame the list first draws: the row
  // template names the atlas, so 49 PNG decodes plus a 512x1024 tiling pass
  // would otherwise hit the handover from the transition.
  GetAchievementIconAtlas();

  BD_DEBUG("[achv] preloaded: list 0x{:08X}, transition 0x{:08X}",
           task_.Address(), intro_.Address());
}

void AchievementsMenu::Create(const Task &parent) {
  state_ = State::INTRO;
  active_ = false;
  cursor_.Reset();
  intro_frames_ = 0;
  fade_ = 0.0f;

  // Twice: Preload takes one screen per call, and opening before the frame it
  // would have reached the list on must not leave us without one.
  Preload(parent);
  Preload(parent);
  if (!task_)
    return;

  // Camp::Diary::MainTask's Exit has just hidden L_dia.csv, which owns the
  // left column, the header, the divider and the record strip, so the
  // transition has to be up the same frame or all four pop out at once. It is
  // already parsed, so this only starts its clock.
  if (intro_)
    intro_.SetVisibleAndPlay(true);

  active_ = true;
  BD_DEBUG("[achv] opened, child task at 0x{:08X}", task_.Address());
}

void AchievementsMenu::Close() {
  // Both are left loaded and list_menu_ with them, so the next visit skips
  // DiscoverMenu's entry build too. Preload only rebuilds them when the Items
  // task itself goes away and Abandon drops the handles.
  intro_.SetVisibleAndPlay(false);
  task_.SetVisibleAndPlay(false);
  if (list_menu_)
    list_menu_.SetVisibleAndPlay(false);

  state_ = State::INTRO;
  active_ = false;
  cursor_.Reset();
  intro_frames_ = 0;
  fade_ = 0.0f;

  BD_DEBUG("[achv] closed");
}

// Not gated on active_: the handles outlive a closed viewer, and a stale one
// would block the reload for the whole of the next Encyclopedia visit.
void AchievementsMenu::Abandon() {
  intro_ = D2AnimeTask();
  task_ = D2AnimeTask();
  list_menu_ = AnimeMenu();
  state_ = State::INTRO;
  active_ = false;
  cursor_.Reset();
  intro_frames_ = 0;
  fade_ = 0.0f;

  BD_DEBUG("[achv] abandoned with the host task");
}

// The engine's own transition state waits on the finished flag, which AnimeData
// raises only for an anime with a length, and ours parses as endless. The
// anime frame counter tracks the animation instead, and it counts drawn
// frames, so it holds at any refresh rate.
bool AchievementsMenu::IntroFinished() {
  if (!intro_)
    return true;
  if (intro_.AnimTime() >=
      static_cast<float>(AchievementsTransitionLayout::kEndFrame))
    return true;
  if (intro_.IsAnimFinished())
    return true;
  return ++intro_frames_ >= kIntroFrameBudget;
}

// The same screen played backwards, which is how the engine leaves every stock
// record screen: Exit seeks the transition to its last frame and flips the rate
// negative (0x822FA3A8 and friends). Frame 1 is the top screen's own picture,
// so handing back to the engine there matches exactly what Enter(1) draws.
void AchievementsMenu::StartOutro() {
  state_ = State::OUTRO;
  intro_frames_ = 0;
  fade_ = 0.0f;

  list_menu_.SetVisibleAndPlay(false);
  task_.SetVisibleAndPlay(false);

  if (!intro_) {
    state_ = State::CLOSING;
    return;
  }

  intro_speed_ = intro_.AnimSpeed();
  // SetVisibleAndPlay rewinds to frame 1 and resets the chains, so the seek has
  // to follow it, not precede it.
  intro_.SetVisibleAndPlay(true);
  intro_.SetAnimTime(
      static_cast<float>(AchievementsTransitionLayout::kEndFrame));
  intro_.SetAnimSpeed(-intro_speed_);

  BD_DEBUG("[achv] outro from anime frame {} at speed {}",
           intro_.AnimTime(), intro_.AnimSpeed());
}

// Stopped here rather than by the engine: AnimeData__AdvanceFrame only clamps a
// reversed timeline at frame 1 when the anime has a length, and ours is
// endless, so it would keep counting down into negative frames.
bool AchievementsMenu::OutroFinished() {
  if (!intro_)
    return true;
  if (intro_.AnimTime() <= 1.0f)
    return true;
  return ++intro_frames_ >= kIntroFrameBudget;
}

bool AchievementsMenu::DiscoverMenu() {
  if (list_menu_)
    return true;
  if (!task_ || !task_.IsReady())
    return false;

  list_menu_ = task_.FindMenu("AchvList");
  if (!list_menu_)
    return false;

  // Seeded a row at a time rather than through SeedEntries, because the
  // enabled flag has to be right here and not only in RefreshVisuals:
  // RebuildVisibleItems and UpdateTemplateVisuals both run later in the same
  // engine pass, so a row scrolled into view would draw one frame in
  // EnableColor first.
  const auto &rows = GetAchievementList();
  for (size_t i = 0; i < rows.size(); ++i)
    list_menu_.AddEntryData(static_cast<int>(i), rows[i].unlocked);

  BD_DEBUG("[achv] discovered list at 0x{:08X}", list_menu_.Address());
  return true;
}

void AchievementsMenu::Transition(State next) {
  state_ = next;
  cursor_.Reset();

  auto &layout = AchievementsLayout::Get();
  layout.title.set(i18n::Text("achv.title"));
  // The redrawn record strip names our own row from the same key the top
  // screen's sixth row uses.
  layout.rowLabel.set(i18n::Text("achv.entry"));

  const AchievementSummary s = GetAchievementSummary();
  layout.summary.set(std::format("{} / {}   {}G / {}G", s.unlocked, s.total,
                                 s.earnedScore, s.totalScore));

  if (state_ == State::FADE_IN) {
    // Shown here for the first time: Create hides it so it can parse behind
    // the transition, and this restarts its anime clock at frame 1.
    task_.SetVisibleAndPlay(true);
    list_menu_.SetVisibleAndPlay(true);
    list_menu_.SetActive(true);
    list_menu_.AttachCursor();
    BD_DEBUG("[achv] state -> FADE_IN");
  }
}

void AchievementsMenu::PopulateNames() {
  AchievementRowTemplate::PopulateNames(list_menu_);
}

void AchievementsMenu::RefreshVisuals() {
  auto &layout = AchievementsLayout::Get();
  layout.listAlpha.set(255.0 * fade_);
  layout.descAlpha.set(200.0 * fade_);
  AchievementRowTemplate::RefreshVisuals(list_menu_, fade_);
}

void AchievementsMenu::UpdateRowDesc(int cursor) {
  const auto &rows = GetAchievementList();
  auto &layout = AchievementsLayout::Get();

  const auto lines = bd::WrapTwoLines(
      cursor >= 0 && cursor < static_cast<int>(rows.size()) ? rows[cursor].desc
                                                            : std::string(),
      kDescWrapChars);
  layout.rowDesc0.set(lines[0]);
  layout.rowDesc1.set(lines[1]);
}

// The list is read-only, so B is the only input it takes. Cue and transition
// both fire on the press, as the Spell Record's cancel branch does in one
// instruction pair (0x822F7858).
void AchievementsMenu::HandleList() {
  if (!CheckAction(Action::Cancel))
    return;
  sfx::Play(sfx::kCancel);
  StartOutro();
}

// Runs before the engine drives the task tree, so flag writes reach
// AnimeMenu_Update the same frame. The cursor then reads one frame stale,
// invisible on a read-only list, and this class stays off the title task that
// ConfigMenu::Update is welded to.
void AchievementsMenu::Update() {
  if (state_ == State::INTRO) {
    // Re-applied every frame for the same reason the list's template vars are:
    // engine async init clears them.
    if (intro_)
      AchievementsTransitionLayout::Get().SyncVars(intro_.AnimeData());
    // Found while the strip is still walking, so the handover costs no frame of
    // its own. The screen has been parsed since Preload.
    DiscoverMenu();
    if (!IntroFinished())
      return;
    // Both draw the strip, divider, header and icon at the same coordinates,
    // so swapping them in one frame shows nothing. Hidden rather than killed:
    // leaving is the same screen played backwards.
    BD_DEBUG("[achv] transition ran {} frames (anime frame {}, length {})",
             intro_frames_, intro_.AnimTime(), intro_.AnimLength());
    intro_.SetVisibleAndPlay(false);
    state_ = State::INIT;
  }

  if (state_ == State::INIT) {
    if (!DiscoverMenu())
      return;
    Transition(State::FADE_IN);
  }

  if (state_ == State::OUTRO) {
    if (intro_)
      AchievementsTransitionLayout::Get().SyncVars(intro_.AnimeData());
    if (!OutroFinished())
      return;
    BD_DEBUG("[achv] outro ran {} frames (anime frame {})", intro_frames_,
             intro_.AnimTime());
    intro_.SetAnimSpeed(intro_speed_);
    intro_.SetVisibleAndPlay(false);
    state_ = State::CLOSING;
    return;
  }

  if (state_ == State::CLOSING)
    return;

  // FADE_IN and LIST keep the same screen up. Only the alpha ramp and whether
  // input is read differ. There is no ramp the other way: the stock record
  // screens are hidden outright on the frame their reverse transition starts.
  if (state_ == State::FADE_IN) {
    fade_ += 1.0f / kFadeFrames;
    if (fade_ >= 1.0f) {
      fade_ = 1.0f;
      state_ = State::LIST;
      BD_DEBUG("[achv] state -> LIST");
    }
  }

  // Engine async init resets template vars and active flags, so both are
  // re-applied every frame.
  list_menu_.SetActive(true);
  PopulateNames();
  RefreshVisuals();

  cursor_.Poll(list_menu_, [&](int c) { UpdateRowDesc(c); });

  AchievementsLayout::Get().SyncVars(task_.AnimeData());

  if (state_ == State::LIST)
    HandleList();
}

} // namespace bd::engine
