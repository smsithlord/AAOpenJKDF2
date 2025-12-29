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

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_HOST_H */
