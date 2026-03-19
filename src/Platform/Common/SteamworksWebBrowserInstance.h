#ifndef STEAMWORKS_WEB_BROWSER_INSTANCE_H
#define STEAMWORKS_WEB_BROWSER_INSTANCE_H

#include "EmbeddedInstance.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a SteamworksWebBrowserInstance (stub).
 * material_name: in-game material to render to
 * Returns NULL on failure. */
EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* material_name);

/* Destroy a SteamworksWebBrowserInstance and free resources. */
void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst);

#ifdef __cplusplus
}
#endif

#endif /* STEAMWORKS_WEB_BROWSER_INSTANCE_H */
