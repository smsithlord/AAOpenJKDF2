/*
 * OpenJKDF2 Libretro Core
 *
 * Minimalistic Libretro implementation that loads a hard-coded game
 * with placeholder callbacks for demonstration purposes.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "libretro.h"

/* Configuration */
#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480
#define AUDIO_SAMPLE_RATE 44100
#define FRAME_RATE 60.0
#define AUDIO_FRAMES_PER_UPDATE ((int)(AUDIO_SAMPLE_RATE / FRAME_RATE))  /* ~735 */

/* Global State */
static struct {
    /* Frontend callbacks */
    retro_video_refresh_t video_cb;
    retro_audio_sample_t audio_cb;
    retro_audio_sample_batch_t audio_batch_cb;
    retro_input_poll_t input_poll_cb;
    retro_input_state_t input_state_cb;
    retro_environment_t environ_cb;

    /* Game state */
    bool game_loaded;
    uint16_t input_state[8];  /* Controller state for up to 8 ports */

    /* Frame counter for demo purposes */
    uint32_t frame_count;

    /* Framebuffer (XRGB8888 format) */
    uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

    /* Audio buffer */
    int16_t audio_buffer[AUDIO_FRAMES_PER_UPDATE * 2];  /* Stereo */
} g_state;

/* ========================================================================
 * Core API Implementation
 * ======================================================================== */

RETRO_API unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void retro_init(void)
{
    /* Initialize global state */
    memset(&g_state, 0, sizeof(g_state));

    /* TODO: Initialize OpenJKDF2 subsystems */
}

RETRO_API void retro_deinit(void)
{
    /* TODO: Cleanup OpenJKDF2 subsystems */
    memset(&g_state, 0, sizeof(g_state));
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));

    info->library_name = "OpenJKDF2";
    info->library_version = "0.9.8";
    info->valid_extensions = "gob";
    info->need_fullpath = true;   /* We need the file path, not memory buffer */
    info->block_extract = false;  /* Frontend can extract archives */
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));

    /* Video configuration */
    info->geometry.base_width = SCREEN_WIDTH;
    info->geometry.base_height = SCREEN_HEIGHT;
    info->geometry.max_width = SCREEN_WIDTH;
    info->geometry.max_height = SCREEN_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;

    /* Timing configuration */
    info->timing.fps = FRAME_RATE;
    info->timing.sample_rate = AUDIO_SAMPLE_RATE;
}

/* ========================================================================
 * Callback Setters
 * ======================================================================== */

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    g_state.environ_cb = cb;

    /* Set pixel format to XRGB8888 (32-bit) */
    if (cb) {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
        cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

        /* Tell frontend we require a game to be loaded */
        bool no_game = false;
        cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    }
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    g_state.video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
    g_state.audio_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    g_state.audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    g_state.input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    g_state.input_state_cb = cb;
}

/* ========================================================================
 * Game Loading
 * ======================================================================== */

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    if (!game)
        return false;

    /* For now, we'll hard-code the game loading */
    /* TODO: Initialize OpenJKDF2 engine with hard-coded paths */
    /* Expected path structure:
     *   - System directory: Contains JK game data (GOB files)
     *   - Hard-coded level: "01narshadda.jkl" or similar
     */

    /* Get system directory from frontend */
    const char *system_dir = NULL;
    if (g_state.environ_cb) {
        g_state.environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
    }

    /* TODO: Use system_dir to locate JK assets */
    /* Example: sprintf(gob_path, "%s/jk/Res1hi.gob", system_dir); */

    g_state.game_loaded = true;
    g_state.frame_count = 0;

    return true;
}

RETRO_API void retro_unload_game(void)
{
    /* TODO: Cleanup game-specific resources */
    g_state.game_loaded = false;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
                                       const struct retro_game_info *info,
                                       size_t num_info)
{
    /* We don't support special game types */
    return false;
}

/* ========================================================================
 * Core Loop
 * ======================================================================== */

RETRO_API void retro_run(void)
{
    int i;

    /* Poll input from frontend */
    if (g_state.input_poll_cb)
        g_state.input_poll_cb();

    /* Read input state (placeholder - not used yet) */
    if (g_state.input_state_cb) {
        for (i = 0; i < 16; i++) {
            if (g_state.input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i))
                g_state.input_state[0] |= (1 << i);
            else
                g_state.input_state[0] &= ~(1 << i);
        }
    }

    /* TODO: Run one frame of OpenJKDF2 game logic */
    /* Example: sithMain_Tick() or equivalent */

    /* Generate placeholder video frame */
    /* Simple pattern: alternating colors based on frame count */
    {
        uint32_t color1 = 0xFF000000 | ((g_state.frame_count & 0xFF) << 16);  /* Red channel changes */
        uint32_t color2 = 0xFF000000 | ((g_state.frame_count & 0xFF) << 8);   /* Green channel changes */

        for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            /* Create a checkerboard pattern */
            int x = i % SCREEN_WIDTH;
            int y = i / SCREEN_WIDTH;
            g_state.framebuffer[i] = ((x / 32) ^ (y / 32)) & 1 ? color1 : color2;
        }
    }

    /* Submit video frame to frontend */
    if (g_state.video_cb) {
        g_state.video_cb(g_state.framebuffer,
                        SCREEN_WIDTH,
                        SCREEN_HEIGHT,
                        SCREEN_WIDTH * sizeof(uint32_t));
    }

    /* Generate placeholder audio (silence) */
    memset(g_state.audio_buffer, 0, sizeof(g_state.audio_buffer));

    /* TODO: Fill audio buffer with actual game audio */
    /* Example: Sound_Mix(g_state.audio_buffer, AUDIO_FRAMES_PER_UPDATE); */

    /* Submit audio to frontend */
    if (g_state.audio_batch_cb) {
        g_state.audio_batch_cb(g_state.audio_buffer, AUDIO_FRAMES_PER_UPDATE);
    }

    g_state.frame_count++;
}

RETRO_API void retro_reset(void)
{
    /* Reset game state to beginning */
    g_state.frame_count = 0;

    /* TODO: Reset OpenJKDF2 game state */
}

/* ========================================================================
 * Serialization (Save States)
 * ======================================================================== */

RETRO_API size_t retro_serialize_size(void)
{
    /* TODO: Return actual save state size */
    return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    /* TODO: Save state to data buffer */
    return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    /* TODO: Load state from data buffer */
    return false;
}

/* ========================================================================
 * Memory Access (for cheats/debugging)
 * ======================================================================== */

RETRO_API void *retro_get_memory_data(unsigned id)
{
    /* TODO: Expose memory regions */
    return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
    /* TODO: Return memory region sizes */
    return 0;
}

/* ========================================================================
 * Miscellaneous
 * ======================================================================== */

RETRO_API unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    /* Accept any controller type for now */
}

RETRO_API void retro_cheat_reset(void)
{
    /* TODO: Clear all active cheats */
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    /* TODO: Set cheat state */
}
