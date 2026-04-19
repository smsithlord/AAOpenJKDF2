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
#include "../../Primitives/rdModel3.h"
#include "../../Engine/sithCollision.h"
#include "../../Gameplay/sithPlayer.h"
#include "../../Gameplay/sithInventory.h"
#include "../../Gameplay/sithTime.h"
#include "../../Primitives/rdMatrix.h"
#include "../../Primitives/rdVector.h"
#include "../../World/sithSector.h"
#include "../../Raster/rdCache.h"
#include "../../Main/jkRes.h"
#include "../../Main/jkSession.h"
#include "../../General/stdFnames.h"
#include "../../Devices/sithConsole.h"
#include "../../Devices/sithSoundMixer.h"
#include "globals.h"

#include "SDL2_helper.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

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
static aarcadecore_get_thing_screen_status_t g_fn_get_thing_screen_status = NULL;
static aarcadecore_get_thing_marquee_status_t g_fn_get_thing_marquee_status = NULL;
static aarcadecore_free_pixels_t g_fn_free_pixels = NULL;
static aarcadecore_object_used_t g_fn_object_used = NULL;
static aarcadecore_has_spawn_transform_t g_fn_has_spawn_transform = NULL;
static aarcadecore_get_spawn_transform_t g_fn_get_spawn_transform = NULL;
static aarcadecore_get_object_scale_t g_fn_get_object_scale = NULL;
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
static aarcadecore_open_create_item_t     g_fn_open_create_item = NULL;
static aarcadecore_toggle_build_context_menu_t g_fn_toggle_build_context_menu = NULL;
static aarcadecore_is_fullscreen_active_t g_fn_is_fullscreen_active = NULL;
static aarcadecore_exit_fullscreen_t g_fn_exit_fullscreen = NULL;
static aarcadecore_mark_thing_seen_t g_fn_mark_thing_seen = NULL;
static aarcadecore_is_task_visible_t g_fn_is_task_visible = NULL;
static aarcadecore_register_adopted_template_t g_fn_register_adopted_template = NULL;
static aarcadecore_action_command_t g_fn_action_command = NULL;
static aarcadecore_deep_sleep_requested_t g_fn_deep_sleep_requested = NULL;
static aarcadecore_deep_sleep_changed_t   g_fn_deep_sleep_changed = NULL;
static aarcadecore_exit_game_requested_t  g_fn_exit_game_requested = NULL;
static int g_deepSleep = 0;

/* Forward declarations */
static void host_get_current_map(char* mapKeyOut, int mapKeySize);
static int  host_is_template_cabinet(const char* templateName);
static int  host_capture_rect_pixels(int x, int y, int w, int h, void** pixelsOut, int* outW, int* outH);
static void host_reset_thing_texture(int thingIdx);

/* Cursor state (in screen coords) */
static int g_cursorX = 0;
static int g_cursorY = 0;
static GLuint g_cursorTexture = 0;

/* Per-task GL textures (host-managed) — dynamically grown, no hard cap. */
#define TASK_TEX_WIDTH 1024
#define TASK_TEX_HEIGHT 1024
static GLuint* g_taskTextures = NULL;
static int g_taskTexturesCapacity = 0;
static uint16_t* g_taskPixels = NULL; /* shared pixel buffer for task rendering */
static int g_taskTexturesReady = 0;

static void aacore_ensure_task_textures_capacity(int needed)
{
    if (needed <= g_taskTexturesCapacity) return;
    int newCap = g_taskTexturesCapacity ? g_taskTexturesCapacity : 16;
    while (newCap < needed) newCap *= 2;
    GLuint* newArr = (GLuint*)realloc(g_taskTextures, newCap * sizeof(GLuint));
    if (!newArr) return;
    for (int i = g_taskTexturesCapacity; i < newCap; i++) newArr[i] = 0;
    g_taskTextures = newArr;
    g_taskTexturesCapacity = newCap;
}

/* Thing-to-task mapping — each tracked thing gets a unique GL texture.
 * Dynamically grown: no cap on cabinet count. */
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
static ThingTaskMapping* g_thingTaskMap = NULL;
static int g_thingTaskCapacity = 0;
static int g_thingTaskCount = 0;

static void aacore_ensure_thing_task_capacity(int needed)
{
    if (needed <= g_thingTaskCapacity) return;
    int newCap = g_thingTaskCapacity ? g_thingTaskCapacity : 16;
    while (newCap < needed) newCap *= 2;
    ThingTaskMapping* newArr = (ThingTaskMapping*)realloc(g_thingTaskMap, newCap * sizeof(ThingTaskMapping));
    if (!newArr) return;
    for (int i = g_thingTaskCapacity; i < newCap; i++) {
        ThingTaskMapping empty = {0};
        newArr[i] = empty;
    }
    g_thingTaskMap = newArr;
    g_thingTaskCapacity = newCap;
}

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

/* Patch all faces on a thing's model that use the dynscreen material to
 * fullbright (NOTLIT) so active cabinet screens aren't darkened by lighting. */
