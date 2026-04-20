# Libretro in AAOpenJKDF2 — Onboarding Guide

A focused tour of the Libretro integration. Read this before modifying any
file under `src/aarcadecore/libretro_*` or `src/aarcadecore/LibretroInstance.cpp` —
it explains how the pieces actually fit and where the sharp edges are.

For the broader DLL architecture (embedded instance model, host callback API,
library DB, overlay HUD), read
[LLM-AARCADECORE-README.md](LLM-AARCADECORE-README.md) first.

## TL;DR

A Libretro core is just a DLL that obeys a well-defined C ABI. We load it,
feed it input each frame, let it fill a video/audio buffer, and read that
buffer back into a JKDF2 `sithThing`'s dynamic texture (or the overlay HUD
when fullscreen). Everything happens inside `aarcadecore.dll`; the engine
sees only the pixel/audio output through the generic "embedded instance"
adapter.

Two layers, both inside the DLL:

| Layer | File | Role |
|---|---|---|
| **LibretroHost** | [libretro_host.cpp](../../src/aarcadecore/libretro_host.cpp) / [.h](../../src/aarcadecore/libretro_host.h) | Owns one Libretro core instance. Loads the DLL, spins up a dedicated worker thread, services the `retro_*` callbacks, handles HW GL cores, options, SRAM + savestate persistence, audio resampling, ROM archive extraction. Pure backend — knows nothing about JKDF2 or embedded instances. |
| **LibretroInstance** | [LibretroInstance.cpp](../../src/aarcadecore/LibretroInstance.cpp) | `EmbeddedInstance` vtable wrapper. Creates a `LibretroHost`, maps JKDF2 keyboard events to emulated joypad bits, polls physical gamepad via the host's `get_gamepad_state` callback, copies the core's frame into the DynScreen pixel buffer. This is the adapter that lets the engine treat a running SNES game the same way it treats a video player or a web browser. |

`LibretroManager` ([LibretroManager.cpp](../../src/aarcadecore/LibretroManager.cpp))
is tiny — a registry so `host_input_state_callback` can find the `LibretroHost`
that owns the calling thread (`LibretroManager_FindByThread`), plus thin
lifecycle helpers used by `InstanceManager`.

## Threading model

Critical and easy to get wrong.

- **Main thread** (engine thread) owns the engine's GL context. All
  `LibretroInstance` vtable methods (`update`, `render`, `key_*`) run here.
- **Per-host worker thread** owns the core's GL context (when HW render),
  runs `retro_init`, `retro_load_game`, `retro_run`, state save/load,
  option load. Started by `libretro_host_create`.
- Main ↔ worker talk via SDL atomics + semaphores (`wake_sem`, `pending_frames`,
  request/response slots). The main thread *never* calls `retro_*` functions
  directly — always via the worker.

Why a worker at all: many HW cores (Mupen64Plus-Next, PPSSPP, Beetle PSX HW)
make GL calls during `retro_run`. Doing that on the main thread would
clobber the engine's render state. A dedicated thread with its own
shared-with-engine GL context avoids that.

Every libretro callback (`retro_environment_t`, `retro_video_refresh_t`,
`retro_audio_sample_batch_t`, `retro_input_state_t`) runs on the *worker*
thread, inside `retro_run`. If you're reading a callback and it touches
`LibretroHost` fields, assume worker-thread context.

## Frame pipeline

```
engine frame N
  LibretroInstance::update          main thread
    ├─ build joypad mask (gamepad + emulated keyboard)
    ├─ libretro_host_set_input(host, 0, mask)      ──┐ stores to host->input_state[0]
    ├─ libretro_host_set_analog(host, 0, L, lx, ly) ─┤
    ├─ libretro_host_set_analog(host, 0, R, rx, ry) ─┤
    └─ libretro_host_run_frame(host)                 │ SDL_SemPost(wake_sem)
                                                     │
                             ──────────────────────  ▼ worker thread
                                                   retro_run
                                                     ├─ calls retro_input_state_t N times
                                                     │    → host_input_state_callback
                                                     │        → reads input_state / analog_state
                                                     ├─ calls retro_video_refresh_t
                                                     │    (SW path: copies into frame buffer
                                                     │     HW path: renders into FBO, triggers
                                                     │              glReadPixels into frame buffer)
                                                     └─ calls retro_audio_sample_batch_t
                                                          (resample to 48kHz, push to audio ring)

engine frame N+1
  LibretroInstance::render           main thread
    └─ libretro_host_get_frame → memcpy/scale into DynScreen pixel buffer
                                  (16-bit for in-world, 32-bit for fullscreen)
```

