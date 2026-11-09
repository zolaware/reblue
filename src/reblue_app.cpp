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

#include <imgui.h>

#include <rex/api.h>
#include <rex/apu.h>
#include <rex/app.h>
#include <rex/build_stamp.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/hid.h>
#include <rex/platform/file.h>
#include <rex/platform/process.h>
#include <rex/perf/counter.h>
#include <rex/hid/key_chord.h>
#include <rex/ui/overlay.h>
#include <rex/ui/user_interface.h>
#include <rex/version.h>

#include <rex/runtime.h>
#include <rex/string/utf8.h>

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

#if defined(_WIN32)
std::filesystem::path ProgramDir() {
  return rex::platform::GetExecutablePath().parent_path();
}
#endif

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

// Profile names become directory names: strip control chars and path
// separators, trim Windows-hostile trailing space/dot, reject traversal.
std::string SanitizeProfileName(const std::string &raw) {
  std::string out;
  for (char c : raw) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20)
      continue;
    if (c == '/' || c == '\\' || c == ':')
      continue;
    out += c;
  }
  auto b = out.find_first_not_of(" .");
  auto e = out.find_last_not_of(" .");
  if (b == std::string::npos)
    return "default";
  out = out.substr(b, e - b + 1);
  if (out.empty() || out == "." || out == "..")
    return "default";
  return out;
}

// Resolve install_root without the wizard: --game_data_root's parent if set,
// else a schema-matched install record. nullopt on a fresh, unregistered
// install (the installer wires the profile later, in FinishInstaller).
std::optional<std::filesystem::path>
EarlyInstallRoot(const std::filesystem::path &game_data_root) {
  if (!game_data_root.empty())
    return std::filesystem::absolute(game_data_root).parent_path();
  if (auto cfg = bd::installer::InstallConfig::Read())
    if (cfg->schema_version == bd::installer::kInstallSchemaVersion)
      return cfg->install_root;
  return std::nullopt;
}

// BD reads XCONFIG_USER_LANGUAGE once at boot. bd.language is a code from the
// install's own bd_boot.ini Language Set, or 'auto' to follow the host display
// language. Returns 0 through unknown_code when the code names no language.
u32 ResolveUserLanguage(std::string code, bool &autodetect,
                        std::string &unknown_code) {
  code = rex::utf8_lower_ascii(code);
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

REX_OVERLAY_TABLE(
    kReblueOverlays,
    REX_OVERLAY(bd::ui::FadeOverlay, "fade", rex::hid::KeyChord{}),
    REX_OVERLAY(bd::ui::PauseOverlay, "pause", rex::hid::KeyChord{}),
    (rex::ui::OverlayDesc{"console", {rex::hid::VirtualKey::kOem3}, nullptr}));
REX_OVERLAY_TABLE(
    kReblueDevOverlays,
    (rex::ui::OverlayDesc{"settings", {rex::hid::VirtualKey::kF4}, nullptr}),
    (rex::ui::OverlayDesc{"achievements", {rex::hid::VirtualKey::kF7}, nullptr}));

} // namespace

ReblueApp::ReblueApp() : ui_thread_(std::this_thread::get_id()) {
  bd::engine::Bindings::Get().Init();
}

ReblueApp::~ReblueApp() { bd::mem::Bind(nullptr); }

