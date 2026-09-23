# Discord Rich Presence

re:Blue can show what you're doing in-game on your Discord profile, for example "Exploring *\<area\>*", "Traveling the World Map" or "Boss Battle", along with your party leader, their level, your party size and how long you've been playing.

## What it shows

| Game state | Status line | Details line | Small icon |
|---|---|---|---|
| Loading | Loading... | | |
| Title / menu | Title Screen | Main Menu | |
| Moving between areas | Transitioning Areas | | |
| Field | Exploring *\<area\>* / Traveling the World Map | *\<Leader\>* (Lv. *N*) • Party of *N* | Party leader portrait |
| Battle | In Battle / Boss Battle | Location: *\<area\>* | `icon_battle` |

The status updates straight away when a stage loads, a battle starts or ends, a save loads, or the game over screen appears. Otherwise it checks for changes at most every 1.5 seconds, which is Discord's rate limit. It only sends an update when something has actually changed.

## Using it

1. Run the Discord **desktop** app on the same machine. The browser version won't work because re:Blue talks to Discord over its local IPC connection.
2. In Discord, go to **User Settings → Activity Privacy** and turn on **Share my activity**.
3. Launch re:Blue. Rich Presence is on by default.

To turn it off, go to **Config → Gameplay → Advanced → Discord Rich Presence**. The change takes effect right away: turning it off clears your status and closes the connection, and turning it back on reconnects without a restart. The setting is saved as `bd_discord_rpc` in `profiles\default\reblue.toml`.

If Discord isn't running, nothing happens. The game keeps running normally and connects once Discord is available.

## Setting up the Discord application (maintainers)

Discord shows the presence under a registered application, and the image keys have to match art uploaded to that application.

> [!IMPORTANT]
> The application ID in [discord_presence.cpp](../src/engine/discord_presence.cpp) (`kDefaultAppId = "1346000000000000000"`) is a placeholder. Discord won't show anything until it's replaced with a real one.
1. Create an application at the [Discord Developer Portal](https://discord.com/developers/applications) and name it **re:Blue**. Discord shows this name as "Playing re:Blue".
2. Copy the **Application ID** into `kDefaultAppId`.
3. Under **Rich Presence → Art Assets**, upload images with these exact keys:

   | Key | Used for |
   |---|---|
   | `logo` | Large image, always shown |
   | `icon_battle` | Battles |
   | `char_shu`, `char_kluke`, `char_jiro`, `char_marumaro`, `char_zola` | Party leader while exploring |

   Newly uploaded assets can take a few minutes to show up in Discord.

## Build notes

The Discord RPC client library is bundled in [thirdparty/discord-rpc](../thirdparty/discord-rpc) and is built as part of `reblue_thirdparty`: `connection_win.cpp` on Windows and `connection_unix.cpp` everywhere else. You don't need to install anything extra.

## Troubleshooting

Search the log for lines starting with `[discord]`:

- `Discord Rich Presence initialized`: the feature is enabled and is trying to connect.
- `connected to Discord client`: the connection worked. If your profile still shows nothing, check the Activity Privacy setting and the application ID.
- `disconnected` / `error`: Discord closed or rejected the connection. An invalid application ID is the most common cause.

## Code map

- [engine/discord_presence.cpp](../src/engine/discord_presence.cpp): works out the status from the game state and sends updates.
- [engine/game_step.cpp](../src/engine/game_step.cpp): calls `Poll()` on every game logic step.
- [reblue_app.cpp](../src/reblue_app.cpp) / [core/shutdown.cpp](../src/core/shutdown.cpp): start-up and shutdown.
- [core/settings.cpp](../src/core/settings.cpp) / [core/settings_rows.cpp](../src/core/settings_rows.cpp): the `bd_discord_rpc` setting and its menu entry.