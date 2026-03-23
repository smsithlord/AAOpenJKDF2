# AArcade OpenJK — Design Overview

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   OpenJKDF2 Game Engine                  │
│                   (openjkdf2-64.exe)                     │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  sithThing   │  │  rdCache /   │  │  sithCollision │  │
│  │  (3D objects) │  │  std3D (GL)  │  │  (raycasts)   │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘  │
│         │                 │                  │          │
│  ┌──────┴─────────────────┴──────────────────┴───────┐  │
│  │              AACoreManager (C host)                │  │
│  │  - Loads aarcadecore.dll via SDL_LoadObject        │  │
│  │  - Per-thing texture swap (PreRender/PostRender)   │  │
│  │  - Selector ray, spawn/move mode, overlay render   │  │
│  │  - Keybinds, input routing, cursor management      │  │
│  └──────────────────────┬────────────────────────────┘  │
│                         │ C API (function pointers)     │
└─────────────────────────┼───────────────────────────────┘
                          │
┌─────────────────────────┼───────────────────────────────┐
│                aarcadecore.dll                           │
│            (AArcade Core — engine-agnostic)              │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │              Instance Manager                    │    │
│  │  - SpawnedObject tracking (itemId, modelId, etc)│    │
│  │  - Spawn/move pipeline with transform system    │    │
│  │  - Embedded instance lifecycle (SWB, Libretro)  │    │
│  │  - Selector ray, favorites, model cycling       │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌──────────────────┐  ┌──────────────────────────┐    │
│  │   SQLite Library  │  │      Image Loader        │    │
│  │  (library.db)     │  │  (headless Ultralight)   │    │
│  │  - Items, Models  │  │  - Download URLs         │    │
│  │  - Types, Apps    │  │  - Cache as PNG          │    │
│  │  - Maps, Instances│  │  - CRC32 hash paths      │    │
│  │  - Platform files │  │                          │    │
│  └──────────────────┘  └──────────────────────────┘    │
│                                                         │
│  ┌─────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │  Ultralight  │ │  Steamworks  │ │    Libretro     │  │
│  │  (HUD/menus) │ │ Web Browser  │ │   (emulators)   │  │
│  │              │ │   (SWB)      │ │                 │  │
│  │  Renders to  │ │  Renders to  │ │  Renders to     │  │
│  │  overlay +   │ │  per-thing   │ │  per-thing      │  │
│  │  per-thing   │ │  textures +  │ │  textures       │  │
│  │  textures    │ │  fullscreen  │ │                 │  │
│  └──────┬──────┘ └──────┬───────┘ └────────┬────────┘  │
│         │               │                  │            │
│         └───────────────┴──────────────────┘            │
│              EmbeddedInstance vtable                     │
│              (init, render, input, navigate)             │
└─────────────────────────────────────────────────────────┘
```

## Component Roles

### OpenJKDF2 Game Engine (`openjkdf2-64.exe`)
The host application — a reverse-engineered Jedi Knight: Dark Forces II engine. Provides 3D rendering, physics, sector-based levels, and the COG scripting system. AArcade uses it as the spatial environment where media objects exist.

### AACoreManager (`AACoreManager.c/.h`)
The bridge between the engine and the DLL. Written in C (engine's language). Responsibilities:
- Loads `aarcadecore.dll` at startup via `SDL_LoadObject`
- Provides host callbacks (printf, key state, current map)
- Per-thing texture rendering: swaps GL texture IDs on shared DynScreen/DynMarquee materials before each thing renders
- Selector ray: raycasts from player aim, skips adjoins, stores hit data
- Spawn/move mode: preview positioning, transform application, template swaps
- Overlay rendering: composites HUD over SWB content (fullscreen quad or per-thing texture)
- Input routing: keyboard/mouse to DLL, click-through alpha testing, focus routing

### AArcade Core DLL (`aarcadecore.dll`)
Engine-agnostic content management. Written in C++. Has NO engine-specific code — communicates purely through the C API. Could be ported to other engines.

### Embedded Instances (vtable pattern)
All content renderers implement the same `EmbeddedInstanceVtable`:
```
init → shutdown → update → is_active → render
key_down/up/char → mouse_move/down/up/wheel
get_title → get_width/height → navigate → go_back/forward/reload
```

### Ultralight SDK
WebKit-based HTML/CSS/JS renderer. Two roles:
1. **HUD instance**: Renders menus (main menu, tab menu, build context menu, edit item, spawn mode overlay) as fullscreen overlay
2. **Image Loader**: Headless instance that downloads images via `<img>` tags and captures pixels for caching
3. **Overlay compositing**: HUD pixel buffer composited over SWB content (fullscreen or per-thing)

### Steamworks Web Browser (SWB)
Steam's built-in Chromium browser via `ISteamHTMLSurface`. Renders web content (YouTube, websites) to BGRA pixel buffers. Supports navigation (back/forward/reload/LoadURL), title tracking, and mouse/keyboard input.

### Libretro
Emulator core loading system. Loads `.dll` cores (SNES, N64, etc.) and games, renders frames to pixel buffers. Receives gamepad input via the host's `get_key_state` callback.

### SQLite (`library.db`)
Shared media library database. Contains:
- **Items**: Media entries (games, videos, etc.) with title, type, file URL, images
- **Models**: 3D model entries with platform-specific template files
- **Types/Apps/Platforms**: Metadata categories
- **Maps/Instances/InstanceObjects**: Per-level object placement with position/rotation/model

### Image Loader
Parallel image download system using a headless Ultralight view. Downloads URLs → captures rendered pixels → saves as PNG in `./cache/urls/[hash].png`. CRC32 hash (Kodi-compatible) for cache file naming.

## Data Flow

### Spawning an object
```
Library Browser (JS) → aapi.manager.spawnItemObject(itemId)
  → InstanceManager.requestSpawn(item)
    → resolves model + template from SQLite
    → queues SpawnRequest
  → Host polls has_pending_spawn → pop
    → sithTemplate_GetEntryByName(templateName)
    → sithThing_Create(template, pos, orient, sector)
    → RegisterThingTask (creates GL textures)
    → initSpawnedObject (creates SpawnedObject, starts image loading)
  → Enter spawn mode (preview follows aim)
    → Mouse wheel: cycle models (destroy+recreate thing)
    → RMB: transform panel (PYR + XYZ sliders)
  → LMB: confirmSpawn + report_thing_transform (saves to SQLite)
  → Model remembered in localStorage for next spawn
