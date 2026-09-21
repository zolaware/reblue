/**
 * @file    engine/menus/update_prompt.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/update_prompt.h"

#include <string>

#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/text_wrap.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

using Sync = bd::platform::ContentSync;
using Updates = bd::platform::Updates;

// Nothing that hangs may hold the boot: past these the title carries on.
constexpr auto kCheckTimeout = std::chrono::seconds(25);
constexpr auto kLoadTimeout = std::chrono::seconds(3);
// The notice window takes no input, so an error stands for this long instead.
constexpr auto kErrorHold = std::chrono::seconds(8);

// One line of the notice window at its 870px width.
constexpr int kNoticeLineChars = 50;

constexpr const char *kMount = "ui:update-prompt";
constexpr const char *kCsvDir = "d2anime\\reblue\\";
constexpr const char *kCsvName = "l_update.csv";

// The windows read their text and their look out of their parent's var bag,
// so this screen is nothing but the block declaring it.
class PromptLayout final : public AnimeLayout {
public:
  void build(CsvBuilder &b) override { sysmes.Emit(b); }

private:
  SysMesVars sysmes;
};

PromptLayout s_layout;

const char *ErrorKey(Updates::ApplyResult result) {
  switch (result) {
  case Updates::ApplyResult::kDownloadFailed:
    return "update.error.download_failed";
  case Updates::ApplyResult::kHashMismatch:
    return "update.error.hash_mismatch";
  default:
    return "update.error.apply_failed";
  }
}

} // namespace

UpdatePrompt &UpdatePrompt::Get() {
  static UpdatePrompt s;
  return s;
}

void UpdatePrompt::Init(std::filesystem::path install_root) {
  install_root_ = std::move(install_root);
}

bool UpdatePrompt::Active() const {
  const Phase phase = phase_.load();
  return phase != Phase::kIdle && phase != Phase::kDone;
}

// Dropped, not killed: the engine frees them with the host task they hang off.
bool UpdatePrompt::Release() {
  notice_.Drop();
  confirm_.Drop();
  if (host_)
    host_.Kill();
  host_ = D2AnimeTask();
  LayoutMount::Remove(kMount);
  phase_.store(Phase::kDone);
  return false;
}

void UpdatePrompt::ShowCheckLine() {
  const bool app = Updates::Get().State() == Updates::Stage::kChecking;
  notice_.Show(
      host_,
      i18n::Text(app ? "update.status.checking" : "update.status.content"));
}

bool UpdatePrompt::EnterOffers() {
  auto &updates = Updates::Get();
  if (Updates::CanApply() && updates.HasNewer()) {
    if (const auto newer = updates.Newer()) {
      app_version_ = newer->version;
      app_bytes_ = 0;
      if (const auto manifest = updates.Current())
        if (const auto *artifact = manifest->ArtifactForThisPlatform())
          app_bytes_ = artifact->size;

      BD_INFO("[update] offering v{} at the title", app_version_);
      notice_.Kill();
      if (confirm_.Create(host_,
                          i18n::Fmt("update.prompt.app", app_version_,
                                    i18n::Bytes(app_bytes_))
                              .c_str(),
                          i18n::Text("update.prompt.app_ask").c_str())) {
        phase_.store(Phase::kAppOffer);
        return true;
      }
      BD_WARN("[update] the offer window would not open, skipping the offer");
    }
  }
  return EnterContentOffer();
}

bool UpdatePrompt::EnterContentOffer() {
  auto &sync = Sync::Get();
  if (sync.State() != Sync::Stage::kPending) {
    BD_INFO("[update] nothing left to offer, letting the engine through");
    return Release();
  }

  BD_INFO("[update] offering {} content pack(s)", sync.PendingCount());
  notice_.Kill();
  if (!confirm_.Create(host_,
                       i18n::Fmt("update.prompt.content", sync.PendingCount(),
                                 i18n::Bytes(sync.PendingBytes()))
                           .c_str(),
                       i18n::Text("update.prompt.content_ask").c_str())) {
    BD_WARN("[update] the offer window would not open, skipping the offer");
    sync.Decline();
    return Release();
  }
  phase_.store(Phase::kContentOffer);
  return true;
}

bool UpdatePrompt::Hold(const Task &parent) {
  auto &updates = Updates::Get();
  auto &sync = Sync::Get();
  const auto now = std::chrono::steady_clock::now();

  switch (phase_.load()) {
  case Phase::kIdle: {
    // The title's own draw syncs the locale at its menu, which is behind this.
    bd::i18n::SyncLocale();
    updates.BeginCheck();
    if (updates.State() == Updates::Stage::kIdle) {
      BD_INFO("[update] no check to run at the title, letting the engine "
              "through (bd_update_check={}, endpoint '{}')",
              bd::Settings::Get().UpdateCheck(),
              bd::Settings::Get().UpdateUrl());
      phase_.store(Phase::kDone);
      return false;
    }

    // The provider runs per read and the layout outlives it, so the mount
    // object has nothing to keep.
    LayoutMount(kCsvDir).Add(kCsvName, &s_layout).Publish(kMount);
    const std::string csv = std::string(kCsvDir) + kCsvName;
    host_ = D2AnimeTask::Load(parent, csv.c_str(), D2AnimeTask::Reveal::Held);
    if (!host_) {
      BD_WARN("[update] the prompt's own screen would not load, skipping it");
      return Release();
    }
    BD_INFO("[update] holding the title for the check");
    deadline_ = now + kLoadTimeout;
    phase_.store(Phase::kLoading);
    return true;
  }

  case Phase::kLoading:
    if (!host_)
      return Release();
    if (!host_.IsReady()) {
      if (now < deadline_)
        return true;
      BD_WARN("[update] the prompt's own screen did not parse, skipping it");
      return Release();
    }
    // Draws nothing itself, but a hidden parent takes its windows with it.
    host_.SetVisibleAndPlay(true);
    ShowCheckLine();
    deadline_ = now + kCheckTimeout;
    phase_.store(Phase::kChecking);
    return true;

  case Phase::kChecking: {
    const bool waiting = updates.State() != Updates::Stage::kDone ||
                         sync.State() == Sync::Stage::kChecking;
    if (!waiting)
      return EnterOffers();
    ShowCheckLine();
    if (now < deadline_)
      return true;
    BD_WARN("[update] the startup check did not answer in time, carrying on");
    return Release();
  }

  case Phase::kAppOffer:
    if (!confirm_.Poll())
      return true;
    if (!confirm_.Confirmed()) {
      confirm_.Kill();
      return EnterContentOffer();
    }
    confirm_.Kill();
    updates.BeginApply(install_root_);
    phase_.store(Phase::kAppWorking);
    return true;

  case Phase::kAppWorking: {
    if (updates.ApplyState() != Updates::ApplyStage::kDone) {
      const u64 total = updates.ApplyBytesTotal();
      const u64 done = updates.ApplyBytesDone();
      const u64 percent = total == 0 ? 0 : done * 100 / total;
      notice_.Show(host_, i18n::Fmt("update.prompt.downloading", app_version_,
                                    percent));
      return true;
    }
    if (updates.Applied() == Updates::ApplyResult::kStaged) {
      notice_.Show(host_, i18n::Text("update.prompt.staged"));
      phase_.store(Phase::kAppStaged);
      // The swap runs at startup, so the restart is what applies it.
      bd::platform::RequestWarmReboot();
      return true;
    }
    const auto lines = bd::WrapTwoLines(i18n::Text(ErrorKey(updates.Applied())),
                                        kNoticeLineChars);
    notice_.Show(host_, lines[0], lines[1]);
    deadline_ = now + kErrorHold;
    phase_.store(Phase::kAppFailed);
    return true;
  }

  case Phase::kAppFailed:
    if (now < deadline_ && !CheckAction(Action::Confirm) &&
        !CheckAction(Action::Cancel))
      return true;
    notice_.Kill();
    return EnterContentOffer();

  case Phase::kAppStaged:
    return true;

  case Phase::kContentOffer:
    if (!confirm_.Poll())
      return true;
    if (!confirm_.Confirmed()) {
      confirm_.Kill();
      sync.Decline();
      return Release();
    }
    confirm_.Kill();
    sync.BeginFetch();
    phase_.store(Phase::kContentWorking);
    return true;

  case Phase::kContentWorking: {
    if (sync.State() == Sync::Stage::kDone)
      return Release();
    const size_t total = sync.Total();
    const size_t done = sync.Done();
    notice_.Show(host_, i18n::Fmt("update.status.downloading", sync.Current(),
                                  done < total ? done + 1 : total, total));
    return true;
  }

  case Phase::kDone:
    return false;
  }
  return false;
}

} // namespace bd::engine
