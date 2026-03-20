/*
 * Libretro Host Implementation for OpenJKDF2
 *
 * Loads and runs Libretro cores (emulators) within the game,
 * displaying output on dynamic textures like compscreen.mat
 */

#include "libretro_host.h"
#include "../../libretro_examples/libretro.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Function pointer typedefs for loaded core */
typedef void (*retro_init_t)(void);
typedef void (*retro_deinit_t)(void);
typedef unsigned (*retro_api_version_t)(void);
typedef void (*retro_get_system_info_t)(struct retro_system_info *info);
typedef void (*retro_get_system_av_info_t)(struct retro_system_av_info *info);
typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_set_controller_port_device_t)(unsigned port, unsigned device);
typedef void (*retro_reset_t)(void);
typedef void (*retro_run_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *game);
typedef void (*retro_unload_game_t)(void);
typedef unsigned (*retro_get_region_t)(void);
typedef void* (*retro_get_memory_data_t)(unsigned id);
typedef size_t (*retro_get_memory_size_t)(unsigned id);

/* Host state structure */
struct LibretroHost {
    /* DLL handle */
    void* core_dll;

    /* Core function pointers */
    retro_init_t retro_init;
    retro_deinit_t retro_deinit;
    retro_api_version_t retro_api_version;
    retro_get_system_info_t retro_get_system_info;
    retro_get_system_av_info_t retro_get_system_av_info;
    retro_set_environment_t retro_set_environment;
    retro_set_video_refresh_t retro_set_video_refresh;
    retro_set_audio_sample_t retro_set_audio_sample;
    retro_set_audio_sample_batch_t retro_set_audio_sample_batch;
    retro_set_input_poll_t retro_set_input_poll;
    retro_set_input_state_t retro_set_input_state;
    retro_set_controller_port_device_t retro_set_controller_port_device;
    retro_reset_t retro_reset;
    retro_run_t retro_run;
    retro_load_game_t retro_load_game;
    retro_unload_game_t retro_unload_game;
    retro_get_region_t retro_get_region;
    retro_get_memory_data_t retro_get_memory_data;
    retro_get_memory_size_t retro_get_memory_size;

    /* Video state */
    void* video_frame;
    unsigned frame_width;
    unsigned frame_height;
    size_t frame_pitch;
    enum retro_pixel_format pixel_format;
    bool frame_updated;

    /* System info */
    struct retro_system_info system_info;
    struct retro_system_av_info av_info;

    /* Game loaded flag */
    bool game_loaded;

    /* Input state - 16-bit RETRO_DEVICE_JOYPAD button mask per port */
    int16_t input_state[2];

    /* Keyboard state - indexed by RETROK_* id, 1=pressed 0=released */
    uint8_t keyboard_state[512];

    /* Audio state — ring buffer collects samples from core, host pulls them */
    int16_t* audio_ring_buf;
    size_t audio_ring_size;   /* capacity in int16_t samples (L+R interleaved) */
    size_t audio_ring_write;  /* write cursor */
    size_t audio_ring_read;   /* read cursor */
};

/* Global pointer to current host for callback access */
static LibretroHost* g_current_host = NULL;

/* ========================================================================
 * Host Callback Implementations
 * ======================================================================== */

static bool host_environment_callback(unsigned cmd, void* data)
{
    if (!g_current_host) return false;

    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            /* We support frame duplication */
            *(bool*)data = true;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format fmt = *(enum retro_pixel_format*)data;

            /* Accept RGB565 or XRGB8888 */
            if (fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                g_current_host->pixel_format = fmt;
                printf("Libretro: Pixel format set to %s\n",
                       fmt == RETRO_PIXEL_FORMAT_RGB565 ? "RGB565" : "XRGB8888");
                return true;
            }

            printf("Libretro: Unsupported pixel format %d\n", fmt);
            return false;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            /* Return system directory for BIOS files */
            *(const char**)data = "./system";
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            /* Return save directory */
            *(const char**)data = "./saves";
            return true;

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            /* Core is telling us whether it supports running without a game */
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            /* Provide logging callback if needed */
            return false;  /* Not implemented yet */
        }

        default:
            /* Unhandled environment command */
            return false;
    }
}

