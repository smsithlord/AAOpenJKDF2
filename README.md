# AArcade: OpenJK

**BETA 1.0** (2026-04-22) — Windows 64-bit

![Screenshot](docs/images/screenshot.png)

## [Latest Releases](https://github.com/smsithlord/AAOpenJKDF2/releases) | [Report a crash or bug](https://github.com/smsithlord/AAOpenJKDF2/issues)

## What is AArcade: OpenJK?

AArcade: OpenJK is a persistent 3D desktop that runs inside *Jedi Knight: Dark Forces II*. Think of your regular 2D desktop, but in 3D — you spawn shortcuts to files or URLs into the world, or props for decoration. You can launch any shortcut and AAOpenJKDF2 will pause in the background so it doesn't lag you down. Many things can also render directly on in-world screens: web pages, retro games via Libretro, or video files via libmpv.

Singleplayer only, but you usually load maps by going into multiplayer and screaming into the void.

This is a fork of [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2) by shinyquagsire23 — all the heavy lifting of decompiling and reimplementing the original DF2 engine is their work. This fork layers the AArcade framework on top.

## Embedded Frameworks

The AArcade runtime ships as `aarcadecore.dll` alongside `openjkdf2-64.exe`, gluing together a stack of embedded subsystems:

| Framework | Role | Code |
| --- | --- | --- |
| [Ultralight 1.4.0](https://ultralig.ht/) | HTML/CSS/JS HUD overlay + library browser | `src/aarcadecore/UltralightManager.cpp`, `src/aarcadecore/UltralightInstance.cpp` |
| [libmpv 2.0](https://mpv.io/) | In-world video playback on screens | `src/aarcadecore/VideoPlayerInstance.cpp` |
| [Libretro](https://www.libretro.com/) | Retro game cores rendering to in-world screens | `src/aarcadecore/LibretroManager.cpp` |
| [Steamworks SDK 1.64](https://partner.steamgames.com/) | Optional Steam-backed web browser embed | `src/aarcadecore/SteamworksWebBrowser*.cpp` |
| [OpenAL Soft](https://openal-soft.org/) | Audio (inherited from OpenJKDF2) | `lib/openal/` |
| [SDL2](https://www.libsdl.org/), [nlohmann/json](https://github.com/nlohmann/json), [SQLite](https://sqlite.org/) | Supporting infrastructure | various |

HUD UI source (HTML/CSS/JS for the Ultralight-driven overlay) lives in `src/aarcadecore/ui/` and is copied into the build output next to the exe at build time.

## Requirements

- Windows 64-bit
- A legally owned copy of *Jedi Knight: Dark Forces II* (a few game asset files have to be copied — see install steps)
- [Visual C++ 2013 x64 Redistributable](https://aka.ms/highdpimfc2013x64enu)
- [Visual C++ 2015-2022 x64 Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- Some users have reported having to run `openjkdf2-64.exe` as Administrator.

## Install / Getting Started

1. Download `AAOpenJKDF2.zip` from the [Releases page](https://github.com/smsithlord/AAOpenJKDF2/releases).
2. Unzip somewhere outside `Program Files` if possible — e.g. `C:\Games\AAOpenJKDF2\`.
3. Copy `Res1hi.gob` and `Res2.gob` from your *Jedi Knight: Dark Forces II* installation into the extracted `AAOpenJKDF2\resource\` folder.
4. Copy the episode `.gob` files from your JKDF2 `episode\` folder into the extracted `AAOpenJKDF2\episode\` folder.
5. If the VC++ redistributables above aren't installed yet, double-click `Install Redistributables.bat` and follow the prompts.
6. Launch `openjkdf2-64.exe`.

The full end-user guide — keybinds reference, model-yoinking workflow, the shared-episode-folder junction trick, and the known-bugs list — lives in `AAOJK README.txt` inside the release ZIP.

## Building from Source

For the AArcade-enabled Windows build (MSVC x64, produces `aarcadecore.dll` + UI files alongside the exe), see [aarcade/docs/LLM-AAOPENJK-HOW-TO-BUILD.md](aarcade/docs/LLM-AAOPENJK-HOW-TO-BUILD.md).

For the plain upstream OpenJKDF2 targets (macOS / Linux / Android / DSi / WebAssembly), see [BUILDING.md](BUILDING.md).

Windows quick-start:
```bash
git submodule update --init
cmake -S . -B build_aarcade -G "Visual Studio 17 2022" -A x64
cmake --build build_aarcade --config Release --target openjkdf2-64
```
Output: `build_aarcade/Release/openjkdf2-64.exe` + `aarcadecore.dll` + supporting DLLs.

## Developer Documentation

Deep-dive documentation for contributors is under [aarcade/docs/](aarcade/docs/):

- [LLM-README.md](aarcade/docs/LLM-README.md) — MSVC build notes and porting fixes
- [LLM-AARCADECORE-README.md](aarcade/docs/LLM-AARCADECORE-README.md) — `aarcadecore.dll` architecture
- [LLM-UI-README.md](aarcade/docs/LLM-UI-README.md) — the Ultralight-driven UI
- [LLM-HUD-OVERLAY.md](aarcade/docs/LLM-HUD-OVERLAY.md) — HUD overlay internals
- [LLM-LIBRETRO-ONBOARDING.md](aarcade/docs/LLM-LIBRETRO-ONBOARDING.md) — Libretro core integration
- [AAOPENJK-DESIGN-OVERVIEW.md](aarcade/docs/AAOPENJK-DESIGN-OVERVIEW.md) — high-level design
- [DYNAMIC_TEXTURE_GUIDE.md](aarcade/docs/DYNAMIC_TEXTURE_GUIDE.md) — per-thing dynamic textures

## Credits

- **AArcade framework, fork, and integration:** [SM Sith Lord](https://smsithlord.com)
- **Testers:** Jezz, Gnarl
- **Upstream OpenJKDF2 decompilation and engine work:** [shinyquagsire23](https://github.com/shinyquagsire23) and contributors — without their function-by-function reimplementation, none of this would exist. See [the upstream project](https://github.com/shinyquagsire23/OpenJKDF2) for engine-level credits and detailed decompilation methodology.
- **Original game:** *Star Wars Jedi Knight: Dark Forces II* © LucasArts. This project requires a legally owned copy of the original game and ships no game assets.

## License

Source code is licensed under the terms in [LICENSE.md](LICENSE.md).
