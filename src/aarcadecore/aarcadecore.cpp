/*
 * aarcadecore.cpp — DLL entry point and exported functions
 *
 * Manages a single "active instance" for in-game screen rendering,
 * plus a persistent HUD Ultralight overlay for menus/UI.
 */

#include "aarcadecore_api.h"
#include "aarcadecore_internal.h"
#include "libretro_host.h"
#include "SQLiteLibrary.h"
#include "ImageLoader.h"
#include "InstanceManager.h"
#include <string.h>
#include <steam_api.h>

/* Global host callbacks */
AACoreHostCallbacks g_host = {0};

/* Global library database */
SQLiteLibrary g_library;

/* Global image loader */
ImageLoader g_imageLoader;

/* Global instance manager */
InstanceManager g_instanceManager;

/* The active instance renders to in-game 3DO screens and receives
 * input when not in menu mode. NULL = no active screen content. */
static EmbeddedInstance* g_activeInstance = NULL;

/* When set, this instance renders fullscreen in the overlay instead of
 * on its sithThing screen. Game input is suppressed (like main menu). */
static EmbeddedInstance* g_fullscreenInstance = NULL;

/* Task list — all running embedded instances (excluding HUD overlay).
 * The host uses task indices to manage per-thing GL textures. */
#define MAX_TASKS 16
static EmbeddedInstance* g_tasks[MAX_TASKS] = {0};
static int g_taskCount = 0;

static int addTask(EmbeddedInstance* inst)
{
    if (!inst || g_taskCount >= MAX_TASKS) return -1;
    g_tasks[g_taskCount] = inst;
    return g_taskCount++;
}

/* Non-static wrapper for InstanceManager to call */
int aarcadecore_addTask(EmbeddedInstance* inst) { return addTask(inst); }

void aarcadecore_removeTask(int taskIndex) {
    if (taskIndex >= 0 && taskIndex < g_taskCount)
        g_tasks[taskIndex] = NULL;
}

/* Fullscreen instance accessors for InstanceManager */
void aarcadecore_setFullscreenInstance(EmbeddedInstance* inst) { g_fullscreenInstance = inst; }
EmbeddedInstance* aarcadecore_getFullscreenInstance(void) { return g_fullscreenInstance; }

static int getActiveTaskIndex(void)
{
    for (int i = 0; i < g_taskCount; i++) {
        if (g_tasks[i] == g_activeInstance) return i;
    }
    return -1;
}

/* Forward declarations for internal managers */
void LibretroManager_Init(void);
void LibretroManager_Shutdown(void);
void LibretroManager_Update(void);
EmbeddedInstance* LibretroManager_GetActive(void);

void SteamworksWebBrowserManager_Init(void);
void SteamworksWebBrowserManager_Shutdown(void);
void SteamworksWebBrowserManager_Update(void);
EmbeddedInstance* SteamworksWebBrowserManager_GetActive(void);

void UltralightManager_Init(void);
void UltralightManager_Shutdown(void);
void UltralightManager_Update(void);
EmbeddedInstance* UltralightManager_GetActive(void);
void UltralightManager_ToggleMainMenu(void);
bool UltralightManager_IsMainMenuOpen(void);
bool UltralightManager_ShouldOpenEngineMenu(void);
void UltralightManager_ClearEngineMenuFlag(void);
bool UltralightManager_ShouldStartLibretro(void);
void UltralightManager_ClearStartLibretroFlag(void);

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/* Get the instance that should receive input:
 * - When menu is open → HUD Ultralight (for menu interaction)
 * - Otherwise → active instance (e.g., Libretro) */
static EmbeddedInstance* get_input_target(void)
{
    /* Fullscreen embedded instance gets all input */
    if (g_fullscreenInstance) return g_fullscreenInstance;
    if (UltralightManager_IsMainMenuOpen()) {
        EmbeddedInstance* ul = UltralightManager_GetActive();
        if (ul && ul->vtable->is_active(ul)) return ul;
    }
    return g_activeInstance;
}

