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
#include "LibretroCoreConfig.h"
#include <string.h>
#include <vector>
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

/* When set, this instance receives input but is NOT rendered in the overlay.
 * The player sees the in-world screen and sends input via "virtual input". */
static EmbeddedInstance* g_inputModeInstance = NULL;

/* Which instance the overlay.html was loaded for (to detect switches) */
static EmbeddedInstance* g_overlayForInstance = NULL;

/* Last mouse position in overlay coords (1920x1080) for cursor drawing */
static int g_lastMouseX = 0;
static int g_lastMouseY = 0;

/* Task list — all running embedded instances (excluding HUD overlay).
 * The host uses task indices to manage per-thing GL textures.
 * No fixed cap: the player decides how many cabinets to spawn. */
static std::vector<EmbeddedInstance*> g_tasks;

static int addTask(EmbeddedInstance* inst)
{
    if (!inst) return -1;
    /* Reuse a freed slot if available. */
    for (size_t i = 0; i < g_tasks.size(); i++) {
        if (g_tasks[i] == nullptr) {
            g_tasks[i] = inst;
            return (int)i;
        }
    }
    g_tasks.push_back(inst);
    return (int)(g_tasks.size() - 1);
}

/* Non-static wrapper for InstanceManager to call */
int aarcadecore_addTask(EmbeddedInstance* inst) { return addTask(inst); }

void aarcadecore_removeTask(int taskIndex) {
    if (taskIndex >= 0 && taskIndex < (int)g_tasks.size())
        g_tasks[taskIndex] = nullptr;
}

static bool g_overlayDirty = true; /* Set when overlay content changes */
static uint32_t g_engineFrame = 0; /* Incremented each update for per-frame render tracking */

/* Forward declarations needed by setFullscreenInstance */
void UltralightManager_LoadOverlay(void);
void UltralightManager_UnloadOverlay(void);
void UltralightManager_NotifyOverlayMode(const char* mode);
static void notifyOverlayState(void);

/* Fullscreen instance accessors for InstanceManager */
void aarcadecore_setFullscreenInstance(EmbeddedInstance* inst) {
    g_fullscreenInstance = inst;
    g_overlayDirty = true;
    if (inst) {
        if (inst != g_overlayForInstance) {
            UltralightManager_UnloadOverlay();
            UltralightManager_LoadOverlay();
            g_overlayForInstance = inst;
        } else {
            UltralightManager_LoadOverlay();
        }
        notifyOverlayState();
    } else if (!g_inputModeInstance) {
        notifyOverlayState();
    }
}
EmbeddedInstance* aarcadecore_getFullscreenInstance(void) { return g_fullscreenInstance; }

void aarcadecore_clearOverlayAssociation(void) { g_overlayForInstance = NULL; }

/* Input mode instance accessors */
void aarcadecore_setInputModeInstance(EmbeddedInstance* inst) { g_inputModeInstance = inst; g_overlayDirty = true; }
EmbeddedInstance* aarcadecore_getInputModeInstance(void) { return g_inputModeInstance; }

