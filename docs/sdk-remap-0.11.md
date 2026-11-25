# SDK include remap table - this tree vs rexglue 0.11 / 0.12

Why this file exists: CI pins the SDK at stable `v0.10.1`, the newest release that
still exports the CMake targets this tree links (`rex::runtime`, `rex::o1heap`).
Everything on the 0.11 line and later renames those targets *and* reshapes the
header tree, so moving forward is a port, not a version bump. This is the
measurement of that port, taken on 2026-11-25 by extracting the `v0.11.0` and
`nightly-20261122-44b7fdc5` SDK archives and diffing them against the includes
used here. It is a checklist for a future branch, not a plan anyone has to execute
now.

60 distinct `#include <rex/...>` paths across the 279 files here that include one - 490
include lines, measured with grep over `src/`, `thirdparty/` and `cmake/` on 2026-11-25.
(`generated/` adds 4 more paths and 9 more lines, but `generated/reblue_pch.h` and
`generated/reblue_register.cpp` are written by the SDK's own codegen and rewrite themselves,
so no port touches them by hand.)
Verdict: SAME = no edit | MOVED = path-only sed | SPLIT = one old header -> several new | REWORK = API redesign | GONE = no counterpart | VERIFY = candidate only.

| old include | files | verdict | new include(s) | note |
|---|--:|---|---|---|
| `rex/types.h` | 209 | SAME | `rex/types.h` |  |
| `rex/hook.h` | 55 | MOVED | `rex/ppc/hook.h` |  |
| `rex/ppc.h` | 29 | SPLIT | `rex/ppc/{context,call,stack,vec128,vmx,fpu,midasm}.h` | umbrella header gone |
| `rex/runtime.h` | 26 | SPLIT | `rex/kernel/title_runtime.h + rex/kernel/{function_dispatcher,module}.h + rex/app/app.h` | RuntimeConfig gone |
| `rex/cvar.h` | 25 | REWORK | `rex/setting.h + rex/settings_registry.h + rex/console_command.h` | REX_SETTING/REX_COMMAND are members of SettingOwner -> every block restructure |
| `rex/system/kernel_state.h` | 11 | SPLIT | `rex/kernel/title_runtime.h + kernel/thread_registry.h + kernel/module.h + kernel/types/thread.h` |  |
| `rex/filesystem.h` | 11 | SPLIT | `rex/filesystem/file_system.h + rex/platform/file.h` | GetExecutableFolder/Path moved to platform/file.h |
| `rex/ppc/stack.h` | 8 | SAME | `rex/ppc/stack.h` |  |
| `rex/ui/window.h` | 7 | SAME | `rex/ui/window.h` |  |
| `rex/ui/imgui_dialog.h` | 6 | MOVED | `rex/ui/dialog.h` | class renamed; ChoiceConfig/MessageType |
| `rex/graphics/xenos.h` | 6 | SPLIT | `rex/xenos/format.h + rex/xenos/fetch.h (+ state.h)` |  |
| `rex/logging.h` | 5 | MOVED | `rex/logging/logger.h` |  |
| `rex/ui/virtual_key.h` | 4 | MOVED | `rex/input_keys.h` | VirtualKey moved to rex root |
| `rex/ui/keybinds.h` | 4 | REWORK | `rex/hid/binding.h + rex/input_keys.h` | bind registry moved into InputSystem |
| `rex/system/xmemory.h` | 4 | MOVED | `rex/kernel/heap.h (+ kernel/guest_memory.h, kernel/types/memory.h)` | PhysicalHeap/VirtualHeap -> PhysicalHeap/VirtualAllocator |
| `rex/system/thread_state.h` | 4 | MOVED | `rex/kernel/thread_state.h` |  |
| `rex/memory/utils.h` | 4 | SPLIT | `rex/memory/endian.h (make_fourcc) + rex/hash.h (hash_combine)` |  |
| `rex/filesystem/entry.h` | 4 | MOVED | `rex/filesystem/directory.h + rex/filesystem/file_stat.h` |  |
| `rex/ui/windowed_app_context.h` | 3 | REWORK | `rex/app/app.h` | WindowedAppContext gone; App owns lifecycle |
| `rex/ui/ui_event.h` | 3 | SAME | `rex/ui/ui_event.h` |  |
| `rex/system/xcontent.h` | 3 | MOVED | `rex/xam/content_manager.h + rex/kernel/types/content.h` |  |
| `rex/platform/env.h` | 3 | VERIFY | `(no counterpart found)` | host env helpers - vendor into src/host or drop |
| `rex/graphics/pipeline/texture/util.h` | 3 | MOVED | `rex/xenos/texture.h` | GetSubresourcesFromFetchConstant etc. verified there |
| `rex/graphics/pipeline/texture/conversion.h` | 3 | MOVED | `rex/xenos/texture.h` | ConvertTexel*/Untile verified there |
| `rex/version.h` | 2 | SAME | `rex/version.h` |  |
| `rex/ui/window_listener.h` | 2 | REWORK | `rex/ui/ui_driver.h + rex/platform/native_window.h` | listener interfaces gone |
| `rex/ui/immediate_drawer.h` | 2 | GONE | `(no counterpart)` | ImmediateDrawer absent -> vendor into src/ui |
| `rex/ui/flags.h` | 2 | GONE | `(no counterpart)` | vendor the flags enum into the tree |
| `rex/ui/presenter.h` | 1 | GONE | `(no counterpart)` | Presenter/UIDrawContext/AppUIDrawContext all gone; src/gpu/imgui_overlay_drawer.h subclasses rex::ui::AppUIDrawContext -> vendor that pair or rebuild on rex/ui/ui_driver.h |
| `rex/ui/style.h` | 1 | SAME | `rex/ui/style.h` | rex::ui::Style still there in 0.11/0.12, grown (Palette, AchievementsStyle/ToastStyle/SettingsStyle); src/ui/theme.h::Apply keeps compiling |
| `rex/system/xam/content_manager.h` | 2 | MOVED | `rex/xam/content_manager.h` |  |
| `rex/system/function_dispatcher.h` | 2 | MOVED | `rex/kernel/function_dispatcher.h` |  |
| `rex/system/achievement_manager.h` | 1 | MOVED | `rex/xam/achievement_manager.h` |  |
| `rex/system/achievements.h` | 1 | MOVED | `rex/xam/achievement_manager.h` | Unlock/Register verified there |
| `rex/system/format.h` | 1 | MOVED | `rex/xenos/format.h` |  |
| `rex/system/xtypes.h` | 1 | SPLIT | `rex/kernel/types/{list,thread,io}.h` |  |
| `rex/system/xthread.h` | 1 | SPLIT | `rex/kernel/object/kernel_thread.h + rex/kernel/types/thread.h` |  |
| `rex/string.h` | 1 | VERIFY | `rex/string/{text,numeric,path}.h` | trim_string/compare_case* not found -> vendor helpers |
| `rex/rex_app.h` | 1 | MOVED | `rex/app/app.h (+ rex/app/title_app.h)` | ReXApp -> rex::App |
| `rex/ppc/function.h` | 1 | MOVED | `rex/ppc/call.h + rex/ppc/hook.h` |  |
| `rex/ppc/func.h` | 1 | MOVED | `rex/ppc/call.h` | REX_XSTRINGIFY now in rex/platform.h |
| `rex/perf/counter.h` | 1 | MOVED | `rex/platform/perf.h` | basename renamed |
| `rex/memory/mapped_memory.h` | 1 | MOVED | `rex/memory/mapped_ptr.h + rex/kernel/memory_view.h` | MappedMemory -> MappedPtr + MemoryView |
| `rex/logging/api.h` | 1 | MOVED | `rex/logging/log_category.h + rex/logging/logger.h` |  |
| `rex/input/input_system.h` | 1 | MOVED | `rex/hid/input_system.h` |  |
| `rex/input/device_assignment.h` | 1 | VERIFY | `rex/hid/input_system.h + rex/xam/input.h` | SlotAssignment/SharedAssignment absent |
| `rex/filesystem/vfs.h` | 1 | MOVED | `rex/filesystem/{file_system_factory,mount}.h` | VirtualFileSystem gone |
| `rex/filesystem/file.h` | 1 | SAME | `rex/filesystem/file.h` |  |
| `rex/filesystem/devices/stfs_container_device.h` | 1 | MOVED | `rex/filesystem/stfs.h` |  |
| `rex/filesystem/devices/host_path_device.h` | 1 | GONE | `rex/filesystem/file_system_factory.h` | HostPathDevice absent |
| `rex/filesystem/devices/disc_image_device.h` | 1 | VERIFY | `rex/filesystem/{mount,stfs}.h + rex/xam/device.h` | DiscImageDevice absent |
| `rex/filesystem/device.h` | 1 | MOVED | `rex/xam/device.h` |  |
| `rex/embedded_metadata.h` | 1 | MOVED | `rex/ppc/image.h` | also rexglue_embed_metadata() CMake fn |
| `rex/dbg.h` | 1 | MOVED | `rex/platform/process.h (+ rex/kernel/title_debug_output.h)` |  |
| `rex/exception_handler.h` | 1 | MOVED | `rex/platform/exception_handler.h` |  |
| `rex/audio/audio_system.h` | 1 | MOVED | `rex/apu/audio_system.h` |  |
| `rex/audio/downmix.h` | 1 | GONE | `(no counterpart)` | Set/GetSurroundMix absent |
| `rex/graphics/video_mode_util.h` | 1 | GONE | `(no counterpart)` | cvar-coupled helper |
| `rex/hash.h` | 1 | SAME | `rex/hash.h` |  |
| `rex/memory.h` | 1 | SAME | `rex/memory.h` |  |

