#include "aarcadecore_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations */
void UltralightManager_UpdateHudPixelBuffer(void);
EmbeddedInstance* UltralightInstance_Create(const char* htmlPath, const char* material_name);
void UltralightInstance_Destroy(EmbeddedInstance* inst);
bool UltralightInstance_IsCloseRequested(EmbeddedInstance* inst);
void UltralightInstance_LoadURL(EmbeddedInstance* inst, const char* url);
void UltralightInstance_EvaluateScript(EmbeddedInstance* inst, const char* script);

#define UL_BLANK_HTML        "file:///aarcadecore/ui/blank.html"
#define UL_OVERLAY_HTML      "file:///aarcadecore/ui/overlay.html"
#define UL_MAINMENU_HTML     "file:///aarcadecore/ui/mainMenu.html"
#define UL_LIBRARY_HTML      "file:///aarcadecore/ui/library.html"
#define UL_TASKMENU_HTML     "file:///aarcadecore/ui/taskMenu.html"
#define UL_BUILDMENU_HTML    "file:///aarcadecore/ui/buildContextMenu.html"
#define UL_TABMENU_HTML      "file:///aarcadecore/ui/tabMenu.html"
#define UL_HUD_MATERIAL      "DynScreen.mat"

/* The HUD instance is always alive */
static EmbeddedInstance* g_hudInstance = NULL;
static bool g_mainMenuOpen = false;
static bool g_engineMenuRequested = false;
static bool g_startLibretroRequested = false;
static int g_requestedTabIndex = -1; /* -1 = use localStorage default */
static bool g_spawnModeOpen = false;
static bool g_overlayLoaded = false;
static bool g_hudInputActive = false;

/* Persistent HUD pixel buffer — updated each frame the HUD renders */
#define HUD_BUF_W 1920
#define HUD_BUF_H 1080
static uint8_t* g_hudPixelBuffer = NULL;
static bool g_hudPixelBufferValid = false;

#define UL_SPAWNMODE_HTML    "file:///aarcadecore/ui/spawnMode.html"

/* Forward declarations */
void UltralightManager_CloseMainMenu(void);

void UltralightManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Initializing HUD instance...\n");

    g_hudInstance = UltralightInstance_Create(UL_BLANK_HTML, UL_HUD_MATERIAL);
    if (!g_hudInstance) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to create HUD instance\n");
        return;
    }

    if (!g_hudInstance->vtable->init(g_hudInstance)) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to init HUD instance\n");
        UltralightInstance_Destroy(g_hudInstance);
        g_hudInstance = NULL;
        return;
    }

    if (g_host.host_printf) g_host.host_printf("UltralightManager: HUD instance ready (blank)\n");
}

void UltralightManager_Shutdown(void)
{
    if (g_hudInstance) {
        UltralightInstance_Destroy(g_hudInstance);
        g_hudInstance = NULL;
    }
    free(g_hudPixelBuffer);
    g_hudPixelBuffer = NULL;
    g_hudPixelBufferValid = false;
    g_mainMenuOpen = false;
}

void UltralightManager_Update(void)
{
    if (g_hudInstance && g_hudInstance->vtable->update) {
        g_hudInstance->vtable->update(g_hudInstance);

        /* Check if JS requested close */
        if (g_mainMenuOpen && UltralightInstance_IsCloseRequested(g_hudInstance)) {
            if (g_host.host_printf) g_host.host_printf("UltralightManager: JS requested menu close\n");
            UltralightManager_CloseMainMenu();
        }

        /* Update persistent HUD pixel buffer only when overlay is active */
        if (g_overlayLoaded)
            UltralightManager_UpdateHudPixelBuffer();
    }
}

EmbeddedInstance* UltralightManager_GetActive(void)
{
    /* Return HUD instance when menu or spawn mode is open (for overlay rendering) */
    return (g_mainMenuOpen || g_spawnModeOpen) ? g_hudInstance : NULL;
}

void UltralightManager_OpenMainMenu(void)
{
    if (g_mainMenuOpen || !g_hudInstance) return;

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_MAINMENU_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

void UltralightManager_CloseMainMenu(void)
{
    if (!g_mainMenuOpen || !g_hudInstance) return;

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Closing main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_BLANK_HTML);
    g_mainMenuOpen = false;
    g_overlayLoaded = false;
}

void UltralightManager_ToggleMainMenu(void)
{
    if (g_mainMenuOpen)
        UltralightManager_CloseMainMenu();
    else
        UltralightManager_OpenMainMenu();
}

void UltralightManager_OpenLibraryBrowser(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening library browser\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_LIBRARY_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

void UltralightManager_OpenTaskMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening task menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_TASKMENU_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

void UltralightManager_OpenBuildContextMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening build context menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_BUILDMENU_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

void UltralightManager_OpenTabMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening tab menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_TABMENU_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

void UltralightManager_OpenTabMenuToTab(int tabIndex)
{
    g_requestedTabIndex = tabIndex;
    UltralightManager_OpenTabMenu();
}

int UltralightManager_ConsumeRequestedTab(void)
{
    int idx = g_requestedTabIndex;
    g_requestedTabIndex = -1;
    return idx;
}

void UltralightManager_EnterSpawnMode(void)
{
    if (!g_hudInstance || g_spawnModeOpen) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Entering spawn mode\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_SPAWNMODE_HTML);
    g_spawnModeOpen = true;
    g_overlayLoaded = false;
    /* NOTE: g_mainMenuOpen is NOT set — game input stays active */
}

