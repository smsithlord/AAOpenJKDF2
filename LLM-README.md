# OpenJKDF2 Build Notes for Windows/MSVC

This document contains notes for building OpenJKDF2 on Windows with Visual Studio/MSVC, including fixes that were needed to make the build work.

## Quick Start

### Prerequisites
- Python 3.8+ (installed to PATH)
- cog (`pip install cogapp`)
- OpenAL 1.1 SDK with `OPENALDIR` environment variable set to `C:\Program Files (x86)\OpenAL 1.1 SDK`
- CMake 3.x or Visual Studio 2022 (which includes CMake)

### Building (MSVC standalone)
```bash
git submodule update --init
mkdir build_msvc
cd build_msvc
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release --target openjkdf2-64
```

### Building (Libretro Host — with aarcadecore.dll)
This is the main development build. It includes the AArcade Core DLL, Ultralight HUD, Library Browser, and per-thing texture rendering.

**Additional prerequisites:**
- Ultralight SDK 1.4.0 (`ultralight-free-sdk-1.4.0-win-x64/` at repo root)
- Steamworks SDK 1.64 (`steamworks_sdk_164/` at repo root — for Steamworks browser, optional)
- Runtime: Ultralight DLLs + `resources/` folder in game directory

```bash
git submodule update --init
mkdir build_aarcade
cd build_aarcade
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

This builds both `openjkdf2-64.exe` and `aarcadecore.dll`. UI files are auto-copied to the output directory via a CMake post-build step.

### Output Location
- **Executable**: `build_aarcade/Release/openjkdf2-64.exe` (or `build_msvc/Release/`)
- **DLL**: `build_aarcade/Release/aarcadecore.dll`
- **UI files**: `build_aarcade/Release/aarcadecore/ui/` (auto-copied from `src/aarcadecore/ui/`)
- **Required DLLs**: OpenAL32.dll, exchndl.dll, mgwhelp.dll, symsrv.dll, Ultralight*.dll, steam_api64.dll
- **Image cache**: `./cache/urls/` (created automatically by ImageLoader)

## Required Code Changes for MSVC Compilation

The following changes were made to enable successful compilation with MSVC:

### 1. Fix GCC `__attribute__` Compatibility
**File**: `src/types.h` (line 133)

**Issue**: MSVC doesn't support GCC's `__attribute__` syntax.

**Fix**: Added a macro to ignore `__attribute__` when compiling with MSVC:
```c
#if defined(_MSC_VER)
#define ALIGNED_(x) __declspec(align(x))
#define __attribute__(x)  // <-- Added this line
#else
```

### 2. Fix POSIX `unistd.h` Header
**File**: `src/Main/sithCvar.c` (lines 13-15)

**Issue**: `unistd.h` is a POSIX header that doesn't exist on Windows.

**Fix**: Wrapped the include with a conditional:
```c
#ifndef _MSC_VER
#include <unistd.h>
#endif
```

### 3. Fix SDL2 Main Conflict
**File**: `cmake_modules/plat_msvc.cmake` (line 11, 19, 27)

**Issue**: SDL2main library provides its own `main()` wrapper that conflicts with the project's main function.

**Fix**:
- Added `SDL_MAIN_HANDLED` compile definition (line 11)
- Removed `SDL2main` from `SDL2_COMMON_LIBS` (lines 19, 27)

```cmake
add_compile_definitions(SDL_MAIN_HANDLED)
set(SDL2_COMMON_LIBS SDL::SDL)  # Removed SDL2main
```

### 4. Disable WIN32_EXECUTABLE for Console Output
**File**: `cmake_modules/plat_msvc.cmake` (lines 46-47)

**Issue**: WIN32_EXECUTABLE=TRUE expects WinMain instead of main and hides console output.

**Fix**: Changed to FALSE for both Release and Debug builds:
```cmake
set_target_properties(${BIN_NAME} PROPERTIES WIN32_EXECUTABLE FALSE)
```

### 5. Fix File Reading on Windows (CRLF Issue)
**File**: `src/Platform/Common/stdEmbeddedRes.c` (lines 68, 186)

**Issue**: Opening files in text mode `"r"` on Windows causes CRLF→LF conversion, making `ftell()` size mismatch with `fread()` bytes.

**Fix**: Changed both `fopen()` calls to binary mode:
```c
f = fopen(tmp_filepath, "rb");  // Was "r"
```

Also added error checking and debug output (lines 169-177, 194, 205-213).

## Project Structure

### Key Files
- **Shaders**: `resource/shaders/*.glsl` - GLSL shaders compiled at runtime by OpenGL
- **Engine Core**: `src/` - Main source code
- **Platform Layer**: `src/Platform/` - Platform-specific implementations (SDL2, GL, etc.)
- **CMake Modules**: `cmake_modules/` - Build configuration for different platforms

### Shader Loading
Shaders are loaded at runtime from the `resource/shaders/` directory relative to the executable's working directory. They must be placed in:
```
<working_directory>/resource/shaders/*.glsl
```

The `stdEmbeddedRes_Load()` function:
1. Prepends `resource/` to the requested path
2. Converts `/` to `\` on Windows
3. Opens the file in binary mode
4. Falls back to embedded resources if file not found

### Build System
- Uses CMake with platform-specific modules
- `plat_msvc.cmake` handles MSVC/Visual Studio configuration
- Submodules provide SDL2, OpenAL, GLEW, etc.
- Python's `cog` is used to generate code (globals.c/globals.h)

## Dependencies (Git Submodules)
- SDL 2.26.5
- SDL_mixer 2.6.3
- OpenAL 1.23.1
- GLEW 2.2.0
- FreeGLUT 3.4.0
- zlib 1.2.13
- libpng 1.6.39

## Dependencies (Bundled in DLL)
- SQLite 3 (amalgamation — `src/aarcadecore/sqlite3.c/.h`) — library database for the Library Browser

## Troubleshooting

### "Failed to read file" errors
- Ensure shaders are in `resource/shaders/` relative to working directory
- Check that files are readable (not locked by another process)
- Verify CRLF line endings aren't causing issues (should be fixed by binary mode)

### Linker errors about SDL2main
- Ensure `SDL_MAIN_HANDLED` is defined
- Verify `SDL2main` is removed from link libraries
- Check that `WIN32_EXECUTABLE` is set to FALSE

### Missing DLLs at runtime
- Copy DLLs from `build_msvc/` to the directory containing the exe
- Or run from `build_msvc/` directory

## AArcade Core DLL (`aarcadecore.dll`)

All embedded content rendering (Libretro emulation, future Steamworks web browser, etc.) is implemented in a standalone DLL (`aarcadecore.dll`) that communicates with the game engine through a clean C interface. This makes the DLL portable to other game engines.

See **LLM-AARCADECORE-README.md** for full DLL architecture and implementation details.

### Host-Side Integration
- `src/Platform/Common/AACoreManager.h/.c` — Loads `aarcadecore.dll` via `SDL_LoadObject`, provides host callbacks, owns SDL audio device. Per-thing texture rendering via `PreRenderThing`/`PostRenderThing` hooks (flush + texture_id swap on shared compscreen surface). Selector ray (skips adjoins), spawn mode (preview + confirm/cancel), HUD overlay compositing onto per-thing task textures and fullscreen quad.
- `src/Engine/sithRender.c` — `PreRenderThing`/`PostRenderThing` hooks around `rdThing_Draw` in `sithRender_RenderThing`
- `src/Main/aaMainMenu.c` — Keybind handlers: Escape (exit input/fullscreen/menu), Select (LMB, objectUsed), Virtual Input (RMB hold), Input Lock (G toggle), Remember (R), Build (middle mouse), Tasks Tab (F4), Library Tab (F6), Tab Menu (TAB hold). Spawn mode confirm/cancel.
- `src/Devices/sithControl.c` — AA keybind registration (`INPUT_FUNC_AA*`), `EnsureAADefaults` auto-repairs bindings
- `src/Main/jkStrings.c` — Injects AA keybind labels into string table
- `src/Cog/sithCog.c` — `AAOJK_ObjectUsed` COG verb for player activation of AA objects
- `src/Win95/Window.c` — Escape key intercepted (engine's own escape menu suppressed); START/BACK gamepad buttons suppressed when menu open
- `src/Platform/SDL2/stdControl.c` — Keyboard suppressed when menu open; gamepad still polled (Libretro needs it via `get_key_state`)
- `src/Main/Main.c` — `AACoreManager_Init()` / `AACoreManager_Shutdown()` at startup/shutdown
- `src/Main/jkGame.c` — `AACoreManager_Update()` called per frame
- `src/Main/sithMain.c` — Map lifecycle hooks: `sithMain_Open` → `OnMapLoaded`, `sithMain_Close` → `OnMapUnloaded`

### Input Modes
- **Normal gameplay**: Player moves, LMB selects/deselects objects, R remembers (activate without selecting)
- **Input mode** (Virtual Input RMB hold / Input Lock G toggle): Game input suppressed, mouse/keyboard forwarded to SWB. HUD overlay (overlay.html) composited onto per-thing task texture. Escape exits.
- **Fullscreen mode**: SWB renders to fullscreen overlay quad with HUD composited on top. Escape exits.
- **Spawn mode**: Preview object follows aim, LMB confirms, Escape cancels.

### HUD Overlay System
- `overlay.html` — Composited on top of embedded instance content (cursor + browser tab UI)
- Persistent HUD pixel buffer updated each frame (`UltralightManager_UpdateHudPixelBuffer`)
- Fullscreen: composited as BGRA32 alpha blend onto fullscreen quad
- Input mode: scaled + converted from BGRA32 to RGB565 onto per-thing 1024x1024 task texture
- Overlay stays loaded across input/fullscreen toggles; reloads when switching to different instance; unloads when instance deactivates

### Gamepad Button Mapping (SNES physical position)
- Xbox A (bottom) -> SNES B, Xbox B (right) -> SNES A
- Xbox X (left) -> SNES Y, Xbox Y (top) -> SNES X

## Spawning 3DO Objects at Runtime

### Key Files
- `src/Main/jkSpawn.c` / `.h` — Handles H key press to spawn a 3DO at a raycast hit point

### How It Works

**Input detection:**
- `stdControl_ReadKey(DIK_H, &hDown)` checks the H key state each frame
- Debounced to trigger only on key-down edge

**Aim direction (matching weapon fire):**
```c
rdMatrix34 aimMatrix;
_memcpy(&aimMatrix, &player->lookOrientation, sizeof(aimMatrix));
rdMatrix_PreRotate34(&aimMatrix, &player->actorParams.eyePYR);
lookDir = aimMatrix.lvec;
```
This is the same pattern used by `sithWeapon_Fire` — combines the thing's orientation with the player's eye pitch/yaw/roll.

**Raycasting:**
- `sithCollision_SearchRadiusForThings()` casts a ray from the player position along the aim direction
- `sithCollision_NextSearchResult()` returns the first hit with `hitNorm` (surface normal) and `distance`
- `searchResult->surface->parent_sector` gives the correct sector for spawning
- `sithCollision_SearchClose()` must be called when done

**Orientation (constrained look-at):**
The spawned object's bottom rests against the surface and it faces the player:
- `uvec` = surface normal (model's "up" axis aligns to surface)
- `lvec` = player direction projected onto surface plane, then negated (model faces player)
- `rvec` = cross(lvec, uvec)
- A small offset along the normal (0.025 units) prevents the model from sinking into the surface

**Spawning with existing templates:**
- `sithTemplate_GetEntryByName("slcompmoniter")` retrieves a fully-configured template already loaded in the level
- `sithThing_Create(template, &hitPos, &orient, sector, NULL)` creates the instance
- Using level templates is preferred over building templates programmatically, as they have all rendering/collision data pre-configured

### Key APIs for Spawning Things
| Function | File | Purpose |
|----------|------|---------|
| `sithTemplate_GetEntryByName()` | `src/World/sithTemplate.h` | Look up a template by name |
| `sithThing_Create()` | `src/World/sithThing.h` | Spawn a thing from a template at a position/orientation/sector |
| `sithCollision_SearchRadiusForThings()` | `src/Engine/sithCollision.h` | Raycast for surfaces/things |
| `sithCollision_NextSearchResult()` | `src/Engine/sithCollision.h` | Get next collision hit |
| `sithCollision_SearchClose()` | `src/Engine/sithCollision.h` | End collision search |
| `rdMatrix_BuildFromLook34()` | `src/Primitives/rdMatrix.h` | Build rotation matrix from a direction |
| `rdMatrix_PreRotate34()` | `src/Primitives/rdMatrix.h` | Apply euler rotation to a matrix |
| `sithModel_LoadEntry()` | `src/World/sithModel.h` | Load a .3do model (prepends `3do\` automatically) |

### rdMatrix34 Layout
```
rvec = right vector (X axis)
lvec = forward/look vector (Y axis) — the direction the model faces
uvec = up vector (Z axis)
scale = position/translation
```

## Notes for Next Time

### Code Organization
- The project is ~96% complete (2694/2798 functions excluding rasterizer)
- Follows original JK.EXE structure closely
- Uses IDA Pro for reverse engineering
- Function naming follows symbols from Grim Fandango Remastered

### Development Workflow
1. Make changes to source
2. Rebuild: `cmake --build build_msvc --config Release`
3. Test with game assets from GOG/Steam version of Jedi Knight

### Testing
- Requires valid Jedi Knight: Dark Forces II game files
- Place exe and DLLs in game directory with GOB files
- See main README.md for full directory structure requirements
