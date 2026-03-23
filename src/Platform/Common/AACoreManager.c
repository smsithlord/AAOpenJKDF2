/*
 * AACoreManager — Host-side loader for aarcadecore.dll
 *
 * Loads the DLL dynamically, provides engine callbacks, and wraps the API.
 * The HOST owns the engine texture callback and calls the DLL to fill pixels.
 */

#include "AACoreManager.h"
#include "../../aarcadecore/aarcadecore_api.h"
#include "../../Engine/rdDynamicTexture.h"
#include "../../Platform/std3D.h"
#include "../../Win95/Window.h"
#include "../../stdPlatform.h"
#include "../../World/sithThing.h"
#include "../../World/sithTemplate.h"
#include "../../Engine/sithCollision.h"
#include "../../Gameplay/sithPlayer.h"
#include "../../Primitives/rdMatrix.h"
#include "../../Primitives/rdVector.h"
#include "../../World/sithSector.h"
#include "../../Raster/rdCache.h"
#include "globals.h"

#include "SDL2_helper.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* DLL handle and function pointers */
static void* g_dll = NULL;
static aarcadecore_init_t              g_fn_init = NULL;
static aarcadecore_shutdown_t          g_fn_shutdown = NULL;
static aarcadecore_update_t            g_fn_update = NULL;
static aarcadecore_is_active_t         g_fn_is_active = NULL;
static aarcadecore_get_api_version_t   g_fn_get_api_version = NULL;
static aarcadecore_get_material_name_t g_fn_get_material_name = NULL;
static aarcadecore_render_texture_t    g_fn_render_texture = NULL;
static aarcadecore_get_audio_sample_rate_t g_fn_get_audio_sample_rate = NULL;
static aarcadecore_get_audio_samples_t     g_fn_get_audio_samples = NULL;
static aarcadecore_key_down_t              g_fn_key_down = NULL;
static aarcadecore_key_up_t                g_fn_key_up = NULL;
static aarcadecore_key_char_t              g_fn_key_char = NULL;
static aarcadecore_mouse_move_t             g_fn_mouse_move = NULL;
static aarcadecore_mouse_down_t             g_fn_mouse_down = NULL;
static aarcadecore_mouse_up_t               g_fn_mouse_up = NULL;
static aarcadecore_mouse_wheel_t            g_fn_mouse_wheel = NULL;
static aarcadecore_toggle_main_menu_t       g_fn_toggle_main_menu = NULL;
static aarcadecore_is_main_menu_open_t     g_fn_is_main_menu_open = NULL;
static aarcadecore_should_open_engine_menu_t g_fn_should_open_engine_menu = NULL;
static aarcadecore_clear_engine_menu_flag_t  g_fn_clear_engine_menu_flag = NULL;
static aarcadecore_should_start_libretro_t   g_fn_should_start_libretro = NULL;
static aarcadecore_clear_start_libretro_flag_t g_fn_clear_start_libretro_flag = NULL;
static aarcadecore_start_libretro_t          g_fn_start_libretro = NULL;
static aarcadecore_get_task_count_t         g_fn_get_task_count = NULL;
static aarcadecore_render_task_texture_t   g_fn_render_task_texture = NULL;
static aarcadecore_render_overlay_t        g_fn_render_overlay = NULL;
static aarcadecore_has_pending_spawn_t    g_fn_has_pending_spawn = NULL;
static aarcadecore_pop_pending_spawn_t    g_fn_pop_pending_spawn = NULL;
static aarcadecore_init_spawned_object_t   g_fn_init_spawned_object = NULL;
static aarcadecore_confirm_spawn_t        g_fn_confirm_spawn = NULL;
static aarcadecore_get_thing_task_index_t g_fn_get_thing_task_index = NULL;
static aarcadecore_spawn_has_position_t g_fn_spawn_has_position = NULL;
static aarcadecore_spawn_get_template_name_t g_fn_spawn_get_template_name = NULL;
static aarcadecore_on_map_loaded_t g_fn_on_map_loaded = NULL;
static aarcadecore_on_map_unloaded_t g_fn_on_map_unloaded = NULL;
static aarcadecore_report_thing_transform_t g_fn_report_thing_transform = NULL;
static aarcadecore_load_thing_screen_pixels_t g_fn_load_thing_screen_pixels = NULL;
static aarcadecore_load_thing_marquee_pixels_t g_fn_load_thing_marquee_pixels = NULL;
static aarcadecore_free_pixels_t g_fn_free_pixels = NULL;
static aarcadecore_object_used_t g_fn_object_used = NULL;
static aarcadecore_has_spawn_transform_t g_fn_has_spawn_transform = NULL;
static aarcadecore_get_spawn_transform_t g_fn_get_spawn_transform = NULL;
static aarcadecore_get_spawn_model_id_t g_fn_get_spawn_model_id = NULL;
static aarcadecore_update_thing_idx_t g_fn_update_thing_idx = NULL;
static aarcadecore_set_spawn_preview_thing_t g_fn_set_spawn_preview_thing = NULL;
static aarcadecore_reload_thing_images_t g_fn_reload_thing_images = NULL;
static aarcadecore_get_template_for_thing_t g_fn_get_template_for_thing = NULL;
static aarcadecore_remove_spawned_t g_fn_remove_spawned = NULL;
static aarcadecore_has_pending_model_change_t g_fn_has_pending_model_change = NULL;
static aarcadecore_pop_pending_model_change_t g_fn_pop_pending_model_change = NULL;
static aarcadecore_has_pending_move_t g_fn_has_pending_move = NULL;
static aarcadecore_pop_pending_move_t g_fn_pop_pending_move = NULL;
static aarcadecore_enter_input_mode_for_selected_t g_fn_enter_input_mode_for_selected = NULL;
static aarcadecore_exit_input_mode_t g_fn_exit_input_mode = NULL;
static aarcadecore_is_input_mode_active_t g_fn_is_input_mode_active = NULL;
static aarcadecore_deselect_only_t g_fn_deselect_only = NULL;
static aarcadecore_remember_object_t g_fn_remember_object = NULL;
static aarcadecore_set_aimed_thing_t g_fn_set_aimed_thing = NULL;
static aarcadecore_has_pending_destroy_t g_fn_has_pending_destroy = NULL;
static aarcadecore_pop_pending_destroy_t g_fn_pop_pending_destroy = NULL;
static aarcadecore_enter_spawn_mode_t g_fn_enter_spawn_mode = NULL;
static aarcadecore_exit_spawn_mode_t g_fn_exit_spawn_mode = NULL;
static aarcadecore_is_spawn_mode_active_t g_fn_is_spawn_mode_active = NULL;
static aarcadecore_open_tab_menu_to_tab_t g_fn_open_tab_menu_to_tab = NULL;
static aarcadecore_toggle_build_context_menu_t g_fn_toggle_build_context_menu = NULL;
static aarcadecore_is_fullscreen_active_t g_fn_is_fullscreen_active = NULL;
static aarcadecore_exit_fullscreen_t g_fn_exit_fullscreen = NULL;

/* Forward declarations */
static void host_get_current_map(char* mapKeyOut, int mapKeySize);

/* Cursor state (in screen coords) */
static int g_cursorX = 0;
static int g_cursorY = 0;
static GLuint g_cursorTexture = 0;

/* Per-task GL textures (host-managed) */
#define MAX_TASKS 16
#define TASK_TEX_WIDTH 1024
#define TASK_TEX_HEIGHT 1024
static GLuint g_taskTextures[MAX_TASKS] = {0};
static uint16_t* g_taskPixels = NULL; /* shared pixel buffer for task rendering */
static int g_taskTexturesReady = 0;

/* Thing-to-task mapping — each tracked thing gets a unique GL texture */
#define MAX_THING_MAPPINGS 64
#define THING_TEX_SIZE 256
typedef struct {
    void* thing;           /* sithThing* */
    int thingIdx;
    int taskIndex;
    GLuint glTexture;          /* per-thing screen GL texture */
    GLuint marqueeGlTexture;   /* per-thing marquee GL texture */
    int imageLoaded;           /* screen: 0=pending, 1=loaded, -1=failed */
    int marqueeImageLoaded;    /* marquee: 0=pending, 1=loaded, -1=failed */
} ThingTaskMapping;
static ThingTaskMapping g_thingTaskMap[MAX_THING_MAPPINGS] = {0};
static int g_thingTaskCount = 0;

/* Selector ray — which AArcade thing the player is aiming at */
static int g_aimedAtThingIdx = -1;