static int getActiveTaskIndex(void)
{
    for (size_t i = 0; i < g_tasks.size(); i++) {
        if (g_tasks[i] == g_activeInstance) return (int)i;
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
bool UltralightManager_IsSpawnModeOpen(void);
void UltralightManager_NotifySpawnWheel(int delta);
void UltralightManager_ToggleMainMenu(void);
bool UltralightManager_IsMainMenuOpen(void);
bool UltralightManager_ShouldOpenEngineMenu(void);
void UltralightManager_ClearEngineMenuFlag(void);
bool UltralightManager_ShouldStartLibretro(void);
void UltralightManager_ClearStartLibretroFlag(void);
void UltralightManager_LoadOverlay(void);
void UltralightManager_UnloadOverlay(void);
const uint8_t* UltralightManager_GetHudPixels(void);
bool UltralightManager_IsHudInputActive(void);
void UltralightManager_NotifySpawnWheel(int delta);
void UltralightManager_ForwardMouseMove(int x, int y);
void UltralightManager_ForwardKeyDown(int vk_code, int modifiers);
void UltralightManager_ForwardKeyUp(int vk_code, int modifiers);
void UltralightManager_ForwardKeyChar(unsigned int unicode_char, int modifiers);
void UltralightManager_ForwardMouseDown(int button);
void UltralightManager_ForwardMouseUp(int button);

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/* Helper to escape a string for JSON (basic — escapes quotes and backslashes) */
static void jsonEscapeStr(char* dst, int dstSize, const char* src)
{
    int j = 0;
    for (int i = 0; src[i] && j < dstSize - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') { dst[j++] = '\\'; }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

/* Build and send overlay state notification to HUD JS */
static void notifyOverlayState(void)
{
    const char* mode = "";
    if (g_inputModeInstance) mode = "input";
    else if (g_fullscreenInstance) mode = "fullscreen";

    EmbeddedInstance* target = g_inputModeInstance ? g_inputModeInstance : g_fullscreenInstance;
    const EmbeddedItemInstance* inst = target ? g_instanceManager.getInstanceForBrowser(target) : nullptr;

    const char* instanceType = "";
    if (target) {
        switch (target->type) {
            case EMBEDDED_STEAMWORKS_BROWSER: instanceType = "swb"; break;
            case EMBEDDED_LIBRETRO: instanceType = "libretro"; break;
            case EMBEDDED_ULTRALIGHT: instanceType = "ultralight"; break;
            case EMBEDDED_VIDEO_PLAYER: instanceType = "videoplayer"; break;
            default: break;
        }
    }

    char urlEsc[1024] = "", titleEsc[512] = "", itemIdEsc[128] = "";
    char corePathEsc[512] = "", gamePathEsc[1024] = "";
    if (inst) {
        jsonEscapeStr(urlEsc, sizeof(urlEsc), inst->url.c_str());
        jsonEscapeStr(titleEsc, sizeof(titleEsc), inst->title.c_str());
        jsonEscapeStr(itemIdEsc, sizeof(itemIdEsc), inst->itemId.c_str());
    }

    /* Extract core/game paths for Libretro instances */
    if (target && target->type == EMBEDDED_LIBRETRO && target->user_data) {
        typedef struct { LibretroHost* host; const char* core_path; const char* game_path; } LRDataFull;
        LRDataFull* lrd = (LRDataFull*)target->user_data;
        if (lrd->core_path) jsonEscapeStr(corePathEsc, sizeof(corePathEsc), lrd->core_path);
        if (lrd->game_path) jsonEscapeStr(gamePathEsc, sizeof(gamePathEsc), lrd->game_path);
    }

    /* Extract file path for Video Player instances */
    char videoFileEsc[1024] = "";
    if (target && target->type == EMBEDDED_VIDEO_PLAYER && target->vtable->get_title) {
        const char* vf = target->vtable->get_title(target);
        if (vf) jsonEscapeStr(videoFileEsc, sizeof(videoFileEsc), vf);
    }

    bool canBack = target && target->vtable->can_go_back ? target->vtable->can_go_back(target) : false;
    bool canFwd = target && target->vtable->can_go_forward ? target->vtable->can_go_forward(target) : false;

    char json[4096];
    snprintf(json, sizeof(json),
        "{\"mode\":\"%s\",\"isFullscreen\":%s,\"isInputMode\":%s,"
        "\"url\":\"%s\",\"title\":\"%s\",\"itemId\":\"%s\","
        "\"instanceType\":\"%s\",\"canGoBack\":%s,\"canGoForward\":%s,"
        "\"corePath\":\"%s\",\"gamePath\":\"%s\",\"videoFile\":\"%s\"}",
        mode,
        g_fullscreenInstance ? "true" : "false",
        g_inputModeInstance ? "true" : "false",
        urlEsc, titleEsc, itemIdEsc,
        instanceType,
        canBack ? "true" : "false",
        canFwd ? "true" : "false",
        corePathEsc, gamePathEsc, videoFileEsc);

    UltralightManager_NotifyOverlayMode(json);
}

/* Check if the HUD pixel at overlay coords (x,y) is fully opaque (HUD element) */
static bool isHudPixelOpaque(int x, int y)
{
    const uint8_t* buf = UltralightManager_GetHudPixels();
    if (!buf || x < 0 || x >= 1920 || y < 0 || y >= 1080) return false;
    return buf[(y * 1920 + x) * 4 + 3] >= 250;
}

/* Get the instance that should receive input:
 * - When menu is open → HUD Ultralight (for menu interaction)
 * - Otherwise → active instance (e.g., Libretro) */
static EmbeddedInstance* get_input_target(void)
{
    /* Input mode instance gets priority (hidden overlay, input only) */
    if (g_inputModeInstance) return g_inputModeInstance;
    /* Fullscreen embedded instance gets all input */
    if (g_fullscreenInstance) return g_fullscreenInstance;
    if (UltralightManager_IsMainMenuOpen()) {
        EmbeddedInstance* ul = UltralightManager_GetActive();
        if (ul && ul->vtable->is_active(ul)) return ul;
    }
    return g_activeInstance;
}

/* Get the LibretroHost* from any running Libretro task */
static LibretroHost* get_active_libretro_host(void)
{
    typedef struct { LibretroHost* host; } LRData;

    /* Search all tasks for a running Libretro instance */
    for (size_t i = 0; i < g_tasks.size(); i++) {
        if (g_tasks[i] && g_tasks[i]->type == EMBEDDED_LIBRETRO &&
            g_tasks[i]->vtable->is_active(g_tasks[i]))
            return ((LRData*)g_tasks[i]->user_data)->host;
    }
    return NULL;
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
    g_library.open("aarcadecore/library.db");
    g_library.ensureSchema();
    g_library.setPlatformKey(OPENJK_PLATFORM_ID);

    /* Load Libretro core configurations and scan for DLLs */
    g_coreConfigMgr.loadConfig();
    g_coreConfigMgr.scanCores();

    /* First-run: seed the default OpenJK model library if empty. Gate checks
     * the OpenJK-scoped models table (platformKey_ was set on line 308) so
     * existing users with any OpenJK models already registered are untouched.
     * importDefaultLibrary() is itself idempotent per-template as a safety net. */
    if (g_library.getModels(0, 1).empty()) {
        if (g_host.host_printf)
            g_host.host_printf("AACore: Empty library detected, importing default OpenJK models...\n");
        g_instanceManager.importDefaultLibrary();
    }

    /* Initialize the image loader (headless Ultralight view for thumbnail caching) */
    g_imageLoader.init();

    /* Tasks are created on demand, not at startup */
    g_tasks.clear();
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

/* Exposed for render functions to check frame number */
uint32_t aarcadecore_getEngineFrame(void) { return g_engineFrame; }

AARCADECORE_EXPORT void aarcadecore_update(void)
{
    g_engineFrame++;

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
    for (size_t i = 0; i < g_tasks.size(); i++) {
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
    /* "Active" = input mode on = menu, fullscreen overlay, or input mode is open.
     * Having an active instance (e.g., Libretro on screens) does NOT mean input mode. */
    return g_inputModeInstance != NULL || g_fullscreenInstance != NULL || UltralightManager_IsMainMenuOpen();
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
    /* When HUD has input focus (form field), send keys to HUD instead */
    if ((g_fullscreenInstance || g_inputModeInstance) && UltralightManager_IsHudInputActive()) {
        UltralightManager_ForwardKeyDown(vk_code, modifiers);
        return;
    }
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_down)
        inst->vtable->key_down(inst, vk_code, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_key_up(int vk_code, int modifiers)
{
    if ((g_fullscreenInstance || g_inputModeInstance) && UltralightManager_IsHudInputActive()) {
        UltralightManager_ForwardKeyUp(vk_code, modifiers);
        return;
    }
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_up)
        inst->vtable->key_up(inst, vk_code, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_key_char(unsigned int unicode_char, int modifiers)
{
    if ((g_fullscreenInstance || g_inputModeInstance) && UltralightManager_IsHudInputActive()) {
        UltralightManager_ForwardKeyChar(unicode_char, modifiers);
        return;
    }
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->key_char)
        inst->vtable->key_char(inst, unicode_char, modifiers);
}

AARCADECORE_EXPORT void aarcadecore_mouse_move(int x, int y)
{
    g_lastMouseX = x;
    g_lastMouseY = y;

    bool overlayActive = g_fullscreenInstance || g_inputModeInstance;

    /* Always forward mouse_move to HUD for cursor tracking */
    if (overlayActive)
        UltralightManager_ForwardMouseMove(x, y);

    /* Forward to instance only if HUD pixel is not opaque (click-through) */
    EmbeddedInstance* inst = get_input_target();
    if (inst && inst->vtable->mouse_move) {
        if (overlayActive && isHudPixelOpaque(x, y))
            return; /* HUD element under cursor — don't forward to instance */
        if ((g_fullscreenInstance == inst || g_inputModeInstance == inst) && inst->vtable->get_width && inst->vtable->get_height) {
            int sx = x * inst->vtable->get_width(inst) / 1920;
            int sy = y * inst->vtable->get_height(inst) / 1080;
            inst->vtable->mouse_move(inst, sx, sy);
        } else {
            inst->vtable->mouse_move(inst, x, y);
        }
    }
}

AARCADECORE_EXPORT void aarcadecore_mouse_down(int button)
{
    bool overlayActive = g_fullscreenInstance || g_inputModeInstance;
    bool hudOpaque = overlayActive && isHudPixelOpaque(g_lastMouseX, g_lastMouseY);

    if (hudOpaque) {
        /* Click on opaque HUD element — send only to HUD */
        UltralightManager_ForwardMouseDown(button);
    } else {
        /* Click through to instance */
        EmbeddedInstance* inst = get_input_target();
        if (inst && inst->vtable->mouse_down)
            inst->vtable->mouse_down(inst, button);
    }
}

AARCADECORE_EXPORT void aarcadecore_mouse_up(int button)
{
    bool overlayActive = g_fullscreenInstance || g_inputModeInstance;
    bool hudOpaque = overlayActive && isHudPixelOpaque(g_lastMouseX, g_lastMouseY);

    if (hudOpaque) {
        UltralightManager_ForwardMouseUp(button);
    } else {
        EmbeddedInstance* inst = get_input_target();
        if (inst && inst->vtable->mouse_up)
            inst->vtable->mouse_up(inst, button);
    }
}

AARCADECORE_EXPORT void aarcadecore_mouse_wheel(int delta)
{
    /* During spawn mode, notify HUD JS for model cycling */
    if (UltralightManager_IsSpawnModeOpen()) {
        if (g_host.host_printf)
            g_host.host_printf("AACore: Spawn wheel delta=%d\n", delta);
        UltralightManager_NotifySpawnWheel(delta);
        return;
    }
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
    return g_inputModeInstance != NULL || g_fullscreenInstance != NULL || UltralightManager_IsMainMenuOpen();
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
    return (int)g_tasks.size();
}

AARCADECORE_EXPORT bool aarcadecore_render_task_texture(
    int taskIndex, void* pixelData, int width, int height, int is16bit, int bpp)
{
    if (taskIndex < 0 || taskIndex >= (int)g_tasks.size()) return false;
    EmbeddedInstance* inst = g_tasks[taskIndex];
    if (!inst || !inst->vtable->is_active(inst) || !inst->vtable->render) return false;
    inst->vtable->render(inst, pixelData, width, height, is16bit, bpp);

    /* Composite HUD overlay onto task texture in input mode (not fullscreen) */
    if (g_inputModeInstance && !g_fullscreenInstance && inst == g_inputModeInstance && is16bit) {
        const uint8_t* hudBuf = UltralightManager_GetHudPixels();
        if (hudBuf) {
            uint16_t* pixels = (uint16_t*)pixelData;
            for (int ty = 0; ty < height; ty++) {
                int hy = ty * 1080 / height;
                if (hy >= 1080) hy = 1079;
                for (int tx = 0; tx < width; tx++) {
                    int hx = tx * 1920 / width;
                    if (hx >= 1920) hx = 1919;
                    int hoff = (hy * 1920 + hx) * 4; /* BGRA */
                    uint8_t a = hudBuf[hoff + 3];
                    if (a == 0) continue;
                    uint8_t b = hudBuf[hoff + 0];
                    uint8_t g = hudBuf[hoff + 1];
                    uint8_t r = hudBuf[hoff + 2];
                    if (a == 255) {
                        pixels[ty * width + tx] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    } else {
                        /* Alpha blend with existing RGB565 pixel */
                        uint16_t existing = pixels[ty * width + tx];
                        uint8_t er = ((existing >> 11) & 0x1F) << 3;
                        uint8_t eg = ((existing >> 5) & 0x3F) << 2;
                        uint8_t eb = (existing & 0x1F) << 3;
                        float fa = a / 255.0f, inv = 1.0f - fa;
                        uint8_t or_ = (uint8_t)(r * fa + er * inv);
                        uint8_t og = (uint8_t)(g * fa + eg * inv);
                        uint8_t ob = (uint8_t)(b * fa + eb * inv);
                        pixels[ty * width + tx] = ((or_ >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3);
                    }
                }
            }
        }
    }

    return true;
}

static void compositeHudOver(uint8_t* dst, const uint8_t* hud, int pixelCount)
{
    for (int i = 0; i < pixelCount; i++) {
        int off = i * 4;
        unsigned a = hud[off + 3];
        if (a == 0) continue;
        if (a == 255) { memcpy(dst + off, hud + off, 4); continue; }
        unsigned inv = 255 - a;
        dst[off + 0] = (uint8_t)((hud[off + 0] * a + dst[off + 0] * inv + 127) / 255);
        dst[off + 1] = (uint8_t)((hud[off + 1] * a + dst[off + 1] * inv + 127) / 255);
        dst[off + 2] = (uint8_t)((hud[off + 2] * a + dst[off + 2] * inv + 127) / 255);
        dst[off + 3] = 255;
    }
}

AARCADECORE_EXPORT bool aarcadecore_render_overlay(void* pixelData, int width, int height)
{
    const uint8_t* hudBuf = UltralightManager_GetHudPixels();

    /* Input mode without fullscreen (and not spawn mode): no overlay needed — skip quad entirely */
    if (g_inputModeInstance && !g_fullscreenInstance && !UltralightManager_IsSpawnModeOpen()) {
        return false;
    }

    /* Fullscreen: render instance to clean buffer, copy to output, composite HUD on top.
     * Instance render is a no-op if no new frame — clean buffer stays intact.
     * We always copy + composite so cursor updates don't corrupt the clean frame. */
    if (g_fullscreenInstance && g_fullscreenInstance->vtable->is_active(g_fullscreenInstance)
        && g_fullscreenInstance->vtable->render) {
        static uint8_t* g_cleanFrameBuffer = NULL;
        int bufSize = width * height * 4;
        if (!g_cleanFrameBuffer)
            g_cleanFrameBuffer = (uint8_t*)malloc(bufSize);
        /* Render instance to clean buffer (no-op if no new frame) */
        g_fullscreenInstance->vtable->render(g_fullscreenInstance, g_cleanFrameBuffer, width, height, 0, 32);
        /* Copy clean frame to output */
        memcpy(pixelData, g_cleanFrameBuffer, bufSize);
        /* Composite HUD on top of the copy */
        if (hudBuf)
            compositeHudOver((uint8_t*)pixelData, hudBuf, width * height);
        g_overlayDirty = false;
        return true;
    }

    /* HUD-native page (menus) — render directly, no compositing needed */
    EmbeddedInstance* ul = UltralightManager_GetActive();
    if (!ul || !ul->vtable->is_active(ul) || !ul->vtable->render)
        return false;

    ul->vtable->render(ul, pixelData, width, height, 0, 32);
    g_overlayDirty = false;
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

AARCADECORE_EXPORT void aarcadecore_init_spawned_object(int thingIdx)
{
    g_instanceManager.initSpawnedObject(thingIdx);
}

void UltralightManager_NotifySpawnConfirmed(void);
AARCADECORE_EXPORT void aarcadecore_confirm_spawn(int thingIdx)
{
    g_instanceManager.confirmSpawn(thingIdx);
    UltralightManager_NotifySpawnConfirmed();
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

AARCADECORE_EXPORT int aarcadecore_get_thing_screen_status(int thingIdx)
{
    return g_instanceManager.getScreenImageStatus(thingIdx);
}

AARCADECORE_EXPORT int aarcadecore_get_thing_marquee_status(int thingIdx)
{
    return g_instanceManager.getMarqueeImageStatus(thingIdx);
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

AARCADECORE_EXPORT void aarcadecore_deselect_only(void)
{
    g_instanceManager.deselectOnly();
}

AARCADECORE_EXPORT void aarcadecore_remember_object(int thingIdx)
{
    g_instanceManager.rememberObject(thingIdx);
}

AARCADECORE_EXPORT bool aarcadecore_has_pending_destroy(void)
{
    return g_instanceManager.hasPendingDestroy();
}

AARCADECORE_EXPORT int aarcadecore_pop_pending_destroy(void)
{
    return g_instanceManager.popPendingDestroy();
}

AARCADECORE_EXPORT bool aarcadecore_has_spawn_transform(void)
{
    return g_instanceManager.hasSpawnTransform();
}

AARCADECORE_EXPORT void aarcadecore_get_spawn_transform(float* p, float* y, float* r, bool* isWorldRot,
    float* ox, float* oy, float* oz, bool* isWorldOff, bool* useRaycast, float* scale)
{
    g_instanceManager.getSpawnTransform(p, y, r, isWorldRot, ox, oy, oz, isWorldOff, useRaycast, scale);
}

AARCADECORE_EXPORT float aarcadecore_get_object_scale(int thingIdx)
{
    return g_instanceManager.getObjectScale(thingIdx);
}

AARCADECORE_EXPORT void aarcadecore_clear_spawn_transform(void)
{
    g_instanceManager.clearSpawnTransform();
}

AARCADECORE_EXPORT const char* aarcadecore_get_spawn_model_id(void)
{
    static std::string s;
    s = g_instanceManager.getSpawnModelId();
    return s.c_str();
}

AARCADECORE_EXPORT void aarcadecore_update_thing_idx(int oldIdx, int newIdx)
{
    g_instanceManager.updateThingIdx(oldIdx, newIdx);
}

AARCADECORE_EXPORT void aarcadecore_set_spawn_preview_thing(int thingIdx)
{
    g_instanceManager.setSpawnPreviewThingIdx(thingIdx);
}

AARCADECORE_EXPORT void aarcadecore_reload_thing_images(int thingIdx)
{
    g_instanceManager.reloadImagesForThing(thingIdx);
}

static std::string g_lastTemplateForThing;
AARCADECORE_EXPORT const char* aarcadecore_get_template_for_thing(int thingIdx)
{
    g_lastTemplateForThing = g_instanceManager.getTemplateForThing(thingIdx);
    return g_lastTemplateForThing.c_str();
}

AARCADECORE_EXPORT void aarcadecore_remove_spawned(int thingIdx)
{
    g_instanceManager.removeSpawnedByThingIdx(thingIdx);
}

static std::string g_lastPoppedModelChange;

AARCADECORE_EXPORT bool aarcadecore_has_pending_model_change(void)
{
    return g_instanceManager.hasPendingModelChange();
}

AARCADECORE_EXPORT const char* aarcadecore_pop_pending_model_change(void)
{
    g_lastPoppedModelChange = g_instanceManager.popPendingModelChange();
    return g_lastPoppedModelChange.c_str();
}

AARCADECORE_EXPORT bool aarcadecore_has_pending_move(void)
{
    return g_instanceManager.hasPendingMove();
}

AARCADECORE_EXPORT int aarcadecore_pop_pending_move(void)
{
    return g_instanceManager.popPendingMove();
}

AARCADECORE_EXPORT void aarcadecore_set_aimed_thing(int thingIdx)
{
    g_instanceManager.setAimedThing(thingIdx);
}

void UltralightManager_OpenTabMenu(void);
void UltralightManager_OpenTabMenuToTab(int tabIndex);
void UltralightManager_OpenCreateItem(const char* file);
void UltralightManager_EnterSpawnMode(void);
void UltralightManager_ExitSpawnMode(void);
bool UltralightManager_IsSpawnModeOpen(void);
AARCADECORE_EXPORT void aarcadecore_enter_spawn_mode(void)
{
    if (UltralightManager_IsMainMenuOpen())
        UltralightManager_ToggleMainMenu();
    UltralightManager_EnterSpawnMode();
}

AARCADECORE_EXPORT void aarcadecore_exit_spawn_mode(void)
{
    UltralightManager_ExitSpawnMode();
}

AARCADECORE_EXPORT bool aarcadecore_is_spawn_mode_active(void)
{
    return UltralightManager_IsSpawnModeOpen();
}

AARCADECORE_EXPORT void aarcadecore_open_tab_menu_to_tab(int tabIndex)
{
    if (UltralightManager_IsMainMenuOpen())
        UltralightManager_ToggleMainMenu();
    if (tabIndex >= 0)
        UltralightManager_OpenTabMenuToTab(tabIndex);
    else
        UltralightManager_OpenTabMenu();
}

AARCADECORE_EXPORT void aarcadecore_open_create_item(const char* file)
{
    if (UltralightManager_IsMainMenuOpen())
        UltralightManager_ToggleMainMenu();
    UltralightManager_OpenCreateItem(file);
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

AARCADECORE_EXPORT void aarcadecore_mark_thing_seen(int thingIdx)
{
    int taskIdx = g_instanceManager.getTaskIndexForThing(thingIdx);
    if (taskIdx >= 0 && taskIdx < (int)g_tasks.size() && g_tasks[taskIdx])
        g_tasks[taskIdx]->lastSeenFrame = g_engineFrame;
}

AARCADECORE_EXPORT bool aarcadecore_is_task_visible(int taskIndex)
{
    if (taskIndex < 0 || taskIndex >= (int)g_tasks.size() || !g_tasks[taskIndex])
        return false;

    EmbeddedInstance* task = g_tasks[taskIndex];

    /* Always visible if it's the fullscreen or input mode instance */
    if (task == g_fullscreenInstance || task == g_inputModeInstance)
        return true;

    /* Visible if seen this frame or last frame */
    return task->lastSeenFrame >= g_engineFrame - 1;
}

AARCADECORE_EXPORT bool aarcadecore_register_adopted_template(const char* templateName)
{
    return g_instanceManager.registerAdoptedTemplate(templateName);
}

AARCADECORE_EXPORT bool aarcadecore_action_command(const char* cmd)
{
    if (!cmd) return false;

    if (strcmp(cmd, "ObjectMove") == 0) {
        int aimed = g_instanceManager.getAimedThingIdx();
        if (aimed < 0) return false;
        g_instanceManager.requestMove(aimed);
        return true;
    }

    if (strcmp(cmd, "ObjectRemove") == 0) {
        int aimed = g_instanceManager.getAimedThingIdx();
        if (aimed < 0) return false;
        g_instanceManager.destroyObject(aimed);
        return true;
    }

    if (strcmp(cmd, "ObjectClone") == 0) {
        const SpawnedObject* obj = g_instanceManager.getAimedObject();
        if (!obj) return false;
        if (obj->itemId.empty()) {
            /* Model-only object — clone without item */
            if (!obj->modelId.empty()) {
                g_instanceManager.requestSpawnModel(obj->modelId);
                return true;
            }
            return false;
        }
        Arcade::Item item = g_library.getItemById(obj->itemId);
        if (item.id.empty()) return false;
        g_instanceManager.requestSpawn(item, obj->modelId, obj->scale);
        return true;
    }

    if (strcmp(cmd, "TaskClose") == 0) {
        /* Try aimed object's task first (resolve slave→master) */
        int aimedIdx = g_instanceManager.getAimedThingIdx();
        if (aimedIdx >= 0) {
            int resolved = g_instanceManager.resolveMasterThingIdx(aimedIdx);
            for (int i = 0; i < g_instanceManager.getSpawnedCount(); i++) {
                const SpawnedObject* obj = g_instanceManager.getSpawned(i);
                if (obj && obj->thingIdx == resolved) {
                    const EmbeddedItemInstance* inst = g_instanceManager.getItemInstance(obj->itemId);
                    if (inst && inst->active) {
                        g_instanceManager.deactivateInstance(obj->itemId);
                        return true;
                    }
                    break;
                }
            }
        }
        /* Fall back to first active task */
        auto active = g_instanceManager.getActiveInstances();
        if (!active.empty()) {
            g_instanceManager.deactivateInstance(active[0]->itemId);
            return true;
        }
        return false;
    }

    if (g_host.host_printf)
        g_host.host_printf("AACore: Unknown action command '%s'\n", cmd);
    return false;
}

AARCADECORE_EXPORT void aarcadecore_enter_input_mode_for_selected(void)
{
    EmbeddedInstance* target = g_instanceManager.getInputTarget();
    /* During spawn mode, use HUD instance as input target for transform panel */
    if (!target && UltralightManager_IsSpawnModeOpen()) {
        target = UltralightManager_GetActive();
    }
    if (target) {
        g_inputModeInstance = target;
        /* During spawn mode, don't load overlay.html — keep spawnMode.html */
        if (!UltralightManager_IsSpawnModeOpen()) {
            if (target != g_overlayForInstance) {
                UltralightManager_UnloadOverlay();
                UltralightManager_LoadOverlay();
                g_overlayForInstance = target;
            } else {
                UltralightManager_LoadOverlay();
            }
        }
        notifyOverlayState();
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Entered input mode\n");
    }
}

AARCADECORE_EXPORT void aarcadecore_exit_input_mode(void)
{
    if (g_inputModeInstance) {
        g_inputModeInstance = NULL;
        notifyOverlayState();
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Exited input mode\n");
    }
}

AARCADECORE_EXPORT bool aarcadecore_is_input_mode_active(void)
{
    return g_inputModeInstance != NULL;
}

/* ========================================================================
 * Deep sleep — DLL can request sleep, host notifies state changes
 * ======================================================================== */

bool g_deepSleepRequested = false;
static bool g_isDeepSleeping = false;

AARCADECORE_EXPORT bool aarcadecore_deep_sleep_requested(void)
{
    if (g_deepSleepRequested) {
        g_deepSleepRequested = false; /* fire & forget */
        return true;
    }
    return false;
}

AARCADECORE_EXPORT void aarcadecore_deep_sleep_changed(bool sleeping)
{
    g_isDeepSleeping = sleeping;
    if (g_host.host_printf)
        g_host.host_printf("aarcadecore: Deep sleep %s\n", sleeping ? "entered" : "exited");
}

bool g_exitGameRequested = false;

AARCADECORE_EXPORT bool aarcadecore_exit_game_requested(void)
{
    if (g_exitGameRequested) {
        g_exitGameRequested = false;
        return true;
    }
    return false;
}