```

### Action commands (weapon slot hotkeys)
```
sithWeapon.c → AACoreManager_OnWeaponSlotPressed(slot)
  → g_fn_action_command("ObjectMove" | "ObjectRemove" | "ObjectClone" | "TaskClose")
  → aarcadecore.cpp dispatches to InstanceManager methods
  → Returns true to swallow the weapon switch keypress
Slot mapping: 0=TaskClose, 2=ObjectRemove, 3=ObjectClone, 4=ObjectMove
```

### Uniform scale
```
Per-object scale stored in SpawnedObject.scale and instance_objects.scale (SQLite).
PreRenderThing: scales lookOrientation axes by object scale factor.
PostRenderThing: restores by 1/scale.
Live preview during spawn/move mode via spawnScale_ in spawn transform.
Clone copies source object's scale. Scale slider in spawnMode.html (0.1–10x).
```

### Remembered model (localStorage)
```
On spawn confirm: spawnMode.html saves model ID to localStorage:
  - lastSpawnModelId (global)
  - lastSpawnModelId_ITEMID (per-item)
On new spawn: InstanceManager.requestSpawn reads localStorage via
  UltralightManager_EvalLocalStorage → EvaluateScript on HUD view.
  Uses remembered model instead of default template.
```

### Rendering per-thing content
```
Each frame:
  1. SWB/Libretro renders to 16-bit RGB565 pixel buffer
     - Per-frame dedup: lastRenderedFrame skips duplicate 16-bit renders
     - PreRenderThing marks thing as seen (lastSeenFrame for throttling)
  2. HUD overlay composited on top (if input mode)
  3. Host uploads pixels to GL texture
  4. PreRenderThing: swap DynScreen texture_id → thing's GL texture
  5. Engine renders the thing with swapped texture
  6. PostRenderThing: restore original texture_id
  7. rdCache_Flush between things to prevent batching
```

### Fullscreen overlay rendering
```
Fullscreen embedded instance:
  1. Instance renders to g_cleanFrameBuffer (1920x1080 BGRA32)
     - Skipped if no new content (buffer stays clean from last render)
  2. Clean buffer copied to output pixelData
  3. HUD composited on top of copy (integer alpha blend, no float math)
  4. Host uploads to GL texture + draws fullscreen quad

Input mode (no fullscreen): render_overlay returns false → no quad drawn
Spawn mode: HUD rendered directly (no instance underneath)
```

### Input routing
```
SDL events → Window.c
  → AACoreManager_IsActive() check
    → If active: mouse/keyboard → aarcadecore.dll
      → get_input_target() determines receiver:
        - Input mode: SWB (with HUD alpha click-through)
        - Fullscreen: SWB + HUD overlay
        - Menu: HUD Ultralight
        - Spawn mode: HUD (transform panel)
    → If not active: normal game input
```

## Key Files

| Area | Files |
|------|-------|
| Host bridge | `src/Platform/Common/AACoreManager.c/.h` |
| Input handling | `src/Main/aaMainMenu.c` |
| DLL core | `src/aarcadecore/aarcadecore.cpp`, `aarcadecore_api.h` |
| Instance management | `src/aarcadecore/InstanceManager.cpp/.h` |
| Ultralight HUD | `src/aarcadecore/UltralightManager.cpp`, `UltralightInstance.cpp` |
| SWB | `src/aarcadecore/SteamworksWebBrowserInstance.cpp` |
| Libretro | `src/aarcadecore/LibretroInstance.cpp` |
| Libretro core config | `src/aarcadecore/LibretroCoreConfig.cpp/.h` |
| SQLite | `src/aarcadecore/SQLiteLibrary.cpp/.h` |
| Image cache | `src/aarcadecore/ImageLoader.cpp/.h` |
| UI pages | `src/aarcadecore/ui/*.html/*.js/*.css` |
| Types/structs | `src/aarcadecore/ArcadeTypes.h`, `aarcadecore_internal.h` |
