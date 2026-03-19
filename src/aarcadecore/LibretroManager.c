#include "aarcadecore_internal.h"
#include <stdlib.h>

/* Forward declarations (internal to DLL) */
EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name);
void LibretroInstance_Destroy(EmbeddedInstance* inst);

#define LIBRETRO_CORE_PATH "bsnes_libretro.dll"
#define LIBRETRO_GAME_PATH "testgame.zip"
#define LIBRETRO_MATERIAL  "compscreen.mat"

static EmbeddedInstance* g_activeInstance = NULL;

void LibretroManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("LibretroManager: Initializing...\n");

    g_activeInstance = LibretroInstance_Create(LIBRETRO_CORE_PATH, LIBRETRO_GAME_PATH, LIBRETRO_MATERIAL);
    if (!g_activeInstance) {
        if (g_host.host_printf) g_host.host_printf("LibretroManager: Failed to create instance\n");
        return;
    }

    if (!g_activeInstance->vtable->init(g_activeInstance)) {
        if (g_host.host_printf) g_host.host_printf("LibretroManager: Failed to init instance\n");
        LibretroInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void LibretroManager_Shutdown(void)
{
    if (g_activeInstance) {
        LibretroInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void LibretroManager_Update(void)
{
    if (g_activeInstance && g_activeInstance->vtable->update)
        g_activeInstance->vtable->update(g_activeInstance);
}

EmbeddedInstance* LibretroManager_GetActive(void)
{
    return g_activeInstance;
}