/* Get the LibretroHost* from the active instance (if it's Libretro) */
static LibretroHost* get_active_libretro_host(void)
{
    if (!g_activeInstance || g_activeInstance->type != EMBEDDED_LIBRETRO)
        return NULL;
    if (!g_activeInstance->vtable->is_active(g_activeInstance))
        return NULL;
    typedef struct { LibretroHost* host; } LRData;
    return ((LRData*)g_activeInstance->user_data)->host;
}

/* ========================================================================
 * Exported functions
 * ======================================================================== */

AARCADECORE_EXPORT int aarcadecore_get_api_version(void)
{
    return AARCADECORE_API_VERSION;
}

AARCADECORE_EXPORT bool aarcadecore_init(const AACoreHostCallbacks* host_callbacks)
{
    if (!host_callbacks)
        return false;

    if (host_callbacks->api_version != AARCADECORE_API_VERSION) {
        if (host_callbacks->host_printf)
            host_callbacks->host_printf("AACore: API version mismatch (host=%d, dll=%d)\n",
                                         host_callbacks->api_version, AARCADECORE_API_VERSION);
        return false;
    }

    memcpy(&g_host, host_callbacks, sizeof(g_host));

    if (g_host.host_printf)
        g_host.host_printf("AACore: Initializing (API v%d)...\n", AARCADECORE_API_VERSION);

    /* Initialize Steam API (persists for DLL lifetime) */
    extern bool SteamworksWebBrowser_InitSteamAPI(void);
    SteamworksWebBrowser_InitSteamAPI();

    /* Initialize the HUD overlay (always alive, starts with blank.html) */
    UltralightManager_Init();

    /* Open the library database and ensure schema is up to date */
    g_library.open("G:/Documents Sym Links/GitHub/aarcade-core/x64/Release/library.db");
    g_library.ensureSchema();

    /* Initialize the image loader (headless Ultralight view for thumbnail caching) */
    g_imageLoader.init();

    /* Tasks are created on demand, not at startup */
    g_taskCount = 0;
    g_activeInstance = NULL;

    if (g_host.host_printf)
        g_host.host_printf("AACore: Ready\n");

    return true;
}

AARCADECORE_EXPORT void aarcadecore_shutdown(void)
{
    if (g_host.host_printf)
        g_host.host_printf("AACore: Shutting down...\n");

    g_imageLoader.shutdown();
    g_library.close();
    LibretroManager_Shutdown();
    SteamworksWebBrowserManager_Shutdown();
    UltralightManager_Shutdown();
    g_activeInstance = NULL;

    /* Shut down Steam API last (after all browsers destroyed) */
    extern void SteamworksWebBrowser_ShutdownSteamAPI(void);
    SteamworksWebBrowser_ShutdownSteamAPI();

    memset(&g_host, 0, sizeof(g_host));
}

AARCADECORE_EXPORT void aarcadecore_update(void)
{
    /* Pump Steam callbacks globally (handles all browsers + pending removals) */
    extern bool SteamworksWebBrowser_IsSteamReady(void);
    if (SteamworksWebBrowser_IsSteamReady())
        SteamAPI_RunCallbacks();

    /* Update all running tasks — they all tick regardless of which is active */
    UltralightManager_Update();
    LibretroManager_Update();
    SteamworksWebBrowserManager_Update();
    g_imageLoader.update();

    /* Update all tasks in the task list (includes dynamically spawned browsers) */
    for (int i = 0; i < g_taskCount; i++) {
        if (g_tasks[i] && g_tasks[i]->vtable->update)
            g_tasks[i]->vtable->update(g_tasks[i]);
    }

    /* Process image loader completions (callbacks for cached/downloaded images) */
    g_imageLoader.processCompletions();

    /* Sync titles from browser instances */
    g_instanceManager.updateTitles();
}

AARCADECORE_EXPORT bool aarcadecore_is_active(void)
{
    /* "Active" = input mode on = menu or fullscreen overlay is open.
     * Having an active instance (e.g., Libretro on screens) does NOT mean input mode. */
    return g_fullscreenInstance != NULL || UltralightManager_IsMainMenuOpen();
}

