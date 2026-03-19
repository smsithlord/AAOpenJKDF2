#include "SteamworksWebBrowserManager.h"
#include "SteamworksWebBrowserInstance.h"
#include "../../stdPlatform.h"

static EmbeddedInstance* g_activeInstance = NULL;

void SteamworksWebBrowserManager_Init(void)
{
    stdPlatform_Printf("SteamworksWebBrowserManager: Init (stub)\n");
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
