/*
 * SteamworksWebBrowserInstance — Stub EmbeddedInstance for Steamworks web browser
 */

#include "aarcadecore_internal.h"
#include <stdlib.h>
#include <stdint.h>

static bool swb_init(EmbeddedInstance* inst)
{
    if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowser: Init (stub)\n");
    return true;
}

static void swb_shutdown(EmbeddedInstance* inst)
{
    if (g_host.host_printf) g_host.host_printf("SteamworksWebBrowser: Shutdown (stub)\n");
}

static void swb_update(EmbeddedInstance* inst) { }

static bool swb_is_active(EmbeddedInstance* inst) { return true; }

static void swb_render(EmbeddedInstance* inst,
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    if (is16bit) {
        uint16_t* dest = (uint16_t*)pixelData;
        int i;
        for (i = 0; i < width * height; i++) {
            int x = i % width;
            int y = i / width;
            dest[i] = (uint16_t)(((x ^ y) & 0x8) ? 0x07E0 : 0x001F);
        }
    }
}

static const EmbeddedInstanceVtable g_swbVtable = {
    swb_init, swb_shutdown, swb_update, swb_is_active, swb_render
};

EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    if (!inst) return NULL;
    inst->type = EMBEDDED_STEAMWORKS_BROWSER;
    inst->vtable = &g_swbVtable;
    inst->target_material = material_name;
    inst->user_data = NULL;
    return inst;
}

void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown) inst->vtable->shutdown(inst);
    free(inst);
}