AARCADECORE_EXPORT const char* aarcadecore_get_material_name(void)
{
    /* The active instance determines what renders on in-game screens */
    if (g_activeInstance && g_activeInstance->target_material)
        return g_activeInstance->target_material;
    return "DynScreen.mat"; /* default fallback */
}

AARCADECORE_EXPORT void aarcadecore_render_texture(
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    /* Render the active screen instance to in-game textures */
    if (g_activeInstance && g_activeInstance->vtable->is_active(g_activeInstance) && g_activeInstance->vtable->render) {
        g_activeInstance->vtable->render(g_activeInstance, pixelData, width, height, is16bit, bpp);
    }
}

/* ========================================================================
 * Audio (from active Libretro instance)
 * ======================================================================== */

AARCADECORE_EXPORT int aarcadecore_get_audio_sample_rate(void)
{
    LibretroHost* host = get_active_libretro_host();
    return host ? libretro_host_get_sample_rate(host) : 0;
}

AARCADECORE_EXPORT int aarcadecore_get_audio_samples(int16_t* buffer, int max_frames)
{
    LibretroHost* host = get_active_libretro_host();
    return host ? libretro_host_read_audio(host, buffer, max_frames) : 0;
}

/* ========================================================================
 * Input forwarding (to input target — menu HUD or active instance)
 * ======================================================================== */

AARCADECORE_EXPORT void aarcadecore_key_down(int vk_code, int modifiers)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_down)
        inst->vtable->key_down(inst, vk_code, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_key_up(int vk_code, int modifiers)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_up)
        inst->vtable->key_up(inst, vk_code, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_key_char(unsigned int unicode_char, int modifiers)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_char)
        inst->vtable->key_char(inst, unicode_char, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_mouse_move(int x, int y)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->mouse_move) {
        /* Scale from overlay coords (1920x1080) to instance native coords when fullscreen */
        if (g_fullscreenInstance == inst && inst->vtable->get_width && inst->vtable->get_height) {
            x = x * inst->vtable->get_width(inst) / 1920;
            y = y * inst->vtable->get_height(inst) / 1080;
        }
        inst->vtable->mouse_move(inst, x, y);
    }
}

AARCADECORE_EXPORT void aarcadecore_mouse_down(int button)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->mouse_down)
        inst->vtable->mouse_down(inst, button);
}

AARCADECORE_EXPORT void aarcadecore_mouse_up(int button)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->mouse_up)
        inst->vtable->mouse_up(inst, button);
}

AARCADECORE_EXPORT void aarcadecore_mouse_wheel(int delta)
{
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->mouse_wheel)
        inst->vtable->mouse_wheel(inst, delta);
}

/* ========================================================================
 * Main menu (HUD overlay)
 * ======================================================================== */

AARCADECORE_EXPORT void aarcadecore_toggle_main_menu(void)
{
    UltralightManager_ToggleMainMenu();
}

AARCADECORE_EXPORT bool aarcadecore_is_main_menu_open(void)
{
    return g_fullscreenInstance != NULL || UltralightManager_IsMainMenuOpen();
}

AARCADECORE_EXPORT bool aarcadecore_should_open_engine_menu(void)
{
    return UltralightManager_ShouldOpenEngineMenu();
}

AARCADECORE_EXPORT void aarcadecore_clear_engine_menu_flag(void)
{
    UltralightManager_ClearEngineMenuFlag();
}

AARCADECORE_EXPORT bool aarcadecore_should_start_libretro(void)
{
    return UltralightManager_ShouldStartLibretro();
}

AARCADECORE_EXPORT void aarcadecore_clear_start_libretro_flag(void)
{
    UltralightManager_ClearStartLibretroFlag();
}

