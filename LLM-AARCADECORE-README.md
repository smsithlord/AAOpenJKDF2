# AArcade Core DLL — Implementation Guide

`aarcadecore.dll` is a standalone DLL that provides embedded content rendering (Libretro emulation, Steamworks web browser, etc.) to any host game engine through a clean C interface.

## Architecture Overview

```
Host Game Engine                          aarcadecore.dll
(OpenJKDF2, or any engine)                (engine-independent)
──────────────────────────                ──────────────────────
AACoreManager                             aarcadecore_init(callbacks)
  │                                         │
  ├─ SDL_LoadObject("aarcadecore.dll")      ├─ Stores host callbacks
  ├─ Loads 9 flat exported functions        ├─ LibretroManager
  ├─ Calls aarcadecore_init(callbacks)      │    └─ LibretroInstance
  │    provides: printf, get_key_state      │         └─ LibretroHost (ring buffers)
  │                                         ├─ SteamworksWebBrowserManager (stubs)
  ├─ Registers engine texture callback      │
  │    for aarcadecore_get_material_name()  │
  │                                         │
  ├─ Opens SDL audio device                 │
  │    at aarcadecore_get_audio_sample_rate()│
  │                                         │
  ├─ Per frame:                             ├─ Per frame:
  │    aarcadecore_update()  ─────────────> │    Runs Libretro emulation
  │                                         │    Forwards gamepad input
  │                                         │    Collects audio in ring buffer
  │                                         │
  ├─ Engine texture callback fires:         │
  │    aarcadecore_render_texture() ──────> │    Fills pixel buffer with frame
  │                                         │
  └─ SDL audio callback fires:             │
       aarcadecore_get_audio_samples() ───> └─ Returns audio from ring buffer
```

## Public API (`src/aarcadecore/aarcadecore_api.h`)

The DLL exports flat C functions. The host loads them via `SDL_LoadFunction` or `GetProcAddress`.

### Lifecycle
| Export | Purpose |
|--------|---------|
| `aarcadecore_get_api_version()` | Returns `AARCADECORE_API_VERSION` (currently 2) for compatibility check |
| `aarcadecore_init(callbacks)` | Initialize DLL with host-provided callbacks |
| `aarcadecore_shutdown()` | Shutdown and free all resources |
| `aarcadecore_update()` | Run one frame (call per game frame) |
| `aarcadecore_is_active()` | Returns true if any embedded instance is running |

### Rendering
| Export | Purpose |
|--------|---------|
| `aarcadecore_get_material_name()` | Returns the material name to render to (e.g., `"compscreen.mat"`) |
| `aarcadecore_render_texture(pixelData, w, h, is16bit, bpp)` | Fill a pixel buffer with the current frame |

### Audio
| Export | Purpose |
|--------|---------|
| `aarcadecore_get_audio_sample_rate()` | Returns sample rate in Hz (e.g., 32040 for SNES) |
| `aarcadecore_get_audio_samples(buffer, max_frames)` | Pull interleaved stereo int16_t PCM samples |

### Main Menu / Overlay
| Export | Purpose |
|--------|---------|
| `aarcadecore_toggle_main_menu()` | Toggle the main menu HUD overlay on/off |
| `aarcadecore_is_main_menu_open()` | Check if the main menu is currently open |
| `aarcadecore_should_open_engine_menu()` | Check if DLL requests opening the engine's native menu |
| `aarcadecore_clear_engine_menu_flag()` | Clear the engine menu request flag |
| `aarcadecore_should_start_libretro()` | Check if DLL requests starting a Libretro instance |
| `aarcadecore_clear_start_libretro_flag()` | Clear the Libretro start request flag |
| `aarcadecore_start_libretro()` | Actually start the Libretro instance |
| `aarcadecore_render_overlay(pixelData, w, h)` | Render the fullscreen overlay (BGRA pixels) |

### Task Management
| Export | Purpose |
|--------|---------|
| `aarcadecore_get_task_count()` | Returns number of running tasks |
| `aarcadecore_render_task_texture(idx, pixelData, w, h, is16bit, bpp)` | Render a specific task to a pixel buffer |