// Runs once every module is connected and the config file, the environment and
// the command line are applied, so every setting reads its final value here.
rex::Result<void> ReblueApp::OnInit(rex::Config &config, rex::API &api) {
  cvars_ = api.Get<rex::cvar::ICvarRegistry>();
  auto *hid = api.Get<rex::hid::IHIDBackend>();
  if (!hid)
    return rex::Err(rex::ErrorCategory::kModule, "reblue needs rex-hid-sdl3 for pad, keyboard and mouse input");
  bd::platform::SetHID(hid);
  auto *ui = api.Get<rex::ui::IUserInterface>();
  if (!ui)
    return rex::Err(rex::ErrorCategory::kModule,
                    "reblue needs rexui-sdl3 for its window, dialogs and "
                    "overlays");
  bd::platform::SetUI(ui);
  if (!location_warning_.empty())
    ui->ShowMessage(rex::ui::MessageKind::kWarning, "re:Blue",
                    location_warning_);
  bd::audio::Settings::Get().Attach(api.Get<rex::apu::IAPUBackend>());
  bd::SetCvars(cvars_);
  bd::engine::Bindings::Get().Register(*cvars_);
  bd::SettingsMigration::Apply();
  // Every Settings object adopts the config file and the command line here, so
  // every later hook can read a setting through its object.
  bd::Settings::Get().Init();
  bd::gpu::Settings::Get().Init();
  bd::audio::Settings::Get().Init();
  bd::vfs::Settings::Get().Init();
  bd::engine::Settings::Get().Init();
  bd::engine::Bindings::Get().ReportParseErrors();
  bd::engine::GameOptions::Get().Init();

  bd::engine::Achievements::Init();
  bd::engine::Gimmicks::Get().Init();

  REXLOG_INFO(bd, "re:Blue v" REBLUE_VERSION_STRING " [" REXGLUE_BUILD_CONFIG
          "] " REBLUE_BUILD_PLATFORM);
  REXLOG_INFO(bd, "  commit:  " REBLUE_GIT_COMMIT " on " REBLUE_GIT_BRANCH "{}{}",
          REBLUE_GIT_DIRTY ? " (local modifications)" : "",
          kOfficialBuild ? "" : " (development build)");
  REXLOG_INFO(bd, "  built:   " REBLUE_BUILD_TIMESTAMP " with " REBLUE_BUILD_COMPILER);
  REXLOG_INFO(bd, "  sdk:     {}", rex::GetBuildStamp());

  // bd_profiler predates perf.profiler and is read from the config file, which
  // loads after OnConfigure could have mapped it.
  if (bd::Settings::Get().Profiler()) {
    rex::platform::Profiler::Startup();
    if (rex::platform::Profiler::IsEnabled())
      REXLOG_INFO(bd, "Tracy profiler started, connect a viewer to capture.");
    else
      REXLOG_WARN(bd, "bd_profiler set, but this build has no profiler compiled in.");
  }

  // Before the runtime's APU latches it. The queue is the only buffer between the
  // guest mixer and the device, and at the stock depth a mixer pump stall
  // overruns it audibly. Moving the default keeps an explicit user value.
  if (auto moved = cvars_->SetDefault(
          "apu.max_queued_frames",
          std::to_string(bd::audio::Settings::Get().QueueFrames()));
      !moved)
    REXLOG_WARN(bd, "{}", moved.error().message);

  // The runtime reads these slices when LaunchTitle constructs it.
  config.runtime.kernel.user_language =
      ResolveUserLanguage(REXCVAR_GET(bd::cvars::language),
                          language_autodetected_, unknown_language_code_);
  // Devmode aims dumps and captures at the game folder, which the SDK mounts
  // read-only otherwise.
  if (bd::Settings::Get().Devmode())
    config.filesystem.game_mount_flags = rex::filesystem::MountFlags::kWritable;
  return rex::Ok();
}

void ReblueApp::OnCreateWindow(rex::ui::TitleUI &ui) {
  bd::ui::PauseOverlay::InitFonts(&ui.GetFonts());
#ifdef REBLUE_BUILD_INSTALLER
  bd::installer::InitInstallerFonts(&ui.GetFonts());
#endif
  bd::ui::Theme::Apply(ui.GetStyle());
  bd::ui::Theme::ApplyImGui(ui.GetImGuiStyle());
  auto overlay_drawer = std::make_unique<bd::gpu::ImGuiOverlayDrawer>();
  overlay_drawer_ = overlay_drawer.get();
  ui.SetOverlayDrawer(std::move(overlay_drawer));
  ui.GetWindow().SetTitle(std::string("re:Blue v" REBLUE_VERSION_STRING " (") +
                       bd::gpu::Video::BackendName() + ")");
  ui.GetWindow().SetAspectRatio(bd::gpu::Output::ConfiguredAspectRatio());
  (void)ui.GetUI().AddOverlays(kReblueOverlays);
  ui.GetUI().ShowOverlay("fade", true);
  ui.GetUI().ShowOverlay("pause", true);
  if (bd::Settings::Get().Devmode())
    (void)ui.GetUI().AddOverlays(kReblueDevOverlays);
}

