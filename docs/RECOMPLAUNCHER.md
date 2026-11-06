# RecompLauncher integration

Use the Windows zip, the Linux zip containing the AppImage, or the macOS zip.
Each places `recomplauncher.json` beside the program or app bundle so the launcher
can read it. The original standalone AppImage and macOS DMG remain available for
the native setup wizard. Ordinary standalone builds also stage a manifest beside
the executable.

If RecompLauncher offers several Linux builds, select the zip. It contains the
stable `reblue.AppImage` filename named by the manifest; a standalone AppImage
cannot supply a release-local sidecar manifest by itself.

## Setup

Keep all three of your own **Global-release (US/EU) Blue Dragon disc ISOs** in one
folder. Select DVD 1 or that folder. Setup examines ISO contents and finds the
three disc markers and Blue Dragon executable title IDs; filenames do not matter.
It rejects missing or duplicate discs and does not search other folders. This
checks title/disc identity and extraction completeness, not a Redump checksum or
the authenticity of every asset. No game data is distributed with this project.

The launcher invokes:

```text
reblue --prepare-content --game-dir <DVD1.iso-or-folder> --install-dir <installation>
reblue --recomplauncher-play --install-dir <installation>
```

On Windows the program is `reblue.exe`; Linux packages use the AppImage and macOS
packages use `reblue.app/Contents/MacOS/reblue`. Both roles are handled before SDL
video initialization. Preparation needs no window or GPU and emits
`progress <0..1> <message>` lines. Play validates the prepared folder, then uses
the existing SDK startup/shutdown path with an explicit game-data root.

The installer extracts all available languages and movies using the same disc
reader and extraction plan as the wizard. Unlike the wizard's permissive repair
path, this setup fails if the extraction plan is missing a required file.

The installation folder must be separate from the disc-image folder. Files are
prepared under `.recomplauncher-preparing/new/game`; only a successful setup is
published as `game`. An existing `game` folder is replaced only if its
`.recomplauncher-content` marker identifies it as managed by this integration.
Unowned game folders, source disc images, profiles, saves and settings are kept.
If publishing fails, setup attempts to restore the previous game folder. If that
restore fails, its files remain under `.recomplauncher-preparing/old` for recovery.

An interrupted setup can leave `.recomplauncher-preparing`. Keep it for recovery
or move it aside once all setup processes have stopped, then retry. It is never
silently deleted by the next setup. Do not run preparation while re:Blue is open.

These commands do not register or replace a global native installation record,
copy or relaunch the application elsewhere, or invoke the graphical setup wizard.
Continue using RecompLauncher to start this managed installation; launching the
program without these flags keeps the native install-record/wizard behavior.

## Settings and saves

The external launch uses the native `default` profile at
`profiles/default/reblue.toml`. The manifest exposes resolution, display mode,
frame rate, V-Sync, aspect ratio, MSAA, anisotropic filtering and depth of field.
Values use the game's existing cvars, and unrelated TOML keys are preserved by
RecompLauncher. Advanced settings, mods, input bindings and other native profiles
remain available through re:Blue's own UI.

No controller, button-prompt or full-screen environment overrides are added.
Native input detection and user settings remain in charge; the display control
changes full screen only through an explicit settings choice.

Save declarations cover the default profile's saves, achievements and settings.
Folder actions open its default save folder and the installation's `logs` folder.
RecompLauncher's own backup filters still apply. A custom `bd_saves_path` remains
honored by the game; adjust the manifest's save/action paths before relying on
backups or actions for a custom location. Game files, caches and DLC are not
declared as saves.

Invalid external arguments return 2. Validation or setup errors return 1 with a
message on inherited standard error. Direct launch uses the SDK's native return
code; it does not open a wizard when content preparation is missing.

## Development checks

The filesystem/discovery tests need no SDK, game data or graphics device:

```sh
cmake -S tests/launcher -B out/launcher-tests
cmake --build out/launcher-tests --config Release
ctest --test-dir out/launcher-tests -C Release --output-on-failure
```

Pass `-DCMAKE_PREFIX_PATH=<SDK>` and `-DREBLUE_TEST_WITH_SDK=ON` to also compile the
SDK startup adapter and strict installer, and build `disc_probe`. The optional
`disc_probe <disc-image-folder>` runs the production title/disc checks against
your own images without extracting them or starting a renderer. The full game
build still requires the normal SDK, guest-code generation and game assets.
