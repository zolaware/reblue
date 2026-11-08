# RecompLauncher integration

Use the Windows zip, standalone Linux AppImage, or macOS zip. Windows and macOS
packages place `recomplauncher.json` beside the program or app bundle. The macOS
DMG remains available for the native setup wizard, and ordinary standalone builds
also stage a manifest beside the executable.

The Linux AppImage contains `/recomplauncher.json` at its AppDir root. A
RecompLauncher build containing [embedded AppImage manifest support (#12)](https://github.com/elripalda/RecompLauncher/pull/12)
reads it directly, without extracting a sidecar. That support is merged upstream;
no Linux ZIP wrapper or separate manifest download is needed.

The launcher binds `.AppImage` entries in both `start.program` and
`prepare.program` to the installed image's actual filename, so versioned downloads
and renamed AppImages work. Keep one AppImage in its installation folder. The
manifest's `{install}` paths continue to refer to that folder, not the image's
read-only internal filesystem. Packaging disables SquashFS extended attributes
for compatibility with the embedded-manifest reader. The integration guide is
included inside the image at `usr/share/doc/reblue/RECOMPLAUNCHER.md`.

## Setup

Choose your own **Global-release (US/EU) Blue Dragon DVD 1, DVD 2 and DVD 3 ISOs**
in the three required Easy Setup slots. Images may be in separate folders and
filenames do not matter. This flow uses the [named multidisc inputs merged in
RecompLauncher #22](https://github.com/elripalda/RecompLauncher/pull/22); use a
launcher build containing that support (and #12 for embedded AppImage discovery).

Setup verifies the Blue Dragon XEX title ID and disc marker in each selected
image against its slot. Wrong discs, missing paths, duplicate files/hard links,
and ambiguous disc identities fail before installation. It does not substitute
another image found beside the selected one. This validates title/disc identity
and extraction completeness, not a Redump checksum or authenticity of every
asset. No game data is distributed with this project.

The launcher invokes:

```text
reblue --prepare-content --disc1 <DVD1.iso> --disc2 <DVD2.iso> --disc3 <DVD3.iso> --install-dir <installation>
reblue --recomplauncher-play --install-dir <installation>
```

DLC is optional. Select up to three of your own STFS packages in the DLC slots:
Ultra Hard Mod, Six Treasures and Shuffle Dungeon. Package files need no extension.
The launcher passes each selected path as a single `--dlc=<path>` argument; an
unselected slot contributes no argument. Both preparation modes accept this flag
repeatedly, for example:

```text
reblue --prepare-content --disc1 <DVD1.iso> --disc2 <DVD2.iso> --disc3 <DVD3.iso> --install-dir <installation> --dlc=<package1> --dlc=<package2>
```

All selected DLC is validated before disc extraction using the native STFS,
Blue Dragon title, marketplace content type and supported content-ID checks.
Duplicate packs are rejected. Keep package sources outside the installation.
After disc preparation succeeds, the native installer imports missing packs into
`<installation>/dlc`, including their native license-header sidecars. Installed
packs are kept on subsequent runs, and profile enable/disable choices are untouched.
If a DLC import fails, setup reports failure and keeps the prepared game and packs
already installed, so it can be retried. Removing a launcher selection does not
uninstall a pack; use re:Blue's DLC menu to disable or remove installed content.
The native wizard and in-game DLC flow remain available.

The existing folder-discovery command remains available:

```text
reblue --prepare-content --game-dir <DVD1.iso-or-folder> --install-dir <installation>
```

With `--game-dir`, keep all three images in one folder and select DVD 1 or that
folder. Discovery finds the three discs by their contents and rejects missing
or duplicate candidates. Do not combine `--game-dir` with `--disc1`/`--disc2`/
`--disc3`; explicit inputs require all three flags once each. The native wizard's
three-disc discovery and setup behavior is unchanged.

On Windows the program is `reblue.exe`; Linux packages use the AppImage and macOS
packages use `reblue.app/Contents/MacOS/reblue`. Both roles are handled before SDL
video initialization. Preparation needs no window or GPU and emits
`progress <0..1> <message>` lines. Play validates the prepared folder, then uses
the existing SDK startup/shutdown path with an explicit game-data root.

The installer extracts all available languages and movies using the same disc
reader and extraction plan as the wizard. Unlike the wizard's permissive repair
path, this setup fails if the extraction plan is missing a required file.

The installation folder must be separate from each selected disc-image folder. Files are
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

Check AppDir staging and argument forwarding with:

```sh
python3 -m unittest discover -s tests/launcher -p package_test.py -v
```

The shared profile checks verify three required disc inputs, three optional DLC
inputs and their CLI arguments. The Linux checks execute the production packaging script with a fixture packer,
without downloading tools or needing game assets. They verify the root manifest,
guide, single-AppImage output, and preparation/launch arguments through `AppRun`.
Other hosts check the shared Windows/Linux/macOS program declarations and skip
Linux-only packaging/entry checks.

Pass `-DCMAKE_PREFIX_PATH=<SDK>` and `-DREBLUE_TEST_WITH_SDK=ON` to also compile the
SDK startup adapter and strict installer, and build `disc_probe`. The optional
`disc_probe <disc-image-folder>` runs the production title/disc checks against
your own images without extracting them or starting a renderer. The full game
build still requires the normal SDK, guest-code generation and game assets.