rex::Result<void> ReblueApp::OnConfigure(rex::Config &config) {
  config.single_instance = "reblue";
  // reblue renders natively, so it loads no graphics module.
  config.modules = {"rexapu-sdl3", "rexhid-sdl3", "rexui-sdl3"};
  // Blue Dragon is single player and polls title user 0, so every pad drives it.
  config.hid.shared_pads = true;
  config.ui.min_size = {640, 360};
  config.cvars.tables.push_back({"bd", bd::cvars::kReblueCvars});
  config.cvars.SetDefault("ui.cursor_timeout", "5");

  // The SDK's bind defaults are arbitrary. These are the PC convention layout
  // for this game. A default moves under a value the user set, so a rebound
  // key stays what they chose. Cancel rides on RMB the way confirm rides on
  // LMB, and Q pairs with E on the other shoulder so the shop pager and the
  // leader cycle stay reachable.
  struct BindDefault {
    const char *cvar;
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
    config.cvars.SetDefault(bind.cvar, bind.value);

  // The profile comes from the command line, before any config file is read,
  // so a stale key in a profile toml can never override the launcher's choice.
  std::string requested = "default";
  if (auto profile = GetArgs().Get("bd.profile"))
    requested = std::string(*profile);
  active_profile_ = SanitizeProfileName(requested);

  rex::PathsConfig &paths = config.paths;
  config.log.categories.push_back(&rex::log::bd);
  config.log.file.folder = bd::AppRootFolder() / "logs";
  config.log.file.folder_budget = kLogDirBudget;
  config.log.flush_interval = std::chrono::seconds(1);

  // A package is not a data directory. Before an install exists, anchor its
  // transient config and the installer's default destination in writable user
  // storage, a completed install replaces this with the selected profile path.
  if (bd::IsPackagedApplication()) {
    paths.config_file = bd::AppRootFolder() / "reblue.toml";
  }
  bd::SettingsMigration::RewriteFile(paths.config_file);

  auto install_root = EarlyInstallRoot(
      std::filesystem::path(GetArgs().Get("game_data_root").value_or("")));
  if (!install_root)
    return rex::Ok(); // fresh install: FinishInstaller wires the profile

  install_root_ = *install_root;
  bd::SetAppRoot(install_root_);
  config.log.file.folder = bd::AppRootFolder() / "logs";
  profile_root_ = install_root_ / "profiles" / active_profile_;
  std::error_code ec;
  std::filesystem::create_directories(profile_root_, ec);

  const auto profile_cfg = profile_root_ / "reblue.toml";
  paths.user_data_root = profile_root_;
  paths.config_file = profile_cfg;
  bd::SettingsMigration::RewriteFile(profile_cfg);
  bd::platform::SetProfileContext(active_profile_, profile_cfg);

#if defined(_WIN32)
  const auto program_dir = ProgramDir();
  if (program_dir != install_root_) {
    const auto warning =
        std::format("Warning: re:Blue is running outside of its install "
                    "directory. Please run the exe from {}",
                    install_root_.string());
    if (rex::platform::IsDebuggerAttached())
      REXLOG_WARN(bd, "{}", warning);
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
      rex::RequestRestart(bd::platform::ProfileArgs());
      return rex::Ok();
    }
  }
#endif
  return rex::Ok();
}

rex::PathsConfig
ReblueApp::PathsForInstall(const rex::PathsConfig &defaults,
                           const bd::installer::InstallConfig &cfg) {
  rex::PathsConfig paths = defaults;
  paths.game_data_root = cfg.game_data_path();
  paths.user_data_root = profile_root_; // profiles/<name>, not <root>/user
  paths.cache_root = ResolveCacheRoot();
  return paths;
}

bool ReblueApp::NeedsUpgradePrompt(
    const bd::installer::InstallConfig &cfg) const {
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
  return !cfg.Portable() && ProgramDir() != cfg.install_root;
#else
  (void)cfg;
  return false;
#endif
}

void ReblueApp::RestampInstall(const bd::installer::InstallConfig &cfg) {
  if (!cfg.Write())
    REXLOG_WARN(bd, "Install record restamp failed, this upgrade will be offered again");
}

#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
void ReblueApp::BeginUpgrade(const bd::installer::InstallConfig &cfg) {
  upgrade_prompt_ = std::make_unique<bd::installer::UpgradePrompt>(
      cfg.install_root, cfg.app_version,
      [this, cfg](bool accepted) { FinishUpgrade(accepted, cfg); });
  GetUI().AddDialog(*upgrade_prompt_);
}

