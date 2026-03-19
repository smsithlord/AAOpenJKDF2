#ifndef LIBRETRO_HOST_H
#define LIBRETRO_HOST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - actual libretro.h types will be in implementation */
struct retro_game_info;
struct retro_system_info;
struct retro_system_av_info;

/* Opaque handle to libretro host state */
typedef struct LibretroHost LibretroHost;

/**
 * Initialize the Libretro host and load a core
 *
 * @param core_path Path to the Libretro core DLL (e.g., "bsnes_libretro.dll")
 * @return Pointer to initialized host, or NULL on failure
 */
LibretroHost* libretro_host_create(const char* core_path);

/**
 * Load a game ROM into the core
 *
 * @param host The libretro host instance
 * @param game_path Path to the game file (e.g., "testgame.zip")
 * @return true on success, false on failure
 */
bool libretro_host_load_game(LibretroHost* host, const char* game_path);

/**
 * Set the joypad input state for a given port
 *
 * @param host The libretro host instance
 * @param port Controller port (0 or 1)
 * @param buttons 16-bit RETRO_DEVICE_JOYPAD button mask
 */
void libretro_host_set_input(LibretroHost* host, unsigned port, int16_t buttons);

/**
 * Run one frame of emulation
 * This will invoke callbacks which update internal buffers
 *
 * @param host The libretro host instance
 */
void libretro_host_run_frame(LibretroHost* host);

/**
 * Get the current video frame buffer
 *
 * @param host The libretro host instance
 * @param out_width Pointer to store frame width
 * @param out_height Pointer to store frame height
 * @param out_pitch Pointer to store frame pitch (bytes per line)
 * @param out_is_xrgb8888 Pointer to store pixel format flag (true=XRGB8888, false=RGB565)
 * @return Pointer to frame data, or NULL if no frame available
 */
const void* libretro_host_get_frame(LibretroHost* host,
                                     unsigned* out_width,
                                     unsigned* out_height,
                                     size_t* out_pitch,
                                     bool* out_is_xrgb8888);

/**
 * Reset the game
 *
 * @param host The libretro host instance
 */
void libretro_host_reset(LibretroHost* host);

/**
 * Shutdown and free the libretro host
 *
 * @param host The libretro host instance
 */
void libretro_host_destroy(LibretroHost* host);

/**
 * Get system information from the core
 *
 * @param host The libretro host instance
 * @param out_name Pointer to store library name (do not free)
 * @param out_version Pointer to store library version (do not free)
 */
void libretro_host_get_system_info(LibretroHost* host,
                                    const char** out_name,
                                    const char** out_version);

/**
 * Get the audio sample rate requested by the loaded core
 * @return sample rate in Hz, or 0 if no game loaded
 */
int libretro_host_get_sample_rate(LibretroHost* host);

/**
 * Read audio samples from the ring buffer
 * @param host The libretro host instance
 * @param buffer Output buffer for interleaved stereo int16_t samples
 * @param max_frames Maximum frames to read (1 frame = L+R = 2 samples)
 * @return Number of frames actually written
 */
int libretro_host_read_audio(LibretroHost* host, int16_t* buffer, int max_frames);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_HOST_H */
