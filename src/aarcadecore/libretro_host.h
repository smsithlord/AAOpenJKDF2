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
 * Hot-swap the loaded ROM on an existing host without tearing down the core
 * or HW GL context. Saves SRAM + state for the outgoing game, calls
 * retro_unload_game, then loads the new game. Equivalent to calling
 * libretro_host_load_game on an already-loaded host.
 *
 * Use case: same core, different ROM (e.g. switching N64 games on the same
 * loaded mupen64plus_next instance) — avoids the multi-second DLL reload
 * pause that destroying + recreating would cost.
 *
 * @return true on success, false on failure (in which case the host is left
 *         with no game loaded — caller should treat as a fresh-load failure).
 */
bool libretro_host_swap_game(LibretroHost* host, const char* game_path);

/**
 * Returns the core path the host was created with (e.g.
 * "aarcadecore/libretro/cores/mupen64plus_next_libretro.dll"). Used by the
 * manager to decide whether a new ROM request can hot-swap on the existing
 * host or needs a full destroy+create.
 *
 * The returned pointer is owned by the host; do not free.
 */
const char* libretro_host_get_core_path(LibretroHost* host);

/**
 * Set the joypad input state for a given port
 *
 * @param host The libretro host instance
 * @param port Controller port (0 or 1)
 * @param buttons 16-bit RETRO_DEVICE_JOYPAD button mask
 */
void libretro_host_set_input(LibretroHost* host, unsigned port, int16_t buttons);

/**
 * Set the analog stick state for a given port + stick.
 *
 * @param host  The libretro host instance
 * @param port  Controller port (0 or 1)
 * @param stick RETRO_DEVICE_INDEX_ANALOG_LEFT (0) or RETRO_DEVICE_INDEX_ANALOG_RIGHT (1)
 * @param x     Axis X, -32768..32767
 * @param y     Axis Y, -32768..32767 (Y+ = down, per SDL/libretro convention)
 */
void libretro_host_set_analog(LibretroHost* host, unsigned port, unsigned stick, int16_t x, int16_t y);

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

/**
 * Set keyboard state for raw mode (RETRO_DEVICE_KEYBOARD)
 * @param retrok_id RETROK_* key code (0-511)
 * @param pressed 1 for pressed, 0 for released
 */
void libretro_host_set_key_state(LibretroHost* host, unsigned retrok_id, int pressed);

/* ------------------------------------------------------------------------
 * Core options bridge — used by the JS/UI layer to inspect and edit the
 * options the loaded core declared via SET_VARIABLES / SET_CORE_OPTIONS*.
 *
 * Values are validated against the core's declared value set before being
 * accepted, then persisted to aarcadecore/libretro/config/<core>.opt.
 * ------------------------------------------------------------------------ */

/**
 * Serialize the loaded core's options as JSON into a caller-provided buffer.
 * Each entry: {"key","display","default","current","values":[...]}
 *
 * @return Number of bytes written (excluding the NUL terminator).
 */
int  libretro_host_get_options_json(LibretroHost* host, char* out, int max_len);

/**
 * Set the runtime value of a core option, at either the core tier or the
 * per-game override tier. Value is validated against the core's declared set
 * (except when clearing a game override with an empty value), marked dirty so
 * the core re-reads on its next GET_VARIABLE_UPDATE, and persisted to disk.
 *
 * @param tier "core" (default if null) writes core-tier; "game" writes per-game
 *             override. Pass tier="game" with value="" to clear the game
 *             override (= inherit core value).
 * @return true if accepted, false if the value isn't in the declared set.
 */
bool libretro_host_set_option(LibretroHost* host, const char* key, const char* value, const char* tier);

/* Reach a Libretro host through its EmbeddedInstance wrapper (implemented in
 * LibretroInstance.cpp). The active wrapper is returned by
 * LibretroManager_GetActive(); pass it here to obtain the LibretroHost so the
 * options-bridge functions above can be called from the JS layer. */
struct EmbeddedInstance;
LibretroHost* LibretroInstance_GetHost(struct EmbeddedInstance* inst);

/* ------------------------------------------------------------------------
 * Per-thread host registry (implemented in LibretroManager.cpp).
 *
 * Libretro callbacks have no user_data parameter; the host that owns the
 * calling thread is found via SDL_ThreadID(). Internal API used by
 * libretro_host.cpp around every retro_*() call.
 * ------------------------------------------------------------------------ */
void          LibretroManager_RegisterThreadOwner(LibretroHost* host);
void          LibretroManager_UnregisterThreadOwner(LibretroHost* host);
LibretroHost* LibretroManager_FindByThread(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_HOST_H */