## CMake targets: old layout vs new layout

old (v0.10.1 and every earlier tag): rex::runtime rex::o1heap rex::gpu-xenos rex::aes128 rex::disasm rex::disruptorplus rex::dxc-headers rex::kernel rex::libavcodec rex::libavutil rex::moltenvk rex::mspack rex::renderdoc rex::simde rex::system rex::tomlplusplus rex::TracyClient rex::vulkan-loader rex::xxhash
new (nightly-20261109 = v0.11.0 = nightly-20261122): rex::base rex::ppc rex::xenos rex::kernel rex::xam rex::xbdm rex::app rex::apu rex::hid rex::ui rex::gpu rex::filesystem rex::tlsf rex::simde rex::sdk rex::TracyClient rex::vulkan-loader rex::moltenvk + *-sdl3/-d3d12/-vulkan/-null driver modules

The tree links only rex::runtime (CMakeLists.txt:133, thirdparty/CMakeLists.txt:41, cmake/generated.cmake:61) and rex::o1heap (CMakeLists.txt:155). Both names are absent from every 0.11 and 0.12 tag, stable or nightly. Recommended replacement: link `rex::sdk` (aggregate of base/ppc/xenos/kernel/xam/xbdm/app) in all three places.

## SDK release map (verified against rexglue/rexglue-sdk releases, 2026-11-25)

