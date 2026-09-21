/**
 * @file    core/settings.h
 * @brief   Core settings: the ones that belong to no single module.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <functional>
#include <string>

#include <rex/types.h>

// Console category every reblue setting registers under.
inline constexpr char kCvarGroup[] = "Blue Dragon";

namespace bd {

// A cvar's storage form. Every Set* that writes through
// rex::cvar::SetFlagByName spells its value with these, so the three settings
// objects cannot drift on how a bool or a double is written.
std::string FormatCvar(i32 v);
std::string FormatCvar(bool v);
std::string FormatCvar(f64 v);

enum class UpdateChannel : i32 { Stable, Nightly };

const char *ToString(UpdateChannel channel);

class Settings {
public:
  static Settings &Get();

  // Adopts the current cvar values, then registers a change callback per
  // setting so console, config file and launch argument writes reach here.
  // Called once from ReblueApp::OnPostInitLogging, the first consumer hook
  // after rex::cvar::LoadConfig has run.
  void Init();

  // Re-reads every setting from cvar storage. rex::cvar::ResetToDefault and
  // ResetAllToDefaults write the storage without firing a change callback, so
  // anything that calls them calls this after.
  void AdoptCvars();

  bool Devmode() const { return devmode_; }
  bool SetDevmode(bool v);

  // Devmode drives the guest's own debug config, which exists only once the
  // guest has booted. engine installs the applier then, and from that point
  // every change reaches it, including one typed at the console.
  void SetDevmodeApplier(std::function<void()> applier);

  bool DbgPrint() const { return dbgPrint_; }
  bool SetDbgPrint(bool v);

  // "auto" or a BD language code. Restart-bound: the value updates here as
  // soon as it is set, but nothing rereads it until the next boot.
  const std::string &Language() const { return language_; }
  bool SetLanguage(const std::string &v);

  bool I18nKeys() const { return i18nKeys_; }
  bool SetI18nKeys(bool v);

  const std::string &LanguagePath() const { return languagePath_; }

  i32 PerfHistorySeconds() const { return perfHistorySeconds_; }
  bool SetPerfHistorySeconds(i32 v);

  bool PerfCSV() const { return perfCSV_; }
  bool SetPerfCSV(bool v);

  // Read at startup only, and compiled out of playtest builds.
  bool Profiler() const { return profiler_; }

  i32 ShutdownTimeoutMs() const { return shutdownTimeoutMs_; }
  bool SetShutdownTimeoutMs(i32 v);

  // The one startup network setting: it covers the release check and the
  // content packs the manifest names, since a player who declines one has
  // declined the other.
  bool UpdateCheck() const { return updateCheck_; }
  bool SetUpdateCheck(bool v);

  bd::UpdateChannel UpdateChannel() const { return updateChannel_; }
  bool SetUpdateChannel(bd::UpdateChannel v);

  const std::string &UpdateUrl() const { return updateUrl_; }

  // Empty means the default location, resolved by reblue_app.
  const std::string &SavesPath() const { return savesPath_; }
  const std::string &CachePath() const { return cachePath_; }

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the other ten.
  void AdoptDevmode();
  void AdoptDbgPrint();
  void AdoptLanguage();
  void AdoptI18nKeys();
  void AdoptLanguagePath();
  void AdoptPerfHistorySeconds();
  void AdoptPerfCSV();
  void AdoptProfiler();
  void AdoptShutdownTimeoutMs();
  void AdoptUpdateCheck();
  void AdoptUpdateBase();
  void AdoptUpdateChannel();
  void AdoptSavesPath();
  void AdoptCachePath();

  void ComposeUpdateUrl();

  bool devmode_ = false;
  bool dbgPrint_ = false;
  std::string language_ = "auto";
  bool i18nKeys_ = false;
  std::string languagePath_ = "lang";
  i32 perfHistorySeconds_ = 20;
  bool perfCSV_ = false;
  bool profiler_ = false;
  i32 shutdownTimeoutMs_ = 1500;
  bool updateCheck_ = true;
  std::string updateBase_;
  bd::UpdateChannel updateChannel_ = bd::UpdateChannel::Stable;
  std::string updateUrl_;
  std::string savesPath_;
  std::string cachePath_;

  std::function<void()> devmodeApplier_;
};

} // namespace bd
