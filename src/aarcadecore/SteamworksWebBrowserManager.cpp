#include "aarcadecore_internal.h"

/* Forward declarations */
EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* url, const char* material_name);
void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst);

#define SWB_DEFAULT_URL      "https://smsithlord.com/starfighter/"
#define SWB_DEFAULT_MATERIAL "compscreen.mat"

static EmbeddedInstance* g_activeInstance = NULL;

void SteamworksWebBrowserManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowserManager: Initializing...\n");

    g_activeInstance = SteamworksWebBrowserInstance_Create(SWB_DEFAULT_URL, SWB_DEFAULT_MATERIAL);
    if (!g_activeInstance) {
        if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowserManager: Failed to create instance\n");
        return;
    }

    if (!g_activeInstance->vtable->init(g_activeInstance)) {
        if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowserManager: Failed to init instance\n");
        SteamworksWebBrowserInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void SteamworksWebBrowserManager_Shutdown(void)
{
    if (g_activeInstance) {
        SteamworksWebBrowserInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void SteamworksWebBrowserManager_Update(void)
{
    if (g_activeInstance && g_activeInstance->vtable->update)
        g_activeInstance->vtable->update(g_activeInstance);
}

EmbeddedInstance* SteamworksWebBrowserManager_GetActive(void)
{
    return g_activeInstance;
}
