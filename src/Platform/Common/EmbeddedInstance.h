#ifndef EMBEDDED_INSTANCE_H
#define EMBEDDED_INSTANCE_H

#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMBEDDED_NONE = 0,
    EMBEDDED_LIBRETRO,
    EMBEDDED_STEAMWORKS_BROWSER
} EmbeddedInstanceType;

typedef struct EmbeddedInstance EmbeddedInstance;

/* Vtable function signatures */
typedef bool (*EmbeddedInstance_InitFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_ShutdownFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_UpdateFn)(EmbeddedInstance* inst);
typedef bool (*EmbeddedInstance_IsActiveFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_RenderCallbackFn)(
    EmbeddedInstance* inst, rdMaterial* material, rdTexture* texture,
    int mipLevel, void* pixelData, int width, int height, rdTexFormat format);

typedef struct EmbeddedInstanceVtable {
    EmbeddedInstance_InitFn         init;
    EmbeddedInstance_ShutdownFn     shutdown;
    EmbeddedInstance_UpdateFn       update;
    EmbeddedInstance_IsActiveFn     is_active;
    EmbeddedInstance_RenderCallbackFn render_callback;
} EmbeddedInstanceVtable;

struct EmbeddedInstance {
    EmbeddedInstanceType type;
    const EmbeddedInstanceVtable* vtable;
    const char* target_material;  /* material name to render to, e.g. "compscreen.mat" */
    void* user_data;              /* type-specific state */
};

/* Common helpers */
void EmbeddedInstance_RegisterTexture(EmbeddedInstance* inst);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDED_INSTANCE_H */