/* Selector ray hit data — reused by spawn mode positioning */
static int g_selectorRayHasHit = 0;
static rdVector3 g_selectorRayHitPos;
static rdVector3 g_selectorRayHitNorm;
static sithSector* g_selectorRayHitSector = NULL;

/* Spawn/move mode — preview thing follows player aim until confirmed/cancelled */
static int g_spawnModeActive = 0;
static int g_spawnPreviewThingIdx = -1;
static void* g_spawnPreviewThing = NULL; /* sithThing* */
static uint32_t g_spawnOrigCollide = 0;  /* saved collide flags for restore */
static int g_spawnModeIsMove = 0;           /* 0 = spawn, 1 = move */
static rdVector3 g_moveOrigPos;
static rdMatrix34 g_moveOrigOrient;
static sithSector* g_moveOrigSector = NULL;
static char g_moveOrigTemplate[64] = {0};

/* Cached dynscreen material and original texture_id for restore */
static rdMaterial* g_dynscreenMaterial = NULL;
/* Cached dynmarquee material */
static rdMaterial* g_dynmarqueeMaterial = NULL;
static rdTexture* g_origMarqueeTextures = NULL;
static int g_origMarqueeAlphaTexId = -1;
static int g_origMarqueeOpaqueTexId = -1;
static rdTexture* g_originalTextures = NULL;
static int g_origAlphaTexId = -1;
static int g_origOpaqueTexId = -1;

/* Fullscreen overlay state */
static GLuint g_overlayTexture = 0;
static uint32_t* g_overlayPixels = NULL;
#define OVERLAY_WIDTH  1920
#define OVERLAY_HEIGHT 1080

/* SDL audio device for playing DLL audio */
static SDL_AudioDeviceID g_audio_dev = 0;

/* ========================================================================
 * Host callback implementations
 * ======================================================================== */

static void host_printf(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    stdPlatform_Printf("%s", buf);
}

static int host_get_key_state(int key_index)
{
    if (key_index < 0 || key_index >= JK_NUM_KEYS)
        return 0;
    return stdControl_aKeyInfo[key_index];
}

/* ========================================================================
 * Per-thing texture helpers
 * ======================================================================== */

/* Generate a unique RGB565 color from a thingIdx */
static uint16_t aacore_color_for_thingIdx(int thingIdx)
{
    unsigned int h = (unsigned int)thingIdx;
    h = (h * 2654435761u) >> 16; /* Knuth multiplicative hash */
    uint8_t r = ((h >> 0) & 0x1F) | 0x10;
    uint8_t g = ((h >> 5) & 0x3F) | 0x10;
    uint8_t b = ((h >> 11) & 0x1F) | 0x10;
    return (r << 11) | (g << 5) | b;
}

/* Create a 256x256 GL texture filled with a solid RGB565 color */
static GLuint aacore_create_solid_texture(uint16_t color)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    uint16_t* pixels = (uint16_t*)malloc(THING_TEX_SIZE * THING_TEX_SIZE * sizeof(uint16_t));
    for (int i = 0; i < THING_TEX_SIZE * THING_TEX_SIZE; i++)
        pixels[i] = color;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, THING_TEX_SIZE, THING_TEX_SIZE, 0,
                 GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, pixels);
    free(pixels);
    return tex;
}

/* Find DynScreen and DynMarquee materials on a sithThing's model, cache them */
static rdMaterial* aacore_find_dynscreen_material(sithThing* thing)
{
    if (g_dynscreenMaterial && g_dynmarqueeMaterial) return g_dynscreenMaterial;
    if (!thing || thing->rdthing.type != RD_THINGTYPE_MODEL || !thing->rdthing.model3)
        return NULL;

    rdModel3* model = thing->rdthing.model3;
    for (unsigned int m = 0; m < model->numMaterials; m++) {
        rdMaterial* mat = model->materials[m];
        if (!mat || mat->num_textures == 0) continue;
        if (!g_dynscreenMaterial && strstr(mat->mat_full_fpath, "dynscreen")) {
            g_dynscreenMaterial = mat;
            g_originalTextures = mat->textures;
            g_origAlphaTexId = mat->textures[0].alphaMats[0].texture_id;
            g_origOpaqueTexId = mat->textures[0].opaqueMats[0].texture_id;
            stdPlatform_Printf("AACoreManager: Found dynscreen material %p (%s)\n", mat, mat->mat_full_fpath);
        }
        if (!g_dynmarqueeMaterial && strstr(mat->mat_full_fpath, "dynmarquee")) {
            g_dynmarqueeMaterial = mat;
            g_origMarqueeTextures = mat->textures;
            g_origMarqueeAlphaTexId = mat->textures[0].alphaMats[0].texture_id;
            g_origMarqueeOpaqueTexId = mat->textures[0].opaqueMats[0].texture_id;
            stdPlatform_Printf("AACoreManager: Found dynmarquee material %p (%s)\n", mat, mat->mat_full_fpath);
        }
    }
    return g_dynscreenMaterial;
}

/* ========================================================================
 * SDL audio callback — pulls samples from the DLL
 * ======================================================================== */

