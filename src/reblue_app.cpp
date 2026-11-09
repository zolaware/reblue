/**
 * @file    reblue_app.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "reblue_app.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cstdlib>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <rex/platform/perf.h>
#include <rex/app/app.h>
#include <rex/title.h>
#include <rex/filesystem/file_system.h>
#include <rex/logging/log_category.h>
#include <rex/logging/logger.h>
#include <rex/result.h>
#include <rex/settings_registry.h>
#include <rex/platform/file.h>
#include <rex/platform/process.h>
#include <rex/input_keys.h>
#include <rex/kernel/title_runtime.h>
#include <rex/ui/ui_system.h>
#include <rex/version.h>

#include <rex/string/text.h>

#include "audio/audio.h"
#include "core/app_root.h"
#include "core/build_info.h"
#include "core/cvar_table.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/settings_migration.h"
#include "core/settings_model.h"
#include "engine/engine.h"
#include "generated/reblue_init.h"
#include "gpu/gpu.h"
#include "gpu/host_heap.h"
#include "gpu/pipeline/pso_precache.h"
#include "installer/installer.h"
#include "platform/platform.h"
#include "vfs/vfs.h"
#include "core/memory_helpers.h"

namespace {

// The profile dir IS user_data_root. Saves sit in a sibling subtree of
// achievements/ and any XAM content, so there is no content manager contention.
std::filesystem::path
ResolveSavesRoot(const std::filesystem::path &profile_root) {
  const auto &override_path = bd::Settings::Get().SavesPath();
  if (!override_path.empty())
    return std::filesystem::path(override_path);
  return profile_root / "saves";
}

constexpr u64 kLogDirBudget = u64(50) << 20;

constexpr bool kOfficialBuild = REBLUE_OFFICIAL_BUILD != 0;

// Created if absent so PSO capture can write into it.
std::filesystem::path ResolveCacheRoot() {
  std::filesystem::path root = bd::CacheRootFor(bd::AppRootFolder());
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  return root;
}

bool ParseFlag(std::string_view text) {
  return text.empty() || text == "true" || text == "1" || text == "yes" ||
         text == "on";
}

// Devmode before any setting is read: the command line wins, then the
// environment, then the profile config file. Devmode decides whether the game
// mount is writable, and that is a title setting fixed at launch.
bool DevmodeRequested(const rex::CLIArgList *args,
                      const std::filesystem::path &config_file) {
  if (args) {
    if (args->Has("bd.devmode"))
      return ParseFlag(args->Get("bd.devmode").value_or(""));
    if (args->Has("no-bd.devmode"))
      return false;
  }
  if (const char *env = std::getenv("REX_BD_DEVMODE"))
    return ParseFlag(env);
  std::error_code ec;
  if (config_file.empty() || !std::filesystem::exists(config_file, ec))
    return false;
  try {
    const toml::table parsed = toml::parse_file(config_file.string());
    return parsed["bd"]["devmode"].value_or(false);
  } catch (const toml::parse_error &) {
    return false;
  }
}

struct EarlyInstallInfo {
  std::filesystem::path root;
  std::filesystem::path game_root;
};

// Resolve the install without the wizard: --game_data_root's parent if set, else
// a schema-matched install record. nullopt on a fresh, unregistered install
// (the installer wires the profile later, then restarts into it).
std::optional<EarlyInstallInfo>
EarlyInstall(const std::filesystem::path &game_data_root) {
  if (!game_data_root.empty())
    return EarlyInstallInfo{
        .root = std::filesystem::absolute(game_data_root).parent_path(),
        .game_root = {}};
  if (auto cfg = bd::installer::InstallConfig::Read())
    if (cfg->schema_version == bd::installer::kInstallSchemaVersion)
      return EarlyInstallInfo{.root = cfg->install_root,
                              .game_root = cfg->game_data_path()};
  return std::nullopt;
}

// BD reads XCONFIG_USER_LANGUAGE once at boot. bd.language is a code from the
// install's own bd_boot.ini Language Set, or 'auto' to follow the host display
// language. Returns 0 through unknown_code when the code names no language.
u32 ResolveUserLanguage(std::string code, bool &autodetect,
                        std::string &unknown_code) {
  code = rex::string::utf8_lower_ascii(code);
  autodetect = code.empty() || code == "auto";
  u32 lang = autodetect ? bd::platform::DetectOsXLanguage()
                        : bd::platform::XLanguageFromCode(code);
  if (!autodetect && lang == 0) {
    unknown_code = code;
    lang = 1;
  }
  if (lang < 1 || lang > 12)
    lang = 1;
  return lang;
}

template <typename T>
std::unique_ptr<rex::ui::IDialog> CreateOverlay(rex::ui::IUISystem &ui) {
  return std::make_unique<T>(ui);
}

constexpr rex::ui::OverlayConfig kReblueOverlays[] = {
    {"fade", rex::KeyChord{}, CreateOverlay<bd::ui::FadeOverlay>},
    {"pause", rex::KeyChord{}, CreateOverlay<bd::ui::PauseOverlay>},
    {"perf", rex::KeyChord{}, CreateOverlay<bd::ui::PerfOverlay>},
};

} // namespace

ReblueApp::ReblueApp() : rex::app::TitleApp("reblue", kPPCImageConfig) {}

ReblueApp::~ReblueApp() { bd::mem::Bind(nullptr); }

rex::AppSysRequirements ReblueApp::GetRequirements() const {
  // reblue renders natively, so it names no graphics family and no Xenos
  // provider loads.
  return {.graphics_family = rex::GraphicsFamily::kNone, .audio = true, .input = true, .ui = true};
}

// Runs once every provider is connected and the config file, the environment
// and the command line are applied, so every setting reads its final value
// here.
rex::Result<void> ReblueApp::OnInit() {
  if (!location_warning_.empty())
    GetUI().ShowMessage(rex::MessageType::kWarning, "re:Blue",
                        location_warning_);
  bd::platform::SetInput(&GetInput());
  bd::platform::SetUI(&GetUI());
  bd::platform::SetGraphics(&GetGraphics());
  bd::engine::Bindings::Get().Init(GetInput());
  bd::engine::Bindings::Get().Register();
  bd::SettingsMigration::Apply();
  // Every Settings object adopts the config file and the command line here, so
  // every later hook can read a setting through its object.
  bd::Settings::Get().Init();
  bd::gpu::Settings::Get().Init();
  bd::vfs::Settings::Get().Init();
  bd::engine::Settings::Get().Init();
  bd::engine::Bindings::Get().ReportParseErrors();
  bd::engine::GameOptions::Get().Init();

  bd::engine::Achievements::Init();
  bd::engine::Gimmicks::Get().Init();

  REXBD_INFO("re:Blue v" REBLUE_VERSION_STRING " [" REXGLUE_BUILD_CONFIG
          "] " REBLUE_BUILD_PLATFORM);
  REXBD_INFO("  commit:  " REBLUE_GIT_COMMIT " on " REBLUE_GIT_BRANCH "{}{}",
          REBLUE_GIT_DIRTY ? " (local modifications)" : "",
          kOfficialBuild ? "" : " (development build)");
  REXBD_INFO("  built:   " REBLUE_BUILD_TIMESTAMP " with " REBLUE_BUILD_COMPILER);
  REXBD_INFO("  sdk:     {}", rex::GetBuildStamp());

  // The runtime reads the language when LaunchTitle loads the title.
  const u32 language = ResolveUserLanguage(bd::cvars::Get().language.Get(),
                                           language_autodetected_,
                                           unknown_language_code_);
  if (auto set = rex::GetSettingsRegistry().SetText(rex::SettingLayer::kTitle,
                                            "kernel.user_language",
                                            std::to_string(language));
      !set)
    REXBD_WARN("{}", set.error().message);

  // The SDK settings and achievements overlays are developer tools here.
  if (!bd::Settings::Get().Devmode()) {
    for (const char *name : {"ui.bind.settings", "ui.bind.achievements"})
      (void)rex::GetSettingsRegistry().SetText(rex::SettingLayer::kTitle, name, "");
  }

  rex::ui::IUISystem &ui = GetUI();
  bd::ui::PauseOverlay::InitFonts(&ui.GetFonts());
#ifdef REBLUE_BUILD_INSTALLER
  bd::installer::InitInstallerFonts(&ui.GetFonts());
#endif
  bd::ui::Theme::Apply(ui.GetStyle());
  bd::ui::Theme::ApplyImGui(ui.GetImGuiStyle());
  overlay_drawer_ = std::make_unique<bd::gpu::ImGuiOverlayDrawer>();
  (void)ui.GetSettings().title.Set(std::string("re:Blue v" REBLUE_VERSION_STRING " (") +
                     bd::gpu::Video::BackendName() + ")");
  (void)ui.AddOverlays(kReblueOverlays);
  ui.ShowOverlay("fade", true);
  ui.ShowOverlay("pause", true);
  ui.ShowOverlay("perf", true);
  (void)ui.GetSettings().aspect_ratio.Set(bd::gpu::Output::ConfiguredWindowAspect());
  ui_listener_guard_ = ui.AddEventListener(*this);
  return rex::Ok();
}

rex::Result<void> ReblueApp::OnConfigure(rex::SettingsRegistry &settings) {
  REX_TRY_VOID(rex::GetLogger().AddCategories(rex::log::kReblueLog));
  REX_TRY_VOID(settings.Register(bd::cvars::Table::Get()));

  const auto title = [&settings](std::string_view name, std::string_view value) {
    if (auto set = settings.SetText(rex::SettingLayer::kTitle, name, value);
        !set)
      REXBD_WARN("default {} not applied: {}", name, set.error().message);
  };
  title("app.single_instance", "reblue");
  // Blue Dragon is single player and polls title user 0, so every pad drives it.
  title("hid.shared_pads", "true");
  title("ui.min_width", "640");
  title("ui.min_height", "360");
  title("ui.cursor_timeout", "5");
  // The F3 slot is reblue's own frame time overlay.
  title("ui.bind.debug", "");
  title("log.folder_budget", std::to_string(kLogDirBudget));

  // The audio output guard and the pacing queue sit where the stock values left
  // an audible gap or overrun.
  title("apu.lfe", "0");
  title("apu.max_queued_frames", "16");

  settings.AddAlias("bd.audio_gain", "apu.gain");
  settings.AddAlias("bd.audio_center", "apu.center");
  settings.AddAlias("bd.audio_surround", "apu.surround");
  settings.AddAlias("bd.audio_lfe", "apu.lfe");
  settings.AddAlias("bd.audio_qframes", "apu.max_queued_frames");
  settings.AddAlias("bd.vibration", "hid.vibration");
  settings.AddAlias("bd.vsync", "gpu.vsync");
  settings.AddAlias("bd.fps_limit", "gpu.frame_limit");
  settings.AddAlias("bd.profiler", "perf.profiler");

  // The SDK's bind defaults are arbitrary. These are the PC convention layout
  // for this game. A default moves under a value the user set, so a rebound
  // key stays what they chose. Cancel rides on RMB the way confirm rides on
  // LMB, and Q pairs with E on the other shoulder so the shop pager and the
  // leader cycle stay reachable.
  struct BindDefault {
    const char *setting;
    const char *value;
  };
  constexpr BindDefault kBindDefaults[] = {
      {"hid.keybind_a", "LMB,Ctrl+Space"},
      {"hid.keybind_b", "RMB,Escape"},
      {"hid.keybind_x", "Space"},
      {"hid.keybind_y", "C"},
      {"hid.keybind_left_trigger", "Z"},
      {"hid.keybind_right_trigger", "F"},
      {"hid.keybind_left_shoulder", "E"},
      {"hid.keybind_lstick_up", "W"},
      {"hid.keybind_lstick_down", "S"},
      {"hid.keybind_lstick_left", "A"},
      {"hid.keybind_lstick_right", "D"},
      {"hid.keybind_right_shoulder", "Q"},
      {"hid.keybind_lstick_press", "K"},
      {"hid.keybind_rstick_press", "L"},
      {"hid.keybind_back", "Tab"},
      {"hid.keybind_start", "M"},
  };
  for (const auto &bind : kBindDefaults)
    title(bind.setting, bind.value);

  // The profile comes from the command line, before any config file is read,
  // so a stale key in a profile toml can never override the launcher's choice.
  std::string requested = "default";
  if (args_) {
    if (auto profile = args_->Get("bd.profile"))
      requested = std::string(*profile);
  }
  active_profile_ = rex::platform::SanitizeFolderName(requested);

  title("app.log_root", (bd::AppRootFolder() / "logs").string());

  // A package is not a data directory. Before an install exists, anchor its
  // transient config and the installer's default destination in writable user
  // storage, a completed install replaces this with the selected profile path.
  std::filesystem::path config_file;
  if (rex::platform::IsPackagedApp()) {
    config_file = bd::AppRootFolder() / "reblue.toml";
    title("app.config_file", config_file.string());
  }
  bd::SettingsMigration::RewriteFile(config_file);

  const std::filesystem::path game_root_arg(
      args_ ? args_->Get("game_data_root").value_or("") : "");
  const auto install = EarlyInstall(game_root_arg);
  if (!install)
    return rex::Ok(); // fresh install: the installer restarts into the profile

  install_root_ = install->root;
  bd::SetAppRoot(install_root_);
  title("app.log_root", (bd::AppRootFolder() / "logs").string());
  profile_root_ = install_root_ / "profiles" / active_profile_;
  std::error_code ec;
  std::filesystem::create_directories(profile_root_, ec);

  const auto profile_cfg = profile_root_ / "reblue.toml";
  title("app.user_root", profile_root_.string());
  title("app.config_file", profile_cfg.string());
  title("app.cache_root", ResolveCacheRoot().string());
  if (!install->game_root.empty())
    title("title.game_root", install->game_root.string());
  bd::SettingsMigration::RewriteFile(profile_cfg);
  if (DevmodeRequested(args_, profile_cfg))
    title("title.game_writable", "true");
  bd::platform::SetProfileContext(active_profile_, profile_cfg);

#if defined(_WIN32)
  const auto program_dir = rex::platform::GetExecutableFolder();
  if (program_dir != install_root_) {
    const auto warning =
        std::format("Warning: re:Blue is running outside of its install "
                    "directory. Please run the exe from {}",
                    install_root_.string());
    if (rex::platform::IsDebuggerAttached())
      REXBD_WARN("{}", warning);
    else
      location_warning_ = warning;
  }
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
  bd::platform::ClearReplacedFiles(install_root_);
#else
  // A build-dir exe run against this install is not part of it, so swapping
  // release binaries in under it would strand the debugger on the wrong image.
  // The mac bundle lives wherever the user dragged it, so there is no folder.
#if defined(_WIN32)
  if (program_dir == install_root_)
#endif
  {
    bd::platform::ClearReplacedFiles(install_root_);
    if (bd::platform::InstallStagedUpdate(install_root_)) {
      rex::app::RequestRestart(bd::platform::ProfileArgs());
      return rex::Ok();
    }
  }
#endif
  return rex::Ok();
}

bool ReblueApp::NeedsUpgradePrompt(
    const bd::installer::InstallConfig &cfg) const {
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  return !cfg.Portable() && rex::platform::GetExecutableFolder() != cfg.install_root;
#else
  (void)cfg;
  return false;
#endif
}

void ReblueApp::RestampInstall(const bd::installer::InstallConfig &cfg) {
  if (!cfg.Write())
    REXBD_WARN("Install record restamp failed, this upgrade will be offered again");
}

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
void ReblueApp::BeginUpgrade(const bd::installer::InstallConfig &cfg) {
  upgrade_prompt_ = std::make_unique<bd::installer::UpgradePrompt>(
      cfg.install_root, cfg.app_version,
      [this, cfg](bool accepted) { FinishUpgrade(accepted, cfg); });
  upgrade_prompt_guard_ = GetUI().AddDialog(*upgrade_prompt_);
}

void ReblueApp::FinishUpgrade(bool accepted,
                              const bd::installer::InstallConfig &cfg) {
  if (!accepted) {
    REXBD_INFO("Upgrade declined, booting the install as it stands");
    upgrade_prompt_guard_.Reset();
    upgrade_prompt_.reset();
    return;
  }
  upgrade_prompt_guard_.Reset();
    upgrade_prompt_.reset();

  std::string copy_error;
  if (!bd::installer::CopyProgramTo(cfg.install_root, copy_error)) {
    bd::platform::ShowFatal(
        "Upgrade failed", "Could not copy " + copy_error + " into " +
                              cfg.install_root.string() +
                              "\n\nThe installed version is untouched.");
    rex::app::RequestShutdown(1);
    return;
  }
  RestampInstall(cfg);
  handoff_exe_ = cfg.install_root / "reblue.exe";
  rex::app::RequestShutdown(0);
}
#endif

bool ReblueApp::PrepareSetup() {
  const bool user_supplied_path = args_ && args_->Has("game_data_root");

  if (user_supplied_path) {
    const std::filesystem::path game_root(bd::CvarGet("title.game_root"));
    if (!std::filesystem::exists(game_root / "default.xex")) {
      bd::platform::ShowFatal(
          "reblue - invalid game directory",
          "Path '" + game_root.string() +
              "' does not contain default.xex. "
              "Pass the directory that contains default.xex (typically the "
              "'base' subdirectory of an installed copy).");
      rex::app::RequestShutdown(1);
      return false;
    }
    return false;
  }

  // A stale entry is kept rather than dropped, so the wizard opens in repair
  // mode against the install it names.
  bool repair_requested = false;
#ifdef REBLUE_BUILD_INSTALLER
  repair_requested = repair_requested_;
#endif

  std::optional<bd::installer::InstallConfig> existing_install;
  if (auto cfg = bd::installer::InstallConfig::Read()) {
    if (cfg->schema_version == bd::installer::kInstallSchemaVersion &&
        !repair_requested) {
      if (cfg->app_version != REBLUE_VERSION_STRING) {
        REXBD_INFO("Install at {} records version '{}', running '{}'",
                cfg->install_root.string(), cfg->app_version,
                REBLUE_VERSION_STRING);
        const bool newer = bd::platform::Version::Compare(
                               REBLUE_VERSION_STRING, cfg->app_version) > 0;
        if (kOfficialBuild && newer) {
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
          if (NeedsUpgradePrompt(*cfg)) {
            // The prompt draws through the setup frames, so the renderer has
            // to be up before it is raised. From here FinishUpgrade owns the
            // boot.
            if (!BeginPreTitleUI())
              return false;
            BeginUpgrade(*cfg);
            return true;
          }
#endif
          RestampInstall(*cfg);
        }
      }
      REXBD_INFO("Resolved install from {}",
              bd::installer::ToString(cfg->connector));
      REXBD_INFO("  install root:   {}", cfg->install_root.string());
      REXBD_INFO("  game data:      {}", cfg->game_data_path().string());
      REXBD_INFO("  user data:      {}", cfg->user_data_path().string());
      for (int i = 0; i < bd::installer::kDiscCount; ++i)
        REXBD_DEBUG("  disc{} hash:     {}", i + 1, cfg->iso_fingerprints[i]);
      return false;
    }
    if (repair_requested)
      REXBD_INFO("--repair: opening wizard in repair mode on existing install");
    else
      REXBD_DEBUG("Install schema {} != current {}, opening wizard in repair mode",
               cfg->schema_version, bd::installer::kInstallSchemaVersion);
    existing_install = std::move(cfg);
  }

#ifdef REBLUE_BUILD_INSTALLER
  if (bd::cvars::Get().no_installer.Get()) {
    bd::platform::ShowFatal(
        "reblue - game not installed",
        "Game not installed. Either run without --no-installer to launch the "
        "installer, or pass the installed game directory as the first "
        "argument.");
    rex::app::RequestShutdown(1);
    return false;
  }

  // No Runtime yet, so the wizard renders through the overlay hook in the
  // setup frames until the title launches.
  if (!BeginPreTitleUI())
    return false;

  const bool repair = existing_install.has_value();
  // Setup already put the executable in the directory the user picked.
  const auto default_install_dir =
      repair ? existing_install->install_root : bd::AppRootFolder();
  installer_wizard_ = std::make_unique<bd::installer::InstallerWizard>(
      default_install_dir, repair,
      existing_install ? &*existing_install : nullptr,
      [this](bool completed, const auto &cfg, const auto &choices) {
        FinishInstaller(completed, cfg, choices);
      });
  installer_wizard_guard_ = GetUI().AddDialog(*installer_wizard_);
  return true;
#else
  // No built-in installer: the external reBlue launcher performs the install,
  // writing the install record that InstallConfig::Read consumes above.
  bd::platform::ShowFatal(
      "reblue - game not installed",
      "No installed game was found. Install reblue with the reBlue launcher, "
      "or pass the installed game directory as the first argument.");
  rex::app::RequestShutdown(1);
  return false;
#endif
}

bool ReblueApp::SetupActive() const {
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  return installer_wizard_ != nullptr || upgrade_prompt_ != nullptr;
#elif defined(REBLUE_BUILD_INSTALLER)
  return installer_wizard_ != nullptr;
#else
  return false;
#endif
}

void ReblueApp::PresentSetupFrame() { bd::gpu::Video::PresentOverlayFrame(); }

void ReblueApp::RunHandoff() {
  if (!handoff_exe_)
    return;
  std::vector<std::string> args;
  if (auto profile = bd::platform::ProfileArgs(); !profile[1].empty())
    args = std::move(profile);
  if (auto launched = rex::platform::Relaunch(*handoff_exe_, args,
                                              handoff_exe_->parent_path());
      !launched) {
    REXBD_ERROR("handoff relaunch failed: {}", launched.error().message);
    bd::platform::ShowFatal("Could not start re:Blue",
                            "Run " + handoff_exe_->string() + " by hand.");
  }
}

bool ReblueApp::BeginPreTitleUI() {
  if (!CreateDevice()) {
    bd::platform::ShowFatal("reblue - renderer init failed",
                            "Failed to initialize the renderer.");
    rex::app::RequestShutdown(1);
    return false;
  }
  return true;
}

#ifdef REBLUE_BUILD_INSTALLER
void ReblueApp::FinishInstaller(bool completed,
                                const bd::installer::InstallConfig &cfg,
                                const bd::installer::WizardChoices &choices) {
  installer_wizard_guard_.Reset();
  installer_wizard_.reset();

  if (!completed) {
    rex::app::RequestShutdown(1);
    return;
  }

  install_root_ = cfg.install_root;
  bd::SetAppRoot(install_root_);

#if defined(_WIN32)
  std::string copy_error;
  if (!bd::installer::CopyProgramTo(install_root_, copy_error)) {
    const auto answer = bd::platform::UI()->ShowChoice(
        {.type = rex::MessageType::kError,
         .title = "Install failed",
         .body = "Could not copy " + copy_error + " into " +
                 install_root_.string() +
                 "\n\nNothing was recorded, so a retry starts over.",
         .buttons = {"Retry", "Quit"}});
    if (answer && *answer == 0) {
      rex::app::RequestRestart(bd::platform::ProfileArgs());
      return;
    }
    rex::app::RequestShutdown(1);
    return;
  }
#endif

  if (!cfg.Write())
    REXBD_WARN("Install record write failed, continuing into game for this session");
  REXBD_INFO("Installed to {}", cfg.install_root.string());
  for (int i = 0; i < bd::installer::kDiscCount; ++i)
    REXBD_INFO("  disc{} hash:     {}", i + 1, cfg.iso_fingerprints[i]);

  profile_root_ = install_root_ / "profiles" / active_profile_;
  std::error_code ec;
  std::filesystem::create_directories(profile_root_, ec);
  bd::platform::SetProfileContext(active_profile_,
                                  profile_root_ / "reblue.toml");

  // Nothing else writes this file before the guest boots. A profile config that
  // was not the one loaded this session is read back first, so saving keeps the
  // settings it already held.
  if (choices.reset_config || !choices.settings.empty() ||
      choices.update_check.has_value()) {
    const auto profile_cfg = bd::platform::ConfigFilePath();
    if (choices.reset_config) {
      for (rex::SettingBase *setting : rex::GetSettingsRegistry().Enumerate()) {
        const std::string &name = setting->GetName();
        if (name.starts_with("app.") || name.starts_with("title."))
          continue;
        (void)setting->Reset();
      }
    } else if (std::filesystem::exists(profile_cfg, ec)) {
      if (auto loaded = rex::GetSettingsRegistry().Load(profile_cfg); !loaded)
        REXBD_WARN("config read back failed: {}", loaded.error().message);
    }
    // After the load, not before: it would otherwise put back whatever the
    // rows held when the wizard opened.
    for (const auto &pick : choices.settings) {
      const int row = bd::SettingsFindRow(pick.page, pick.label);
      if (row >= 0)
        bd::SetSelectedOption(pick.page, row, pick.option);
    }
    if (choices.update_check.has_value())
      bd::Settings::Get().SetUpdateCheck(*choices.update_check);
    if (auto written = bd::SaveConfig(profile_cfg); !written)
      REXBD_WARN("config save failed: {}", written.error().message);
  }

#if defined(_WIN32)
  if (choices.create_shortcut) {
    std::string shortcut_error;
    if (!bd::platform::CreateDesktopShortcut(
            install_root_ / bd::installer::kGameExecutable, "re:Blue",
            shortcut_error))
      REXBD_WARN("Could not create the desktop shortcut: {}", shortcut_error);
  }

  // What boots the game is the exe sitting in the install directory, which is
  // not always here. Everything above has to be written before the hand-off:
  // what starts next reads it back.
  if (rex::platform::GetExecutableFolder() != install_root_) {
    handoff_exe_ = install_root_ / bd::installer::kGameExecutable;
    rex::app::RequestShutdown(0);
    return;
  }
#endif

  // The roots below the profile are startup settings, so the boot that follows
  // reads them from the install record instead of this one patching them in.
  rex::app::RequestRestart(bd::platform::ProfileArgs());
}
#endif // REBLUE_BUILD_INSTALLER

rex::Result<void> ReblueApp::OnTitleLaunch(rex::kernel::TitleRuntime &title) {
  bd::platform::SetTitle(&title);
  bd::audio::Playback::Get().Bind(&title);
  bd::mem::Bind(&title);

  {
    const u32 lang = static_cast<u32>(bd::CvarInt("kernel.user_language", 1));
    if (!unknown_language_code_.empty())
      REXBD_WARN("Unknown bd.language '{}', using English. Valid: auto, us, jp, "
              "de, fr, es, it, kr, tw, cn, po",
              unknown_language_code_);
    REXBD_INFO("UI language: {} (XLanguage {}, {})",
            bd::platform::XLanguageName(lang), lang,
            language_autodetected_ ? "auto-detected" : "config override");
  }

  const std::filesystem::path profile_root(bd::CvarGet("app.user_root"));
  REXBD_INFO("profile active '{}' (user data: {})", active_profile_,
          profile_root.string());

  bd::vfs::VFS::Get().Init(std::filesystem::path(bd::CvarGet("title.game_root")),
                           bd::CacheRoot());
  bd::vfs::VFS::Get().SetProfile(profile_root);

  // Arms the channel watch. The check itself runs at the title, ahead of
  // the guest's own downloadable-content load.
  bd::platform::Updates::Get().Init(install_root_);

  bd::engine::MountSaves(&title.GetFileSystem(),
                         ResolveSavesRoot(profile_root));

  if (bd::Settings::Get().Devmode())
    bd::vfs::VFS::Get().PrepareDevWriteDirs();

  // Enabled packs are published into the profile's XAM content tree so the SDK
  // ContentManager enumerates and mounts them and the guest's own title screen
  // DLC pipeline runs natively.
  bd::vfs::VFS::Get().DLC().Publish();
  // Map/standard pack content (dungeon scripts, geometry, models, enemy AI)
  // that the guest does not route through its single "download" IPK slot.
  bd::vfs::VFS::Get().DLC().MountArchives();

  if (!CreateDevice())
    return rex::Err<void>(rex::ErrorCategory::kRuntime,
                          "native renderer initialization failed");
  return rex::Ok();
}

bool ReblueApp::CreateDevice() {
  rex::ui::IUISystem &ui = GetUI();
  const auto native = ui.GetNativeWindow();
  if (!native)
    return false;
  u32 width = static_cast<u32>(ui.GetSettings().window_width.Get());
  u32 height = static_cast<u32>(ui.GetSettings().window_height.Get());
  if (!width || !height) {
    const rex::VideoMode mode = ui.GetVideoMode();
    width = mode.width;
    height = mode.height;
  }
  if (!bd::gpu::Video::CreateHostDevice(*native, width, height))
    return false;
  InstallOverlayDrawHook();
  ui.RaiseWindow();
  return true;
}

// Present runs on the title thread and ImGui on the UI thread, so this marshals
// synchronously. The guest holds s.mutex while parked, leaving the UI thread
// sole command list accessor. The setup frames present from the UI thread
// itself and draw inline.
void ReblueApp::InstallOverlayDrawHook() {
  bd::gpu::Video::SetOverlayDrawHook([this](plume::RenderCommandList *cmd,
                                            plume::RenderFramebuffer *fb, u32 w,
                                            u32 h) {
    // The UI thread stops pumping once shutdown starts, so a marshal from here
    // would block the guest with s.mutex held. Dropping the overlay for the
    // last frame costs nothing.
    if (rex::app::IsShutdownRequested())
      return;
    // Draw would return having done nothing, and the round trip parks this
    // thread for as long as the UI thread takes to wake.
    if (!GetUI().HasDialogs() && !PendingOverlayWork())
      return;
    auto draw = [this, cmd, fb, w, h] {
      REX_PROFILE_SCOPE("OverlayDraw");
      MaybeShowUpdatePrompt();
      UpdateCheckStatus();
      overlay_drawer_->SetTarget(cmd, fb, w, h);
      GetUI().DrawOverlays(*overlay_drawer_);
    };
    if (GetUI().IsUIThread()) {
      draw();
      return;
    }
    // Split so a capture prices the marshal (this zone minus the inner one)
    // apart from the ImGui work itself.
    REX_PROFILE_SCOPE("OverlayMarshal");
    GetUI().RunSynchronous(draw);
  });
}

bool ReblueApp::PendingOverlayWork() const {
  using Sync = bd::platform::ContentSync;
  using Updates = bd::platform::Updates;

  auto &updates = Updates::Get();
  if (updates.State() == Updates::Stage::kChecking)
    return true;
  if (updates.CanApply() && updates.HasNewer() &&
      updates.Generation() != update_prompt_generation_)
    return true;
  const auto sync = Sync::Get().State();
  return sync == Sync::Stage::kChecking || sync == Sync::Stage::kFetching;
}

void ReblueApp::UpdateCheckStatus() {
  const bool finished = bd::ui::UpdateStatusOverlay::Finished();
  if (!update_status_ && !finished) {
    update_status_ = std::make_unique<bd::ui::UpdateStatusOverlay>();
    update_status_guard_ = GetUI().AddDialog(*update_status_);
  } else if (update_status_ && finished) {
    update_status_guard_.Reset();
    update_status_.reset();
  }
}

void ReblueApp::MaybeShowUpdatePrompt() {
  if (update_prompt_ && update_prompt_->IsClosed())
    update_prompt_guard_.Reset();
    update_prompt_.reset();
  if (!bd::platform::Updates::Get().CanApply())
    return;
  const u32 generation = bd::platform::Updates::Get().Generation();
  if (generation == update_prompt_generation_)
    return;
  // The title puts this answer to the player in the engine's own windows.
  if (bd::engine::UpdatePrompt::Get().Active()) {
    update_prompt_generation_ = generation;
    return;
  }
  const auto newer = bd::platform::Updates::Get().Newer();
  if (!newer)
    return;
  update_prompt_generation_ = generation;

  bd::ui::UpdatePromptContext ctx;
  ctx.version = newer->version;
  if (const auto manifest = bd::platform::Updates::Get().Current()) {
    if (const auto *artifact = manifest->ArtifactForThisPlatform())
      ctx.size = artifact->size;
  }

  update_prompt_ = std::make_unique<bd::ui::UpdatePromptDialog>(std::move(ctx));
  update_prompt_guard_ = GetUI().AddDialog(*update_prompt_);
}

void ReblueApp::OnInputEvent(rex::InputEvent &) {}

void ReblueApp::OnUIEvent(const rex::ui::UIEvent &event) {
  using Type = rex::ui::UIEvent::Type;
  if (event.GetType() == Type::kClosing) {
    update_prompt_guard_.Reset();
    update_prompt_.reset();
    update_status_guard_.Reset();
    update_status_.reset();
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
    upgrade_prompt_guard_.Reset();
    upgrade_prompt_.reset();
#endif
#ifdef REBLUE_BUILD_INSTALLER
    installer_wizard_guard_.Reset();
  installer_wizard_.reset();
#endif
    return;
  }
  if (event.GetType() == Type::kResize)
    bd::gpu::Video::RequestResize();
}

std::span<const rex::CLIAction> ReblueApp::GetActions() const {
  return kActions;
}

rex::Result<void> ReblueApp::OnAction(const rex::CLIActionHit &hit) {
  if (hit.action.name == "repair")
    repair_requested_ = true;
  return rex::Ok();
}

// The renderer and host heap are bound to one title load, so a title exit ends
// the process.
void ReblueApp::OnTitleExit(rex::kernel::TitleExitReason reason) {
  const char *name = "unknown";
  switch (reason) {
  case rex::kernel::TitleExitReason::kNormalExit:
    name = "kNormalExit";
    break;
  case rex::kernel::TitleExitReason::kHardExit:
    name = "kHardExit";
    break;
  case rex::kernel::TitleExitReason::kTitleDataError:
    name = "kTitleDataError";
    break;
  case rex::kernel::TitleExitReason::kBugCheck:
    name = "kBugCheck";
    break;
  }
  REXBD_INFO("title exited ({})", name);
  rex::app::RequestShutdown(0);
}

void ReblueApp::OnShutdown() {
  bd::CvarUnwatchAll();
  bd::audio::Playback::Get().Bind(nullptr);
  bd::platform::SetTitle(nullptr);
  bd::mem::Bind(nullptr);
  ui_listener_guard_.Reset();
  bd::platform::ShutdownKeyCapture();
  bd::engine::Bindings::Get().Unregister();
  bd::vfs::VFS::Get().Prefetch().Shutdown();
  bd::vfs::VFS::Get().Log().Shutdown();
  bd::gpu::Video::BeginShutdown();
  bd::gpu::FlushPSOCapture();
  bd::gpu::StopPSOPrecache();
  bd::gpu::Video::Shutdown();
  bd::platform::SetUI(nullptr);
  bd::platform::SetGraphics(nullptr);
  bd::platform::SetInput(nullptr);
  bd::gpu::HostHeap::Get().Shutdown();
}
