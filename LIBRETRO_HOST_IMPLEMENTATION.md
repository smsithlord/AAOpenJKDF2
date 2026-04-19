# Libretro HOST Implementation - In-Game Emulation

> **⚠️ ARCHIVED — HISTORICAL DOC.** This describes the original in-exe Libretro host (files `src/Platform/Common/libretro_host.c`, `libretro_integration.c`, etc.). That implementation was superseded and **moved into `aarcadecore.dll`**. The current live implementation lives in `src/aarcadecore/libretro_host.cpp`, `LibretroInstance.cpp`, `LibretroManager.cpp`, with host-side glue in `src/Platform/Common/AACoreManager.c`. See `LLM-AARCADECORE-README.md` for current architecture. The file references in this doc are no longer valid.

This document describes the implementation of a Libretro HOST within OpenJKDF2, allowing emulators to run inside the game with output displayed on dynamic textures like `compscreen.mat`.

## Overview

OpenJKDF2 now includes a **Libretro host** that can load Libretro cores (emulators), run games, and display the video output on in-game computer screens via the dynamic texture system.

### What This Does

- Loads a hard-coded Libretro core (`bsnes_libretro.dll`)
- Runs a hard-coded game ROM (`testgame.zip`)
- Captures emulator video output every frame
- Displays the output on `compscreen.mat` textures in the game world
- Runs synchronized with the game's frame rate

### What This Doesn't Do (Yet)

- Audio playback (audio callbacks are stubbed)
- Controller input (input callbacks return no input)
- Save states
- Dynamic core/game selection (hard-coded for now)

## Implementation Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────┐
│                     OpenJKDF2 Game                      │
│                                                         │
│  ┌──────────────┐         ┌────────────────────────┐  │
│  │ jkGame.c     │────────>│ libretro_integration.c │  │
│  │ (game loop)  │  update │ (high-level API)       │  │
│  └──────────────┘         └────────┬───────────────┘  │
│                                     │                   │
│  ┌──────────────┐         ┌────────▼───────────────┐  │
│  │ Main.c       │────────>│ libretro_host.c        │  │
│  │ (startup)    │  init   │ (core loading/running) │  │
│  └──────────────┘         └────────┬───────────────┘  │
│                                     │ SDL_LoadObject   │
│                           ┌─────────▼──────────────┐   │
│                           │  bsnes_libretro.dll    │   │
│                           │  (SNES emulator core)  │   │
│                           └─────────┬──────────────┘   │
│                                     │ video_refresh    │
│  ┌──────────────┐         ┌────────▼───────────────┐  │
│  │ compscreen   │<────────│ Frame Buffer           │  │
│  │ .mat         │  copy   │ (captured frames)      │  │
│  │ (texture)    │         └────────────────────────┘  │
│  └──────────────┘                                      │
└─────────────────────────────────────────────────────────┘
```

### File Structure

#### Core Files

1. **[src/Platform/Common/libretro_host.h](src/Platform/Common/libretro_host.h)**
   - Public API for Libretro host
   - Functions: create, load_game, run_frame, get_frame, destroy

2. **[src/Platform/Common/libretro_host.c](src/Platform/Common/libretro_host.c)**
   - Low-level Libretro host implementation
   - Loads DLL using SDL_LoadObject
   - Implements all required Libretro callbacks
   - Manages core lifecycle and frame capture

3. **[src/Platform/Common/libretro_integration.h](src/Platform/Common/libretro_integration.h)**
   - High-level integration API
   - Functions: init, update, shutdown, is_active

4. **[src/Platform/Common/libretro_integration.c](src/Platform/Common/libretro_integration.c)**
   - Integrates Libretro with OpenJKDF2 systems
   - Registers compscreen.mat dynamic texture callback
   - Hard-codes core and game paths
   - Converts pixel formats

#### Modified Files

5. **[src/Main/Main.c](src/Main/Main.c)**
   - Added `#include "Platform/Common/libretro_integration.h"`
   - Calls `libretro_integration_init()` during startup (after std3D_Startup)
   - Calls `libretro_integration_shutdown()` during shutdown