void ReblueApp::FinishUpgrade(bool accepted,
                              const bd::installer::InstallConfig &cfg) {
  if (!accepted) {
    REXLOG_INFO(bd, "Upgrade declined, booting the install as it stands");
    GetAPI().UpdatePaths(PathsForInstall(GetAPI().GetConfig().paths, cfg));
    upgrade_prompt_.reset();
    return;
  }
  upgrade_prompt_.reset();

  std::string copy_error;
  if (!bd::installer::CopyProgramTo(cfg.install_root, copy_error)) {
    bd::platform::ShowFatal(
        "Upgrade failed", "Could not copy " + copy_error + " into " +
                              cfg.install_root.string() +
                              "\n\nThe installed version is untouched.");
    rex::RequestShutdown(1);
    return;
  }
  RestampInstall(cfg);
  handoff_exe_ = cfg.install_root / "reblue.exe";
  rex::RequestShutdown(0);
}
#endif

bool ReblueApp::PrepareSetup() {
  const rex::PathsConfig defaults = GetAPI().GetConfig().paths;
  const bool user_supplied_path = GetArgs().Has("game_data_root");

  if (user_supplied_path) {
    if (!std::filesystem::exists(defaults.game_data_root / "default.xex")) {
      bd::platform::ShowFatal(
          "reblue - invalid game directory",
          "Path '" + defaults.game_data_root.string() +
              "' does not contain default.xex. "
              "Pass the directory that contains default.xex (typically the "
              "'base' subdirectory of an installed copy).");
      rex::RequestShutdown(1);
      return false;
    }
    rex::PathsConfig paths = defaults;
    paths.cache_root = ResolveCacheRoot();
    GetAPI().UpdatePaths(paths);
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
        REXLOG_INFO(bd, "Install at {} records version '{}', running '{}'",
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
      REXLOG_INFO(bd, "Resolved install from {}",
              bd::installer::ToString(cfg->connector));
      REXLOG_INFO(bd, "  install root:   {}", cfg->install_root.string());
      REXLOG_INFO(bd, "  game data:      {}", cfg->game_data_path().string());
      REXLOG_INFO(bd, "  user data:      {}", cfg->user_data_path().string());
      for (int i = 0; i < bd::installer::kDiscCount; ++i)
        REXLOG_DEBUG(bd, "  disc{} hash:     {}", i + 1, cfg->iso_fingerprints[i]);
      GetAPI().UpdatePaths(PathsForInstall(defaults, *cfg));
      return false;
    }
    if (repair_requested)
      REXLOG_INFO(bd, "--repair: opening wizard in repair mode on existing install");
    else
      REXLOG_DEBUG(bd, "Install schema {} != current {}, opening wizard in repair mode",
               cfg->schema_version, bd::installer::kInstallSchemaVersion);
    existing_install = std::move(cfg);
  }

#ifdef REBLUE_BUILD_INSTALLER
  if (REXCVAR_GET(bd::cvars::no_installer)) {
    bd::platform::ShowFatal(
        "reblue - game not installed",
        "Game not installed. Either run without --no-installer to launch the "
        "installer, or pass the installed game directory as the first "
        "argument.");
    rex::RequestShutdown(1);
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
      overlay_drawer_, default_install_dir, repair,
      existing_install ? &*existing_install : nullptr,
      [this](bool completed, const auto &cfg, const auto &choices) {
        FinishInstaller(completed, cfg, choices);
      });
  GetUI().AddDialog(*installer_wizard_);
  return true;
#else
  // No built-in installer: the external reBlue launcher performs the install,
  // writing the install record that InstallConfig::Read consumes above.
  bd::platform::ShowFatal(
      "reblue - game not installed",
      "No installed game was found. Install reblue with the reBlue launcher, "
      "or pass the installed game directory as the first argument.");
  rex::RequestShutdown(1);
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
    REXLOG_ERROR(bd, "handoff relaunch failed: {}", launched.error().message);
    bd::platform::ShowFatal("Could not start re:Blue",
                            "Run " + handoff_exe_->string() + " by hand.");
  }
}

bool ReblueApp::BeginPreTitleUI() {
  rex::ui::IWindow *window = GetWindow();
  if (!window || !bd::gpu::Video::CreateHostDevice(*window)) {
    bd::platform::ShowFatal("reblue - renderer init failed",
                            "Failed to initialize the renderer.");
    rex::RequestShutdown(1);
    return false;
  }
  InstallOverlayDrawHook();
  window->Raise();
  return true;
}

