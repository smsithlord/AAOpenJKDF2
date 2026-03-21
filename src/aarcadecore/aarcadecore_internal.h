/*
 * aarcadecore_internal.h — Internal types for the AArcade Core DLL
 */

#ifndef AARCADECORE_INTERNAL_H
#define AARCADECORE_INTERNAL_H

#include "aarcadecore_api.h"
#include <stdbool.h>
#include <stdint.h>

/* Note: No extern "C" here — all DLL internals are C++ */

typedef enum {
    EMBEDDED_NONE = 0,
    EMBEDDED_LIBRETRO,
    EMBEDDED_STEAMWORKS_BROWSER,
    EMBEDDED_ULTRALIGHT
} EmbeddedInstanceType;

typedef struct EmbeddedInstance EmbeddedInstance;

typedef bool (*EmbeddedInstance_InitFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_ShutdownFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_UpdateFn)(EmbeddedInstance* inst);
typedef bool (*EmbeddedInstance_IsActiveFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_RenderFn)(
    EmbeddedInstance* inst, void* pixelData,
    int width, int height, int is16bit, int bpp);
typedef void (*EmbeddedInstance_KeyDownFn)(EmbeddedInstance* inst, int vk_code, int modifiers);
typedef void (*EmbeddedInstance_KeyUpFn)(EmbeddedInstance* inst, int vk_code, int modifiers);
typedef void (*EmbeddedInstance_KeyCharFn)(EmbeddedInstance* inst, unsigned int unicode_char, int modifiers);
typedef void (*EmbeddedInstance_MouseMoveFn)(EmbeddedInstance* inst, int x, int y);
typedef void (*EmbeddedInstance_MouseDownFn)(EmbeddedInstance* inst, int button);
typedef void (*EmbeddedInstance_MouseUpFn)(EmbeddedInstance* inst, int button);
typedef void (*EmbeddedInstance_MouseWheelFn)(EmbeddedInstance* inst, int delta);
typedef const char* (*EmbeddedInstance_GetTitleFn)(EmbeddedInstance* inst);

typedef struct EmbeddedInstanceVtable {
    EmbeddedInstance_InitFn     init;
    EmbeddedInstance_ShutdownFn shutdown;
    EmbeddedInstance_UpdateFn   update;
    EmbeddedInstance_IsActiveFn is_active;
    EmbeddedInstance_RenderFn   render;
    EmbeddedInstance_KeyDownFn  key_down;
    EmbeddedInstance_KeyUpFn    key_up;
    EmbeddedInstance_KeyCharFn  key_char;
    EmbeddedInstance_MouseMoveFn  mouse_move;
    EmbeddedInstance_MouseDownFn  mouse_down;
    EmbeddedInstance_MouseUpFn    mouse_up;
    EmbeddedInstance_MouseWheelFn mouse_wheel;
    EmbeddedInstance_GetTitleFn get_title;
} EmbeddedInstanceVtable;

struct EmbeddedInstance {
    EmbeddedInstanceType type;
    const EmbeddedInstanceVtable* vtable;
    const char* target_material;
    void* user_data;
};

/* Global host callbacks (set during aarcadecore_init) */
extern AACoreHostCallbacks g_host;

#endif /* AARCADECORE_INTERNAL_H */