static void aacore_audio_callback(void* userdata, Uint8* stream, int len)
{
    int16_t* out = (int16_t*)stream;
    int samples_needed = len / (int)sizeof(int16_t);
    int frames_needed = samples_needed / 2;
    int frames_read = 0;

    if (g_fn_get_audio_samples)
        frames_read = g_fn_get_audio_samples(out, frames_needed);

    /* Fill remainder with silence */
    int samples_read = frames_read * 2;
    if (samples_read < samples_needed)
        memset(out + samples_read, 0, (samples_needed - samples_read) * sizeof(int16_t));
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void AACoreManager_Init(void)
{
    int api_version;
    AACoreHostCallbacks callbacks;

    stdPlatform_Printf("AACoreManager: Loading aarcadecore.dll...\n");

    g_dll = SDL_LoadObject("aarcadecore.dll");
    if (!g_dll) {
        stdPlatform_Printf("AACoreManager: Failed to load DLL: %s\n", SDL_GetError());
        return;
    }

    /* Load exported functions */
    #define LOAD_FN(name) \
        g_fn_##name = (aarcadecore_##name##_t)SDL_LoadFunction(g_dll, "aarcadecore_" #name); \
        if (!g_fn_##name) { \
            stdPlatform_Printf("AACoreManager: Missing symbol aarcadecore_%s\n", #name); \
            SDL_UnloadObject(g_dll); g_dll = NULL; return; \
        }

    LOAD_FN(get_api_version)
    LOAD_FN(init)
    LOAD_FN(shutdown)
    LOAD_FN(update)
    LOAD_FN(is_active)
    LOAD_FN(get_material_name)
    LOAD_FN(render_texture)
    LOAD_FN(get_audio_sample_rate)
    LOAD_FN(get_audio_samples)
    LOAD_FN(key_down)
    LOAD_FN(key_up)
    LOAD_FN(key_char)
    LOAD_FN(mouse_move)
    LOAD_FN(mouse_down)
    LOAD_FN(mouse_up)
    LOAD_FN(mouse_wheel)
    LOAD_FN(toggle_main_menu)
    LOAD_FN(is_main_menu_open)
    LOAD_FN(should_open_engine_menu)
    LOAD_FN(clear_engine_menu_flag)
    LOAD_FN(should_start_libretro)
    LOAD_FN(clear_start_libretro_flag)
    LOAD_FN(start_libretro)
    LOAD_FN(get_task_count)
    LOAD_FN(render_task_texture)
    LOAD_FN(render_overlay)
    LOAD_FN(has_pending_spawn)
    LOAD_FN(pop_pending_spawn)
    LOAD_FN(init_spawned_object)
    LOAD_FN(confirm_spawn)
    LOAD_FN(get_thing_task_index)
    LOAD_FN(spawn_has_position)
    LOAD_FN(spawn_get_template_name)
    LOAD_FN(on_map_loaded)
    LOAD_FN(on_map_unloaded)
    LOAD_FN(report_thing_transform)
    LOAD_FN(load_thing_screen_pixels)
    LOAD_FN(load_thing_marquee_pixels)
    LOAD_FN(free_pixels)
    LOAD_FN(object_used)
    LOAD_FN(has_spawn_transform)
    LOAD_FN(get_spawn_transform)
    LOAD_FN(get_spawn_model_id)
    LOAD_FN(update_thing_idx)
    LOAD_FN(set_spawn_preview_thing)
    LOAD_FN(reload_thing_images)
    LOAD_FN(get_template_for_thing)
    LOAD_FN(remove_spawned)
    LOAD_FN(has_pending_model_change)
    LOAD_FN(pop_pending_model_change)
    LOAD_FN(has_pending_move)
    LOAD_FN(pop_pending_move)
    LOAD_FN(enter_input_mode_for_selected)
    LOAD_FN(exit_input_mode)
    LOAD_FN(is_input_mode_active)
    LOAD_FN(deselect_only)
    LOAD_FN(remember_object)
    LOAD_FN(set_aimed_thing)
    LOAD_FN(has_pending_destroy)
    LOAD_FN(pop_pending_destroy)
    LOAD_FN(enter_spawn_mode)
    LOAD_FN(exit_spawn_mode)
    LOAD_FN(is_spawn_mode_active)
    LOAD_FN(open_tab_menu_to_tab)
    LOAD_FN(toggle_build_context_menu)
    LOAD_FN(is_fullscreen_active)
    LOAD_FN(exit_fullscreen)
    #undef LOAD_FN

    /* Verify API version */
    api_version = g_fn_get_api_version();
    if (api_version != AARCADECORE_API_VERSION) {
        stdPlatform_Printf("AACoreManager: API version mismatch (dll=%d, host=%d)\n",
                          api_version, AARCADECORE_API_VERSION);
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
        return;
    }

    /* Provide host callbacks */
    callbacks.api_version = AARCADECORE_API_VERSION;
    callbacks.host_printf = host_printf;
    callbacks.get_key_state = host_get_key_state;
    callbacks.get_current_map = host_get_current_map;

    if (!g_fn_init(&callbacks)) {
        stdPlatform_Printf("AACoreManager: DLL init failed\n");
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
        return;
    }

    /* Dynamic texture callback disabled — using per-thing GL texture swap instead.
     * rdDynamicTexture_Register("dynscreen.mat", aacore_texture_callback, NULL); */

    /* Open SDL audio device to play DLL audio */
    {
        int sample_rate = g_fn_get_audio_sample_rate();
        if (sample_rate > 0) {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof(want));
            want.freq = sample_rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 2048;
            want.callback = aacore_audio_callback;
            want.userdata = NULL;

            g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (g_audio_dev > 0) {
                SDL_PauseAudioDevice(g_audio_dev, 0);
                stdPlatform_Printf("AACoreManager: Audio initialized - %d Hz\n", have.freq);
            } else {
                stdPlatform_Printf("AACoreManager: Failed to open audio: %s\n", SDL_GetError());
            }
        }
    }

    stdPlatform_Printf("AACoreManager: Initialized successfully\n");
}

void AACoreManager_Shutdown(void)
{
    if (g_audio_dev > 0) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }

    if (g_fn_shutdown)
        g_fn_shutdown();

    if (g_dll) {
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
    }

    g_fn_init = NULL;
    g_fn_shutdown = NULL;
    g_fn_update = NULL;
    g_fn_is_active = NULL;
    g_fn_get_api_version = NULL;
    g_fn_get_material_name = NULL;
    g_fn_render_texture = NULL;
    g_fn_get_audio_sample_rate = NULL;
    g_fn_get_audio_samples = NULL;
    g_fn_key_down = NULL;
    g_fn_key_up = NULL;
    g_fn_key_char = NULL;
    g_fn_mouse_move = NULL;
    g_fn_mouse_down = NULL;
    g_fn_mouse_up = NULL;
    g_fn_mouse_wheel = NULL;
    g_fn_toggle_main_menu = NULL;

    if (g_cursorTexture) { glDeleteTextures(1, &g_cursorTexture); g_cursorTexture = 0; }
    g_fn_is_main_menu_open = NULL;
    g_fn_should_open_engine_menu = NULL;
    g_fn_clear_engine_menu_flag = NULL;
    g_fn_should_start_libretro = NULL;
    g_fn_clear_start_libretro_flag = NULL;
    g_fn_start_libretro = NULL;
    g_fn_get_task_count = NULL;
    g_fn_render_task_texture = NULL;
    g_fn_render_overlay = NULL;
    g_fn_has_pending_spawn = NULL;
    g_fn_pop_pending_spawn = NULL;
    g_fn_init_spawned_object = NULL;
    g_fn_confirm_spawn = NULL;
    g_fn_get_thing_task_index = NULL;
    g_fn_spawn_has_position = NULL;
    g_fn_spawn_get_template_name = NULL;
    g_fn_on_map_loaded = NULL;
    g_fn_on_map_unloaded = NULL;
    g_fn_report_thing_transform = NULL;
    g_fn_load_thing_screen_pixels = NULL;
    g_fn_load_thing_marquee_pixels = NULL;
    g_fn_free_pixels = NULL;
    g_fn_object_used = NULL;
    g_fn_has_spawn_transform = NULL;
    g_fn_get_spawn_transform = NULL;
    g_fn_get_spawn_model_id = NULL;
    g_fn_update_thing_idx = NULL;
    g_fn_set_spawn_preview_thing = NULL;
    g_fn_reload_thing_images = NULL;
    g_fn_get_template_for_thing = NULL;
    g_fn_remove_spawned = NULL;
    g_fn_has_pending_model_change = NULL;
    g_fn_pop_pending_model_change = NULL;
    g_fn_has_pending_move = NULL;
    g_fn_pop_pending_move = NULL;
    g_fn_enter_input_mode_for_selected = NULL;
    g_fn_exit_input_mode = NULL;
    g_fn_is_input_mode_active = NULL;
    g_fn_deselect_only = NULL;
    g_fn_remember_object = NULL;
    g_fn_set_aimed_thing = NULL;
    g_fn_has_pending_destroy = NULL;
    g_fn_pop_pending_destroy = NULL;
    g_fn_enter_spawn_mode = NULL;
    g_fn_exit_spawn_mode = NULL;
    g_fn_is_spawn_mode_active = NULL;
    g_fn_open_tab_menu_to_tab = NULL;
    g_fn_toggle_build_context_menu = NULL;
    g_fn_is_fullscreen_active = NULL;
    g_fn_exit_fullscreen = NULL;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_taskTextures[i]) { glDeleteTextures(1, &g_taskTextures[i]); g_taskTextures[i] = 0; }
    }
    if (g_taskPixels) { free(g_taskPixels); g_taskPixels = NULL; }
    g_taskTexturesReady = 0;

    /* Restore original texture_id and free per-thing GL textures */
    if (g_originalTextures && g_origAlphaTexId >= 0) {
        g_originalTextures[0].alphaMats[0].texture_id = g_origAlphaTexId;
        g_originalTextures[0].opaqueMats[0].texture_id = g_origOpaqueTexId;
    }
    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].glTexture) {
            glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
            g_thingTaskMap[i].glTexture = 0;
        }
    }
    g_thingTaskCount = 0;
    g_dynscreenMaterial = NULL;
    g_dynmarqueeMaterial = NULL;
    g_origMarqueeTextures = NULL;
    g_origMarqueeAlphaTexId = -1;
    g_origMarqueeOpaqueTexId = -1;
    g_originalTextures = NULL;
    g_origAlphaTexId = -1;
    g_origOpaqueTexId = -1;

    if (g_overlayTexture) { glDeleteTextures(1, &g_overlayTexture); g_overlayTexture = 0; }
    if (g_overlayPixels) { free(g_overlayPixels); g_overlayPixels = NULL; }

    stdPlatform_Printf("AACoreManager: Shutdown complete\n");
}

/* Get the spawn template from the DLL, with fallbacks */
static sithThing* aacore_get_spawn_template(void)
{
    char tmplName[64] = {0};
    if (g_fn_spawn_get_template_name)
        g_fn_spawn_get_template_name(tmplName, sizeof(tmplName));
    if (!tmplName[0])
        strncpy(tmplName, "aaojk_movie_stand_standard", sizeof(tmplName) - 1);

    sithThing* tmpl = sithTemplate_GetEntryByName(tmplName);
    if (!tmpl) {
        stdPlatform_Printf("AACoreManager: Template '%s' not found, trying fallback\n", tmplName);
        tmpl = sithTemplate_GetEntryByName("aaojk_movie_stand_standard");
        if (tmpl) strncpy(tmplName, "aaojk_movie_stand_standard", sizeof(tmplName) - 1);
    }
    if (!tmpl)
        stdPlatform_Printf("AACoreManager: No spawn template found\n");
    else
        stdPlatform_Printf("AACoreManager: Using template '%s'\n", tmplName);
    return tmpl;
}

