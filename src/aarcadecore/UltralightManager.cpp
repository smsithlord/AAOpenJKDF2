#include "aarcadecore_internal.h"

/* Forward declarations */
EmbeddedInstance* UltralightInstance_Create(const char* htmlPath, const char* material_name);
void UltralightInstance_Destroy(EmbeddedInstance* inst);
bool UltralightInstance_IsCloseRequested(EmbeddedInstance* inst);

#define UL_MAINMENU_HTML     "file:///aarcadecore/ui/mainMenu.html"
#define UL_MAINMENU_MATERIAL "compscreen.mat"

static EmbeddedInstance* g_mainMenuInstance = NULL;

/* Forward declaration */
void UltralightManager_CloseMainMenu(void);

void UltralightManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Initialized\n");
}

void UltralightManager_Shutdown(void)
{
    if (g_mainMenuInstance) {
        UltralightInstance_Destroy(g_mainMenuInstance);
        g_mainMenuInstance = NULL;
    }
}

void UltralightManager_Update(void)
{
    if (g_mainMenuInstance && g_mainMenuInstance->vtable->update) {
        g_mainMenuInstance->vtable->update(g_mainMenuInstance);

        /* Check if JS requested close */
        if (UltralightInstance_IsCloseRequested(g_mainMenuInstance)) {
            if (g_host.host_printf) g_host.host_printf("UltralightManager: JS requested menu close\n");
            UltralightManager_CloseMainMenu();
        }
    }
}

EmbeddedInstance* UltralightManager_GetActive(void)
{
    return g_mainMenuInstance;
}

void UltralightManager_OpenMainMenu(void)
{
    if (g_mainMenuInstance) return; /* already open */

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Opening main menu\n");

    g_mainMenuInstance = UltralightInstance_Create(UL_MAINMENU_HTML, UL_MAINMENU_MATERIAL);
    if (!g_mainMenuInstance) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to create menu instance\n");
        return;
    }

    if (!g_mainMenuInstance->vtable->init(g_mainMenuInstance)) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to init menu\n");
        UltralightInstance_Destroy(g_mainMenuInstance);
        g_mainMenuInstance = NULL;
    }
}

void UltralightManager_CloseMainMenu(void)
{
    if (!g_mainMenuInstance) return;

    if (g_host.host_printf) g_host.host_printf("UltralightManager: Closing main menu\n");
    UltralightInstance_Destroy(g_mainMenuInstance);
    g_mainMenuInstance = NULL;
}

void UltralightManager_ToggleMainMenu(void)
{
    if (g_mainMenuInstance)
        UltralightManager_CloseMainMenu();
    else
        UltralightManager_OpenMainMenu();
}

bool UltralightManager_IsMainMenuOpen(void)
{
    return g_mainMenuInstance != NULL;
}
