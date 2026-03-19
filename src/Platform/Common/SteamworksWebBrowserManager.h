#ifndef STEAMWORKS_WEB_BROWSER_MANAGER_H
#define STEAMWORKS_WEB_BROWSER_MANAGER_H

#include "EmbeddedInstance.h"

#ifdef __cplusplus
extern "C" {
#endif

void SteamworksWebBrowserManager_Init(void);
void SteamworksWebBrowserManager_Shutdown(void);
void SteamworksWebBrowserManager_Update(void);
EmbeddedInstance* SteamworksWebBrowserManager_GetActive(void);

#ifdef __cplusplus
}
#endif

#endif /* STEAMWORKS_WEB_BROWSER_MANAGER_H */