| tag | version | date | include layout | targets | verdict for this tree |
|---|---|---|---|---|---|
| `nightly-20261002-bd833a2a` | 0.10.0.24-dev | 2026-10-02 | 308 rex headers (old layout) | rex::runtime, rex::o1heap, rex::gpu-xenos | what CI pinned before the 2026-11-25 re-pin; 60/60 includes OK |
| `v0.10.1` (stable) | 0.10.1 | 2026-11-06 | byte-identical header set; only `rex/version.h` differs; same 1305 files, same five platform slices | identical target set | what CI pins now: drop-in re-pin, zero source edits |
| `nightly-20261109-2d51d257` | 0.11.0-dev | 2026-11-09 | 148 rex headers (new layout) | rex::base/ppc/xenos/kernel/xam/xbdm/app/apu/hid/ui/filesystem/sdk | 9 of the 60 paths here compile unchanged, 51 do not |
| `v0.11.0` (latest stable) | 0.11.0 | 2026-11-14 | same new layout as 20261109 | same new targets | same full remap cost as 0.12 |
| `nightly-20261122-44b7fdc5` | 0.12.0-dev | 2026-11-22 | identical file set to the 0.11 nightlies | identical target set | same remap, plus 400 commits of fixes |

- 400 commits separate `nightly-20261002-bd833a2a` from `nightly-20261122-44b7fdc5`; the 51 include paths this tree uses that no longer exist are what that gap measures. Of those 51, 45 have a documented counterpart in the table (MOVED/SPLIT/REWORK/VERIFY) and 6 have none at all (GONE) - the vendoring of those 6 is the part of the port that is not a sed.
- There is no nightly or stable that keeps `rex::runtime`/`rex::o1heap` while carrying the new header layout, so the "two-step via 0.11" route does not exist: the remap is paid once, at 0.11 or at 0.12.
- 0.11 to 0.12 is header-layout-stable, so the remap can be done against stable `v0.11.0` and the pins moved to the newest 0.12 nightly afterwards.
- Suggested staging: (1) re-pin the pins to stable `v0.10.1` (done 2026-11-25: zero header diff, removes the nightly dependency); (2) do the header/target remap on a branch against `v0.11.0`; (3) re-pin to `nightly-20261122-44b7fdc5` or a newer 0.12 nightly once that branch lands.

## CMake migration shape (from `lib/cmake/rexglue/rexglue_project.cmake` in the 0.11/0.12 archives)

- The SDK no longer writes or updates `generated/rexglue.cmake`. Delete that file once and put this in CMakeLists.txt (project `foobar`, entrypoint `default.xex`):

```cmake
if (REXSDK_DIR)
  add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else ()
  find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif ()
rexglue_project(foobar OUTPUT_DIR generated/default)
```

- `REXSDK_DIR` builds the SDK from a source submodule; otherwise `find_package(rexglue)` searches `CMAKE_PREFIX_PATH`. The version number is the SDK that wrote the project; CMake accepts it or newer with the same major.
- An unedited project keeps configuring off the stale generated file, but every configure prints one deprecation warning from `lib/cmake/rexglue/rexglue_project.cmake:14`; `-Wno-deprecated` silences it, `-Werror=deprecated` makes it fail the configure.
- Driver modules are separate imported targets now (`rex::gpu-xenos-{vulkan,d3d12,null}`, `rex::hid-{sdl3,xinput}`, `rex::apu-sdl3`, `rex::ui-sdl3`, `rex::vulkan-loader`, `rex::moltenvk`); an application links `rex::sdk`, which aggregates base/ppc/xenos/kernel/xam/xbdm/app.