6. **[src/Main/jkGame.c](src/Main/jkGame.c)**
   - Added `#include "Platform/Common/libretro_integration.h"`
   - Calls `libretro_integration_update()` every frame in jkGame_Update()

## How It Works

### Initialization Sequence

1. **Game Startup** ([Main.c:441](src/Main/Main.c#L441))
   ```c
   libretro_integration_init();
   ```
   - Loads `bsnes_libretro.dll` using SDL_LoadObject
   - Gets function pointers for all Libretro API functions
   - Calls core's `retro_init()`
   - Sets up all callback functions
   - Loads `testgame.zip` ROM
   - Registers `compscreen.mat` dynamic texture callback

2. **Callback Registration**
   - Environment callback: Handles core queries (pixel format, directories, etc.)
   - Video callback: Captures rendered frames into buffer
   - Audio callbacks: Stubbed (ignored for now)
   - Input callbacks: Stubbed (return no input)

### Per-Frame Update

1. **Game Loop** ([jkGame.c:147](src/Main/jkGame.c#L147))
   ```c
   libretro_integration_update();
   ```
   - Calls core's `retro_run()`
   - Core renders one frame and calls our video_refresh callback
   - Frame data is copied into our buffer

2. **Texture Update** (automatic via dynamic texture system)
   - When `compscreen.mat` is rendered, callback is invoked
   - Captured frame is converted to texture format (RGB565)
   - Pixel data is copied into texture buffer
   - Texture is marked dirty and re-uploaded to GPU

### Pixel Format Conversion and Scaling

The emulator outputs in either RGB565 or XRGB8888 format, while `compscreen.mat` typically uses RGB565. The implementation includes **automatic scaling** to handle resolution mismatches (e.g., 256x224 SNES → 128x128 texture):

```c
// Calculate scaling factors (fixed point with 16-bit fraction)
int scale_x = ((int)frame_width << 16) / width;
int scale_y = ((int)frame_height << 16) / height;

for (y = 0; y < height; y++) {
    int src_y = (y * scale_y) >> 16;  // Scale texture Y to frame Y

    for (x = 0; x < width; x++) {
        int src_x = (x * scale_x) >> 16;  // Scale texture X to frame X

        if (is_xrgb8888) {
            // Convert XRGB8888 to RGB565
            uint32_t color = src[src_y * pitch + src_x];
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            dest[y * width + x] = rgb565;
        } else {
            // Direct RGB565 copy with scaling
            dest[y * width + x] = src[src_y * pitch + src_x];
        }
    }
}
```

This uses **nearest-neighbor interpolation** with fixed-point arithmetic for efficient scaling without floating-point operations.

## Configuration

### Hard-Coded Settings

Currently defined in [libretro_integration.c:17-18](src/Platform/Common/libretro_integration.c#L17-L18):

```c
#define LIBRETRO_CORE_PATH "bsnes_libretro.dll"
#define LIBRETRO_GAME_PATH "testgame.zip"
```

### File Locations

The implementation expects:
- **Core DLL**: `bsnes_libretro.dll` in the game directory (or system PATH)
- **ROM File**: `testgame.zip` in the game directory
- **System Files**: `./system/` directory for BIOS files (if needed)
- **Save Files**: `./saves/` directory for save data (if implemented)

## Building

The implementation is automatically included in regular builds. The source files are picked up by the existing CMake glob patterns:

```cmake
file(GLOB ENGINE_SOURCE_FILES
    ${PROJECT_SOURCE_DIR}/src/Platform/Common/*.c
)
```

No special build flags are required. The code uses SDL2's `SDL_LoadObject` which is already a dependency.

## Testing

### Minimal Test Setup

1. Place `bsnes_libretro.dll` in the game directory
2. Place a SNES ROM named `testgame.zip` in the game directory
3. Launch OpenJKDF2
4. Load any level with a computer screen (e.g., `01narshadda.jkl`)
5. Look at computer screens - they should show the running SNES game

### Expected Output

Console output during startup:
```
Libretro: Initializing integration...
Libretro: Loading core from bsnes_libretro.dll
Libretro: Core API version: 1
Libretro: Loaded core: bsnes v115
Libretro: Loading game from testgame.zip
Libretro: Game loaded - Resolution: 256x224, FPS: 60.00
Libretro: Integration initialized successfully
Libretro: Emulator output will appear on compscreen.mat
```

Periodic output during gameplay:
```
Libretro: Frame 600
Libretro: Frame 1200
...
```

## API Reference

### High-Level Integration API

```c
// Initialize - call during game startup
void libretro_integration_init(void);

// Update - call every frame from game loop
void libretro_integration_update(void);

// Shutdown - call during game shutdown
void libretro_integration_shutdown(void);

// Check status
bool libretro_integration_is_active(void);
```

### Low-Level Host API

```c
// Create host and load core
LibretroHost* libretro_host_create(const char* core_path);

// Load game ROM
bool libretro_host_load_game(LibretroHost* host, const char* game_path);

// Run one frame of emulation
void libretro_host_run_frame(LibretroHost* host);

// Get current frame buffer
const void* libretro_host_get_frame(LibretroHost* host,
                                     unsigned* out_width,
                                     unsigned* out_height,
                                     size_t* out_pitch,
                                     bool* out_is_xrgb8888);

// Reset emulation
void libretro_host_reset(LibretroHost* host);

// Destroy host
void libretro_host_destroy(LibretroHost* host);

// Get system info
void libretro_host_get_system_info(LibretroHost* host,
                                    const char** out_name,
                                    const char** out_version);
```

## Troubleshooting

### Core fails to load

**Symptom**: `Libretro: Failed to load core DLL: [error]`

**Solutions**:
- Ensure `bsnes_libretro.dll` is in the game directory or PATH
- Check that the DLL is the correct architecture (32-bit vs 64-bit)
- On Linux, rename to `bsnes_libretro.so` and update LIBRETRO_CORE_PATH
- Verify the DLL is a valid Libretro core (has required exports)

### Game fails to load

**Symptom**: `Libretro: Core failed to load game`

**Solutions**:
- Ensure ROM file exists and is named `testgame.zip`
- Verify the ROM format is compatible with the core (SNES ROM for bsnes)
- Check if core requires BIOS files in `./system/` directory

### No video output on compscreen

**Symptom**: Computer screens are black or show default content

**Solutions**:
- Verify initialization succeeded (check console for "Integration initialized successfully")
- Ensure update is being called (check for periodic "Frame XXX" messages)
- Try a different level with computer screens
- Check that the dynamic texture system is working (test with example callback)

### Performance issues

**Symptom**: Game runs slowly when emulator is active

**Solutions**:
- The emulator runs every frame - this may be too frequent
- Modify [jkGame.c:147](src/Main/jkGame.c#L147) to run less often:
  ```c
  static int frame_counter = 0;
  if (frame_counter++ % 2 == 0) {  // Run every other frame
      libretro_integration_update();
  }
  ```

## Future Enhancements

### Audio Implementation

Add audio mixing by:
1. Implementing `host_audio_sample_batch_callback` to queue samples
2. Mixing into OpenJKDF2's audio stream
3. Handling sample rate conversion if needed

### Input Implementation

Add controller support by:
1. Mapping OpenJKDF2 input to Libretro button IDs
2. Implementing `host_input_state_callback` to return button states
3. Allowing player to control emulated game

### Dynamic Core/Game Loading

Replace hard-coded paths with:
1. Configuration file or CVars
2. In-game menu to select core and ROM
3. Multiple emulators on different screens

### Save States

Implement state management by:
1. Implementing `retro_serialize` and `retro_unserialize`
2. Saving state to disk
3. Loading state on startup or user request

### Multiple Instances

Support multiple emulators by:
1. Creating multiple LibretroHost instances
2. Registering different texture callbacks
3. Managing multiple cores simultaneously

## Credits

- **Libretro API**: https://www.libretro.com/
- **bsnes Core**: byuu/Near
- **OpenJKDF2**: shinyquagsire23 and contributors
- **Dynamic Texture System**: See [DYNAMIC_TEXTURE_IMPLEMENTATION.md](DYNAMIC_TEXTURE_IMPLEMENTATION.md)
