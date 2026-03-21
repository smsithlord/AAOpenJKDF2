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

    /* Initialize the HUD overlay (always alive, starts with blank.html) */
    UltralightManager_Init();

    /* Open the library database */
    g_library.open("G:/Documents Sym Links/GitHub/aarcade-core/x64/Release/library.db");

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
    memset(&g_host, 0, sizeof(g_host));
}

AARCADECORE_EXPORT void aarcadecore_update(void)
{
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

    /* Sync titles from browser instances */
    g_instanceManager.updateTitles();
}

AARCADECORE_EXPORT bool aarcadecore_is_active(void)
{
    /* "Active" = input mode on = menu is open.
     * Having an active instance (e.g., Libretro on screens) does NOT mean input mode. */
    return UltralightManager_IsMainMenuOpen();
}

AARCADECORE_EXPORT const char* aarcadecore_get_material_name(void)
{
    /* The active instance determines what renders on in-game screens */
    if (g_activeInstance && g_activeInstance->target_material)
        return g_activeInstance->target_material;
    return "compscreen.mat"; /* default fallback */
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
    if (inst && inst->vtable->mouse_move)
        inst->vtable->mouse_move(inst, x, y);
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
    return UltralightManager_IsMainMenuOpen();
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
    /* The overlay always comes from the HUD Ultralight instance */
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

AARCADECORE_EXPORT void aarcadecore_pop_pending_spawn(void)
{
    g_instanceManager.popPendingSpawn();
}

AARCADECORE_EXPORT void aarcadecore_confirm_spawn(int thingIdx)
{
    g_instanceManager.confirmSpawn(thingIdx);
}

AARCADECORE_EXPORT int aarcadecore_get_selected_thing_idx(void)
{
    return g_instanceManager.getSelectedThingIdx();
}

AARCADECORE_EXPORT int aarcadecore_get_active_instance_count(void)
{
    return g_instanceManager.getActiveInstanceCount();
}

AARCADECORE_EXPORT int aarcadecore_get_thing_task_index(int thingIdx)
{
    return g_instanceManager.getTaskIndexForThing(thingIdx);
}