AARCADECORE_EXPORT void aarcadecore_start_libretro(void)
{
    /* Close menu, start Libretro, set as active instance */
    if (UltralightManager_IsMainMenuOpen())
        UltralightManager_ToggleMainMenu();
    LibretroManager_Init();
    g_activeInstance = LibretroManager_GetActive();
    if (g_host.host_printf)
        g_host.host_printf("AACore: Libretro set as active instance\n");
}

/* ========================================================================
 * Fullscreen overlay (rendered by HUD Ultralight, separate from active instance)
 * ======================================================================== */

AARCADECORE_EXPORT int aarcadecore_get_task_count(void)
{
    return g_taskCount;
}

AARCADECORE_EXPORT bool aarcadecore_render_task_texture(
    int taskIndex, void* pixelData, int width, int height, int is16bit, int bpp)
{
    if (taskIndex < 0 || taskIndex >= g_taskCount) return false;
    EmbeddedInstance* inst = g_tasks[taskIndex];
    if (!inst || !inst->vtable->is_active(inst) || !inst->vtable->render) return false;
    inst->vtable->render(inst, pixelData, width, height, is16bit, bpp);
    return true;
}

AARCADECORE_EXPORT bool aarcadecore_render_overlay(void* pixelData, int width, int height)
{
    /* Fullscreen embedded instance takes priority over HUD */
    if (g_fullscreenInstance && g_fullscreenInstance->vtable->is_active(g_fullscreenInstance)
        && g_fullscreenInstance->vtable->render) {
        g_fullscreenInstance->vtable->render(g_fullscreenInstance, pixelData, width, height, 0, 32);
        return true;
    }

    /* Otherwise render HUD overlay as before */
    EmbeddedInstance* ul = UltralightManager_GetActive();
    if (!ul || !ul->vtable->is_active(ul) || !ul->vtable->render)
        return false;

    ul->vtable->render(ul, pixelData, width, height, 0, 32);
    return true;
}

/* ========================================================================
 * Instance Manager exports
 * ======================================================================== */

AARCADECORE_EXPORT bool aarcadecore_has_pending_spawn(void)
{
    return g_instanceManager.hasPendingSpawn();
}

static SpawnRequest g_lastPoppedSpawn;

AARCADECORE_EXPORT void aarcadecore_pop_pending_spawn(void)
{
    g_lastPoppedSpawn = g_instanceManager.popPendingSpawn();
}

AARCADECORE_EXPORT void aarcadecore_confirm_spawn(int thingIdx)
{
    g_instanceManager.confirmSpawn(thingIdx);
}

AARCADECORE_EXPORT int aarcadecore_get_selected_thing_idx(void)
{
    return g_instanceManager.getSelectedThingIdx();
}

AARCADECORE_EXPORT bool aarcadecore_get_thing_screen_path(int thingIdx, char* pathOut, int pathSize)
{
    std::string path = g_instanceManager.getScreenImagePath(thingIdx);
    if (path.empty()) return false;
    if (pathOut && pathSize > 0) {
        strncpy(pathOut, path.c_str(), pathSize - 1);
        pathOut[pathSize - 1] = '\0';
    }
    return true;
}

AARCADECORE_EXPORT bool aarcadecore_load_thing_screen_pixels(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut)
{
    if (!pixelsOut || !widthOut || !heightOut) return false;
    std::string cachePath = g_instanceManager.getScreenImagePath(thingIdx);
    if (cachePath.empty()) return false;

    /* Get cached BGRA pixels from the ImageLoader's in-memory cache */
    uint8_t* pixels = nullptr;
    int w = 0, h = 0;
    if (!g_imageLoader.getPixels(cachePath, &pixels, &w, &h))
        return false;

    /* Copy pixels so the caller owns the memory (cache persists for other readers) */
    size_t pixelBytes = (size_t)w * h * 4;
    uint8_t* copy = (uint8_t*)malloc(pixelBytes);
    memcpy(copy, pixels, pixelBytes);

    *pixelsOut = copy;
    *widthOut = w;
    *heightOut = h;
    return true;
}