/* Look up a template by name with fallback */
static sithThing* aacore_get_spawn_template_by_name(const char* name)
{
    sithThing* tmpl = sithTemplate_GetEntryByName(name);
    if (!tmpl) {
        stdPlatform_Printf("AACoreManager: Template '%s' not found, trying aaojk_movie_stand_standard\n", name);
        tmpl = sithTemplate_GetEntryByName("aaojk_movie_stand_standard");
    }
    return tmpl;
}

/* Spawn a thing at the player's aim point */
static sithThing* aacore_spawn_at_player_aim(void)
{
    sithThing* player = sithPlayer_pLocalPlayerThing;
    if (!player) return NULL;

    sithThing* tmpl = aacore_get_spawn_template();
    if (!tmpl) return NULL;

    /* Get aim direction (player orientation + eye pitch) */
    rdMatrix34 aimMatrix;
    _memcpy(&aimMatrix, &player->lookOrientation, sizeof(aimMatrix));
    rdMatrix_PreRotate34(&aimMatrix, &player->actorParams.eyePYR);
    rdVector3 lookDir = aimMatrix.lvec;

    /* Raycast forward from player */
    sithCollision_SearchRadiusForThings(player->sector, player, &player->position,
                                        &lookDir, 10.0f, 0.0f, 0x1);
    sithCollisionSearchEntry* searchResult = sithCollision_NextSearchResult();
    if (!searchResult || !searchResult->surface) {
        sithCollision_SearchClose();
        stdPlatform_Printf("AACoreManager: No surface hit for spawn\n");
        return NULL;
    }

    rdVector3 hitPos, hitNorm;
    hitNorm = searchResult->hitNorm;
    float dist = searchResult->distance;
    sithSector* hitSector = searchResult->surface->parent_sector;
    sithCollision_SearchClose();

    /* Hit position = player + lookDir * distance */
    hitPos.x = player->position.x + lookDir.x * dist;
    hitPos.y = player->position.y + lookDir.y * dist;
    hitPos.z = player->position.z + lookDir.z * dist;

    /* Build orientation: up = surface normal, forward = away from player (facing outward) */
    rdVector3 uvec = hitNorm;
    rdVector3 toPlayer;
    rdVector_Sub3(&toPlayer, &hitPos, &player->position);
    /* Project toPlayer onto the surface plane */
    float dot = rdVector_Dot3(&toPlayer, &uvec);
    toPlayer.x -= dot * uvec.x;
    toPlayer.y -= dot * uvec.y;
    toPlayer.z -= dot * uvec.z;
    rdVector_Normalize3Acc(&toPlayer);

    rdVector3 lvec, rvec;
    if (rdVector_Len3(&toPlayer) < 0.001f) {
        /* Degenerate — pick an arbitrary forward */
        lvec.x = 1.0f; lvec.y = 0.0f; lvec.z = 0.0f;
    } else {
        lvec = toPlayer;
    }
    rdVector_Cross3(&rvec, &lvec, &uvec);
    rdVector_Normalize3Acc(&rvec);
    rdVector_Cross3(&lvec, &uvec, &rvec);
    rdVector_Normalize3Acc(&lvec);

    /* No vertical offset — 3DO origin is at the bottom */

    rdMatrix34 orient;
    orient.rvec = rvec;
    orient.lvec = lvec;
    orient.uvec = uvec;
    rdVector_Zero3(&orient.scale);

    sithThing* spawned = sithThing_Create((sithThing*)tmpl, &hitPos, &orient, hitSector, NULL);
    if (spawned) {
        stdPlatform_Printf("AACoreManager: Spawned at (%.2f, %.2f, %.2f) thingIdx=%d\n",
                          hitPos.x, hitPos.y, hitPos.z, spawned->thingIdx);
    }
    return spawned;
}

/* Spawn a thing at an explicit position (for restoring saved objects) */
static sithThing* aacore_spawn_at_position(float px, float py, float pz, int sectorId, float rx, float ry, float rz)
{
    sithThing* player = sithPlayer_pLocalPlayerThing;
    if (!player) return NULL;

    sithThing* tmpl = aacore_get_spawn_template();
    if (!tmpl) return NULL;

    rdVector3 pos;
    pos.x = px; pos.y = py; pos.z = pz;

    /* Build orientation from PYR angles */
    rdMatrix34 orient;
    rdVector3 pyr;
    pyr.x = rx; /* pitch */
    pyr.y = ry; /* yaw */
    pyr.z = rz; /* roll */
    rdMatrix_BuildRotate34(&orient, &pyr);

    /* Use saved sector ID → fall back to FindSectorAtPos → fall back to player's sector */
    sithSector* sector = NULL;
    if (sectorId >= 0 && sithWorld_pCurrentWorld && sectorId < (int)sithWorld_pCurrentWorld->numSectors) {
        sector = &sithWorld_pCurrentWorld->sectors[sectorId];
    }
    if (!sector && sithWorld_pCurrentWorld) {
        sector = sithSector_sub_4F8D00(sithWorld_pCurrentWorld, &pos);
    }
    if (!sector) {
        sector = player->sector;
    }

    sithThing* spawned = sithThing_Create((sithThing*)tmpl, &pos, &orient, sector, NULL);
    if (spawned) {
        stdPlatform_Printf("AACoreManager: Restored at (%.2f, %.2f, %.2f) thingIdx=%d\n",
                          px, py, pz, spawned->thingIdx);
    }
    return spawned;
}

