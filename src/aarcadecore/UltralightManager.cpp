#include "aarcadecore_internal.h"

/* Forward declarations */
EmbeddedInstance* UltralightInstance_Create(const char* htmlPath, const char* material_name);
void UltralightInstance_Destroy(EmbeddedInstance* inst);
bool UltralightInstance_IsCloseRequested(EmbeddedInstance* inst);
void UltralightInstance_LoadURL(EmbeddedInstance* inst, const char* url);

#define UL_BLANK_HTML        "file:///aarcadecore/ui/blank.html"
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
    }
}

EmbeddedInstance* UltralightManager_GetActive(void)
{
    /* Only return the instance as "active" when menu is open
     * (so input mode and overlay rendering activate) */
    return g_mainMenuOpen ? g_hudInstance : NULL;
}

void UltralightManager_OpenMainMenu(void)
{
    if (g_mainMenuOpen || !g_hudInstance) return;

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_MAINMENU_HTML);
    g_mainMenuOpen = true;
}

void UltralightManager_CloseMainMenu(void)
{
    if (!g_mainMenuOpen || !g_hudInstance) return;

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Closing main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_BLANK_HTML);
    g_mainMenuOpen = false;
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
    g_mainMenuOpen = true; /* keep overlay active for input/rendering */
}

void UltralightManager_OpenTaskMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening task menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_TASKMENU_HTML);
    g_mainMenuOpen = true;
}

void UltralightManager_OpenBuildContextMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening build context menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_BUILDMENU_HTML);
    g_mainMenuOpen = true;
}

void UltralightManager_OpenTabMenu(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening tab menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_TABMENU_HTML);
    g_mainMenuOpen = true;
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

void UltralightManager_OpenMainMenuPage(void)
{
    if (!g_hudInstance) return;
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Returning to main menu\n");
    UltralightInstance_LoadURL(g_hudInstance, UL_MAINMENU_HTML);
    g_mainMenuOpen = true;
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
