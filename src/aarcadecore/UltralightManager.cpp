#include "aarcadecore_internal.h"

/* Forward declarations */
EmbeddedInstance* UltralightInstance_Create(const char* htmlPath, const char* material_name);
void UltralightInstance_Destroy(EmbeddedInstance* inst);

#define UL_DEFAULT_HTML     "file:///aarcadecore/ui/ui.html"
#define UL_DEFAULT_MATERIAL "compscreen.mat"

static EmbeddedInstance* g_activeInstance = NULL;

void UltralightManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("UltralightManager: Initializing...\n");

    g_activeInstance = UltralightInstance_Create(UL_DEFAULT_HTML, UL_DEFAULT_MATERIAL);
    if (!g_activeInstance) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to create instance\n");
        return;
    }

    if (!g_activeInstance->vtable->init(g_activeInstance)) {
        if (g_host.host_printf) g_host.host_printf("UltralightManager: Failed to init instance\n");
        UltralightInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void UltralightManager_Shutdown(void)
{
    if (g_activeInstance) {
        UltralightInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void UltralightManager_Update(void)
{
    if (g_activeInstance && g_activeInstance->vtable->update)
        g_activeInstance->vtable->update(g_activeInstance);
}

EmbeddedInstance* UltralightManager_GetActive(void)
{
    return g_activeInstance;
}