One `retro_run` happens per engine frame (not per libretro frame — there
is no rate correction yet; fast cores are effectively capped at the
engine's FPS).

## HW render path

Only cores that advertise `RETRO_ENVIRONMENT_SET_HW_RENDER`. Today that's
Mupen64Plus-Next (N64), PPSSPP (PSP), Beetle PSX HW, etc.

- Host provides `libretro_create_gl_context` / `libretro_set_current_gl_context`
  / `libretro_destroy_gl_context` via `AACoreHostCallbacks`.
  See [AACoreManager.c](../../src/Platform/Common/AACoreManager.c) —
  `host_libretro_create_gl_context` creates an SDL GL context on the
  *engine's* window that `SHARE_WITH_CURRENT_CONTEXT`, so FBOs / textures
  / PBOs created by the core are visible from the engine's texture cache.
- `LibretroHost::fbo` is a host-side FBO bound as the core's default
  framebuffer target. `hw_get_current_framebuffer` returns it. The core
  renders all layers here.
- On `retro_video_refresh(data=RETRO_HW_FRAME_BUFFER_VALID, ...)`, we do
  `glBindFramebuffer(GL_READ_FRAMEBUFFER, host->fbo)` +
  `glReadPixels` into `host->frame_buf`. From then on the HW path merges
  back into the SW path: the pixels sit in `frame_buf` and
  `libretro_host_get_frame` hands them to `LibretroInstance::render`.

**Known gap:** parallel-n64 (GLide64) uses GLSM — a libretro macro layer
that intercepts every GL call and batches state changes. Without a GLSM
implementation on our side, Glide64 renders black frames. Mupen64Plus-Next's
GLES3 plugin works; Parallel N64 does not. Don't try to "fix" this by
tweaking FBO binds — the gap is architectural and needs a real GLSM
frontend.

## SW (software) render path

Everything that doesn't set `RETRO_ENVIRONMENT_SET_HW_RENDER`. `video_refresh`
gets a pointer + pitch + format (`RETRO_PIXEL_FORMAT_RGB565` /
`XRGB8888` / `0RGB1555`), we memcpy into `frame_buf`. The scale + format
conversion to RGB565 for in-world DynScreen happens in
`LibretroInstance::render` — `scale_x/scale_y` downscales the core's native
res to the 256×256 (or whatever) DynScreen texture; fullscreen path
preserves native res and writes 32-bit.

## Input pipeline

Two sources, unified in `libretro_inst_update`.

### Physical gamepad (polling)

Main thread each frame:
```
LibretroInstance::update
  → libretro_build_joypad_state()
      → g_host.get_gamepad_state(0, &state)     ← AACoreHostCallbacks.get_gamepad_state
          → host_get_gamepad_state in AACoreManager.c
              → stdControl_GetGamepad(0) returns SDL_GameController*
              → SDL_GameControllerGetButton / GetAxis directly
```

**Important:** we *bypass* `stdControl_aKeyInfo` for gamepad reads. The
engine's `stdControl` polling is suppressed when AArcade input lock mode is
active (per `stdControl_bControlsActive` gate in `stdControl_ReadControls`),
so reading gamepad state through it would silently break the moment the
user locks input to a cabinet. Going direct to SDL is the point — gamepad
works regardless of engine state.

- Button mapping: Xbox A→RETRO B (SNES face position, not name). See
  `libretro_build_joypad_state` for the full table.
- Analog sticks: left→ `INDEX_ANALOG_LEFT`, right→ `INDEX_ANALOG_RIGHT`.
  N64 cores auto-interpret the right stick as the C-buttons — no extra
  mapping needed.
- Triggers are read as axes (`lt`/`rt`) and converted to L2/R2 button
  presses above `AA_TRIGGER_THRESH` (0x2666).

### Keyboard (event-driven)

SDL keydown → `Window.c` → `AACoreManager_KeyDown(vk, mods)` →
`aarcadecore_key_down` → input-target instance's `key_down` vtable →
`libretro_inst_key_down`. Only fires when `AACoreManager_IsActive()` is
true (fullscreen / input mode / main menu). Outside those states, the
engine consumes the event.

Two libretro keyboard modes (`LibretroInstanceData::inputMode`):
- **Emulated (default):** `vk_to_emulated_joypad` maps WASD/arrows/Enter/JKQE
  to `RETRO_DEVICE_ID_JOYPAD_*` bits, OR'd into `emulatedJoypad`, which then
  ORs with the physical gamepad in `libretro_inst_update`.
- **Raw:** `vk_to_retrok` maps VK codes to `RETROK_*` codes and pokes
  `libretro_host_set_key_state`, which writes `host->keyboard_state[]`.
  The core reads it via `input_state_callback(device=RETRO_DEVICE_KEYBOARD)`.

If you add new emulated keys, put them in `vk_to_emulated_joypad` — not a
new callback path.

## Audio pipeline

Core calls `retro_audio_sample_batch_t` (worker thread) with interleaved
stereo samples at `av_info.timing.sample_rate`. We:

1. Resample to **48 kHz** (our target, `LIBRETRO_TARGET_AUDIO_RATE`).
   `host->resample_phase` carries the fractional source-index between
   batches so cross-batch boundaries don't click.
2. Write into the lock-free audio ring (`audio_ring_buf`, ~250 ms @ 48 kHz
   stereo, rounded up to 32768 int16). Atomic `audio_ring_write` /
   `audio_ring_read` indices.
3. Main-thread SDL audio callback (registered by `AACoreManager.c`) calls
   `aarcadecore_get_audio_samples` → `libretro_host_pull_audio`, which reads
   from the ring.

If audio is crackling / dropping, suspect the resampler phase carry or the
ring over/underflow. Don't blame the SDL device.

## Option system

Cores declare their options via `RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2`
(or the older V1 / `SET_VARIABLES`). Every variant ends up in
`LibretroHost::options` as `CoreOptionDef` + a string-valued current-value
map in `LibretroHost::options_current`.

- **Per-core config:** `aarcadecore/libretro/config/<core>.opt`. Loaded by
  `load_persisted_options` on core init, saved by `save_persisted_options`
  whenever the user changes an option.
- **Per-game overrides:** `aarcadecore/libretro/config/games/<core>/<game>.opt`.
  Loaded by `load_game_options` in the worker's `worker_do_load_game`
  (and after a swap via `worker_do_swap_unload`), saved on destroy.
  Format: plain `key=value` lines; an empty value means "inherit core
  default for this game" (i.e. clear the override).
- **Resolution order when the core queries a var:** game override →
  per-core current → core's own declared default.

UI surface: the Libretro Options menu at
[src/aarcadecore/ui/libretroOptions.html](../../src/aarcadecore/ui/libretroOptions.html)
/ [.js](../../src/aarcadecore/ui/libretroOptions.js). It calls JS bridge
`aapi.manager.getLibretroCoreOptions()` (size-query C ABI — passing
`out=NULL` returns the byte count needed, then allocate + call again;
required for ~85-option cores like Mupen64Plus-Next whose JSON is ~30KB)
and `aapi.manager.setLibretroCoreOption(key, value, tier)` where `tier`
is `"core"` or `"game"` (game + empty value = clear override).

## On-disk layout

```
aarcadecore/
└── libretro/
    ├── cores/          # Core DLLs (mupen64plus_next_libretro.dll, etc.)
    ├── system/<core>/  # Core-specific BIOS / data files (core asks for this via RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY)
    ├── saves/<core>/   # .srm SRAM files, one per game
    ├── content/<core>/ # Extracted ROMs from .zip/.7z archives (libretro_archive.cpp)
    ├── state/<core>/   # Serialized retro_serialize state per game
    └── config/
        ├── <core>.opt              # Per-core options
        └── games/<core>/<game>.opt # Per-game option overrides
```

## Instance lifecycle

```
createItem.js / library.js
   └─ aapi.manager.spawnLibretroCore(item)
      └─ (C++) InstanceManager::requestSpawn(item) queues a SpawnRequest
         └─ host polls aarcadecore_has_pending_spawn each frame, spawns a
            sithThing from the model's template, calls aarcadecore_confirm_spawn
            └─ LibretroInstance_Create(core_path, game_path, material_name)
               └─ libretro_host_create                  // loads DLL, starts worker
                  └─ worker: retro_set_environment, retro_init, option scan
               └─ libretro_host_load_game                // worker: retro_load_game
                  └─ load_game_options                   // apply per-game overrides
                  └─ apply saved SRAM / state if present // warmup frames for cores that need them (Mupen64Plus-Next)
               └─ instance added to task list → visible on DynScreen
```

Cores that need warmup frames before `retro_unserialize` works (Mupen64Plus-Next
is the notorious one) stash the pending state in `pending_state_buf` and
retry post-`retro_run` for a handful of frames. See the worker's state-load
path.

Teardown: `libretro_inst_shutdown` → `libretro_host_destroy` → worker
stops → `save_persisted_options` + `save_game_options` → `retro_unload_game`
+ `retro_deinit` → `SDL_UnloadObject`.

## Visibility throttling

`libretro_inst_update` at the top of its body:

```cpp
if (inst != aarcadecore_getFullscreenInstance() &&
    inst != aarcadecore_getInputModeInstance() &&
    inst->lastSeenFrame + 1 < aarcadecore_getEngineFrame())
    return;
```

If the cabinet isn't on screen this frame, we skip `retro_run` entirely.
This keeps a room full of cabinets from melting the CPU. A consequence:
when the player aims away, `libretro_host_set_input` isn't called either,
so the last joypad state persists in `host->input_state[0]` until the
cabinet is visible again.

## Finding the active instance

Three different meanings of "active":

- `aarcadecore_getActiveInstance()` — the instance rendering to the
  player's currently-selected in-world screen.
- `aarcadecore_getFullscreenInstance()` — the instance occupying the
  full overlay (if any).
- `aarcadecore_getInputModeInstance()` — the instance that keyboard/mouse
  events route to.

The JS bridge's `findActiveLibretroInstance` (in
[UltralightInstance.cpp](../../src/aarcadecore/UltralightInstance.cpp)) walks
these in priority order — input mode → fullscreen → any active libretro
in the task list — and is what the options menu should call to identify
"the core the user is looking at."

