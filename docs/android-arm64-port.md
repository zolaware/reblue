# Android ARM64/Vulkan porting notes

This document describes an **unofficial proof-of-concept** Android port of
re:Blue. It is implementation documentation, not a statement that Android is
an upstream-supported platform.

Reference implementation:
<https://github.com/dj5927/reblue-android>

No Blue Dragon disc image, `default.xex`, extracted game data, audio/movie
assets, or signing credentials are included in that source repository.

## Validated build shape

The current prototype uses:

- Android NDK `28.2.13676358`;
- `arm64-v8a` only;
- `minSdk 28`;
- `targetSdk` / `compileSdk` 35;
- Vulkan renderer;
- SDLActivity Java glue;
- a shared re:Blue host library named `libmain.so`;
- the matching ReXGlue `librexruntime.so` packaged in the APK.

For Android, the CMake target is a shared library rather than the desktop
executable. SDLActivity loads `librexruntime` and `libmain`, then enters the
normal native re:Blue startup path. The built-in desktop disc installer is
currently disabled on Android; game data is prepared externally and selected
by the launcher.

The static recompilation is still generated from the NTSC-U executable. The
Android work does not change the guest-code target and does not make regional
`default.xex` files interchangeable.

## Game data and persistent configuration

The Java launcher accepts either the game directory itself or its parent
containing `game/`. It validates at least `default.xex` and `bd_boot.ini`
before starting the native activity.

The prototype passes Android-specific paths to native code with environment
variables:

- `REBLUE_GAME_DATA_ROOT` - selected extracted game directory;
- `REBLUE_CONFIG_PATH` - persistent `reblue.toml` path;
- `REBLUE_DIAG_FILE` - writable startup/diagnostic trace.

Android is treated as a packaged application, so writable configuration/cache
state is kept outside the APK rather than beside the executable/library.

## Vulkan work that was needed on mobile GPUs

The desktop Vulkan path could not be used unchanged on every tested Android
Vulkan driver. The prototype therefore keeps multiple guest-shader/descriptor
strategies available and chooses a compatible path at runtime.

The important implementation areas were:

1. **Descriptor strategy fallback.** The fastest path uses the normal bindless
   layout when the required device features are available. Mobile-compatible
   fixed-descriptor and UBO-oriented paths are available when they are not.
2. **Separate Android shader-cache variants.** Android packages compatible
   precompiled guest-shader cache forms for the bindless and fallback layouts,
   instead of assuming the desktop cache layout is universally usable.
3. **Android-compatible host shader variants.** Vulkan host shaders that need a
   different descriptor/UBO layout are compiled as additional SPIR-V blobs and
   selected only on Android.
4. **Pipeline/cache selection.** Pipeline creation and guest-shader lookup must
   use the same descriptor strategy for a device. Mixing cached shaders from a
   different layout produces invalid descriptors or pipeline failures.
5. **Vulkan surface/runtime loading.** The NDK supplies `libvulkan.so`; SDL3
   supplies the Android window/surface and volk resolves Vulkan entry points.

The reference code contains the concrete compatibility paths and is a better
source for exact feature checks than hard-coding a single Android GPU model in
upstream documentation.

## Android lifecycle and input

The native runtime is hosted by SDLActivity. The prototype adds Android-side
handling for:

- landscape activity startup;
- pause/resume/stop/destroy lifecycle transitions;
- physical SDL game controllers;
- touch controls;
- phone vibration through Android `Vibrator` / `VibrationEffect`;
- startup tracing that remains available when native initialization exits
  before the normal log UI can be reached.

Vibration is deliberately stopped on pause/destroy; on resume the Java side
restarts the pulse only when the last requested amplitude is still non-zero.

## Packaging

The Gradle application is intentionally thin. Native libraries are staged in
`app/src/main/jniLibs/arm64-v8a/`; retail game data stays outside the APK.
Release signing is optional and driven by a local, ignored properties file, so
keystores/passwords are never part of the source tree.

The prototype has passed Java/Gradle compilation, ARM64 native linking, APK
assembly, and package/signature validation. It has also been used during the
Android port work to run re:Blue on ARM64/Vulkan hardware.

## Remaining work before calling Android supported

The largest remaining concern is driver coverage. Android Vulkan
implementations vary substantially in descriptor-indexing limits and behavior,
so the fallback paths should be tested on a broader set of Adreno, Mali and
other GPUs before upstream support is advertised.

Controller mappings, vibration behavior, lifecycle restoration, file access on
newer Android storage models, and performance/shader-cache coverage also need
more device testing.

If Android support is desired upstream, the reference implementation is best
split into smaller reviewable changes rather than merged as one large port:

1. Android CMake/ReXGlue/SDL build target;
2. Gradle + SDLActivity wrapper and writable path plumbing;
3. lifecycle/input/vibration integration;
4. Vulkan descriptor/shader-cache compatibility fallbacks;
5. Android CI/package automation after device behavior is stable.
