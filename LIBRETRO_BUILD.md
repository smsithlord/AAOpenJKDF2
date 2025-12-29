# OpenJKDF2 Libretro Core - Build Instructions

This document describes how to build OpenJKDF2 as a Libretro core for use with RetroArch and other Libretro frontends.

## Overview

The Libretro implementation is a **minimalistic proof-of-concept** that demonstrates the integration pattern. Currently, it:

- Loads with hard-coded game paths (placeholder)
- Outputs a test pattern video (animated checkerboard)
- Outputs silent audio
- Accepts input but doesn't process it yet
- Uses do-nothing placeholder callbacks

## Building

### Windows (MSVC)

```bash
mkdir build_libretro
cd build_libretro
cmake .. -DPLAT_LIBRETRO=ON -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Output: `openjkdf2_libretro.dll`

### Windows (MinGW)

```bash
mkdir build_libretro
cd build_libretro
cmake .. -DPLAT_LIBRETRO=ON -G "MinGW Makefiles"
cmake --build . --config Release
```

Output: `openjkdf2_libretro.dll`

### Linux

```bash
mkdir build_libretro
cd build_libretro
cmake .. -DPLAT_LIBRETRO=ON
make -j$(nproc)
```

Output: `openjkdf2_libretro.so`

### macOS

```bash
mkdir build_libretro
cd build_libretro
cmake .. -DPLAT_LIBRETRO=ON
make -j$(sysctl -n hw.ncpu)
```

Output: `openjkdf2_libretro.dylib`

## Installation

### RetroArch

1. Copy the built core to your RetroArch cores directory:
   - **Windows**: `RetroArch\cores\`
   - **Linux**: `~/.config/retroarch/cores/` or `/usr/lib/libretro/`
   - **macOS**: `~/Library/Application Support/RetroArch/cores/`

2. Launch RetroArch

3. Go to `Load Core` → Select the OpenJKDF2 core

4. Go to `Load Content` → Select any `.gob` file (placeholder - not used yet)

5. The core should launch and display an animated test pattern

## Current Implementation Status

### ✅ Implemented

- All required Libretro API exports (~25 functions)
- Callback registration (video, audio, input, environment)
- Basic game loading flow
- Frame generation loop (60 FPS)
- Audio generation loop (44100 Hz, stereo)
- System info reporting (name, version, extensions)

### ❌ Not Yet Implemented

- OpenJKDF2 engine integration
- Actual game loading from GOB files
- Real video rendering (currently shows test pattern)
- Real audio mixing (currently outputs silence)
- Input processing
- Save state support
- Memory map exposure
- Core options/settings

## Implementation Files

- **Core Entry Point**: [`src/Platform/Libretro/libretro_core.c`](src/Platform/Libretro/libretro_core.c)
- **CMake Platform Module**: [`cmake_modules/plat_libretro.cmake`](cmake_modules/plat_libretro.cmake)
- **API Header**: [`libretro_examples/libretro.h`](libretro_examples/libretro.h)

## Next Steps for Full Integration

To integrate the actual OpenJKDF2 engine, the following work is needed:

1. **Replace SDL2/OpenAL calls** with Libretro callbacks
   - Video: Redirect rendering to `retro_video_refresh_t` buffer
   - Audio: Redirect `Sound_Mix()` to `retro_audio_sample_batch_t`
   - Input: Map `retro_input_state_t` to OpenJKDF2 input system

2. **Initialize OpenJKDF2 engine** in `retro_load_game()`
   - Load GOB files from content path
   - Initialize rendering subsystem (without OpenGL context)
   - Load hard-coded level (e.g., "01narshadda.jkl")

3. **Integrate game loop** in `retro_run()`
   - Call `sithMain_Tick()` or equivalent for game logic
   - Render frame to framebuffer
   - Mix audio to buffer

4. **Handle file paths** using environment callbacks
   - `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY` for game data
   - `RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY` for save files

5. **Add save state support**
   - Serialize game state in `retro_serialize()`
   - Deserialize in `retro_unserialize()`

## Testing the Placeholder

The current implementation generates a test pattern that changes over time:

- **Video**: 640x480 XRGB8888 checkerboard with animated colors
- **Audio**: Silence (735 frames per update at 44100 Hz)
- **Frame Rate**: 60 FPS
- **Input**: Polled but not processed

This serves as a proof-of-concept showing that the Libretro integration works correctly.

## Troubleshooting

### Core fails to load

- Check that the core is in the correct directory
- Verify the core file has the correct extension (`.dll`/`.so`/`.dylib`)
- Check RetroArch logs for error messages

### Black screen

- This is expected in the current placeholder implementation
- The test pattern should be visible if video callbacks are working

### No audio

- This is expected - the current implementation outputs silence

## References

- [Libretro API Documentation](https://docs.libretro.com/)
- [OpenJKDF2 Project](https://github.com/shinyquagsire23/OpenJKDF2)
- [Dynamic Texture Implementation](DYNAMIC_TEXTURE_IMPLEMENTATION.md)
