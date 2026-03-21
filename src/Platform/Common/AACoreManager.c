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
static aarcadecore_confirm_spawn_t        g_fn_confirm_spawn = NULL;

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
    GLuint glTexture;      /* per-thing GL texture (256x256 solid color) */
} ThingTaskMapping;
static ThingTaskMapping g_thingTaskMap[MAX_THING_MAPPINGS] = {0};
static int g_thingTaskCount = 0;

/* Cached compscreen material and original texture_id for restore */
static rdMaterial* g_compscreenMaterial = NULL;
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

/* Find the compscreen rdMaterial on a sithThing's model, cache it */
static rdMaterial* aacore_find_compscreen_material(sithThing* thing)
{
    if (g_compscreenMaterial) return g_compscreenMaterial;
    if (!thing || thing->rdthing.type != RD_THINGTYPE_MODEL || !thing->rdthing.model3)
        return NULL;

    rdModel3* model = thing->rdthing.model3;
    for (unsigned int m = 0; m < model->numMaterials; m++) {
        rdMaterial* mat = model->materials[m];
        if (!mat || mat->num_textures == 0) continue;
        if (strstr(mat->mat_full_fpath, "compscreen")) {
            g_compscreenMaterial = mat;
            g_originalTextures = mat->textures;
            g_origAlphaTexId = mat->textures[0].alphaMats[0].texture_id;
            g_origOpaqueTexId = mat->textures[0].opaqueMats[0].texture_id;
            stdPlatform_Printf("AACoreManager: Found compscreen material %p (%s, origAlphaTexId=%d, origOpaqueTexId=%d)\n",
                              mat, mat->mat_full_fpath, g_origAlphaTexId, g_origOpaqueTexId);
            return mat;
        }
    }
    return NULL;
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
    LOAD_FN(confirm_spawn)
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

    if (!g_fn_init(&callbacks)) {
        stdPlatform_Printf("AACoreManager: DLL init failed\n");
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
        return;
    }

    /* Dynamic texture callback disabled — using per-thing GL texture swap instead.
     * rdDynamicTexture_Register("compscreen.mat", aacore_texture_callback, NULL); */

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
    g_fn_confirm_spawn = NULL;

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
    g_compscreenMaterial = NULL;
    g_originalTextures = NULL;
    g_origAlphaTexId = -1;
    g_origOpaqueTexId = -1;

    if (g_overlayTexture) { glDeleteTextures(1, &g_overlayTexture); g_overlayTexture = 0; }
    if (g_overlayPixels) { free(g_overlayPixels); g_overlayPixels = NULL; }

    stdPlatform_Printf("AACoreManager: Shutdown complete\n");
}

/* Spawn a compscreen thing at the player's aim point (reuses jkSpawn raycast logic) */
static sithThing* aacore_spawn_at_player_aim(void)
{
    sithThing* player = sithPlayer_pLocalPlayerThing;
    if (!player) return NULL;

    sithThing* tmpl = sithTemplate_GetEntryByName("slcompmoniter");
    if (!tmpl) {
        stdPlatform_Printf("AACoreManager: Template 'slcompmoniter' not found\n");
        return NULL;
    }

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

    /* Hit position = player + lookDir * distance + normal * offset */
    hitPos.x = player->position.x + lookDir.x * dist + hitNorm.x * 0.02f;
    hitPos.y = player->position.y + lookDir.y * dist + hitNorm.y * 0.02f;
    hitPos.z = player->position.z + lookDir.z * dist + hitNorm.z * 0.02f;

    /* Build orientation: up = surface normal, forward = toward player */
    rdVector3 uvec = hitNorm;
    rdVector3 toPlayer;
    rdVector_Sub3(&toPlayer, &player->position, &hitPos);
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

    /* Small offset along normal to prevent sinking */
    hitPos.x += hitNorm.x * 0.025f;
    hitPos.y += hitNorm.y * 0.025f;
    hitPos.z += hitNorm.z * 0.025f;

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

void AACoreManager_Update(void)
{
    if (g_fn_update)
        g_fn_update();

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

    /* Process pending spawn requests from the DLL */
    if (g_fn_has_pending_spawn && g_fn_has_pending_spawn()) {
        char itemId[256] = {0};
        char url[1024] = {0};
        g_fn_pop_pending_spawn(itemId, sizeof(itemId), url, sizeof(url));

        stdPlatform_Printf("AACoreManager: Processing spawn - item=%s url=%s\n", itemId, url);

        sithThing* spawned = aacore_spawn_at_player_aim();
        if (spawned) {
            AACoreManager_RegisterThingTask(spawned, spawned->thingIdx, 0);
            if (g_fn_confirm_spawn)
                g_fn_confirm_spawn(spawned->thingIdx);
        } else {
            stdPlatform_Printf("AACoreManager: Spawn failed\n");
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

    /* Find and cache the compscreen material on first call */
    rdMaterial* mat = aacore_find_compscreen_material(thing);
    if (!mat) {
        stdPlatform_Printf("AACoreManager: WARNING: No compscreen material found on thing %d\n", thingIdx);
        return;
    }

    /* Create per-thing GL texture with unique color */
    uint16_t color = aacore_color_for_thingIdx(thingIdx);
    GLuint glTex = aacore_create_solid_texture(color);

    /* Store mapping */
    g_thingTaskMap[g_thingTaskCount].thing = pSithThing;
    g_thingTaskMap[g_thingTaskCount].thingIdx = thingIdx;
    g_thingTaskMap[g_thingTaskCount].taskIndex = taskIndex;
    g_thingTaskMap[g_thingTaskCount].glTexture = glTex;
    g_thingTaskCount++;

    stdPlatform_Printf("AACoreManager: Registered thing %p (thingIdx=%d) task %d, glTex=%u, color=0x%04X\n",
                      pSithThing, thingIdx, taskIndex, glTex, color);
}

void AACoreManager_PreRenderThing(void* pSithThing)
{
    if (!g_compscreenMaterial || g_thingTaskCount == 0) return;

    for (int i = 0; i < g_thingTaskCount; i++) {
        if (g_thingTaskMap[i].thing == pSithThing) {
            /* Flush pending faces so previous thing's faces draw with previous texture_id */
            rdCache_Flush();
            /* Overwrite the shared surface's texture_id to this thing's GL texture */
            g_originalTextures[0].alphaMats[0].texture_id = g_thingTaskMap[i].glTexture;
            g_originalTextures[0].opaqueMats[0].texture_id = g_thingTaskMap[i].glTexture;
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
    if (!g_fn_is_main_menu_open || !g_fn_is_main_menu_open())
        return;
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

    /* Draw cursor wedge */
    if (!g_cursorTexture) {
        uint32_t white = 0xFFFFFFFF;
        glGenTextures(1, &g_cursorTexture);
        glBindTexture(GL_TEXTURE_2D, g_cursorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    }
    /* Draw a small wedge/triangle as cursor using a tiny quad */
    {
        float cx = (float)g_cursorX;
        float cy = (float)g_cursorY;
        float cw = 16.0f;
        float ch = 20.0f;
        std3D_DrawUITexturedQuad(g_cursorTexture, cx, cy, cw, ch);
    }
}
