#include "aarcadecore_internal.h"

static EmbeddedInstance* g_activeInstance = NULL;

void SteamworksWebBrowserManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowserManager: Init (stub)\n");
}

void SteamworksWebBrowserManager_Shutdown(void)
{
    /* stub */
    g_activeInstance = NULL;
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