#ifdef REBLUE_BUILD_INSTALLER
void ReblueApp::FinishInstaller(bool completed,
                                const bd::installer::InstallConfig &cfg,
                                const bd::installer::WizardChoices &choices) {
  installer_wizard_.reset();

  if (!completed) {
    rex::RequestShutdown(1);
    return;
  }

  install_root_ = cfg.install_root;
  bd::SetAppRoot(install_root_);

#if defined(_WIN32)
  std::string copy_error;
  if (!bd::installer::CopyProgramTo(install_root_, copy_error)) {
    const auto answer = bd::platform::UI()->ShowChoice(
        {.kind = rex::ui::MessageKind::kError,
         .title = "Install failed",
         .body = "Could not copy " + copy_error + " into " +
                 install_root_.string() +
                 "\n\nNothing was recorded, so a retry starts over.",
         .buttons = {"Retry", "Quit"}});
    if (answer && *answer == 0) {
      rex::RequestRestart(bd::platform::ProfileArgs());
      return;
    }
    rex::RequestShutdown(1);
    return;
  }
#endif

  if (!cfg.Write())
    REXLOG_WARN(bd, "Install record write failed, continuing into game for this session");
  REXLOG_INFO(bd, "Installed to {}", cfg.install_root.string());
  for (int i = 0; i < bd::installer::kDiscCount; ++i)
    REXLOG_INFO(bd, "  disc{} hash:     {}", i + 1, cfg.iso_fingerprints[i]);

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
      for (const rex::cvar::VarBase *var : cvars_->Enumerate()) {
        if (var->GetDesc().type != rex::cvar::Type::kCommand)
          (void)cvars_->ResetToDefault(var->GetDesc().name);
      }
    } else if (std::filesystem::exists(profile_cfg, ec)) {
      rex::Config saved;
      if (rex::LoadConfigFile(saved, profile_cfg)) {
        for (const auto &o : saved.cvars.values)
          (void)cvars_->Set(o.name, o.value);
      }
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
    if (auto written = rex::SaveConfigFile(*cvars_, profile_cfg); !written)
      REXLOG_WARN(bd, "config save failed: {}", written.error().message);
  }

#if defined(_WIN32)
  if (choices.create_shortcut) {
    std::string shortcut_error;
    if (!bd::platform::CreateDesktopShortcut(
            install_root_ / bd::installer::kGameExecutable, "re:Blue",
            shortcut_error))
      REXLOG_WARN(bd, "Could not create the desktop shortcut: {}", shortcut_error);
  }

  // What boots the game is the exe sitting in the install directory, which is
  // not always here. Everything above has to be written before the hand-off:
  // what starts next reads it back.
  if (ProgramDir() != install_root_) {
    handoff_exe_ = install_root_ / bd::installer::kGameExecutable;
    rex::RequestShutdown(0);
    return;
  }
#endif

  rex::PathsConfig paths = GetAPI().GetConfig().paths;
  paths.game_data_root = cfg.game_data_path();
  paths.user_data_root = profile_root_;
  paths.cache_root = ResolveCacheRoot();
  GetAPI().UpdatePaths(paths);
}
#endif // REBLUE_BUILD_INSTALLER

rex::Result<void> ReblueApp::OnTitleLaunch(rex::Runtime &rt) {
  bd::mem::Bind(&rt);

  {
    const u32 lang = GetAPI().GetConfig().runtime.kernel.user_language;
    if (!unknown_language_code_.empty())
      REXLOG_WARN(bd, "Unknown bd.language '{}', using English. Valid: auto, us, jp, "
              "de, fr, es, it, kr, tw, cn, po",
              unknown_language_code_);
    REXLOG_INFO(bd, "UI language: {} (XLanguage {}, {})",
            bd::platform::XLanguageName(lang), lang,
            language_autodetected_ ? "auto-detected" : "config override");
  }

  const auto profile_root = rt.GetUserDataRoot();
  REXLOG_INFO(bd, "profile active '{}' (user data: {})", active_profile_,
          profile_root.string());

  bd::vfs::VFS::Get().Init(rt.GetGameDataRoot(), rt.GetCacheRoot());
  bd::vfs::VFS::Get().SetProfile(profile_root);

  // Arms the channel watch. The check itself runs at the title, ahead of
  // the guest's own downloadable-content load.
  bd::platform::Updates::Get().Init(install_root_);

  bd::engine::MountSaves(rt.GetFileSystem(), ResolveSavesRoot(profile_root));

  if (bd::Settings::Get().Devmode())
    bd::vfs::VFS::Get().PrepareDevWriteDirs();

  // Enabled packs are published into the profile's XAM content tree so the SDK
  // ContentManager enumerates and mounts them and the guest's own title screen
  // DLC pipeline runs natively.
  bd::vfs::VFS::Get().DLC().Publish();
  // Map/standard pack content (dungeon scripts, geometry, models, enemy AI)
  // that the guest does not route through its single "download" IPK slot.
  bd::vfs::VFS::Get().DLC().MountArchives();

  rex::ui::IWindow *window = GetWindow();
  if (!window || !bd::gpu::Video::CreateHostDevice(*window))
    return rex::Err<void>(rex::ErrorCategory::kRuntime,
                          "native renderer initialization failed");
  InstallOverlayDrawHook();
  window->Raise();
  return rex::Ok();
}