AARCADECORE_EXPORT bool aarcadecore_load_thing_marquee_pixels(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut)
{
    if (!pixelsOut || !widthOut || !heightOut) return false;
    std::string cachePath = g_instanceManager.getMarqueeImagePath(thingIdx);
    if (cachePath.empty()) return false;

    uint8_t* pixels = nullptr;
    int w = 0, h = 0;
    if (!g_imageLoader.getPixels(cachePath, &pixels, &w, &h))
        return false;

    size_t pixelBytes = (size_t)w * h * 4;
    uint8_t* copy = (uint8_t*)malloc(pixelBytes);
    memcpy(copy, pixels, pixelBytes);

    *pixelsOut = copy;
    *widthOut = w;
    *heightOut = h;
    return true;
}

AARCADECORE_EXPORT void aarcadecore_free_pixels(void* pixels)
{
    free(pixels);
}

AARCADECORE_EXPORT void aarcadecore_on_map_loaded(void)
{
    g_instanceManager.onMapLoaded();
}

AARCADECORE_EXPORT void aarcadecore_on_map_unloaded(void)
{
    g_instanceManager.onMapUnloaded();
}

AARCADECORE_EXPORT void aarcadecore_report_thing_transform(int thingIdx,
    float px, float py, float pz, int sectorId, float pitch, float yaw, float roll)
{
    g_instanceManager.reportThingTransform(thingIdx, px, py, pz, sectorId, pitch, yaw, roll);
}

AARCADECORE_EXPORT bool aarcadecore_spawn_has_position(float* px, float* py, float* pz, int* sectorId, float* rx, float* ry, float* rz)
{
    if (!g_lastPoppedSpawn.hasExplicitPosition) return false;
    if (px) *px = g_lastPoppedSpawn.posX;
    if (py) *py = g_lastPoppedSpawn.posY;
    if (pz) *pz = g_lastPoppedSpawn.posZ;
    if (sectorId) *sectorId = g_lastPoppedSpawn.sectorId;
    if (rx) *rx = g_lastPoppedSpawn.rotX;
    if (ry) *ry = g_lastPoppedSpawn.rotY;
    if (rz) *rz = g_lastPoppedSpawn.rotZ;
    return true;
}

AARCADECORE_EXPORT void aarcadecore_spawn_get_template_name(char* nameOut, int nameSize)
{
    if (nameOut && nameSize > 0) {
        const std::string& name = g_lastPoppedSpawn.templateName;
        strncpy(nameOut, name.c_str(), nameSize - 1);
        nameOut[nameSize - 1] = '\0';
    }
}

AARCADECORE_EXPORT int aarcadecore_get_active_instance_count(void)
{
    return g_instanceManager.getActiveInstanceCount();
}

AARCADECORE_EXPORT int aarcadecore_get_thing_task_index(int thingIdx)
{
    return g_instanceManager.getTaskIndexForThing(thingIdx);
}

AARCADECORE_EXPORT void aarcadecore_object_used(int thingIdx)
{
    g_instanceManager.objectUsed(thingIdx);
}

AARCADECORE_EXPORT bool aarcadecore_has_pending_destroy(void)
{
    return g_instanceManager.hasPendingDestroy();
}

AARCADECORE_EXPORT int aarcadecore_pop_pending_destroy(void)
{
    return g_instanceManager.popPendingDestroy();
}

AARCADECORE_EXPORT void aarcadecore_set_aimed_thing(int thingIdx)
{
    g_instanceManager.setAimedThing(thingIdx);
}

void UltralightManager_OpenBuildContextMenu(void);
AARCADECORE_EXPORT void aarcadecore_toggle_build_context_menu(void)
{
    if (UltralightManager_IsMainMenuOpen())
        UltralightManager_ToggleMainMenu(); /* close current menu */
    else
        UltralightManager_OpenBuildContextMenu();
}

AARCADECORE_EXPORT bool aarcadecore_is_fullscreen_active(void)
{
    return g_fullscreenInstance != NULL;
}

AARCADECORE_EXPORT void aarcadecore_exit_fullscreen(void)
{
    g_fullscreenInstance = NULL;
}