`LibretroManager_FindByThread` is unrelated — it maps thread ID → host
so the libretro callbacks (which run on the worker) can find their own
host. Never confuse the two.

## API versioning

`AARCADECORE_API_VERSION` in `aarcadecore_api.h` — bump it when you change
the `AACoreHostCallbacks` struct shape or DLL export signatures. The host
refuses to initialize the DLL if the versions don't match, which is what
you want — a mismatched DLL would read past the callback struct and crash.

Current version history (as of this doc):

- 6 → 7: added `AACoreGamepadState` + `get_gamepad_state` callback so
  the DLL can poll SDL gamepads directly, bypassing `stdControl`.

## Common pitfalls

- **Worker-thread vs main-thread GL:** always make the right context
  current before any `gl*` call. The worker's context is activated via
  `AACore_SetCurrentGLContextFn` inside the worker's GL ops. Don't call
  `libretro_host_*` render helpers from the main thread expecting them to
  find a context.
- **Non-reentrant cores:** some cores (e.g. any Mupen build) keep
  globals. We only instantiate one `LibretroHost` per core DLL at a time —
  `InstanceManager::requestSpawn` is fine because it serializes through
  `pendingSpawns_`, but don't try to hand-roll two instances of the same
  core.
- **Option JSON truncation:** always use the size-query ABI
  (`libretro_host_get_options_json(host, NULL, 0)` for the size, then
  allocate). Fixed-size buffers break on ~85-option cores.
