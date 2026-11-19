# iOS (experimental)

An iPhone/iPad build of the macOS port: the same recompiled guest code, plume (Vulkan over MoltenVK), and SDL 3 (UIKit). It needs an Apple developer team to sign, a device with 8 GB of memory or more (iPhone 15 Pro and later, M-series iPad), and iOS 18+. A game controller is required (no touch controls yet).

## Companion dependency branches

Until upstreamed, the iOS cross-build depends on:
- `rexglue-sdk` iOS arm64 runtime support (`REXSDK_DIR`): https://github.com/rexglue/rexglue-sdk/pull/610
- `MoltenVK` iOS CMake support (submodule of `rexglue-sdk`): https://github.com/KhronosGroup/MoltenVK/pull/2909
- `plume` iOS UIKit windowing (`thirdparty/plume`): https://github.com/zolaware/plume/pull/3

## What differs from macOS

| Area | iOS |
|---|---|
| Guest memory | 4 GB arena mapped with `mmap(PROT_NONE)` + `MAP_SHARED` + `vm_remap` in `rexglue-sdk`. Requires `com.apple.developer.kernel.extended-virtual-addressing` and `com.apple.developer.kernel.increased-memory-limit`. |
| Runtime dylibs | `librexruntime.dylib`, `libSDL3.dylib`, and `libMoltenVK.dylib` are embedded in `reblue.app/Frameworks/` (`@rpath`) and signed with the app's signing identity. |
| Shader cache | Reuses the host-generated `shader_cache.cpp` (`-DREBLUE_PREBUILT_SHADER_CACHE=...`) so `dxc` does not run on device. |
| Files | Everything lives in the app's `Documents/` folder (visible in Finder/Files): `game/`, `profiles/default/saves/`, `cache/`, `logs/`. `game/`, `cache/`, and `logs/` are excluded from iCloud backup. |
| Window | Fullscreen high-DPI UIKit window; `bd::platform::ios::FitMetalView` keeps the Metal layer sized to the landscape window. |

## Build

1. Build the macOS host target once to generate `out/build/mac-arm64-release/generated/shader_cache.cpp` and guest sources.
2. Configure and build the iOS target:

```bash
cmake -S . -B out/build/ios-arm64 -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DREXSDK_DIR=/path/to/rexglue-sdk \
  -DREBLUE_IOS_BUNDLE_ID=<your.bundle.id> \
  -DREBLUE_PREBUILT_SHADER_CACHE=$PWD/out/build/mac-arm64-release/generated/shader_cache.cpp
cmake --build out/build/ios-arm64 --target reblue
```

3. Sign and install on a connected iPhone:

```bash
tools/ios_deploy.sh <TEAMID> <your.bundle.id> <DEVICE_UDID> [--launch]
```

## Game data

Copy the `game` (and optional `cache`) folders from a desktop install into the app's `Documents/`:

```bash
xcrun devicectl device copy to --device <DEVICE_UDID> \
  --domain-type appDataContainer --domain-identifier <your.bundle.id> \
  --source "/path/to/Blue Dragon/game" --destination Documents/game
```

## iCloud Drive saves (iOS and macOS)

`src/platform/cloud_saves.{h,cpp}` mirrors save slots through `REBLUE_ICLOUD_CONTAINER` (default `iCloud.<REBLUE_IOS_BUNDLE_ID>` on iOS, opt-in on macOS with the same container ID):

- At startup each slot in `profiles/<profile>/saves/` is compared with `<container>/Documents/Saves/<slot>` by `savegame.dat` mtime (2 s tolerance). Newer copies sync in either direction; replaced local slots are backed up under `.cloud-backup/` (keeping the 10 newest).
- After each in-game save (`bdSaveGameWriteThumbnail_hook`), the updated slot is copied to iCloud in the background.
- The container is public and appears as **Blue Dragon** in iCloud Drive (Files / Finder).
- A macOS build configured with `-DREBLUE_ICLOUD_CONTAINER=iCloud.<your.bundle.id>` reads `~/Library/Mobile Documents/iCloud~<your~bundle~id>/Documents/Saves` directly with no macOS iCloud entitlement required.
- Controlled by the `bd_icloud_saves` setting (default `true`).
