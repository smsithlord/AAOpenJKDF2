#include "EmbeddedInstance.h"
#include "../../Engine/rdDynamicTexture.h"
#include "../../stdPlatform.h"

/* Bridge callback: routes the dynamic texture callback to the EmbeddedInstance's vtable */
static void EmbeddedInstance_DynamicTextureCallback(
    rdMaterial* material, rdTexture* texture, int mipLevel,
    void* pixelData, int width, int height, rdTexFormat format, void* userData)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)userData;
    if (!inst || !inst->vtable || !inst->vtable->render_callback)
        return;
    if (mipLevel != 0)
        return;
    inst->vtable->render_callback(inst, material, texture, mipLevel, pixelData, width, height, format);
}

void EmbeddedInstance_RegisterTexture(EmbeddedInstance* inst)
{
    if (!inst || !inst->target_material)
        return;
    rdDynamicTexture_Register(inst->target_material, EmbeddedInstance_DynamicTextureCallback, inst);
    stdPlatform_Printf("EmbeddedInstance: Registered texture callback for '%s'\n", inst->target_material);
}
