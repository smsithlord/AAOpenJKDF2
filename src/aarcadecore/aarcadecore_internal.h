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
typedef int (*EmbeddedInstance_GetWidthFn)(EmbeddedInstance* inst);
typedef int (*EmbeddedInstance_GetHeightFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_NavigateFn)(EmbeddedInstance* inst, const char* url);
typedef void (*EmbeddedInstance_GoBackFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_GoForwardFn)(EmbeddedInstance* inst);
typedef void (*EmbeddedInstance_ReloadFn)(EmbeddedInstance* inst);
typedef bool (*EmbeddedInstance_CanGoBackFn)(EmbeddedInstance* inst);
typedef bool (*EmbeddedInstance_CanGoForwardFn)(EmbeddedInstance* inst);

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
    EmbeddedInstance_GetWidthFn get_width;
    EmbeddedInstance_GetHeightFn get_height;
    EmbeddedInstance_NavigateFn navigate;
    EmbeddedInstance_GoBackFn go_back;
    EmbeddedInstance_GoForwardFn go_forward;
    EmbeddedInstance_ReloadFn reload;
    EmbeddedInstance_CanGoBackFn can_go_back;
    EmbeddedInstance_CanGoForwardFn can_go_forward;
} EmbeddedInstanceVtable;

struct EmbeddedInstance {
    EmbeddedInstanceType type;
    const EmbeddedInstanceVtable* vtable;
    const char* target_material;
    void* user_data;
    uint32_t lastRenderedFrame;  /* frame when render() was last called */
    uint32_t lastSeenFrame;      /* frame when thing's material was seen in engine render */
};

/* Global host callbacks (set during aarcadecore_init) */
extern AACoreHostCallbacks g_host;

/* Fullscreen / input mode instance pointers (defined in aarcadecore.cpp) */
EmbeddedInstance* aarcadecore_getFullscreenInstance(void);
void aarcadecore_setFullscreenInstance(EmbeddedInstance* inst);
EmbeddedInstance* aarcadecore_getInputModeInstance(void);
void aarcadecore_setInputModeInstance(EmbeddedInstance* inst);

#endif /* AARCADECORE_INTERNAL_H */