- **Re-entering a libretro overlay:** navigating to `libretroOptions.html`
  via `location.href` reloads the Ultralight view, clearing JS state. Any
  "active" state you need to preserve must live on the C++ side.
- **Gamepad during input lock:** the bug history here is important. In
  v6 and earlier, gamepad flowed through `stdControl_aKeyInfo[0x100+]`
  which `stdControl_ReadControls` zeroes when input lock suppresses
  engine polling. v7's `get_gamepad_state` bypass fixed that. If you
  see "gamepad stopped working after entering input mode," check that
  `get_gamepad_state` is wired up in the host's `AACoreHostCallbacks`.
- **Parallel-N64 black frames:** see "HW render path" above. Not our bug
  in the usual sense, but users will report it as one. Direct them to
  Mupen64Plus-Next.

## Where to read next

- [LLM-AARCADECORE-README.md](LLM-AARCADECORE-README.md) — the containing
  DLL's full architecture (embedded instance vtable, library DB, overlay
  HUD).
- [LLM-HUD-OVERLAY.md](LLM-HUD-OVERLAY.md) — how the Ultralight menu UI
  is plumbed.
- [LIBRETRO_BUILD.md](LIBRETRO_BUILD.md) — building libretro cores from
  source (when a stock release is missing a feature or miscompiled).
- The libretro API headers themselves: [libretro_examples/libretro.h](../../libretro_examples/libretro.h).
  The canonical source of truth for every `RETRO_*` constant. Read it.