void AACoreManager_Update(void)
{
    if (g_fn_update)
        g_fn_update();

    /* Selector ray — find which AArcade thing the player is aiming at,
     * and store the first solid surface hit for spawn mode reuse */
    {
        sithThing* player = sithPlayer_pLocalPlayerThing;
        int newAimedAt = -1;
        g_selectorRayHasHit = 0;
        g_selectorRayHitSector = NULL;
        if (player && player->sector) {
            rdMatrix34 aimMatrix;
            _memcpy(&aimMatrix, &player->lookOrientation, sizeof(aimMatrix));
            rdMatrix_PreRotate34(&aimMatrix, &player->actorParams.eyePYR);
            rdVector3 lookDir = aimMatrix.lvec;

            sithCollision_SearchRadiusForThings(player->sector, player,
                &player->position, &lookDir, 50.0f, 0.0f, 0);
            sithCollisionSearchEntry* hit = sithCollision_NextSearchResult();
            while (hit) {
                /* Skip adjoin surfaces (portals between sectors) */
                if (hit->hitType & SITHCOLLISION_ADJOINCROSS) {
                    hit = sithCollision_NextSearchResult();
                    continue;
                }
                /* Store first solid surface hit for spawn mode */
                if (!g_selectorRayHasHit && hit->surface) {
                    g_selectorRayHasHit = 1;
                    g_selectorRayHitNorm = hit->hitNorm;
                    g_selectorRayHitPos.x = player->position.x + lookDir.x * hit->distance;
                    g_selectorRayHitPos.y = player->position.y + lookDir.y * hit->distance;
                    g_selectorRayHitPos.z = player->position.z + lookDir.z * hit->distance;
                    g_selectorRayHitSector = hit->surface->parent_sector;
                }
                /* Check for tracked AArcade things */
                if ((hit->hitType & SITHCOLLISION_THING) && hit->receiver && g_thingTaskCount > 0) {
                    int idx = hit->receiver->thingIdx;
                    for (int i = 0; i < g_thingTaskCount; i++) {
                        if (g_thingTaskMap[i].thingIdx == idx) {
                            newAimedAt = idx;
                            break;
                        }
                    }
                    if (newAimedAt >= 0) break;
                }
                hit = sithCollision_NextSearchResult();
            }
            sithCollision_SearchClose();
        }
        if (newAimedAt != g_aimedAtThingIdx) {
            g_aimedAtThingIdx = newAimedAt;
            if (g_fn_set_aimed_thing)
                g_fn_set_aimed_thing(newAimedAt);
        }
    }

    /* Pre-render all task textures — each task gets its own GL texture */
    if (g_fn_get_task_count && g_fn_render_task_texture) {
        int count = g_fn_get_task_count();
        if (count > MAX_TASKS) count = MAX_TASKS;

        if (!g_taskPixels)
            g_taskPixels = (uint16_t*)malloc(TASK_TEX_WIDTH * TASK_TEX_HEIGHT * 2);

        for (int i = 0; i < count; i++) {
            /* Fill with green by default */
            for (int j = 0; j < TASK_TEX_WIDTH * TASK_TEX_HEIGHT; j++)
                g_taskPixels[j] = (0 << 11) | (63 << 5) | 0;

            g_fn_render_task_texture(i, g_taskPixels, TASK_TEX_WIDTH, TASK_TEX_HEIGHT, 1, 16);

            if (!g_taskTextures[i]) {
                glGenTextures(1, &g_taskTextures[i]);
            }
            glBindTexture(GL_TEXTURE_2D, g_taskTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, TASK_TEX_WIDTH, TASK_TEX_HEIGHT, 0,
                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, g_taskPixels);
        }
        g_taskTexturesReady = count;
    }

    /* Spawn mode: reposition preview thing using selector ray hit data */
    if (g_spawnModeActive && g_spawnPreviewThing && g_selectorRayHasHit) {
        sithThing* player = sithPlayer_pLocalPlayerThing;
        sithThing* preview = (sithThing*)g_spawnPreviewThing;
        if (player) {
            /* Build orientation: up = surface normal, facing outward (away from player) */
            rdVector3 uvec = g_selectorRayHitNorm;
            rdVector3 toPlayer;
            rdVector_Sub3(&toPlayer, &g_selectorRayHitPos, &player->position);
            float dot = rdVector_Dot3(&toPlayer, &uvec);
            toPlayer.x -= dot * uvec.x;
            toPlayer.y -= dot * uvec.y;
            toPlayer.z -= dot * uvec.z;
            rdVector_Normalize3Acc(&toPlayer);

            rdVector3 lvec, rvec;
            if (rdVector_Len3(&toPlayer) < 0.001f) {
                lvec.x = 1.0f; lvec.y = 0.0f; lvec.z = 0.0f;
            } else {
                lvec = toPlayer;
            }
            rdVector_Cross3(&rvec, &lvec, &uvec);
            rdVector_Normalize3Acc(&rvec);
            rdVector_Cross3(&lvec, &uvec, &rvec);
            rdVector_Normalize3Acc(&lvec);

            rdMatrix34 orient;
            orient.rvec = rvec;
            orient.lvec = lvec;
            orient.uvec = uvec;
            rdVector_Zero3(&orient.scale);

            rdVector3 hitPos = g_selectorRayHitPos;
            sithSector* finalSector = g_selectorRayHitSector;

            /* Apply transform overrides from spawn mode panel */
            if (g_fn_has_spawn_transform && g_fn_has_spawn_transform()) {
                float rp, ry, rr, ox, oy, oz;
                bool isWorldRot = false, isWorldOff = false, useRaycast = false;
                g_fn_get_spawn_transform(&rp, &ry, &rr, &isWorldRot, &ox, &oy, &oz, &isWorldOff, &useRaycast);

                /* Position offset (applied before rotation) */
                if (ox != 0.0f || oy != 0.0f || oz != 0.0f) {
                    if (isWorldOff) {
                        hitPos.x += ox;
                        hitPos.y += oy;
                        hitPos.z += oz;
                    } else {
                        /* Relative to surface-aligned orientation */
                        hitPos.x += orient.rvec.x * ox + orient.lvec.x * oy + orient.uvec.x * oz;
                        hitPos.y += orient.rvec.y * ox + orient.lvec.y * oy + orient.uvec.y * oz;
                        hitPos.z += orient.rvec.z * ox + orient.lvec.z * oy + orient.uvec.z * oz;
                    }

                    /* Determine final position + sector for offset */
                    if (useRaycast) {
                        /* Secondary raycast from player to the target offset position.
                         * If it hits something, use the hit point. Otherwise use target. */
                        rdVector3 rayDir;
                        rdVector_Sub3(&rayDir, &hitPos, &player->position);
                        float rayLen = rdVector_Len3(&rayDir);
                        if (rayLen > 0.001f) {
                            rdVector_Normalize3Acc(&rayDir);
                            sithCollision_SearchRadiusForThings(player->sector, player,
                                &player->position, &rayDir, rayLen + 0.1f, 0.0f, 0x1);
                            sithCollisionSearchEntry* hit2 = sithCollision_NextSearchResult();
                            int foundHit = 0;
                            while (hit2) {
                                if (hit2->hitType & SITHCOLLISION_ADJOINCROSS) {
                                    hit2 = sithCollision_NextSearchResult();
                                    continue;
                                }
                                /* Hit a solid surface before reaching target */
                                if (hit2->distance < rayLen) {
                                    hitPos.x = player->position.x + rayDir.x * hit2->distance;
                                    hitPos.y = player->position.y + rayDir.y * hit2->distance;
                                    hitPos.z = player->position.z + rayDir.z * hit2->distance;
                                }
                                if (hit2->surface && hit2->surface->parent_sector)
                                    finalSector = hit2->surface->parent_sector;
                                foundHit = 1;
                                break;
                            }
                            sithCollision_SearchClose();
                            if (!foundHit && sithWorld_pCurrentWorld) {
                                sithSector* found = sithSector_sub_4F8D00(sithWorld_pCurrentWorld, &hitPos);
                                if (found) finalSector = found;
                            }
                        }
                    } else {
                        /* Use FindSectorAtPos */
                        if (sithWorld_pCurrentWorld) {
                            sithSector* found = sithSector_sub_4F8D00(sithWorld_pCurrentWorld, &hitPos);
                            if (found) finalSector = found;
                        }
                    }
                }

                /* Rotation */
                if (isWorldRot) {
                    rdVector3 pyr;
                    pyr.x = rp; pyr.y = ry; pyr.z = rr;
                    rdMatrix_BuildRotate34(&orient, &pyr);
                } else if (rp != 0.0f || ry != 0.0f || rr != 0.0f) {
                    rdVector3 pyr;
                    pyr.x = rp; pyr.y = ry; pyr.z = rr;
                    rdMatrix_PreRotate34(&orient, &pyr);
                }
            }

            sithThing_SetPosAndRot(preview, &hitPos, &orient);

            if (finalSector && preview->sector != finalSector)
                sithThing_MoveToSector(preview, finalSector, 0);
        }
    }

    /* Spawn mode: handle model change (destroy + recreate with new template) */
    if (g_spawnModeActive && g_spawnPreviewThing &&
        g_fn_has_pending_model_change && g_fn_has_pending_model_change()) {
        const char* newTmplName = g_fn_pop_pending_model_change();
        if (newTmplName && newTmplName[0]) {
            sithThing* oldThing = (sithThing*)g_spawnPreviewThing;
            rdVector3 savedPos = oldThing->position;
            rdMatrix34 savedOrient;
            _memcpy(&savedOrient, &oldThing->lookOrientation, sizeof(rdMatrix34));
            sithSector* savedSector = oldThing->sector;

            /* Unregister old thing */
            int oldIdx = g_spawnPreviewThingIdx;
            for (int i = 0; i < g_thingTaskCount; i++) {
                if (g_thingTaskMap[i].thingIdx == oldIdx) {
                    if (g_thingTaskMap[i].glTexture) glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
                    if (g_thingTaskMap[i].marqueeGlTexture) glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
                    for (int j = i; j < g_thingTaskCount - 1; j++)
                        g_thingTaskMap[j] = g_thingTaskMap[j + 1];
                    g_thingTaskCount--;
                    break;
                }
            }

            /* Destroy old thing */
            if (oldThing->type != SITH_THING_FREE)
                sithThing_Destroy(oldThing);

            /* Create new thing with new template */
            sithThing* tmpl = aacore_get_spawn_template_by_name(newTmplName);
            if (tmpl) {
                sithThing* newThing = sithThing_Create(tmpl, &savedPos, &savedOrient, savedSector, NULL);
                if (newThing) {
                    AACoreManager_RegisterThingTask(newThing, newThing->thingIdx, 0);
                    g_spawnPreviewThingIdx = newThing->thingIdx;
                    g_spawnPreviewThing = newThing;
                    /* Make non-solid */
                    g_spawnOrigCollide = newThing->collide;
                    newThing->collide = 0;
                    /* Update DLL bookkeeping with new thingIdx + reload images */
                    if (g_fn_update_thing_idx)
                        g_fn_update_thing_idx(oldIdx, newThing->thingIdx);
                    if (g_fn_set_spawn_preview_thing)
                        g_fn_set_spawn_preview_thing(newThing->thingIdx);
                    if (g_fn_reload_thing_images)
                        g_fn_reload_thing_images(newThing->thingIdx);
                    stdPlatform_Printf("AACoreManager: Swapped spawn template to '%s' (thingIdx=%d)\n",
                        newTmplName, newThing->thingIdx);
                }
            }
        }
    }

    /* Process pending spawn requests from the DLL */
    if (g_fn_has_pending_spawn && g_fn_has_pending_spawn()) {
        g_fn_pop_pending_spawn();

        sithThing* spawned = NULL;
        float px, py, pz, rx, ry, rz;
        int sectorId = -1;
        int isRestore = 0;
        if (g_fn_spawn_has_position && g_fn_spawn_has_position(&px, &py, &pz, &sectorId, &rx, &ry, &rz)) {
            /* Restore spawn: use explicit position */
            spawned = aacore_spawn_at_position(px, py, pz, sectorId, rx, ry, rz);
            isRestore = 1;
        } else {
            /* User spawn: raycast from player aim */
            spawned = aacore_spawn_at_player_aim();
        }

        if (spawned) {
            AACoreManager_RegisterThingTask(spawned, spawned->thingIdx, 0);
            if (isRestore) {
                /* Restoring saved objects: init + confirm immediately */
                if (g_fn_init_spawned_object)
                    g_fn_init_spawned_object(spawned->thingIdx);
                if (g_fn_confirm_spawn)
                    g_fn_confirm_spawn(spawned->thingIdx);
            } else {
                /* New user spawn: init (triggers image loading) then enter spawn mode */
                if (g_fn_init_spawned_object)
                    g_fn_init_spawned_object(spawned->thingIdx);
                if (g_fn_set_spawn_preview_thing)
                    g_fn_set_spawn_preview_thing(spawned->thingIdx);
                g_spawnModeActive = 1;
                g_spawnPreviewThingIdx = spawned->thingIdx;
                g_spawnPreviewThing = spawned;
                /* Make preview non-solid so selector ray doesn't hit it */
                g_spawnOrigCollide = spawned->collide;
                spawned->collide = 0;
                if (g_fn_enter_spawn_mode)
                    g_fn_enter_spawn_mode();
                stdPlatform_Printf("AACoreManager: Entered spawn mode for thingIdx=%d\n", spawned->thingIdx);
            }
        } else {
            stdPlatform_Printf("AACoreManager: Spawn failed\n");
        }
    }

    /* Process pending destroy requests from the DLL */
    while (g_fn_has_pending_destroy && g_fn_has_pending_destroy()) {
        int destroyIdx = g_fn_pop_pending_destroy();
        if (destroyIdx >= 0) {
            /* Unregister from thing-task map and free GL textures */
            for (int i = 0; i < g_thingTaskCount; i++) {
                if (g_thingTaskMap[i].thingIdx == destroyIdx) {
                    if (g_thingTaskMap[i].glTexture)
                        glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
                    if (g_thingTaskMap[i].marqueeGlTexture)
                        glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
                    /* Shift remaining entries down */
                    for (int j = i; j < g_thingTaskCount - 1; j++)
                        g_thingTaskMap[j] = g_thingTaskMap[j + 1];
                    g_thingTaskCount--;
                    break;
                }
            }

            /* Clear aimed-at if this was the aimed thing */
            if (g_aimedAtThingIdx == destroyIdx)
                g_aimedAtThingIdx = -1;

            /* Destroy the sithThing in the engine */
            if (sithWorld_pCurrentWorld && destroyIdx < (int)sithWorld_pCurrentWorld->numThingsLoaded) {
                sithThing* thing = &sithWorld_pCurrentWorld->things[destroyIdx];
                if (thing->type != SITH_THING_FREE) {
                    sithThing_Destroy(thing);
                    stdPlatform_Printf("AACoreManager: Destroyed sithThing %d\n", destroyIdx);
                }
            }
        }
    }

    /* Process pending move requests from the DLL */
    while (g_fn_has_pending_move && g_fn_has_pending_move()) {
        int moveIdx = g_fn_pop_pending_move();
        if (moveIdx >= 0 && !g_spawnModeActive) {
            /* Find the thing in our task map */
            sithThing* thing = NULL;
            for (int i = 0; i < g_thingTaskCount; i++) {
                if (g_thingTaskMap[i].thingIdx == moveIdx) {
                    thing = (sithThing*)g_thingTaskMap[i].thing;
                    break;
                }
            }
            if (thing) {
                /* Save original state for cancel */
                g_moveOrigPos = thing->position;
                _memcpy(&g_moveOrigOrient, &thing->lookOrientation, sizeof(rdMatrix34));
                g_moveOrigSector = thing->sector;

                /* Save original template name for restore on cancel */
                if (g_fn_get_template_for_thing) {
                    const char* tmpl = g_fn_get_template_for_thing(moveIdx);
                    strncpy(g_moveOrigTemplate, tmpl ? tmpl : "", sizeof(g_moveOrigTemplate) - 1);
                    g_moveOrigTemplate[sizeof(g_moveOrigTemplate) - 1] = '\0';
                }

                /* Enter spawn mode as move */
                g_spawnModeActive = 1;
                g_spawnModeIsMove = 1;
                g_spawnPreviewThingIdx = moveIdx;
                g_spawnPreviewThing = thing;
                if (g_fn_set_spawn_preview_thing)
                    g_fn_set_spawn_preview_thing(moveIdx);
                /* Make non-solid */
                g_spawnOrigCollide = thing->collide;
                thing->collide = 0;
                if (g_fn_enter_spawn_mode)
                    g_fn_enter_spawn_mode();
                stdPlatform_Printf("AACoreManager: Entered move mode for thingIdx=%d\n", moveIdx);
            }
        }
    }

    /* Poll for screen images that have been cached by the ImageLoader */
    if (g_fn_load_thing_screen_pixels && g_fn_free_pixels) {
        for (int i = 0; i < g_thingTaskCount; i++) {
            if (g_thingTaskMap[i].imageLoaded != 0) continue; /* already loaded or failed */

            void* pixels = NULL;
            int w = 0, h = 0;
            if (g_fn_load_thing_screen_pixels(g_thingTaskMap[i].thingIdx, &pixels, &w, &h)) {
                /* Create GL texture from BGRA pixels */
                GLuint newTex;
                glGenTextures(1, &newTex);
                glBindTexture(GL_TEXTURE_2D, newTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                g_fn_free_pixels(pixels);

                /* Replace the solid color texture */
                if (g_thingTaskMap[i].glTexture)
                    glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
                g_thingTaskMap[i].glTexture = newTex;
                g_thingTaskMap[i].imageLoaded = 1;

                stdPlatform_Printf("AACoreManager: Screen image loaded for thingIdx=%d (%dx%d, glTex=%u)\n",
                                  g_thingTaskMap[i].thingIdx, w, h, newTex);
            }
        }
    }

    /* Poll for marquee images */
    if (g_fn_load_thing_marquee_pixels && g_fn_free_pixels) {
        for (int i = 0; i < g_thingTaskCount; i++) {
            if (g_thingTaskMap[i].marqueeImageLoaded != 0) continue;

            void* pixels = NULL;
            int w = 0, h = 0;
            if (g_fn_load_thing_marquee_pixels(g_thingTaskMap[i].thingIdx, &pixels, &w, &h)) {
                GLuint newTex;
                glGenTextures(1, &newTex);
                glBindTexture(GL_TEXTURE_2D, newTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                g_fn_free_pixels(pixels);

                if (g_thingTaskMap[i].marqueeGlTexture)
                    glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
                g_thingTaskMap[i].marqueeGlTexture = newTex;
                g_thingTaskMap[i].marqueeImageLoaded = 1;

                stdPlatform_Printf("AACoreManager: Marquee image loaded for thingIdx=%d (%dx%d, glTex=%u)\n",
                                  g_thingTaskMap[i].thingIdx, w, h, newTex);
            }
        }
    }
}

bool AACoreManager_IsActive(void)
{
    if (g_fn_is_active)
        return g_fn_is_active();
    return false;
}

void AACoreManager_KeyDown(int vk_code, int modifiers)
{
    if (g_fn_key_down)
        g_fn_key_down(vk_code, modifiers);
}

void AACoreManager_KeyUp(int vk_code, int modifiers)
{
    if (g_fn_key_up)
        g_fn_key_up(vk_code, modifiers);
}

void AACoreManager_KeyChar(unsigned int unicode_char, int modifiers)
{
    if (g_fn_key_char)
        g_fn_key_char(unicode_char, modifiers);
}

void AACoreManager_MouseMove(int x, int y)
{
    g_cursorX = x;
    g_cursorY = y;
    if (g_fn_mouse_move) {
        /* Map screen coords to overlay coords */
        int ox = (g_cursorX * OVERLAY_WIDTH) / (Window_xSize > 0 ? Window_xSize : 1);
        int oy = (g_cursorY * OVERLAY_HEIGHT) / (Window_ySize > 0 ? Window_ySize : 1);
        g_fn_mouse_move(ox, oy);
    }
}

void AACoreManager_MouseDown(int button)
{
    if (g_fn_mouse_down)
        g_fn_mouse_down(button);
}

void AACoreManager_MouseUp(int button)
{
    if (g_fn_mouse_up)
        g_fn_mouse_up(button);
}

void AACoreManager_MouseWheel(int delta)
{
    if (g_fn_mouse_wheel)
        g_fn_mouse_wheel(delta);
}

void AACoreManager_RegisterThingTask(void* pSithThing, int thingIdx, int taskIndex)
{
    if (g_thingTaskCount >= MAX_THING_MAPPINGS) return;

    sithThing* thing = (sithThing*)pSithThing;

    /* Find and cache the dynscreen material on first call */
    rdMaterial* mat = aacore_find_dynscreen_material(thing);
    if (!mat) {
        stdPlatform_Printf("AACoreManager: WARNING: No dynscreen material found on thing %d\n", thingIdx);
        return;
    }

    /* Create per-thing GL textures with unique color */
    uint16_t color = aacore_color_for_thingIdx(thingIdx);
    GLuint screenTex = aacore_create_solid_texture(color);
    GLuint marqueeTex = aacore_create_solid_texture(color);

    /* Store mapping */
    g_thingTaskMap[g_thingTaskCount].thing = pSithThing;
    g_thingTaskMap[g_thingTaskCount].thingIdx = thingIdx;
    g_thingTaskMap[g_thingTaskCount].taskIndex = taskIndex;
    g_thingTaskMap[g_thingTaskCount].glTexture = screenTex;
    g_thingTaskMap[g_thingTaskCount].marqueeGlTexture = marqueeTex;
    g_thingTaskMap[g_thingTaskCount].imageLoaded = 0;
    g_thingTaskMap[g_thingTaskCount].marqueeImageLoaded = 0;
    g_thingTaskCount++;

    stdPlatform_Printf("AACoreManager: Registered thing %p (thingIdx=%d) task %d, screenTex=%u, marqueeTex=%u\n",
                      pSithThing, thingIdx, taskIndex, screenTex, marqueeTex);
}

void AACoreManager_OnMapLoaded(void)
{
    if (g_fn_on_map_loaded) g_fn_on_map_loaded();
}

void AACoreManager_OnMapUnloaded(void)
{
    /* Clean up per-thing GL textures and reset task map */
    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].glTexture)
            glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
        if (g_thingTaskMap[i].marqueeGlTexture)
            glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
    }
    memset(g_thingTaskMap, 0, sizeof(g_thingTaskMap));
    g_thingTaskCount = 0;

    /* Reset selector ray */
    g_aimedAtThingIdx = -1;
    g_selectorRayHasHit = 0;
    g_selectorRayHitSector = NULL;

    /* Reset spawn/move mode */
    g_spawnModeActive = 0;
    g_spawnPreviewThingIdx = -1;
    g_spawnPreviewThing = NULL;
    g_spawnOrigCollide = 0;
    g_spawnModeIsMove = 0;
    g_moveOrigSector = NULL;
    g_moveOrigTemplate[0] = '\0';

    /* Invalidate cached material pointers (freed with the map) */
    g_dynscreenMaterial = NULL;
    g_dynmarqueeMaterial = NULL;
    g_origMarqueeTextures = NULL;
    g_origMarqueeAlphaTexId = -1;
    g_origMarqueeOpaqueTexId = -1;
    g_originalTextures = NULL;
    g_origAlphaTexId = -1;
    g_origOpaqueTexId = -1;

    if (g_fn_on_map_unloaded) g_fn_on_map_unloaded();
}