void UltralightManager_ExitSpawnMode(void)
{
    if (!g_hudInstance || !g_spawnModeOpen) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Exiting spawn mode\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_BLANK_HTML);
    g_spawnModeOpen = false;
    g_overlayLoaded = false;
}

bool UltralightManager_IsSpawnModeOpen(void)
{
    return g_spawnModeOpen;
}

void UltralightManager_LoadOverlay(void)
{
    if (!g_hudInstance || g_overlayLoaded) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Loading overlay\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_OVERLAY_HTML);
    g_overlayLoaded = true;
}

void UltralightManager_UnloadOverlay(void)
{
    if (!g_hudInstance) return;
    UltralightInstance_LoadURL(g_hudInstance, UL_BLANK_HTML);
    g_overlayLoaded = false;
    g_hudInputActive = false;
}

void UltralightManager_UpdateHudPixelBuffer(void)
{
    if (!g_hudInstance || !g_hudInstance->vtable->render) return;
    if (!g_hudPixelBuffer) {
        g_hudPixelBuffer = (uint8_t*)malloc(HUD_BUF_W * HUD_BUF_H * 4);
        if (!g_hudPixelBuffer) return;
    }
    memset(g_hudPixelBuffer, 0, HUD_BUF_W * HUD_BUF_H * 4);
    g_hudInstance->vtable->render(g_hudInstance, g_hudPixelBuffer, HUD_BUF_W, HUD_BUF_H, 0, 32);
    g_hudPixelBufferValid = true;
}

const uint8_t* UltralightManager_GetHudPixels(void)
{
    return g_hudPixelBufferValid ? g_hudPixelBuffer : NULL;
}

void UltralightManager_SetHudInputActive(bool active) { g_hudInputActive = active; }
bool UltralightManager_IsHudInputActive(void) { return g_hudInputActive; }

void UltralightManager_NotifyOverlayMode(const char* jsonPayload)
{
    if (!g_hudInstance || !jsonPayload) return;
    char script[2048];
    snprintf(script, sizeof(script),
        "if(window.onOverlayModeChanged)"
        "window.onOverlayModeChanged(%s);",
        jsonPayload);
    UltralightInstance_EvaluateScript(g_hudInstance, script);
}

void UltralightManager_ForwardKeyDown(int vk_code, int modifiers)
{
    if (!g_hudInstance || !g_hudInstance->vtable->key_down) return;
    g_hudInstance->vtable->key_down(g_hudInstance, vk_code, modifiers);
}

void UltralightManager_ForwardKeyUp(int vk_code, int modifiers)
{
    if (!g_hudInstance || !g_hudInstance->vtable->key_up) return;
    g_hudInstance->vtable->key_up(g_hudInstance, vk_code, modifiers);
}

void UltralightManager_ForwardKeyChar(unsigned int unicode_char, int modifiers)
{
    if (!g_hudInstance || !g_hudInstance->vtable->key_char) return;
    g_hudInstance->vtable->key_char(g_hudInstance, unicode_char, modifiers);
}

void UltralightManager_ForwardMouseMove(int x, int y)
{
    if (!g_hudInstance || !g_hudInstance->vtable->mouse_move) return;
    g_hudInstance->vtable->mouse_move(g_hudInstance, x, y);
}

void UltralightManager_ForwardMouseDown(int button)
{
    if (!g_hudInstance || !g_hudInstance->vtable->mouse_down) return;
    g_hudInstance->vtable->mouse_down(g_hudInstance, button);
}

void UltralightManager_ForwardMouseUp(int button)
{
    if (!g_hudInstance || !g_hudInstance->vtable->mouse_up) return;
    g_hudInstance->vtable->mouse_up(g_hudInstance, button);
}

void UltralightManager_OpenMainMenuPage(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Returning to main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_MAINMENU_HTML);
    g_mainMenuOpen = true;
    g_overlayLoaded = false;
}

bool UltralightManager_IsMainMenuOpen(void)
{
    return g_mainMenuOpen;
}

void UltralightManager_RequestEngineMenu(void)
{
    g_engineMenuRequested = true;
}

bool UltralightManager_ShouldOpenEngineMenu(void)
{
    return g_engineMenuRequested;
}

void UltralightManager_ClearEngineMenuFlag(void)
{
    g_engineMenuRequested = false;
}

void UltralightManager_RequestStartLibretro(void)
{
    g_startLibretroRequested = true;
}

bool UltralightManager_ShouldStartLibretro(void)
{
    return g_startLibretroRequested;
}

void UltralightManager_ClearStartLibretroFlag(void)
{
    g_startLibretroRequested = false;
}