// Present runs on the guest thread and ImGui on the UI thread, so this marshals
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
    if (rex::IsShutdownRequested())
      return;
    // Draw would return having done nothing, and the round trip parks this
    // thread for as long as the UI thread takes to wake.
    if (!GetUI().HasDialogs() && !PendingOverlayWork())
      return;
    auto draw = [this, cmd, fb, w, h] {
      REX_PROFILE_SCOPE("OverlayDraw");
      MaybeShowUpdatePrompt();
      UpdateCheckStatus();
      bd::gpu::ReblueUIDrawContext ctx(w, h, cmd, fb);
      GetUI().DrawOverlays(ctx);
    };
    if (std::this_thread::get_id() == ui_thread_) {
      draw();
      return;
    }
    // Split so a capture prices the marshal (this zone minus the inner one)
    // apart from the ImGui work itself.
    REX_PROFILE_SCOPE("OverlayMarshal");
    auto drawn = std::make_shared<std::promise<void>>();
    auto done = drawn->get_future();
    if (!rex::ui::InvokeFunction([draw, drawn] {
          draw();
          drawn->set_value();
        }))
      return;
    done.wait();
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
    GetUI().AddDialog(*update_status_);
  } else if (update_status_ && finished) {
    update_status_.reset();
  }
}

void ReblueApp::MaybeShowUpdatePrompt() {
  if (update_prompt_ && update_prompt_->IsClosed())
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
  GetUI().AddDialog(*update_prompt_);
}

bool ReblueApp::OnEvent(const rex::ui::UIEvent &event) {
  using Type = rex::ui::UIEvent::Type;
  if (event.GetType() == Type::kClosing) {
    update_prompt_.reset();
    update_status_.reset();
#if defined(_WIN32) && defined(REBLUE_BUILD_INSTALLER)
    upgrade_prompt_.reset();
#endif
#ifdef REBLUE_BUILD_INSTALLER
    installer_wizard_.reset();
#endif
    return false;
  }
  if (event.GetType() == Type::kResize) {
    bd::gpu::Video::RequestResize();
    return false;
  }
  // The ImGui drawer sat above the keyboard and mouse latches, so a press it
  // wants reaches it first and the game never sees it.
  const bool press = event.GetType() == Type::kKeyDown ||
                     event.GetType() == Type::kChar ||
                     event.GetType() == Type::kMouseDown ||
                     event.GetType() == Type::kMouseMove ||
                     event.GetType() == Type::kMouseWheel;
  const ImGuiIO *io = press && GetUI().HasDialogs() ? GetUI().GetImGuiIO() : nullptr;
  if (io) {
    const bool keyboard = event.As<rex::ui::KeyEvent>() != nullptr;
    if (keyboard ? io->WantCaptureKeyboard : io->WantCaptureMouse)
      return false;
  }
  if ((event.GetType() == Type::kMouseDown || event.GetType() == Type::kMouseWheel) &&
      bd::engine::HostOverlayOwnsPointer())
    return true;
  return false;
}

std::span<const rex::CLIAction> ReblueApp::GetActions() const {
  return kActions;
}

rex::Result<void> ReblueApp::OnAction(const rex::CLIAction &action,
                                      const rex::CLIOption &) {
  if (action.name == "repair")
    repair_requested_ = true;
  return rex::Ok();
}

void ReblueApp::OnShutdown() {
  if (cvars_)
    bd::engine::Bindings::Get().Unregister();
  bd::vfs::VFS::Get().Prefetch().Shutdown();
  bd::vfs::VFS::Get().Log().Shutdown();
  bd::gpu::Video::BeginShutdown();
  bd::gpu::FlushPSOCapture();
  bd::gpu::StopPSOPrecache();
  bd::gpu::Video::Shutdown();
  bd::platform::SetUI(nullptr);
  bd::gpu::HostHeap::Get().Shutdown();
}