### Host Callbacks (`AACoreHostCallbacks`)
The host provides these to the DLL at init time:

```c
typedef struct AACoreHostCallbacks {
    int api_version;                          // Must match AARCADECORE_API_VERSION
    AACore_PrintfFn       host_printf;        // void(const char* fmt, ...)
    AACore_GetKeyStateFn  get_key_state;      // int(int key_index) — nonzero = pressed
} AACoreHostCallbacks;
```

**Key design principle:** The DLL never includes engine headers. All interaction uses opaque types (`void*`) and standard C types. The host provides only what the DLL needs via callbacks.

## DLL Source Files (`src/aarcadecore/`)

| File | Purpose |
|------|---------|
| `aarcadecore_api.h` | Public API — shared between DLL and host. No engine deps. |
| `aarcadecore_internal.h` | Internal types: EmbeddedInstance base struct + vtable |
| `aarcadecore.cpp` | DLL entry point: exports, global host callbacks, delegates to managers |
| `EmbeddedInstance.cpp` | Base helpers (currently minimal) |
| `LibretroInstance.cpp` | EmbeddedInstance implementation for Libretro cores |
| `LibretroManager.cpp` | Creates/manages the active LibretroInstance |
| `SteamworksWebBrowserInstance.cpp` | Steamworks HTML Surface browser — loads URLs, renders BGRA to textures |
| `SteamworksWebBrowserManager.cpp` | Creates/manages the active Steamworks browser instance |
| `UltralightInstance.cpp` | Ultralight HTML renderer — loads local HTML files, CPU rendering to textures |
| `UltralightManager.cpp` | Creates/manages the active Ultralight instance |
| `libretro_host.cpp` / `.h` | Low-level Libretro core loading, audio ring buffer, video frame buffer |
| `ui/ui.html` | Test HTML page for the Ultralight renderer |

**Note:** All DLL source files are C++ (`.cpp`). The public API (`aarcadecore_api.h`) uses `extern "C"` so any C host can load the DLL.

## Embedded Instance Types

| Type | Status | Description |
|------|--------|-------------|
| `EMBEDDED_LIBRETRO` | Working | Runs Libretro emulator cores (e.g., bsnes for SNES). Full video, audio, and gamepad input. |
| `EMBEDDED_STEAMWORKS_BROWSER` | Working | Steamworks HTML Surface. Loads URLs, renders pages. Requires Steam running + `steam_appid.txt`. |
| `EMBEDDED_ULTRALIGHT` | Working | Ultralight HTML renderer. Loads local HTML files for UI. CPU rendering mode. |

Tasks are created on demand (not at startup). The DLL's `aarcadecore_init()` only initializes the Ultralight HUD overlay. Libretro and Steamworks instances are started later via JS bridge commands or host API calls.

## Build Dependencies (not in git)

| SDK | Path | Purpose |
|-----|------|---------|
| Steamworks SDK 1.64 | `steamworks_sdk_164/` | Headers + `steam_api64.lib` for Steamworks browser |
| Ultralight SDK 1.4.0 | `ultralight-free-sdk-1.4.0-win-x64/` | Headers + libs + DLLs for HTML rendering |

Both SDKs must be present on disk for building but are excluded from git via `.gitignore`.

**Runtime DLLs needed in the game directory:**
- `steam_api64.dll` (from Steamworks SDK)
- `Ultralight.dll`, `UltralightCore.dll`, `AppCore.dll`, `WebCore.dll` (from Ultralight SDK)
- `resources/` folder (from Ultralight SDK — contains ICU data, CA certs, etc.)
- `steam_appid.txt` containing `480` (for Steamworks testing)

## Internal Architecture: EmbeddedInstance

The DLL uses a vtable-based "inheritance" pattern in C++:

```c
struct EmbeddedInstance {
    EmbeddedInstanceType type;              // EMBEDDED_LIBRETRO, EMBEDDED_STEAMWORKS_BROWSER
    const EmbeddedInstanceVtable* vtable;   // init, shutdown, update, is_active, render
    const char* target_material;            // e.g., "compscreen.mat"
    void* user_data;                        // type-specific state
};
```

Each concrete type (LibretroInstance, SteamworksWebBrowserInstance) provides its own vtable implementation. The managers create instances and the top-level `aarcadecore.c` delegates to the active instance.

## How Per-Thing Rendering Works

Each spawned compscreen thing gets its own GL texture. The host swaps `texture_id` on the shared `rdDDrawSurface` before each tracked thing is rendered.

1. **Spawn** (H key): `AACoreManager_RegisterThingTask()` creates a 256x256 GL texture with a unique color, stores it in the thing-to-task mapping
2. **PreRenderThing**: Called from `sithRender_RenderThing` before `rdThing_Draw`. If this thing is tracked:
   - `rdCache_Flush()` — draws pending faces with the previous thing's texture_id
   - Overwrites `alphaMats[0].texture_id` and `opaqueMats[0].texture_id` on the shared compscreen surface with this thing's GL texture
3. **rdThing_Draw**: Faces added to deferred render list. They reference the shared surface, which now has this thing's texture_id.
4. **End-of-frame `rdCache_Flush()`**: Draws the last tracked thing's faces

**Why direct overwrite works:** All compscreen tris point to the same `rdDDrawSurface` address. The deferred renderer reads `texture_id` from that address at draw time. By overwriting the value and flushing between things, each thing's faces draw with the correct GL texture.

**Dynamic texture callback** (`rdDynamicTexture_Register`) is currently disabled — the texture_id swap approach doesn't need it.

## How Audio Works

1. **Libretro core** produces audio → `host_audio_sample_batch_callback()` writes to ring buffer inside DLL
2. **Host** calls `aarcadecore_get_audio_sample_rate()` after init → opens SDL audio device at that rate
3. **SDL audio callback** (host-side) calls `aarcadecore_get_audio_samples(buffer, max_frames)`
4. **DLL** reads from the ring buffer via `libretro_host_read_audio()` → returns frames to host
5. **Host** plays the audio through SDL

The ring buffer is lock-free (single producer/single consumer) since the Libretro core writes on the game thread and the SDL audio callback reads on the audio thread.

## How the Main Menu Works

The main menu is a fullscreen Ultralight HUD overlay toggled by pressing Escape.

