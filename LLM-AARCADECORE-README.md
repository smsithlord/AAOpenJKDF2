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

The DLL exports 9 flat C functions. The host loads them via `SDL_LoadFunction` or `GetProcAddress`.

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
| `aarcadecore.c` | DLL entry point: exports, global host callbacks, delegates to managers |
| `EmbeddedInstance.c` | Base helpers (currently minimal) |
| `LibretroInstance.c` | EmbeddedInstance implementation for Libretro cores |
| `LibretroManager.c` | Creates/manages the active LibretroInstance |
| `SteamworksWebBrowserInstance.c` | Stub EmbeddedInstance for future web browser |
| `SteamworksWebBrowserManager.c` | Stub manager for future web browser |
| `libretro_host.c` / `.h` | Low-level Libretro core loading, audio ring buffer, video frame buffer |

## Internal Architecture: EmbeddedInstance

The DLL uses a vtable-based "inheritance" pattern in C:

```c
struct EmbeddedInstance {
    EmbeddedInstanceType type;              // EMBEDDED_LIBRETRO, EMBEDDED_STEAMWORKS_BROWSER
    const EmbeddedInstanceVtable* vtable;   // init, shutdown, update, is_active, render
    const char* target_material;            // e.g., "compscreen.mat"
    void* user_data;                        // type-specific state
};
```

Each concrete type (LibretroInstance, SteamworksWebBrowserInstance) provides its own vtable implementation. The managers create instances and the top-level `aarcadecore.c` delegates to the active instance.

## How Rendering Works

1. **Host** calls `aarcadecore_get_material_name()` → gets `"compscreen.mat"`
2. **Host** registers its own `rdDynamicTextureCallback` with the engine for that material
3. When the engine renders a surface using that material, it calls the host's callback
4. **Host's callback** calls `aarcadecore_render_texture(pixelData, width, height, is16bit, bpp)`
5. **DLL** delegates to the active instance's `render` vtable function
6. **LibretroInstance** reads the current frame from `LibretroHost` and writes scaled pixels

**Why the host owns the callback:** The engine's `rdDynamicTextureCallback` passes engine-specific types (`rdMaterial*`, `rdTexture*`, `rdTexFormat`). The DLL can't know these types. The host bridges between engine types and the DLL's simple `(void* pixelData, int width, int height, int is16bit, int bpp)` signature.

## How Audio Works

1. **Libretro core** produces audio → `host_audio_sample_batch_callback()` writes to ring buffer inside DLL
2. **Host** calls `aarcadecore_get_audio_sample_rate()` after init → opens SDL audio device at that rate
3. **SDL audio callback** (host-side) calls `aarcadecore_get_audio_samples(buffer, max_frames)`
4. **DLL** reads from the ring buffer via `libretro_host_read_audio()` → returns frames to host
5. **Host** plays the audio through SDL

The ring buffer is lock-free (single producer/single consumer) since the Libretro core writes on the game thread and the SDL audio callback reads on the audio thread.

## How Input Works

1. **Host** provides `get_key_state(int key_index)` callback at init
2. **DLL's LibretroInstance** calls `g_host.get_key_state(AACORE_KEY_JOY1_B1)` etc. each frame
3. Key indices match OpenJKDF2's `KEY_JOY1_*` defines (0x100+ range)
4. Builds a 16-bit RETRO_DEVICE_JOYPAD mask and passes to `libretro_host_set_input()`

Button mapping uses physical position (not name) for SNES compatibility:
- Xbox A (bottom) → SNES B, Xbox B (right) → SNES A
- Xbox X (left) → SNES Y, Xbox Y (top) → SNES X

## Host-Side Integration (`src/Platform/Common/AACoreManager.c`)

The host-side manager handles:
1. **DLL loading**: `SDL_LoadObject("aarcadecore.dll")` + symbol lookup for all 9 exports
2. **API version check**: Compares DLL's version with host's `AARCADECORE_API_VERSION`
3. **Host callbacks**: Provides `host_printf` (wraps `stdPlatform_Printf`), `host_get_key_state` (reads `stdControl_aKeyInfo[]`)
4. **Texture registration**: Calls `rdDynamicTexture_Register()` with its own callback that bridges to `aarcadecore_render_texture()`
5. **Audio device**: Opens SDL audio device at DLL's sample rate, callback pulls via `aarcadecore_get_audio_samples()`

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