void AACoreManager_ObjectUsed(int thingIdx)
{
    if (g_fn_object_used)
        g_fn_object_used(thingIdx);
}

void AACoreManager_EnterInputMode(void)
{
    if (g_fn_enter_input_mode_for_selected)
        g_fn_enter_input_mode_for_selected();
}

void AACoreManager_ExitInputMode(void)
{
    if (g_fn_exit_input_mode)
        g_fn_exit_input_mode();
}

bool AACoreManager_IsInputModeActive(void)
{
    return g_fn_is_input_mode_active && g_fn_is_input_mode_active();
}

void AACoreManager_RememberObject(void)
{
    int aimedIdx = g_aimedAtThingIdx;
    /* If there's a selected object, deselect without closing its instance */
    if (g_fn_deselect_only) {
        /* The DLL checks internally if anything is selected */
        g_fn_deselect_only();
    }
    /* If aiming at an object, activate its instance without selecting */
    if (aimedIdx >= 0 && g_fn_remember_object) {
        g_fn_remember_object(aimedIdx);
    }
}

bool AACoreManager_IsSpawnModeActive(void)
{
    return g_spawnModeActive != 0;
}

void AACoreManager_ConfirmSpawn(void)
{
    if (!g_spawnModeActive || !g_spawnPreviewThing) return;

    sithThing* spawned = (sithThing*)g_spawnPreviewThing;
    int thingIdx = g_spawnPreviewThingIdx;

    if (g_spawnModeIsMove) {
        /* Move mode: confirm (saves transform) + save the new position */
        if (g_fn_confirm_spawn)
            g_fn_confirm_spawn(thingIdx);
        if (g_fn_report_thing_transform) {
            rdVector3 pos = spawned->position;
            int sector = spawned->sector ? (int)spawned->sector->id : -1;
            rdVector3 pyr;
            rdMatrix_ExtractAngles34(&spawned->lookOrientation, &pyr);
            g_fn_report_thing_transform(thingIdx, pos.x, pos.y, pos.z, sector, pyr.x, pyr.y, pyr.z);
        }
        stdPlatform_Printf("AACoreManager: Move confirmed for thingIdx=%d\n", thingIdx);
    } else {
        /* Spawn mode: confirm with DLL + save position */
        if (g_fn_confirm_spawn)
            g_fn_confirm_spawn(thingIdx);
        if (g_fn_report_thing_transform) {
            rdVector3 pos = spawned->position;
            int sector = spawned->sector ? (int)spawned->sector->id : -1;
            rdVector3 pyr;
            rdMatrix_ExtractAngles34(&spawned->lookOrientation, &pyr);
            g_fn_report_thing_transform(thingIdx, pos.x, pos.y, pos.z, sector, pyr.x, pyr.y, pyr.z);
        }
        stdPlatform_Printf("AACoreManager: Spawn confirmed at thingIdx=%d\n", thingIdx);
    }

    /* Restore collision on the placed thing */
    if (spawned)
        spawned->collide = g_spawnOrigCollide;

    /* Exit spawn/move mode */
    if (g_fn_exit_spawn_mode)
        g_fn_exit_spawn_mode();
    g_spawnModeActive = 0;
    g_spawnModeIsMove = 0;
    g_spawnPreviewThingIdx = -1;
    g_spawnPreviewThing = NULL;
    if (g_fn_set_spawn_preview_thing)
        g_fn_set_spawn_preview_thing(-1);
}

