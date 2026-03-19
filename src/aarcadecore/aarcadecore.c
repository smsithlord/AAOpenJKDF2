/*
 * aarcadecore.c — DLL entry point and exported functions
 */

#include "aarcadecore_api.h"
#include "aarcadecore_internal.h"
#include "libretro_host.h"
#include <string.h>

/* Global host callbacks */
AACoreHostCallbacks g_host = {0};

/* Forward declarations for internal managers */
void LibretroManager_Init(void);
void LibretroManager_Shutdown(void);
void LibretroManager_Update(void);
EmbeddedInstance* LibretroManager_GetActive(void);

void SteamworksWebBrowserManager_Init(void);
void SteamworksWebBrowserManager_Shutdown(void);
void SteamworksWebBrowserManager_Update(void);
EmbeddedInstance* SteamworksWebBrowserManager_GetActive(void);

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

    LibretroManager_Init();
    SteamworksWebBrowserManager_Init();

    if (g_host.host_printf)
        g_host.host_printf("AACore: Ready\n");

    return true;
}

AARCADECORE_EXPORT void aarcadecore_shutdown(void)
{
    if (g_host.host_printf)
        g_host.host_printf("AACore: Shutting down...\n");

    LibretroManager_Shutdown();
    SteamworksWebBrowserManager_Shutdown();
    memset(&g_host, 0, sizeof(g_host));
}

AARCADECORE_EXPORT void aarcadecore_update(void)
{
    LibretroManager_Update();
    SteamworksWebBrowserManager_Update();
}

AARCADECORE_EXPORT bool aarcadecore_is_active(void)
{
    EmbeddedInstance* lr = LibretroManager_GetActive();
    if (lr && lr->vtable->is_active(lr))
        return true;

    EmbeddedInstance* swb = SteamworksWebBrowserManager_GetActive();
    if (swb && swb->vtable->is_active(swb))
        return true;

    return false;
}

AARCADECORE_EXPORT const char* aarcadecore_get_material_name(void)
{
    EmbeddedInstance* lr = LibretroManager_GetActive();
    if (lr && lr->target_material)
        return lr->target_material;

    EmbeddedInstance* swb = SteamworksWebBrowserManager_GetActive();
    if (swb && swb->target_material)
        return swb->target_material;

    return NULL;
}

AARCADECORE_EXPORT void aarcadecore_render_texture(
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    EmbeddedInstance* lr = LibretroManager_GetActive();
    if (lr && lr->vtable->is_active(lr) && lr->vtable->render) {
        lr->vtable->render(lr, pixelData, width, height, is16bit, bpp);
        return;
    }

    EmbeddedInstance* swb = SteamworksWebBrowserManager_GetActive();
    if (swb && swb->vtable->is_active(swb) && swb->vtable->render) {
        swb->vtable->render(swb, pixelData, width, height, is16bit, bpp);
    }
}

/* Helper: get the LibretroHost* from the active Libretro instance */
static LibretroHost* get_active_libretro_host(void)
{
    EmbeddedInstance* lr = LibretroManager_GetActive();
    if (!lr || !lr->vtable->is_active(lr) || lr->type != EMBEDDED_LIBRETRO)
        return NULL;
    /* user_data points to LibretroInstanceData which starts with LibretroHost* */
    typedef struct { LibretroHost* host; } LRData;
    return ((LRData*)lr->user_data)->host;
}

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