static void host_video_refresh_callback(const void* data, unsigned width,
                                        unsigned height, size_t pitch)
{
    if (!g_current_host) return;

    /* NULL means duplicate previous frame */
    if (!data) {
        return;
    }

    /* Update frame dimensions */
    g_current_host->frame_width = width;
    g_current_host->frame_height = height;
    g_current_host->frame_pitch = pitch;

    /* Allocate/reallocate buffer if needed */
    size_t needed_size = height * pitch;
    g_current_host->video_frame = realloc(g_current_host->video_frame, needed_size);

    if (g_current_host->video_frame) {
        /* Copy frame data */
        memcpy(g_current_host->video_frame, data, needed_size);
        g_current_host->frame_updated = true;
    }
}

/* Ring buffer helper: available samples to read */
static size_t audio_ring_available(void)
{
    if (!g_current_host || !g_current_host->audio_ring_buf) return 0;
    size_t w = g_current_host->audio_ring_write;
    size_t r = g_current_host->audio_ring_read;
    if (w >= r) return w - r;
    return g_current_host->audio_ring_size - r + w;
}

/* Ring buffer helper: free space to write */
static size_t audio_ring_free(void)
{
    if (!g_current_host || !g_current_host->audio_ring_buf) return 0;
    return g_current_host->audio_ring_size - 1 - audio_ring_available();
}

static void host_audio_sample_callback(int16_t left, int16_t right)
{
    if (!g_current_host || !g_current_host->audio_ring_buf) return;
    if (audio_ring_free() < 2) return;
    g_current_host->audio_ring_buf[g_current_host->audio_ring_write] = left;
    g_current_host->audio_ring_write = (g_current_host->audio_ring_write + 1) % g_current_host->audio_ring_size;
    g_current_host->audio_ring_buf[g_current_host->audio_ring_write] = right;
    g_current_host->audio_ring_write = (g_current_host->audio_ring_write + 1) % g_current_host->audio_ring_size;
}

static size_t host_audio_sample_batch_callback(const int16_t* data, size_t frames)
{
    size_t samples, free_space, to_write, i;
    if (!g_current_host || !g_current_host->audio_ring_buf || !data || frames == 0)
        return frames;

    samples = frames * 2; /* stereo interleaved */
    free_space = audio_ring_free();
    to_write = samples < free_space ? samples : free_space;
    for (i = 0; i < to_write; i++) {
        g_current_host->audio_ring_buf[g_current_host->audio_ring_write] = data[i];
        g_current_host->audio_ring_write = (g_current_host->audio_ring_write + 1) % g_current_host->audio_ring_size;
    }
    return to_write / 2; /* return frames written */
}

static void host_input_poll_callback(void)
{
    /* Not implemented - no input for now */
}

