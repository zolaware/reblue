
# re:Blue - Blue Dragon Recompiled

> [!WARNING]
> This is a heavy work in progress. Things will break, things will crash, and large chunks of what you might expect from a "finished" port simply aren't there yet. If you're looking for a polished experience, come back in a while. If you're curious and want to poke at things, you're in the right place.

re:Blue is a static recompilation of Blue Dragon (Xbox 360) built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The goal is to get the game running natively and build real modding support on top of it.

<img width="1282" height="752" alt="Screenshot 2026-04-18 214413" src="https://github.com/user-attachments/assets/f1e2af70-18e7-4359-8f86-48f67fb3e039" />

## Current state

The game boots, the title screen works, and a fair amount of the engine has been reverse engineered to the point where we can hook into it cleanly. Past that, expect rough edges everywhere. Specific issues come and go as things are investigated, so the short version is that it's playable to poke at, not playable to actually play through. Yet.

## Mod manager

There's an in-game mod manager being built using the game's own UI system (d2anime), so it feels like part of the game rather than a bolted-on overlay. You can browse installed mods, toggle them on and off, and it handles the wiring behind the scenes.

<img width="1282" height="752" alt="Screenshot 2026-04-18 214317" src="https://github.com/user-attachments/assets/238a2ee5-9583-4936-aff2-2414a790a879" />
<img width="1282" height="752" alt="Screenshot 2026-04-18 214312" src="https://github.com/user-attachments/assets/ffb7c570-2052-40a4-ac1b-8392a39929ac" />
<img width="1282" height="752" alt="Screenshot 2026-04-18 214303" src="https://github.com/user-attachments/assets/28aabe18-0d7c-4f1b-8b97-5dd2cdef3bda" />


## Mod tools

Proper mod authoring tools are in the works. More on that soon. The short version is that the groundwork for virtual file overrides, texture swaps, and CSV/template edits is already in place, and the tooling to make that approachable is coming.

## Setup

Coming soon.

## Credits

Huge thanks to everyone who's put time into this. re:Blue wouldn't be where it is without you.

* **[rcold](https://github.com/RC0ld)**, who has done an absurd amount for this project. He tests every build, catches things I miss, and knows Blue Dragon's internals better than anyone I've talked to. A lot of re:Blue looks the way it does because of him, and I can't thank him enough.
* The **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** team, for the toolchain this project is built on.
* The wider **Xbox 360 emulation scene**. A lot of the hardest problems were solved by them long before this project started.

## License

See [LICENSE](LICENSE).