static void aacore_patch_dynscreen_faces(sithThing* thing)
{
    if (!thing || thing->rdthing.type != RD_THINGTYPE_MODEL || !thing->rdthing.model3)
        return;

    rdModel3* model = thing->rdthing.model3;
    for (unsigned int g = 0; g < model->numGeosets; g++) {
        rdGeoset* geoset = &model->geosets[g];
        for (unsigned int mi = 0; mi < geoset->numMeshes; mi++) {
            rdMesh* mesh = &geoset->meshes[mi];
            for (int fi = 0; fi < mesh->numFaces; fi++) {
                rdMaterial* faceMat = mesh->faces[fi].material;
                if (faceMat && strstr(faceMat->mat_fpath, "dynscreen")
                    && mesh->faces[fi].lightingMode != RD_LIGHTMODE_FULLYLIT) {
                    mesh->faces[fi].lightingMode = RD_LIGHTMODE_FULLYLIT;
                    stdPlatform_Printf("AACoreManager: Patched face %d on mesh %d to NOTLIT (model=%s)\n",
                                      fi, mi, model->filename);
                }
            }
        }
    }
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
    LOAD_FN(get_thing_screen_status)
    LOAD_FN(get_thing_marquee_status)
    LOAD_FN(free_pixels)
    LOAD_FN(object_used)
    LOAD_FN(has_spawn_transform)
    LOAD_FN(get_spawn_transform)
    LOAD_FN(get_object_scale)
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
    LOAD_FN(open_create_item)
    LOAD_FN(toggle_build_context_menu)
    LOAD_FN(is_fullscreen_active)
    LOAD_FN(exit_fullscreen)
    LOAD_FN(mark_thing_seen)
    LOAD_FN(is_task_visible)
    LOAD_FN(register_adopted_template)
    LOAD_FN(action_command)
    LOAD_FN(deep_sleep_requested)
    LOAD_FN(deep_sleep_changed)
    LOAD_FN(exit_game_requested)
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
    callbacks.is_template_cabinet = host_is_template_cabinet;
    callbacks.capture_rect_pixels = host_capture_rect_pixels;
    callbacks.reset_thing_texture = host_reset_thing_texture;

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
    g_fn_get_thing_screen_status = NULL;
    g_fn_get_thing_marquee_status = NULL;
    g_fn_free_pixels = NULL;
    g_fn_object_used = NULL;
    g_fn_has_spawn_transform = NULL;
    g_fn_get_spawn_transform = NULL;
    g_fn_get_object_scale = NULL;
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
    g_fn_open_create_item = NULL;
    g_fn_toggle_build_context_menu = NULL;
    g_fn_is_fullscreen_active = NULL;
    g_fn_exit_fullscreen = NULL;
    g_fn_mark_thing_seen = NULL;
    g_fn_is_task_visible = NULL;
    g_fn_register_adopted_template = NULL;
    g_fn_action_command = NULL;
    g_fn_deep_sleep_requested = NULL;
    g_fn_deep_sleep_changed = NULL;
    g_fn_exit_game_requested = NULL;
    g_deepSleep = 0;

    for (int i = 0; i < g_taskTexturesCapacity; i++) {
        if (g_taskTextures[i]) { glDeleteTextures(1, &g_taskTextures[i]); g_taskTextures[i] = 0; }
    }
    free(g_taskTextures);
    g_taskTextures = NULL;
    g_taskTexturesCapacity = 0;
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
    free(g_thingTaskMap);
    g_thingTaskMap = NULL;
    g_thingTaskCapacity = 0;
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

    /* Raycast forward from eye position */
    rdVector3 eyePos = player->position;
    rdVector_Add3Acc(&eyePos, &player->actorParams.eyeOffset);
    sithSector* eyeSector = sithCollision_GetSectorLookAt(
        player->sector, &player->position, &eyePos, 0.0f);
    if (!eyeSector) eyeSector = player->sector;
    sithCollision_SearchRadiusForThings(eyeSector, player, &eyePos,
                                        &lookDir, 10.0f, 0.0f, 0);
    sithCollisionSearchEntry* searchResult = sithCollision_NextSearchResult();
    sithSector* spawnRaySector = eyeSector;
    /* Track sector through adjoins to find first solid hit */
    while (searchResult) {
        if (searchResult->hitType & SITHCOLLISION_ADJOINCROSS) {
            if (searchResult->surface && searchResult->surface->adjoin)
                spawnRaySector = searchResult->surface->adjoin->sector;
            searchResult = sithCollision_NextSearchResult();
            continue;
        }
        if (searchResult->surface || (searchResult->hitType & SITHCOLLISION_THING))
            break;
        searchResult = sithCollision_NextSearchResult();
    }
    if (!searchResult) {
        sithCollision_SearchClose();
        stdPlatform_Printf("AACoreManager: No surface hit for spawn\n");
        return NULL;
    }

    rdVector3 hitPos, hitNorm;
    hitNorm = searchResult->hitNorm;
    float dist = searchResult->distance;
    sithSector* hitSector = spawnRaySector;
    sithCollision_SearchClose();

    /* Hit position = eye + lookDir * distance */
    hitPos.x = eyePos.x + lookDir.x * dist;
    hitPos.y = eyePos.y + lookDir.y * dist;
    hitPos.z = eyePos.z + lookDir.z * dist;

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

static int g_wasFullscreen = 0;

static void AACoreManager_EnterDeepSleep(void)
{
    if (g_deepSleep) return;
    if (g_fn_deep_sleep_changed)
        g_fn_deep_sleep_changed(1);
    g_deepSleep = 1;
    sithTime_Pause();
    sithSoundMixer_StopAll();
    if (g_audio_dev > 0)
        SDL_PauseAudioDevice(g_audio_dev, 1);
    stdPlatform_Printf("AACoreManager: Entering deep sleep\n");
}

void AACoreManager_ExitDeepSleep(void)
{
    if (!g_deepSleep) return;
    g_deepSleep = 0;
    sithTime_Resume();
    sithSoundMixer_ResumeAll();
    if (g_audio_dev > 0)
        SDL_PauseAudioDevice(g_audio_dev, 0);
    if (g_fn_deep_sleep_changed)
        g_fn_deep_sleep_changed(0);
    stdPlatform_Printf("AACoreManager: Waking from deep sleep\n");
}

int AACoreManager_IsDeepSleeping(void)
{
    return g_deepSleep;
}

void AACoreManager_Update(void)
{
    if (g_fn_update)
        g_fn_update();

    /* Poll deep sleep request (fire & forget — DLL auto-clears after returning true) */
    if (!g_deepSleep && g_fn_deep_sleep_requested && g_fn_deep_sleep_requested()) {
        AACoreManager_EnterDeepSleep();
    }

    /* Poll exit-game request from the AArcade Main Menu (fire & forget).
     * Save the current session first so -resumeLast can restore the player's
     * position; the engine's normal exit paths only persist on level transitions.
     * Set g_should_exit so the main loop breaks cleanly and Main_Shutdown()
     * runs — calling jk_exit()/exit() mid-frame deadlocks during DLL teardown. */
    if (g_fn_exit_game_requested && g_fn_exit_game_requested()) {
        jkSession_SaveCurrent();
        g_should_exit = 1;
    }
    if (g_deepSleep) return;

    /* Pause/resume engine when entering/exiting fullscreen */
    {
        int isFullscreen = g_fn_is_fullscreen_active && g_fn_is_fullscreen_active();
        if (isFullscreen && !g_wasFullscreen) {
            sithTime_Pause();
        } else if (!isFullscreen && g_wasFullscreen) {
            sithTime_Resume();
        }
        g_wasFullscreen = isFullscreen;
    }

    /* Lazy audio device init — open when a Libretro instance starts producing audio */
    if (g_audio_dev == 0 && g_fn_get_audio_sample_rate) {
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
                stdPlatform_Printf("AACoreManager: Audio lazily initialized - %d Hz\n", have.freq);
            }
        }
    }

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

            /* Compute eye position + sector (matching engine USE behavior) */
            rdVector3 eyePos = player->position;
            rdVector_Add3Acc(&eyePos, &player->actorParams.eyeOffset);
            sithSector* eyeSector = sithCollision_GetSectorLookAt(
                player->sector, &player->position, &eyePos, 0.0f);
            if (!eyeSector) eyeSector = player->sector;

            sithCollision_SearchRadiusForThings(eyeSector, player,
                &eyePos, &lookDir, 50.0f, 0.0f, 0);
                /* 0.0f = pixel-accurate; 0.025f is easier to select things but causes inaccurate placement against walls */
            sithCollisionSearchEntry* hit = sithCollision_NextSearchResult();
            sithSector* raySector = eyeSector;
            while (hit) {
                /* Track sector as ray traverses adjoins */
                if (hit->hitType & SITHCOLLISION_ADJOINCROSS) {
                    if (hit->surface && hit->surface->adjoin)
                        raySector = hit->surface->adjoin->sector;
                    hit = sithCollision_NextSearchResult();
                    continue;
                }
                /* Store first solid hit for spawn mode (world surface or thing) */
                if (!g_selectorRayHasHit &&
                    (hit->surface || ((hit->hitType & SITHCOLLISION_THING) && hit->receiver))) {
                    g_selectorRayHasHit = 1;
                    g_selectorRayHitNorm = hit->hitNorm;
                    g_selectorRayHitPos.x = eyePos.x + lookDir.x * hit->distance;
                    g_selectorRayHitPos.y = eyePos.y + lookDir.y * hit->distance;
                    g_selectorRayHitPos.z = eyePos.z + lookDir.z * hit->distance;
                    g_selectorRayHitSector = raySector;
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

            /* Second ray with larger radius solely for aimed-at detection */
            if (g_thingTaskCount > 0) {
                newAimedAt = -1; /* Reset — second ray is authoritative for aimed-at */
                sithCollision_SearchRadiusForThings(eyeSector, player,
                    &eyePos, &lookDir, 50.0f, 0.025f, 0);
                sithCollisionSearchEntry* hit2 = sithCollision_NextSearchResult();
                while (hit2) {
                    if ((hit2->hitType & SITHCOLLISION_THING) && hit2->receiver) {
                        int idx2 = hit2->receiver->thingIdx;
                        for (int i = 0; i < g_thingTaskCount; i++) {
                            if (g_thingTaskMap[i].thingIdx == idx2) {
                                newAimedAt = idx2;
                                break;
                            }
                        }
                        if (newAimedAt >= 0) break;
                    }
                    hit2 = sithCollision_NextSearchResult();
                }
                sithCollision_SearchClose();
            }
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
        aacore_ensure_task_textures_capacity(count);

        if (!g_taskPixels)
            g_taskPixels = (uint16_t*)malloc(TASK_TEX_WIDTH * TASK_TEX_HEIGHT * 2);

        for (int i = 0; i < count; i++) {
            /* Skip render+upload for tasks not visible on screen */
            if (g_fn_is_task_visible && !g_fn_is_task_visible(i))
                continue;

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
            /* Build orientation from surface normal */
            rdVector3 uvec, lvec, rvec;
            if (fabsf(g_selectorRayHitNorm.z) < 0.1f) {
                /* Vertical wall: upright with yaw facing outward from wall */
                uvec.x = 0.0f; uvec.y = 0.0f; uvec.z = 1.0f;
                lvec.x = -g_selectorRayHitNorm.x;
                lvec.y = -g_selectorRayHitNorm.y;
                lvec.z = -g_selectorRayHitNorm.z;
                rdVector_Cross3(&rvec, &lvec, &uvec);
                rdVector_Normalize3Acc(&rvec);
                rdVector_Cross3(&lvec, &uvec, &rvec);
                rdVector_Normalize3Acc(&lvec);
            } else {
                /* Non-vertical: up = surface normal, facing away from player */
                uvec = g_selectorRayHitNorm;
                rdVector3 toPlayer;
                rdVector_Sub3(&toPlayer, &g_selectorRayHitPos, &player->position);
                float dot = rdVector_Dot3(&toPlayer, &uvec);
                toPlayer.x -= dot * uvec.x;
                toPlayer.y -= dot * uvec.y;
                toPlayer.z -= dot * uvec.z;
                rdVector_Normalize3Acc(&toPlayer);

                if (rdVector_Len3(&toPlayer) < 0.001f) {
                    lvec.x = 1.0f; lvec.y = 0.0f; lvec.z = 0.0f;
                } else {
                    lvec = toPlayer;
                }
                rdVector_Cross3(&rvec, &lvec, &uvec);
                rdVector_Normalize3Acc(&rvec);
                rdVector_Cross3(&lvec, &uvec, &rvec);
                rdVector_Normalize3Acc(&lvec);
            }

            rdMatrix34 orient;
            orient.rvec = rvec;
            orient.lvec = lvec;
            orient.uvec = uvec;
            rdVector_Zero3(&orient.scale);

            rdVector3 hitPos = g_selectorRayHitPos;
            sithSector* finalSector = g_selectorRayHitSector;

            /* Apply transform overrides from spawn mode panel */
            if (g_fn_has_spawn_transform && g_fn_has_spawn_transform()) {
                float rp, ry, rr, ox, oy, oz, spawnScale;
                bool isWorldRot = false, isWorldOff = false, useRaycast = false;
                g_fn_get_spawn_transform(&rp, &ry, &rr, &isWorldRot, &ox, &oy, &oz, &isWorldOff, &useRaycast, &spawnScale);

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
                                &player->position, &rayDir, rayLen + 0.1f, 0.0f, 0);
                            sithCollisionSearchEntry* hit2 = sithCollision_NextSearchResult();
                            int foundHit = 0;
                            rdVector3 secHitNorm = {0};
                            sithSector* raySector2 = player->sector;
                            while (hit2) {
                                /* Track sector as ray traverses adjoins */
                                if (hit2->hitType & SITHCOLLISION_ADJOINCROSS) {
                                    if (hit2->surface && hit2->surface->adjoin)
                                        raySector2 = hit2->surface->adjoin->sector;
                                    hit2 = sithCollision_NextSearchResult();
                                    continue;
                                }
                                /* Hit a solid surface or thing before reaching target */
                                if (hit2->distance < rayLen) {
                                    hitPos.x = player->position.x + rayDir.x * hit2->distance;
                                    hitPos.y = player->position.y + rayDir.y * hit2->distance;
                                    hitPos.z = player->position.z + rayDir.z * hit2->distance;
                                }
                                finalSector = raySector2;
                                secHitNorm = hit2->hitNorm;
                                foundHit = 1;
                                break;
                            }
                            sithCollision_SearchClose();
                            if (!foundHit && sithWorld_pCurrentWorld) {
                                sithSector* found = sithSector_sub_4F8D00(sithWorld_pCurrentWorld, &hitPos);
                                if (found) finalSector = found;
                            }
                            /* Rebuild orientation from secondary hit normal */
                            if (foundHit) {
                                if (fabsf(secHitNorm.z) < 0.1f) {
                                    /* Vertical wall */
                                    orient.uvec.x = 0.0f; orient.uvec.y = 0.0f; orient.uvec.z = 1.0f;
                                    orient.lvec.x = -secHitNorm.x;
                                    orient.lvec.y = -secHitNorm.y;
                                    orient.lvec.z = -secHitNorm.z;
                                    rdVector_Cross3(&orient.rvec, &orient.lvec, &orient.uvec);
                                    rdVector_Normalize3Acc(&orient.rvec);
                                    rdVector_Cross3(&orient.lvec, &orient.uvec, &orient.rvec);
                                    rdVector_Normalize3Acc(&orient.lvec);
                                } else {
                                    /* Non-vertical: up = hit normal, face away from player */
                                    orient.uvec = secHitNorm;
                                    rdVector3 toP;
                                    rdVector_Sub3(&toP, &hitPos, &player->position);
                                    float d2 = rdVector_Dot3(&toP, &orient.uvec);
                                    toP.x -= d2 * orient.uvec.x;
                                    toP.y -= d2 * orient.uvec.y;
                                    toP.z -= d2 * orient.uvec.z;
                                    rdVector_Normalize3Acc(&toP);
                                    if (rdVector_Len3(&toP) < 0.001f) {
                                        orient.lvec.x = 1.0f; orient.lvec.y = 0.0f; orient.lvec.z = 0.0f;
                                    } else {
                                        orient.lvec = toP;
                                    }
                                    rdVector_Cross3(&orient.rvec, &orient.lvec, &orient.uvec);
                                    rdVector_Normalize3Acc(&orient.rvec);
                                    rdVector_Cross3(&orient.lvec, &orient.uvec, &orient.rvec);
                                    rdVector_Normalize3Acc(&orient.lvec);
                                }
                                rdVector_Zero3(&orient.scale);
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

            /* Ask the DLL if the fallback chain has definitively failed; if so, stop
             * polling and mark failed so PreRenderThing can render the model's
             * original (non-dynamic) material. */
            if (g_fn_get_thing_screen_status) {
                int st = g_fn_get_thing_screen_status(g_thingTaskMap[i].thingIdx);
                if (st == -1) {
                    g_thingTaskMap[i].imageLoaded = -1;
                    stdPlatform_Printf("AACoreManager: Screen image failed for thingIdx=%d — using original material\n",
                                      g_thingTaskMap[i].thingIdx);
                    continue;
                }
            }

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

            if (g_fn_get_thing_marquee_status) {
                int st = g_fn_get_thing_marquee_status(g_thingTaskMap[i].thingIdx);
                if (st == -1) {
                    g_thingTaskMap[i].marqueeImageLoaded = -1;
                    stdPlatform_Printf("AACoreManager: Marquee image failed for thingIdx=%d — using original material\n",
                                      g_thingTaskMap[i].thingIdx);
                    continue;
                }
            }

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

void AACoreManager_SetSuppressFire(int suppress) { (void)suppress; }

bool AACoreManager_IsSuppressingFire(void)
{
    /* Compute live to avoid frame-timing bug: sithControl reads this flag
     * before aaMainMenu_Update gets a chance to update it. */
    if (!sithPlayer_pLocalPlayerThing || !sithPlayer_pLocalPlayerThing->actorParams.playerinfo)
        return false;
    bool fistsOut = (sithInventory_GetCurWeapon(sithPlayer_pLocalPlayerThing) == SITHBIN_FISTS);
    bool alive = (sithPlayer_pLocalPlayerThing->actorParams.health > 0.0);
    return fistsOut && alive;
}

bool AACoreManager_AreFistsOut(void)
{
    if (sithPlayer_pLocalPlayerThing && sithPlayer_pLocalPlayerThing->actorParams.playerinfo)
        return sithInventory_GetCurWeapon(sithPlayer_pLocalPlayerThing) == SITHBIN_FISTS;
    return false;
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
    aacore_ensure_thing_task_capacity(g_thingTaskCount + 1);
    if (g_thingTaskCount >= g_thingTaskCapacity) return; /* realloc failed */

    sithThing* thing = (sithThing*)pSithThing;

    /* Find and cache the dynscreen material on first call */
    rdMaterial* mat = aacore_find_dynscreen_material(thing);
    if (!mat) {
        stdPlatform_Printf("AACoreManager: WARNING: No dynscreen material found on thing %d\n", thingIdx);
        return;
    }

    /* Ensure dynscreen faces on this thing's model are fullbright */
    aacore_patch_dynscreen_faces(thing);

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
    if (g_thingTaskMap && g_thingTaskCapacity > 0)
        memset(g_thingTaskMap, 0, g_thingTaskCapacity * sizeof(ThingTaskMapping));
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
        if (g_spawnPreviewThing && g_moveOrigTemplate[0]) {
            sithThing* thing = (sithThing*)g_spawnPreviewThing;

            /* Check if model changed during move */
            const char* curTemplate = g_fn_get_template_for_thing
                ? g_fn_get_template_for_thing(thingIdx) : NULL;
            int templateChanged = !curTemplate || strcmp(curTemplate, g_moveOrigTemplate) != 0;

            if (!templateChanged) {
                /* Same model: just restore position/orientation/sector */
                sithThing_SetPosAndRot(thing, &g_moveOrigPos, &g_moveOrigOrient);
                if (g_moveOrigSector && thing->sector != g_moveOrigSector)
                    sithThing_MoveToSector(thing, g_moveOrigSector, 0);
                thing->collide = g_spawnOrigCollide;
                stdPlatform_Printf("AACoreManager: Move cancelled, position restored (thingIdx=%d)\n", thingIdx);
            } else {
                /* Model changed: destroy+recreate with original template */
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

void AACoreManager_OpenCreateItemWithFile(const char* file)
{
    if (g_fn_open_create_item)
        g_fn_open_create_item(file);
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

bool AACoreManager_OnWeaponSlotPressed(int slot)
{
    if (!g_fn_action_command) return false;

    switch (slot) {
        case 0: return g_fn_action_command("TaskClose");
        case 2: return g_fn_action_command("ObjectRemove");
        case 3: return g_fn_action_command("ObjectClone");
        case 4: return g_fn_action_command("ObjectMove");
        case 5: return AACoreManager_AdoptAimedModel();
        default: return false;
    }
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

/* Check if a template's 3DO model uses dynamic screen/marquee materials */
static int host_is_template_cabinet(const char* templateName)
{
    if (!templateName) return 0;
    sithThing* tmpl = sithTemplate_GetEntryByName(templateName);
    if (!tmpl) return 0;
    rdModel3* model = tmpl->rdthing.model3;
    if (!model) return 0;
    for (uint32_t i = 0; i < model->numMaterials; i++) {
        if (!model->materials[i]) continue;
        const char* matName = model->materials[i]->mat_fpath;
        if (!matName) continue;
        if (strstr(matName, "dynscreen") || strstr(matName, "dynmarquee"))
            return 1;
    }
    return 0;
}

/* Capture a rect of game framebuffer pixels via glReadPixels.
 * Input coords are in overlay space (1920x1080), scaled to actual framebuffer size. */
static int host_capture_rect_pixels(int x, int y, int w, int h, void** pixelsOut, int* outW, int* outH)
{
    if (!pixelsOut || !outW || !outH || w <= 0 || h <= 0) return 0;

    /* Scale from overlay coords (1920x1080) to actual framebuffer size */
    int fbW = Window_xSize;
    int fbH = Window_ySize;
    int fbX = x * fbW / 1920;
    int fbY = y * fbH / 1080;
    int fbRectW = w * fbW / 1920;
    int fbRectH = h * fbH / 1080;
    if (fbRectW <= 0 || fbRectH <= 0) return 0;

    /* Clamp to framebuffer bounds */
    if (fbX < 0) fbX = 0;
    if (fbY < 0) fbY = 0;
    if (fbX + fbRectW > fbW) fbRectW = fbW - fbX;
    if (fbY + fbRectH > fbH) fbRectH = fbH - fbY;

    uint8_t* data = (uint8_t*)malloc(fbRectW * fbRectH * 4);
    if (!data) return 0;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(fbX, fbY, fbRectW, fbRectH, GL_RGBA, GL_UNSIGNED_BYTE, data);

    /* glReadPixels returns bottom-up; flip vertically */
    int rowBytes = fbRectW * 4;
    uint8_t* row = (uint8_t*)malloc(rowBytes);
    for (int top = 0, bot = fbRectH - 1; top < bot; top++, bot--) {
        memcpy(row, data + top * rowBytes, rowBytes);
        memcpy(data + top * rowBytes, data + bot * rowBytes, rowBytes);
        memcpy(data + bot * rowBytes, row, rowBytes);
    }
    free(row);

    *pixelsOut = data;
    *outW = fbRectW;
    *outH = fbRectH;
    return 1;
}

static void host_reset_thing_texture(int thingIdx)
{
    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].thingIdx == thingIdx) {
            g_thingTaskMap[i].imageLoaded = 0;
            g_thingTaskMap[i].marqueeImageLoaded = 0;
            stdPlatform_Printf("AACoreManager: Reset texture polling for thingIdx=%d\n", thingIdx);
            return;
        }
    }
}

void AACoreManager_PreRenderThing(void* pSithThing)
{
    if (!g_dynscreenMaterial || g_thingTaskCount == 0) return;

    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].thing == pSithThing) {
            /* Mark this thing as seen for visibility tracking / throttling */
            if (g_fn_mark_thing_seen)
                g_fn_mark_thing_seen(g_thingTaskMap[i].thingIdx);

            /* Flush pending faces so previous thing's faces draw with previous texture_id */
            rdCache_Flush();

            /* Pick the screen texture:
             *   - Active embedded task (browser, libretro, video) → task texture
             *   - Otherwise → per-thing solid color texture (used for both pending
             *     and definitively-failed loads; the -1 status still matters for
             *     halting the poll loop, but both states render the same way). */
            int taskIdx = -1;
            if (g_fn_get_thing_task_index)
                taskIdx = g_fn_get_thing_task_index(g_thingTaskMap[i].thingIdx);
            bool hasTaskTex = (taskIdx >= 0 && taskIdx < g_taskTexturesCapacity && g_taskTextures[taskIdx]);

            GLuint texToUse = hasTaskTex ? g_taskTextures[taskIdx]
                                         : g_thingTaskMap[i].glTexture;

            g_originalTextures[0].alphaMats[0].texture_id = texToUse;
            g_originalTextures[0].alphaMats[0].texture_loaded = 1;
            g_originalTextures[0].opaqueMats[0].texture_id = texToUse;
            g_originalTextures[0].opaqueMats[0].texture_loaded = 1;

            /* Also swap DynMarquee if available. An embedded task only drives the
             * screen — marquees always come from the image-loader chain, so they
             * use the per-thing solid color whenever no real image is bound. */
            if (g_dynmarqueeMaterial && g_origMarqueeTextures) {
                GLuint marqueeTex = g_thingTaskMap[i].marqueeGlTexture;
                g_origMarqueeTextures[0].alphaMats[0].texture_id = marqueeTex;
                g_origMarqueeTextures[0].alphaMats[0].texture_loaded = 1;
                g_origMarqueeTextures[0].alphaMats[0].is_16bit = 1;
                g_origMarqueeTextures[0].opaqueMats[0].texture_id = marqueeTex;
                g_origMarqueeTextures[0].opaqueMats[0].texture_loaded = 1;
                g_origMarqueeTextures[0].opaqueMats[0].is_16bit = 1;
            }

            /* Apply per-object uniform scale by scaling the orientation matrix axes */
            if (g_fn_get_object_scale) {
                float scale = g_fn_get_object_scale(g_thingTaskMap[i].thingIdx);
                if (scale != 1.0f) {
                    sithThing* pThing = (sithThing*)pSithThing;
                    rdVector_Scale3Acc(&pThing->lookOrientation.rvec, scale);
                    rdVector_Scale3Acc(&pThing->lookOrientation.lvec, scale);
                    rdVector_Scale3Acc(&pThing->lookOrientation.uvec, scale);
                }
            }

            return;
        }
    }
}

void AACoreManager_PostRenderThing(void* pSithThing)
{
    if (!g_dynscreenMaterial || g_thingTaskCount == 0) return;

    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].thing == pSithThing) {
            /* Restore orientation vectors after scaled render */
            if (g_fn_get_object_scale) {
                float scale = g_fn_get_object_scale(g_thingTaskMap[i].thingIdx);
                if (scale != 1.0f) {
                    sithThing* pThing = (sithThing*)pSithThing;
                    float inv = 1.0f / scale;
                    rdVector_Scale3Acc(&pThing->lookOrientation.rvec, inv);
                    rdVector_Scale3Acc(&pThing->lookOrientation.lvec, inv);
                    rdVector_Scale3Acc(&pThing->lookOrientation.uvec, inv);
                }
            }
            return;
        }
    }
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
        int inputMode = g_fn_is_input_mode_active && g_fn_is_input_mode_active();
        int fullscreen = g_fn_is_fullscreen_active && g_fn_is_fullscreen_active();
        if (!menuOpen && !spawnMode && !inputMode && !fullscreen) return;
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

/* ========================================================================
 * Asset Adoption — extract 3DO + materials to resource folder, update addon-static.jkl
 * ======================================================================== */

/* Helper: copy a file from the GOB/resource layer to a disk path.
 * Returns 1 on success, 0 on failure or if dest already exists. */
static int AACoreManager_ExtractResourceFile(const char* srcSubpath, const char* destPath)
{
    /* Skip if destination already exists */
    struct stat st;
    if (stat(destPath, &st) == 0)
        return 1; /* already exists, treat as success */

    stdFile_t src = jkRes_FileOpen(srcSubpath, "rb");
    if (!src) {
        stdPlatform_Printf("AdoptModel: Could not open source '%s'\n", srcSubpath);
        return 0;
    }

    int fileSize = jkRes_FileSize(src);
    if (fileSize <= 0) {
        jkRes_FileClose(src);
        return 0;
    }

    void* buf = malloc(fileSize);
    if (!buf) {
        jkRes_FileClose(src);
        return 0;
    }

    size_t bytesRead = jkRes_FileRead(src, buf, fileSize);
    jkRes_FileClose(src);

    if ((int)bytesRead != fileSize) {
        free(buf);
        return 0;
    }

    FILE* dest = fopen(destPath, "wb");
    if (!dest) {
        free(buf);
        stdPlatform_Printf("AdoptModel: Could not create '%s'\n", destPath);
        return 0;
    }

    fwrite(buf, 1, fileSize, dest);
    fclose(dest);
    free(buf);
    return 1;
}

/* Helper: ensure a directory exists (simple single-level mkdir) */
static void AACoreManager_EnsureDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* Parsed entry from addon-static.jkl */
typedef struct {
    char modelFilename[64];
    char templateLine[256];
} AddonJklEntry;

#define ADDON_JKL_MAX_ENTRIES 4096

/* Read existing addon-static.jkl entries.
 * Returns number of entries read. */
static int AACoreManager_ReadAddonJkl(const char* jklPath, AddonJklEntry* models, int* modelCount,
                                       AddonJklEntry* templates, int* templateCount)
{
    *modelCount = 0;
    *templateCount = 0;

    FILE* f = fopen(jklPath, "r");
    if (!f) return 0;

    char line[512];
    int inModels = 0, inTemplates = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        /* Skip empty lines */
        if (line[0] == '\0') continue;

        if (strstr(line, "SECTION: MODELS")) {
            inModels = 1; inTemplates = 0;
            /* Read and skip the "world models N" line */
            if (fgets(line, sizeof(line), f)) {} /* skip header */
            continue;
        }
        if (strstr(line, "SECTION: TEMPLATES")) {
            inModels = 0; inTemplates = 1;
            /* Read and skip the "world templates N" line */
            if (fgets(line, sizeof(line), f)) {} /* skip header */
            continue;
        }

        /* Check for "end" */
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (strncmp(trimmed, "end", 3) == 0 && (trimmed[3] == '\0' || trimmed[3] == ' ' || trimmed[3] == '\t' || trimmed[3] == '\r')) {
            inModels = 0;
            inTemplates = 0;
            continue;
        }

        if (inModels && *modelCount < ADDON_JKL_MAX_ENTRIES) {
            /* Format: "0: filename.3do" — extract filename after ": " */
            char* colon = strchr(trimmed, ':');
            if (colon) {
                char* fname = colon + 1;
                while (*fname == ' ' || *fname == '\t') fname++;
                strncpy(models[*modelCount].modelFilename, fname, 63);
                models[*modelCount].modelFilename[63] = '\0';
                (*modelCount)++;
            }
        }

        if (inTemplates && *templateCount < ADDON_JKL_MAX_ENTRIES) {
            strncpy(templates[*templateCount].templateLine, trimmed, 255);
            templates[*templateCount].templateLine[255] = '\0';
            /* Extract model filename from template line (model3d=xxx.3do) */
            char* model3d = strstr(trimmed, "model3d=");
            if (model3d) {
                model3d += 8;
                char* end = model3d;
                while (*end && *end != ' ' && *end != '\t') end++;
                int len = (int)(end - model3d);
                if (len > 63) len = 63;
                strncpy(templates[*templateCount].modelFilename, model3d, len);
                templates[*templateCount].modelFilename[len] = '\0';
            }
            (*templateCount)++;
        }
    }

    fclose(f);
    return 1;
}