static int16_t host_input_state_callback(unsigned port, unsigned device,
                                         unsigned index, unsigned id)
{
    if (!g_current_host || port > 1) return 0;
    if (device == RETRO_DEVICE_JOYPAD && index == 0)
        return (g_current_host->input_state[port] >> id) & 1;
    if (device == RETRO_DEVICE_KEYBOARD && id < 512)
        return g_current_host->keyboard_state[id] ? 1 : 0;
    return 0;
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static bool load_core_symbols(LibretroHost* host)
{
#define LOAD_SYM(name) \
    host->name = (name##_t)SDL_LoadFunction(host->core_dll, #name); \
    if (!host->name) { \
        printf("Libretro: Failed to load symbol %s: %s\n", #name, SDL_GetError()); \
        return false; \
    }

    LOAD_SYM(retro_init);
    LOAD_SYM(retro_deinit);
    LOAD_SYM(retro_api_version);
    LOAD_SYM(retro_get_system_info);
    LOAD_SYM(retro_get_system_av_info);
    LOAD_SYM(retro_set_environment);
    LOAD_SYM(retro_set_video_refresh);
    LOAD_SYM(retro_set_audio_sample);
    LOAD_SYM(retro_set_audio_sample_batch);
    LOAD_SYM(retro_set_input_poll);
    LOAD_SYM(retro_set_input_state);
    LOAD_SYM(retro_set_controller_port_device);
    LOAD_SYM(retro_reset);
    LOAD_SYM(retro_run);
    LOAD_SYM(retro_load_game);
    LOAD_SYM(retro_unload_game);
    LOAD_SYM(retro_get_region);
    LOAD_SYM(retro_get_memory_data);
    LOAD_SYM(retro_get_memory_size);

#undef LOAD_SYM

    return true;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

LibretroHost* libretro_host_create(const char* core_path)
{
    LibretroHost* host;

    if (!core_path) {
        printf("Libretro: NULL core path provided\n");
        return NULL;
    }

    /* Allocate host structure */
    host = (LibretroHost*)calloc(1, sizeof(LibretroHost));
    if (!host) {
        printf("Libretro: Failed to allocate host structure\n");
        return NULL;
    }

    /* Load the core DLL */
    printf("Libretro: Loading core from %s\n", core_path);
    host->core_dll = SDL_LoadObject(core_path);
    if (!host->core_dll) {
        printf("Libretro: Failed to load core DLL: %s\n", SDL_GetError());
        free(host);
        return NULL;
    }

    /* Load all function symbols */
    if (!load_core_symbols(host)) {
        SDL_UnloadObject(host->core_dll);
        free(host);
        return NULL;
    }

    /* Verify API version */
    unsigned version = host->retro_api_version();
    if (version != RETRO_API_VERSION) {
        printf("Libretro: API version mismatch (core=%u, expected=%u)\n",
               version, RETRO_API_VERSION);
        SDL_UnloadObject(host->core_dll);
        free(host);
        return NULL;
    }

    printf("Libretro: Core API version: %u\n", version);

    /* Set default pixel format */
    host->pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;

    /* Set global pointer for callbacks */
    g_current_host = host;

    /* CRITICAL: Set environment callback FIRST, before retro_init! */
    host->retro_set_environment(host_environment_callback);

    /* Initialize the core */
    host->retro_init();

    /* Set all other callbacks */
    host->retro_set_video_refresh(host_video_refresh_callback);
    host->retro_set_audio_sample(host_audio_sample_callback);
    host->retro_set_audio_sample_batch(host_audio_sample_batch_callback);
    host->retro_set_input_poll(host_input_poll_callback);
    host->retro_set_input_state(host_input_state_callback);

    /* Get system info */
    host->retro_get_system_info(&host->system_info);
    printf("Libretro: Loaded core: %s v%s\n",
           host->system_info.library_name,
           host->system_info.library_version);

    printf("Libretro: Host initialized successfully\n");
    return host;
}

bool libretro_host_load_game(LibretroHost* host, const char* game_path)
{
    struct retro_game_info game;
    FILE* f;
    void* rom_data = NULL;
    size_t rom_size = 0;

    if (!host || !game_path) {
        printf("Libretro: Invalid parameters to load_game\n");
        return false;
    }

    printf("Libretro: Loading game from %s\n", game_path);

    /* Check if core needs fullpath or data buffer */
    if (host->system_info.need_fullpath) {
        /* Core wants file path, not data */
        game.path = game_path;
        game.data = NULL;
        game.size = 0;
        game.meta = NULL;
    } else {
        /* Core wants data in memory - load the file */
        f = fopen(game_path, "rb");
        if (!f) {
            printf("Libretro: Failed to open game file: %s\n", game_path);
            return false;
        }

        fseek(f, 0, SEEK_END);
        rom_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        rom_data = malloc(rom_size);
        if (!rom_data) {
            printf("Libretro: Failed to allocate ROM buffer\n");
            fclose(f);
            return false;
        }

        fread(rom_data, 1, rom_size, f);
        fclose(f);

        game.path = game_path;
        game.data = rom_data;
        game.size = rom_size;
        game.meta = NULL;
    }

    /* Load the game */
    if (!host->retro_load_game(&game)) {
        printf("Libretro: Core failed to load game\n");
        if (rom_data) free(rom_data);
        return false;
    }

    if (rom_data) free(rom_data);

    /* Get AV info */
    host->retro_get_system_av_info(&host->av_info);
    printf("Libretro: Game loaded - Resolution: %ux%u, FPS: %.2f\n",
           host->av_info.geometry.base_width,
           host->av_info.geometry.base_height,
           host->av_info.timing.fps);

    host->game_loaded = true;

    /* Allocate audio ring buffer — host will pull samples via libretro_host_read_audio */
    {
        int sample_rate = (int)host->av_info.timing.sample_rate;
        if (sample_rate <= 0) sample_rate = 48000;
        host->audio_ring_size = sample_rate / 4;
        if (host->audio_ring_size < 16384) host->audio_ring_size = 16384;
        host->audio_ring_buf = (int16_t*)calloc(host->audio_ring_size, sizeof(int16_t));
        host->audio_ring_write = 0;
        host->audio_ring_read = 0;
    }

    return true;
}

void libretro_host_set_input(LibretroHost* host, unsigned port, int16_t buttons)
{
    if (host && port <= 1) {
        host->input_state[port] = buttons;
    }
}

void libretro_host_run_frame(LibretroHost* host)
{
    if (!host || !host->game_loaded) {
        return;
    }

    host->frame_updated = false;
    host->retro_run();
}

const void* libretro_host_get_frame(LibretroHost* host,
                                     unsigned* out_width,
                                     unsigned* out_height,
                                     size_t* out_pitch,
                                     bool* out_is_xrgb8888)
{
    if (!host || !host->video_frame) {
        return NULL;
    }

    if (out_width) *out_width = host->frame_width;
    if (out_height) *out_height = host->frame_height;
    if (out_pitch) *out_pitch = host->frame_pitch;
    if (out_is_xrgb8888) *out_is_xrgb8888 = (host->pixel_format == RETRO_PIXEL_FORMAT_XRGB8888);

    return host->video_frame;
}

void libretro_host_reset(LibretroHost* host)
{
    if (host && host->game_loaded) {
        host->retro_reset();
    }
}

void libretro_host_destroy(LibretroHost* host)
{
    if (!host) {
        return;
    }

    /* Unload game if loaded */
    if (host->game_loaded) {
        host->retro_unload_game();
    }

    /* Deinitialize core */
    if (host->retro_deinit) {
        host->retro_deinit();
    }

    /* Unload DLL */
    if (host->core_dll) {
        SDL_UnloadObject(host->core_dll);
    }

    /* Free audio ring buffer */
    if (host->audio_ring_buf) {
        free(host->audio_ring_buf);
    }

    /* Free video frame buffer */
    if (host->video_frame) {
        free(host->video_frame);
    }

    /* Clear global pointer if this was the current host */
    if (g_current_host == host) {
        g_current_host = NULL;
    }

    free(host);
    printf("Libretro: Host destroyed\n");
}

void libretro_host_get_system_info(LibretroHost* host,
                                    const char** out_name,
                                    const char** out_version)
{
    if (!host) {
        return;
    }

    if (out_name) *out_name = host->system_info.library_name;
    if (out_version) *out_version = host->system_info.library_version;
}

int libretro_host_get_sample_rate(LibretroHost* host)
{
    if (!host || !host->game_loaded)
        return 0;
    int rate = (int)host->av_info.timing.sample_rate;
    return rate > 0 ? rate : 0;
}

int libretro_host_read_audio(LibretroHost* host, int16_t* buffer, int max_frames)
{
    size_t avail, to_copy, i;
    if (!host || !host->audio_ring_buf || !buffer || max_frames <= 0)
        return 0;

    avail = audio_ring_available();
    to_copy = (size_t)(max_frames * 2); /* stereo samples */
    if (to_copy > avail) to_copy = avail;

    for (i = 0; i < to_copy; i++) {
        buffer[i] = host->audio_ring_buf[host->audio_ring_read];
        host->audio_ring_read = (host->audio_ring_read + 1) % host->audio_ring_size;
    }

    return (int)(to_copy / 2); /* return frames */
}

void libretro_host_set_key_state(LibretroHost* host, unsigned retrok_id, int pressed)
{
    if (host && retrok_id < 512)
        host->keyboard_state[retrok_id] = pressed ? 1 : 0;
}
