#include "EmbeddedInstanceManager.h"
#include "EmbeddedInstance.h"
#include "LibretroManager.h"
#include "SteamworksWebBrowserManager.h"
#include "../../stdPlatform.h"

void EmbeddedInstanceManager_Init(void)
{
    stdPlatform_Printf("EmbeddedInstanceManager: Initializing...\n");

    /* Initialize sub-managers */
    LibretroManager_Init();
    SteamworksWebBrowserManager_Init();

    stdPlatform_Printf("EmbeddedInstanceManager: Ready\n");
}

void EmbeddedInstanceManager_Shutdown(void)
{
    stdPlatform_Printf("EmbeddedInstanceManager: Shutting down...\n");
    LibretroManager_Shutdown();
    SteamworksWebBrowserManager_Shutdown();
}

void EmbeddedInstanceManager_Update(void)
{
    /* Update all active managers */
    LibretroManager_Update();
    SteamworksWebBrowserManager_Update();
}

bool EmbeddedInstanceManager_IsActive(void)
{
    EmbeddedInstance* lr = LibretroManager_GetActive();
    if (lr && lr->vtable->is_active(lr))
        return true;

    EmbeddedInstance* swb = SteamworksWebBrowserManager_GetActive();
    if (swb && swb->vtable->is_active(swb))
        return true;

    return false;
}