/* Write addon-static.jkl with the given entries */
static int AACoreManager_WriteAddonJkl(const char* jklPath, AddonJklEntry* models, int modelCount,
                                        AddonJklEntry* templates, int templateCount)
{
    FILE* f = fopen(jklPath, "w");
    if (!f) {
        stdPlatform_Printf("AdoptModel: Could not write '%s'\n", jklPath);
        return 0;
    }

    fprintf(f, "SECTION: MODELS\n");
    fprintf(f, "world models %d\n", modelCount);
    for (int i = 0; i < modelCount; i++) {
        fprintf(f, "%d:\t%s\n", i, models[i].modelFilename);
    }
    fprintf(f, "end\n");
    fprintf(f, "\n");
    fprintf(f, "SECTION: TEMPLATES\n");
    fprintf(f, "world templates %d\n", templateCount);
    for (int i = 0; i < templateCount; i++) {
        fprintf(f, "%s\n", templates[i].templateLine);
    }
    fprintf(f, "end\n");

    fclose(f);
    return 1;
}

/* Main adoption function — called from host when player aims at a thing and presses the adopt key */
bool AACoreManager_AdoptAimedModel(void)
{
    /* Raycast to find any non-managed sithThing the player is looking at */
    sithThing* player = sithPlayer_pLocalPlayerThing;
    if (!player || !player->sector) {
        sithConsole_Print("AdoptModel: No player.");
        return false;
    }

    rdMatrix34 aimMatrix;
    _memcpy(&aimMatrix, &player->lookOrientation, sizeof(aimMatrix));
    rdMatrix_PreRotate34(&aimMatrix, &player->actorParams.eyePYR);
    rdVector3 lookDir = aimMatrix.lvec;

    rdVector3 eyePos = player->position;
    rdVector_Add3Acc(&eyePos, &player->actorParams.eyeOffset);
    sithSector* eyeSector = sithCollision_GetSectorLookAt(
        player->sector, &player->position, &eyePos, 0.0f);
    if (!eyeSector) eyeSector = player->sector;

    sithThing* thing = NULL;
    sithCollision_SearchRadiusForThings(eyeSector, player,
        &eyePos, &lookDir, 50.0f, 0.025f, 0);
    sithCollisionSearchEntry* hit = sithCollision_NextSearchResult();
    while (hit) {
        if ((hit->hitType & SITHCOLLISION_THING) && hit->receiver) {
            sithThing* candidate = hit->receiver;
            /* Skip things managed by Anarchy Manager */
            int isManaged = 0;
            for (int i = 0; i < g_thingTaskCount; i++) {
                if (g_thingTaskMap[i].thingIdx == candidate->thingIdx) {
                    isManaged = 1;
                    break;
                }
            }
            if (!isManaged && candidate->rdthing.model3) {
                thing = candidate;
                break;
            }
        }
        hit = sithCollision_NextSearchResult();
    }
    sithCollision_SearchClose();

    if (!thing) {
        sithConsole_Print("AdoptModel: No 3DO object in crosshair.");
        return false;
    }

    /* Get the 3DO model */
    rdModel3* model = thing->rdthing.model3;
    const char* modelFilename = model->filename;
    float size = thing->collideSize;
    float moveSize = thing->moveSize;

    /* Use model radius as fallback if sizes are zero */
    if (size <= 0.0f) size = model->radius;
    if (moveSize <= 0.0f) moveSize = model->radius;

    /* === Pre-check: build template name and check for duplicates before extracting === */

    /* Build template name: aaojk_<name_without_ext> */
    char templateName[64];
    strncpy(templateName, modelFilename, 63);
    templateName[63] = '\0';
    char* dot = strrchr(templateName, '.');
    if (dot) *dot = '\0';
    char fullTemplateName[80];
    snprintf(fullTemplateName, sizeof(fullTemplateName), "aaojk_%s", templateName);

    const char* addonJklPath = "resource/jkl/addon-static.jkl";

    /* Read existing entries */
    AddonJklEntry* existingModels = (AddonJklEntry*)malloc(sizeof(AddonJklEntry) * ADDON_JKL_MAX_ENTRIES);
    AddonJklEntry* existingTemplates = (AddonJklEntry*)malloc(sizeof(AddonJklEntry) * ADDON_JKL_MAX_ENTRIES);
    int existingModelCount = 0, existingTemplateCount = 0;

    if (!existingModels || !existingTemplates) {
        free(existingModels);
        free(existingTemplates);
        sithConsole_Print("AdoptModel: Memory allocation failed.");
        return false;
    }

    AACoreManager_ReadAddonJkl(addonJklPath, existingModels, &existingModelCount,
                                existingTemplates, &existingTemplateCount);

    /* Check for duplicate model filename */
    for (int i = 0; i < existingModelCount; i++) {
        if (strcmp(existingModels[i].modelFilename, modelFilename) == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Warning: '%s' already adopted.", modelFilename);
            sithConsole_Print(msg);
            free(existingModels);
            free(existingTemplates);
            return false;
        }
    }

    /* Check for duplicate template name */
    for (int i = 0; i < existingTemplateCount; i++) {
        /* Template line starts with the template name */
        char existingTmpl[128];
        if (sscanf(existingTemplates[i].templateLine, "%127s", existingTmpl) == 1) {
            if (strcmp(existingTmpl, fullTemplateName) == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Warning: template '%s' already exists.", fullTemplateName);
                sithConsole_Print(msg);
                free(existingModels);
                free(existingTemplates);
                return false;
            }
        }
    }

    stdPlatform_Printf("AdoptModel: Adopting '%s' (size=%f, movesize=%f)\n", modelFilename, size, moveSize);

    /* === Step 1: Extract .3do file to resource/3do/ === */
    AACoreManager_EnsureDir("resource");
    AACoreManager_EnsureDir("resource/3do");
    AACoreManager_EnsureDir("resource/mat");
    AACoreManager_EnsureDir("resource/jkl");

    char srcPath[256], destPath[256];
    snprintf(srcPath, sizeof(srcPath), "3do/%s", modelFilename);
    snprintf(destPath, sizeof(destPath), "resource/3do/%s", modelFilename);
    AACoreManager_ExtractResourceFile(srcPath, destPath);

    /* === Step 2: Extract all referenced .mat files === */
    for (uint32_t i = 0; i < model->numMaterials; i++) {
        if (!model->materials[i]) continue;
        const char* matFilename = model->materials[i]->mat_fpath;
        if (!matFilename || !matFilename[0]) continue;

        snprintf(srcPath, sizeof(srcPath), "mat/%s", matFilename);
        snprintf(destPath, sizeof(destPath), "resource/mat/%s", matFilename);
        AACoreManager_ExtractResourceFile(srcPath, destPath);
    }

    /* === Step 3: Update addon-static.jkl === */

    /* Add new model entry */
    if (existingModelCount < ADDON_JKL_MAX_ENTRIES) {
        strncpy(existingModels[existingModelCount].modelFilename, modelFilename, 63);
        existingModels[existingModelCount].modelFilename[63] = '\0';
        existingModelCount++;
    }

    /* Build template line */
    if (existingTemplateCount < ADDON_JKL_MAX_ENTRIES) {
        snprintf(existingTemplates[existingTemplateCount].templateLine,
                 sizeof(existingTemplates[existingTemplateCount].templateLine),
                 "%s\t_walkstruct\tsize=%f\tmovesize=%f\tmodel3d=%s",
                 fullTemplateName, size, moveSize, modelFilename);
        strncpy(existingTemplates[existingTemplateCount].modelFilename, modelFilename, 63);
        existingTemplates[existingTemplateCount].modelFilename[63] = '\0';
        existingTemplateCount++;
    }

    /* Write updated addon-static.jkl */
    AACoreManager_WriteAddonJkl(addonJklPath, existingModels, existingModelCount,
                                 existingTemplates, existingTemplateCount);

    /* Register the adopted template into the library */
    if (g_fn_register_adopted_template)
        g_fn_register_adopted_template(fullTemplateName);

    char msg[128];
    snprintf(msg, sizeof(msg), "Adopted: %s (template: %s)", modelFilename, fullTemplateName);
    sithConsole_Print(msg);

    free(existingModels);
    free(existingTemplates);
    return true;
}