void AACoreManager_CancelSpawn(void)
{
    if (!g_spawnModeActive) return;

    int thingIdx = g_spawnPreviewThingIdx;

    if (g_spawnModeIsMove) {
        /* Move mode: always destroy+recreate with original template at original position */
        if (g_spawnPreviewThing && g_moveOrigTemplate[0]) {
            sithThing* thing = (sithThing*)g_spawnPreviewThing;

            /* Unregister old thing */
            for (int i = 0; i < g_thingTaskCount; i++) {
                if (g_thingTaskMap[i].thingIdx == thingIdx) {
                    if (g_thingTaskMap[i].glTexture) glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
                    if (g_thingTaskMap[i].marqueeGlTexture) glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
                    for (int j = i; j < g_thingTaskCount - 1; j++)
                        g_thingTaskMap[j] = g_thingTaskMap[j + 1];
                    g_thingTaskCount--;
                    break;
                }
            }
            if (thing->type != SITH_THING_FREE)
                sithThing_Destroy(thing);

            /* Recreate with original template at original position */
            sithThing* tmpl = aacore_get_spawn_template_by_name(g_moveOrigTemplate);
            if (tmpl) {
                sithThing* restored = sithThing_Create(tmpl, &g_moveOrigPos, &g_moveOrigOrient, g_moveOrigSector, NULL);
                if (restored) {
                    AACoreManager_RegisterThingTask(restored, restored->thingIdx, 0);
                    restored->collide = g_spawnOrigCollide;
                    if (g_fn_update_thing_idx)
                        g_fn_update_thing_idx(thingIdx, restored->thingIdx);
                    if (g_fn_reload_thing_images)
                        g_fn_reload_thing_images(restored->thingIdx);
                    stdPlatform_Printf("AACoreManager: Restored to '%s' (thingIdx=%d)\n",
                        g_moveOrigTemplate, restored->thingIdx);
                }
            }
        }
        stdPlatform_Printf("AACoreManager: Move cancelled for thingIdx=%d\n", thingIdx);
    } else {
        /* Spawn mode: remove DLL-side SpawnedObject entry + destroy the preview thing */
        if (g_fn_remove_spawned)
            g_fn_remove_spawned(thingIdx);
        for (int i = 0; i < g_thingTaskCount; i++) {
            if (g_thingTaskMap[i].thingIdx == thingIdx) {
                if (g_thingTaskMap[i].glTexture)
                    glDeleteTextures(1, &g_thingTaskMap[i].glTexture);
                if (g_thingTaskMap[i].marqueeGlTexture)
                    glDeleteTextures(1, &g_thingTaskMap[i].marqueeGlTexture);
                for (int j = i; j < g_thingTaskCount - 1; j++)
                    g_thingTaskMap[j] = g_thingTaskMap[j + 1];
                g_thingTaskCount--;
                break;
            }
        }
        if (g_spawnPreviewThing) {
            sithThing* thing = (sithThing*)g_spawnPreviewThing;
            if (thing->type != SITH_THING_FREE)
                sithThing_Destroy(thing);
        }
        stdPlatform_Printf("AACoreManager: Spawn cancelled\n");
    }

    /* Exit spawn/move mode */
    if (g_fn_exit_spawn_mode)
        g_fn_exit_spawn_mode();
    g_spawnModeActive = 0;
    g_spawnModeIsMove = 0;
    g_spawnPreviewThingIdx = -1;
    g_spawnPreviewThing = NULL;
    if (g_fn_set_spawn_preview_thing)
        g_fn_set_spawn_preview_thing(-1);
}