### Flow:
1. **Escape key** → `aaMainMenu_Update()` in `src/Main/aaMainMenu.c` → `AACoreManager_ToggleMainMenu()`
2. **Host** calls `aarcadecore_toggle_main_menu()` DLL export
3. **DLL** swaps the HUD Ultralight instance between `mainMenu.html` and `blank.html`
4. **Each frame** (in `jkGame_Update`): host calls `AACoreManager_DrawOverlay()` which:
   - Calls `aarcadecore_render_overlay()` to get 1920x1080 BGRA pixels
   - Uploads to a GL texture
   - Draws a fullscreen quad via `std3D_DrawUITexturedQuad()` (engine's shader-based UI system)

### JS Bridge — Unified `aapi` Object (C++ ↔ JavaScript):
- In `OnWindowObjectReady` (fires before page scripts), Ultralight creates a `window.aapi` object with namespaces
- **`aapi.manager.*`** — host/engine communication:
  - `aapi.manager.closeMenu()`, `openEngineMenu()`, `startLibretro()`, `openLibraryBrowser()`, `getVersion()`
- **`aapi.library.*`** — SQLite database queries:
  - `aapi.library.getItems(offset, limit)`, `searchItems(query, limit)`
  - `getTypes()`, `getModels()`, `searchModels()`, `getApps()`, `searchApps()`
  - `getMaps()`, `searchMaps()`, `getPlatforms()`, `getInstances()`, `searchInstances()`
- Uses JavaScriptCore C API with direct function callbacks per method (no string dispatch)
- Bridge is set up via `ViewListener::OnWindowObjectReady` so it's available before `DOMContentLoaded`

### Library Browser:
- `library.html` / `library.js` / `style.css` — ported from aarcade-core project
- Opened via `aapi.manager.openLibraryBrowser()` from main menu
- Queries library database through `aapi.library.*` methods
- SQLite database (`library.db`) opened at DLL init via `SQLiteLibrary` class
- `ArcadeTypes.h` defines data structs (Item, Type, Model, App, Map, Platform, Instance)
- Images not yet implemented (placeholder shown)

### Dynamic texture hooks:
- Currently **disabled** — `rdDynamicTexture_Register` call is commented out
- Will be re-enabled for per-thing rendering once a working approach is found
- In-game compscreen surfaces show their original static texture

### Transparency:
- Ultralight view uses `is_transparent = true`
- HTML `background: transparent` on html/body
- GL overlay uses alpha blending via the UI render list

## Input Mode

Input suppression is based on whether the **main menu is open** (`AACoreManager_IsMainMenuOpen()`), not whether any instance is active. This allows gamepad input to flow to Libretro even when the menu is closed.

When the main menu is open:
- **Keyboard**: Game receives nothing — `stdControl_ReadControls` skips keyboard polling.
- **Gamepad**: Game receives nothing — `stdControl_ReadControls` returns before gamepad polling.
- **Mouse**: Game receives nothing — SDL mouse events forwarded exclusively to AArcade.
- **Escape key**: Read directly via `SDL_GetKeyboardState` (bypassing suppressed stdControl) to toggle the menu. Engine's own escape menu is suppressed in `Window.c`.

When the main menu is closed (but a task like Libretro may be running):
- **Keyboard/Mouse**: Normal game input
- **Gamepad**: Normal game input AND Libretro reads gamepad state via `get_key_state` callback (both receive input simultaneously)

## How Input Works

### Keyboard Input (event-based)
The host forwards SDL keyboard events to the DLL via three exports:
- `aarcadecore_key_down(vk_code, modifiers)` — from SDL_KEYDOWN
- `aarcadecore_key_up(vk_code, modifiers)` — from SDL_KEYUP
- `aarcadecore_key_char(unicode_char, modifiers)` — synthesized from SDL_KEYDOWN for printable chars

**Modifier bitmask:** `AACORE_MOD_ALT=1, AACORE_MOD_CTRL=2, AACORE_MOD_SHIFT=4`

Each instance type handles these differently:
- **Steamworks**: Calls `ISteamHTMLSurface::KeyDown/KeyUp/KeyChar`
- **Ultralight**: Constructs `KeyEvent` and calls `View::FireKeyEvent`
- **Libretro**: Two modes (see below)

### Libretro Input Modes

The LibretroInstance has two keyboard input modes (`inputMode` field):

**Emulated mode (default):** Maps keyboard keys to RETRO_DEVICE_JOYPAD buttons:

| Key | SNES Button |
|-----|-------------|
| W / Up Arrow | D-pad Up |
| S / Down Arrow | D-pad Down |
| A / Left Arrow | D-pad Left |
| D / Right Arrow | D-pad Right |
| Enter | Start |
| Shift | Select |
| J | B (action/jump) |
| K | A |
| U | Y |
| I | X |
| Q | L shoulder |
| E | R shoulder |

Emulated keyboard and physical gamepad inputs are OR'd together.

**Raw mode:** Converts VK codes to RETROK_* codes and sets them in a keyboard state array. The Libretro core queries these via `input_state_callback` with `device=RETRO_DEVICE_KEYBOARD`.

### Mouse Input (event-based)
The host forwards SDL mouse events when AArcade is active:
- `aarcadecore_mouse_move(x, y)` — from SDL_MOUSEMOTION, coords mapped to overlay space (0-1920, 0-1080)
- `aarcadecore_mouse_down(button)` / `aarcadecore_mouse_up(button)` — from SDL_MOUSEBUTTONDOWN/UP
- `aarcadecore_mouse_wheel(delta)` — from SDL_MOUSEWHEEL (delta * 120)

**Button constants:** `AACORE_MOUSE_LEFT=0, AACORE_MOUSE_RIGHT=1, AACORE_MOUSE_MIDDLE=2`

Each instance type handles mouse differently:
- **Ultralight**: `View::FireMouseEvent(MouseEvent)` — tracks last mouse position for click events
- **Steamworks**: `ISteamHTMLSurface::MouseMove/MouseDown/MouseUp/MouseWheel`
- **Libretro**: No mouse support (NULL vtable entries)

**Cursor rendering:** A white rectangle is drawn at the mouse position on top of the overlay using `std3D_DrawUITexturedQuad` with a 1x1 white GL texture.

### Gamepad Input (polling-based)
1. **Host** provides `get_key_state(int key_index)` callback at init
2. **DLL's LibretroInstance** polls `g_host.get_key_state(AACORE_KEY_JOY1_B1)` etc. each frame
3. Builds a 16-bit RETRO_DEVICE_JOYPAD mask and passes to `libretro_host_set_input()`

**Gamepad button mapping** uses physical position (not name) for SNES compatibility:
- Xbox A (bottom) → SNES B, Xbox B (right) → SNES A
- Xbox X (left) → SNES Y, Xbox Y (top) → SNES X

## Host-Side Integration (`src/Platform/Common/AACoreManager.c`)

The host-side manager handles:
1. **DLL loading**: `SDL_LoadObject("aarcadecore.dll")` + symbol lookup for all 12 exports
2. **API version check**: Compares DLL's version with host's `AARCADECORE_API_VERSION`
3. **Host callbacks**: Provides `host_printf` (wraps `stdPlatform_Printf`), `host_get_key_state` (reads `stdControl_aKeyInfo[]`)
4. **Texture registration**: Dynamic texture hooks currently disabled (will re-enable for per-thing rendering)
5. **Audio device**: Opens SDL audio device at DLL's sample rate, callback pulls via `aarcadecore_get_audio_samples()`
6. **Task textures**: Pre-renders per-task GL textures each frame (forward-looking infrastructure for per-thing rendering)
7. **Thing-task mapping**: `RegisterThingTask()` maps spawned sithThings to task indices

## Build Configuration

The DLL is built as a SHARED library target in the same CMake project:

```cmake
file(GLOB AARCADECORE_SOURCES ${PROJECT_SOURCE_DIR}/src/aarcadecore/*.c)
add_library(aarcadecore SHARED ${AARCADECORE_SOURCES})
target_link_libraries(aarcadecore PRIVATE SDL::SDL version imm32 setupapi gdi32 winmm ole32 oleaut32 shell32 user32 advapi32)
target_compile_definitions(aarcadecore PRIVATE AARCADECORE_EXPORTS)
```

- `AARCADECORE_EXPORTS` triggers `__declspec(dllexport)` on exported functions
- Links SDL2 (for audio ring buffer timing and Libretro DLL loading)
- Links Windows system libs (required by SDL2 static)
- Source files in `src/aarcadecore/` are excluded from the main engine build via `list(REMOVE_ITEM ...)`

Output: `build_libretro_host/Release/aarcadecore.dll`

## Porting to Another Engine

To use `aarcadecore.dll` in a different game engine:

1. Include `aarcadecore_api.h` (the only header needed)
2. Load the DLL and resolve 9 function symbols
3. Provide `AACoreHostCallbacks` with your engine's printf and key state functions
4. Call `aarcadecore_init(&callbacks)`
5. Register a texture callback in your engine for `aarcadecore_get_material_name()`
6. In that callback, call `aarcadecore_render_texture(pixelData, w, h, is16bit, bpp)`
7. Open an audio device at `aarcadecore_get_audio_sample_rate()` Hz
8. In your audio callback, call `aarcadecore_get_audio_samples(buffer, frames)`
9. Call `aarcadecore_update()` every frame
10. Call `aarcadecore_shutdown()` on exit
