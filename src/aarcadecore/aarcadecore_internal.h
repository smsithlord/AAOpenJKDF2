/*
 * aarcadecore_internal.h — Internal types for the AArcade Core DLL
 */

#ifndef AARCADECORE_INTERNAL_H
#define AARCADECORE_INTERNAL_H

#include "aarcadecore_api.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMBEDDED_NONE = 0,
    EMBEDDED_LIBRETRO,
    EMBEDDED_STEAMWORKS_BROWSER
} EmbeddedInstanceType;

typedef struct EmbeddedInstance EmbeddedInstance;

typedef bool (*EmbeddedInstance_InitFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_ShutdownFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_UpdateFn)(EmbeddedInstance* inst);
typedef bool (*EmbeddedInstance_IsActiveFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_RenderFn)(
    EmbeddedInstance* inst, void* pixelData,
    int width, int height, int is16bit, int bpp);

typedef struct EmbeddedInstanceVtable {
    EmbeddedInstance_InitFn     init;
    EmbeddedInstance_ShutdownFn shutdown;
    EmbeddedInstance_UpdateFn   update;
    EmbeddedInstance_IsActiveFn is_active;
    EmbeddedInstance_RenderFn   render;
} EmbeddedInstanceVtable;

struct EmbeddedInstance {
    EmbeddedInstanceType type;
    const EmbeddedInstanceVtable* vtable;
    const char* target_material;
    void* user_data;
};

/* Global host callbacks (set during aarcadecore_init) */
extern AACoreHostCallbacks g_host;

#ifdef __cplusplus
}
#endif

#endif /* AARCADECORE_INTERNAL_H */