int AACoreManager_GetAimedThingIdx(void)
{
    return g_aimedAtThingIdx;
}

void AACoreManager_OpenTabMenuToTab(int tabIndex)
{
    if (g_fn_open_tab_menu_to_tab)
        g_fn_open_tab_menu_to_tab(tabIndex);
}

void AACoreManager_ToggleBuildContextMenu(void)
{
    if (g_fn_toggle_build_context_menu)
        g_fn_toggle_build_context_menu();
}

bool AACoreManager_IsFullscreenActive(void)
{
    if (g_fn_is_fullscreen_active)
        return g_fn_is_fullscreen_active();
    return false;
}

void AACoreManager_ExitFullscreen(void)
{
    if (g_fn_exit_fullscreen)
        g_fn_exit_fullscreen();
}

/* Host callback: DLL queries current map key (e.g., "smhq.gob-01nf.jkl") */
static void host_get_current_map(char* mapKeyOut, int mapKeySize)
{
    if (sithWorld_pCurrentWorld && mapKeyOut && mapKeySize > 0) {
        snprintf(mapKeyOut, mapKeySize, "%s.gob-%s",
                sithWorld_pCurrentWorld->episodeName,
                sithWorld_pCurrentWorld->map_jkl_fname);
    }
}

void AACoreManager_PreRenderThing(void* pSithThing)
{
    if (!g_dynscreenMaterial || g_thingTaskCount == 0) return;

    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].thing == pSithThing) {
            /* Flush pending faces so previous thing's faces draw with previous texture_id */
            rdCache_Flush();

            /* Try to use the task's rendered texture (browser content) */
            GLuint texToUse = g_thingTaskMap[i].glTexture; /* fallback: solid color */
            if (g_fn_get_thing_task_index) {
                int taskIdx = g_fn_get_thing_task_index(g_thingTaskMap[i].thingIdx);
                if (taskIdx >= 0 && taskIdx < MAX_TASKS && g_taskTextures[taskIdx]) {
                    texToUse = g_taskTextures[taskIdx];
                }
            }

            g_originalTextures[0].alphaMats[0].texture_id = texToUse;
            g_originalTextures[0].alphaMats[0].texture_loaded = 1;
            g_originalTextures[0].opaqueMats[0].texture_id = texToUse;
            g_originalTextures[0].opaqueMats[0].texture_loaded = 1;

            /* Also swap DynMarquee if available */
            if (g_dynmarqueeMaterial && g_origMarqueeTextures) {
                GLuint marqueeTex = g_thingTaskMap[i].marqueeGlTexture;
                g_origMarqueeTextures[0].alphaMats[0].texture_id = marqueeTex;
                g_origMarqueeTextures[0].alphaMats[0].texture_loaded = 1;
                g_origMarqueeTextures[0].alphaMats[0].is_16bit = 1;
                g_origMarqueeTextures[0].opaqueMats[0].texture_id = marqueeTex;
                g_origMarqueeTextures[0].opaqueMats[0].texture_loaded = 1;
                g_origMarqueeTextures[0].opaqueMats[0].is_16bit = 1;
            }
            return;
        }
    }
}

void AACoreManager_PostRenderThing(void* pSithThing)
{
    /* No-op — next PreRenderThing will flush before overwriting the swap */
}

void AACoreManager_ToggleMainMenu(void)
{
    if (g_fn_toggle_main_menu)
        g_fn_toggle_main_menu();
}

bool AACoreManager_IsMainMenuOpen(void)
{
    if (g_fn_is_main_menu_open)
        return g_fn_is_main_menu_open();
    return false;
}

bool AACoreManager_ShouldOpenEngineMenu(void)
{
    if (g_fn_should_open_engine_menu)
        return g_fn_should_open_engine_menu();
    return false;
}

void AACoreManager_ClearEngineMenuFlag(void)
{
    if (g_fn_clear_engine_menu_flag)
        g_fn_clear_engine_menu_flag();
}

bool AACoreManager_ShouldStartLibretro(void)
{
    if (g_fn_should_start_libretro)
        return g_fn_should_start_libretro();
    return false;
}

void AACoreManager_ClearStartLibretroFlag(void)
{
    if (g_fn_clear_start_libretro_flag)
        g_fn_clear_start_libretro_flag();
}

void AACoreManager_StartLibretro(void)
{
    if (g_fn_start_libretro)
        g_fn_start_libretro();
}

void AACoreManager_DrawOverlay(int screenWidth, int screenHeight)
{
    {
        int menuOpen = g_fn_is_main_menu_open && g_fn_is_main_menu_open();
        int spawnMode = g_fn_is_spawn_mode_active && g_fn_is_spawn_mode_active();
        if (!menuOpen && !spawnMode) return;
    }
    if (!g_fn_render_overlay)
        return;

    /* Allocate pixel buffer on first use */
    if (!g_overlayPixels) {
        g_overlayPixels = (uint32_t*)malloc(OVERLAY_WIDTH * OVERLAY_HEIGHT * 4);
        if (!g_overlayPixels) return;
    }

    /* Get pixels from DLL */
    if (!g_fn_render_overlay(g_overlayPixels, OVERLAY_WIDTH, OVERLAY_HEIGHT))
        return;

    /* Create GL texture on first use */
    if (!g_overlayTexture)
        glGenTextures(1, &g_overlayTexture);

    /* Upload BGRA pixels to GL texture */
    glBindTexture(GL_TEXTURE_2D, g_overlayTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OVERLAY_WIDTH, OVERLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, g_overlayPixels);

    /* Add fullscreen quad to the engine's UI render list.
     * This gets drawn by std3D_DrawUIRenderList() using the engine's shader pipeline. */
    std3D_DrawUITexturedQuad(g_overlayTexture, 0.0f, 0.0f, (float)screenWidth, (float)screenHeight);

    /* Cursor is now handled by overlay.html JS — no host-side cursor */
}
