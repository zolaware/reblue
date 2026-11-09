/**
 * @file    core/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/settings.h"

#include "core/cvar_table.h"

#include "core/build_info.h"

#include <algorithm>
#include <charconv>
#include <system_error>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <rex/settings_registry.h>
#include <rex/title.h>
#include <rex/ui/ui_system.h>

#include "platform/host_ui.h"

namespace bd {
namespace {

std::vector<SettingWatches *> g_watch_lists;

template <typename T>
void WatchAs(SettingWatches &handles, rex::SettingBase &base,
             std::shared_ptr<std::function<void(std::string_view,
                                                std::string_view)>> fn) {
  auto &typed = static_cast<rex::Setting<T> &>(base);
  handles.push_back(typed.OnChange([fn, &base](const T &) {
    (*fn)(base.GetName(), base.GetText());
  }));
}

UpdateChannel ChannelOfName(const std::string &name) {
  return name == ToString(UpdateChannel::Nightly) ? UpdateChannel::Nightly
                                                  : UpdateChannel::Stable;
}

} // namespace

rex::SettingBase *FindSetting(std::string_view name) {
  auto found = rex::GetSettingsRegistry().Find(name);
  return found ? *found : nullptr;
}

bool CvarSet(std::string_view name, std::string_view value) {
  return rex::GetSettingsRegistry().SetText(name, value).has_value();
}

std::string CvarGet(std::string_view name) {
  const rex::SettingBase *setting = FindSetting(name);
  return setting ? setting->GetText() : std::string();
}

bool CvarIsSet(std::string_view name) {
  const rex::SettingBase *setting = FindSetting(name);
  return setting && setting->GetLayer() > rex::SettingLayer::kTitle;
}

f64 CvarDouble(std::string_view name, f64 fallback) {
  const std::string text = CvarGet(name);
  f64 out = fallback;
  auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  return r.ec == std::errc() ? out : fallback;
}

i32 CvarInt(std::string_view name, i32 fallback) {
  const std::string text = CvarGet(name);
  i32 out = fallback;
  auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  return r.ec == std::errc() ? out : fallback;
}

void CvarUnwatchAll() {
  for (SettingWatches *handles : g_watch_lists)
    handles->clear();
  g_watch_lists.clear();
}

void CvarWatch(SettingWatches &handles, std::string_view name,
               std::function<void(std::string_view, std::string_view)> fn) {
  rex::SettingBase *base = FindSetting(name);
  if (!base)
    return;
  if (std::find(g_watch_lists.begin(), g_watch_lists.end(), &handles) ==
      g_watch_lists.end())
    g_watch_lists.push_back(&handles);
  auto shared = std::make_shared<
      std::function<void(std::string_view, std::string_view)>>(std::move(fn));
  switch (base->GetType()) {
  case rex::SettingType::kBool:
    return WatchAs<bool>(handles, *base, shared);
  case rex::SettingType::kI32:
    return WatchAs<i32>(handles, *base, shared);
  case rex::SettingType::kI64:
    return WatchAs<i64>(handles, *base, shared);
  case rex::SettingType::kU32:
    return WatchAs<u32>(handles, *base, shared);
  case rex::SettingType::kU64:
    return WatchAs<u64>(handles, *base, shared);
  case rex::SettingType::kF64:
    return WatchAs<f64>(handles, *base, shared);
  case rex::SettingType::kString:
    return WatchAs<std::string>(handles, *base, shared);
  case rex::SettingType::kPath:
    return WatchAs<std::filesystem::path>(handles, *base, shared);
  }
}

std::filesystem::path CacheRoot() {
  return std::filesystem::path(CvarGet("app.cache_root"));
}

bool CvarReset(std::string_view name) {
  return rex::GetSettingsRegistry().Reset(name).has_value();
}

bool CvarResolution(i32 &w, i32 &h) {
  rex::ui::IUISystem *ui = platform::UI();
  const bool configured = !CvarGet("ui.resolution").empty() ||
                          CvarIsSet("ui.video_mode_width") ||
                          CvarIsSet("ui.video_mode_height");
  if (!ui || !configured)
    return false;
  const rex::VideoMode mode = ui->GetVideoMode();
  w = static_cast<i32>(mode.width);
  h = static_cast<i32>(mode.height);
  return w > 0 && h > 0;
}

rex::Result<void> SaveConfig(const std::filesystem::path &file) {
  return rex::GetSettingsRegistry().Save(file);
}

namespace cvars {

Table &Table::Get() {
  static Table *table = new Table();
  return *table;
}

} // namespace cvars

const char *ToString(UpdateChannel channel) {
  switch (channel) {
  case UpdateChannel::Nightly:
    return "nightly";
  case UpdateChannel::Stable:
    break;
  }
  return "stable";
}

UpdateChannel GetUpdateChannel() {
  return ChannelOfName(bd::cvars::Get().update_channel.Get());
}

std::string GetUpdateURL() {
  const std::string base = bd::cvars::Get().update_base.Get();
  return base.empty() ? std::string()
                      : base + "/manifest/" + ToString(GetUpdateChannel()) +
                            ".toml";
}

} // namespace bd
