/*
 * LibretroInstance — EmbeddedInstance implementation for Libretro cores
 */

#include "aarcadecore_internal.h"
#include "libretro_host.h"
#include "../../libretro_examples/libretro.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct LibretroInstanceData {
    LibretroHost* host;
    const char* core_path;
    const char* game_path;
    uint32_t frame_counter;
    int inputMode;         /* 0=emulated, 1=raw */
    int16_t emulatedJoypad; /* joypad mask from keyboard in emulated mode */
} LibretroInstanceData;

#define LIBRETRO_INPUT_EMULATED 0
#define LIBRETRO_INPUT_RAW      1

/* ========================================================================
 * Input — uses host callback to read key state
 * ======================================================================== */

/* SDL_GameControllerButton values — keep in sync with SDL_gamecontroller.h.
 * We mirror them here so LibretroInstance.cpp doesn't need to pull in SDL. */
#define AA_BTN_A             0
#define AA_BTN_B             1
#define AA_BTN_X             2
#define AA_BTN_Y             3
#define AA_BTN_BACK          4
#define AA_BTN_GUIDE         5
#define AA_BTN_START         6
#define AA_BTN_LEFTSTICK     7
#define AA_BTN_RIGHTSTICK    8
#define AA_BTN_LEFTSHOULDER  9
#define AA_BTN_RIGHTSHOULDER 10
#define AA_BTN_DPAD_UP       11
#define AA_BTN_DPAD_DOWN     12
#define AA_BTN_DPAD_LEFT     13
#define AA_BTN_DPAD_RIGHT    14

#define AA_TRIGGER_THRESH    0x2666  /* ~30% of 0x7FFF, matches stdControl_ReadGamepad */

/* Build a RETRO_DEVICE_JOYPAD button mask from the host gamepad snapshot.
 * SNES-style face mapping (physical position, not name):
 *   Xbox A (south)  -> RETRO B   (action/jump, SNES south)
 *   Xbox B (east)   -> RETRO A
 *   Xbox X (west)   -> RETRO Y
 *   Xbox Y (north)  -> RETRO X
 * Uses host->get_gamepad_state instead of stdControl_aKeyInfo so input keeps
 * flowing to the core even when engine input polling is suppressed
 * (input lock mode, pause menus, etc.). */
static int16_t libretro_build_joypad_state(void)
{
    int16_t buttons = 0;
    if (!g_host.get_gamepad_state) return 0;

    AACoreGamepadState s;
    g_host.get_gamepad_state(0, &s);
    if (!s.connected) return 0;

    if (s.buttons & (1u << AA_BTN_A))             buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (s.buttons & (1u << AA_BTN_B))             buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (s.buttons & (1u << AA_BTN_X))             buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (s.buttons & (1u << AA_BTN_Y))             buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (s.buttons & (1u << AA_BTN_BACK))          buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (s.buttons & (1u << AA_BTN_START))         buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (s.buttons & (1u << AA_BTN_LEFTSHOULDER))  buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (s.buttons & (1u << AA_BTN_RIGHTSHOULDER)) buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (s.buttons & (1u << AA_BTN_LEFTSTICK))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L3);
    if (s.buttons & (1u << AA_BTN_RIGHTSTICK))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R3);
    if (s.buttons & (1u << AA_BTN_DPAD_UP))       buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (s.buttons & (1u << AA_BTN_DPAD_DOWN))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (s.buttons & (1u << AA_BTN_DPAD_LEFT))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (s.buttons & (1u << AA_BTN_DPAD_RIGHT))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (s.lt > AA_TRIGGER_THRESH)                 buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (s.rt > AA_TRIGGER_THRESH)                 buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);

    return buttons;
}

/* Push analog stick values for port 0 into the host. Left stick -> index 0,
 * Right stick -> index 1. Most N64 cores automatically treat the right stick
 * as the C-buttons, so no extra mapping needed here. */
static void libretro_push_analog_state(LibretroHost* host)
{
    if (!host || !g_host.get_gamepad_state) return;
    AACoreGamepadState s;
    g_host.get_gamepad_state(0, &s);
    if (!s.connected) {
        libretro_host_set_analog(host, 0, 0, 0, 0);
        libretro_host_set_analog(host, 0, 1, 0, 0);
        return;
    }
    libretro_host_set_analog(host, 0, 0, s.lx, s.ly);
    libretro_host_set_analog(host, 0, 1, s.rx, s.ry);
}

/* ========================================================================
 * Vtable implementations
 * ======================================================================== */

static bool libretro_inst_init(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    const char* core_name;
    const char* core_version;

    if (g_host.host_printf) g_host.host_printf("LibretroInstance: Initializing...\n");

    data->host = libretro_host_create(data->core_path);
    if (!data->host) {
        if (g_host.host_printf) g_host.host_printf("LibretroInstance: Failed to create host\n");
        return false;
    }

    libretro_host_get_system_info(data->host, &core_name, &core_version);
    if (g_host.host_printf) g_host.host_printf("LibretroInstance: Loaded %s v%s\n", core_name, core_version);

    if (!libretro_host_load_game(data->host, data->game_path)) {
        if (g_host.host_printf) g_host.host_printf("LibretroInstance: Failed to load game\n");
        libretro_host_destroy(data->host);
        data->host = NULL;
        return false;
    }

    if (g_host.host_printf) g_host.host_printf("LibretroInstance: Initialized successfully\n");
    return true;
}

static void libretro_inst_shutdown(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (data->host) {
        if (g_host.host_printf) g_host.host_printf("LibretroInstance: Shutting down...\n");
        libretro_host_destroy(data->host);
        data->host = NULL;
    }
}

static void libretro_inst_update(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (!data->host) return;

    /* Skip frame processing if not visible and not fullscreen/input target */
    extern uint32_t aarcadecore_getEngineFrame(void);
    extern EmbeddedInstance* aarcadecore_getFullscreenInstance(void);
    extern EmbeddedInstance* aarcadecore_getInputModeInstance(void);
    if (inst != aarcadecore_getFullscreenInstance() &&
        inst != aarcadecore_getInputModeInstance() &&
        inst->lastSeenFrame + 1 < aarcadecore_getEngineFrame())
        return;

    int16_t joypad_buttons = libretro_build_joypad_state();
    /* OR physical gamepad with emulated keyboard joypad */
    joypad_buttons |= data->emulatedJoypad;
    libretro_host_set_input(data->host, 0, joypad_buttons);
    libretro_push_analog_state(data->host);
    libretro_host_run_frame(data->host);

    data->frame_counter++;
    if (data->frame_counter % 600 == 0 && g_host.host_printf) {
        g_host.host_printf("Libretro: Frame %u\n", data->frame_counter);
    }
}

static bool libretro_inst_is_active(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    return data->host != NULL;
}

static void libretro_inst_render(EmbeddedInstance* inst,
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    const void* frame_data;
    unsigned frame_width, frame_height;
    size_t frame_pitch;
    bool is_xrgb8888;

    if (!data->host) return;

    /* Skip duplicate 16-bit renders in same frame (per-thing texture cached by host).
     * 32-bit fullscreen renders always proceed — different buffer/format. */
    extern uint32_t aarcadecore_getEngineFrame(void);
    uint32_t frame = aarcadecore_getEngineFrame();
    if (is16bit && inst->lastRenderedFrame == frame)
        return;
    if (is16bit)
        inst->lastRenderedFrame = frame;

    frame_data = libretro_host_get_frame(data->host,
                                          &frame_width, &frame_height,
                                          &frame_pitch, &is_xrgb8888);
    if (!frame_data) return;

    if (is16bit) {
        uint16_t* dest = (uint16_t*)pixelData;
        int scale_x = ((int)frame_width << 16) / width;
        int scale_y = ((int)frame_height << 16) / height;

        if (is_xrgb8888) {
            const uint32_t* src = (const uint32_t*)frame_data;
            int y, x;
            for (y = 0; y < height; y++) {
                int src_y = (y * scale_y) >> 16;
                if (src_y >= (int)frame_height) src_y = frame_height - 1;
                for (x = 0; x < width; x++) {
                    int src_x = (x * scale_x) >> 16;
                    if (src_x >= (int)frame_width) src_x = frame_width - 1;
                    uint32_t color = src[src_y * (frame_pitch / 4) + src_x];
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;
                    dest[y * width + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                }
            }
        } else {
            const uint16_t* src = (const uint16_t*)frame_data;
            int y, x;
            for (y = 0; y < height; y++) {
                int src_y = (y * scale_y) >> 16;
                if (src_y >= (int)frame_height) src_y = frame_height - 1;
                for (x = 0; x < width; x++) {
                    int src_x = (x * scale_x) >> 16;
                    if (src_x >= (int)frame_width) src_x = frame_width - 1;
                    dest[y * width + x] = src[src_y * (frame_pitch / 2) + src_x];
                }
            }
        }
    } else {
        /* 32bpp BGRA output (fullscreen overlay) */
        uint32_t* dest = (uint32_t*)pixelData;
        int scale_x = ((int)frame_width << 16) / width;
        int scale_y = ((int)frame_height << 16) / height;

        if (is_xrgb8888) {
            const uint32_t* src = (const uint32_t*)frame_data;
            int y, x;
            for (y = 0; y < height; y++) {
                int src_y = (y * scale_y) >> 16;
                if (src_y >= (int)frame_height) src_y = frame_height - 1;
                for (x = 0; x < width; x++) {
                    int src_x = (x * scale_x) >> 16;
                    if (src_x >= (int)frame_width) src_x = frame_width - 1;
                    uint32_t color = src[src_y * (frame_pitch / 4) + src_x];
                    /* XRGB8888 → BGRA: swap R and B, set A=255 */
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;
                    dest[y * width + x] = (255u << 24) | (r << 16) | (g << 8) | b;
                }
            }
        } else {
            const uint16_t* src = (const uint16_t*)frame_data;
            int y, x;
            for (y = 0; y < height; y++) {
                int src_y = (y * scale_y) >> 16;
                if (src_y >= (int)frame_height) src_y = frame_height - 1;
                for (x = 0; x < width; x++) {
                    int src_x = (x * scale_x) >> 16;
                    if (src_x >= (int)frame_width) src_x = frame_width - 1;
                    uint16_t pixel = src[src_y * (frame_pitch / 2) + src_x];
                    /* RGB565 → BGRA */
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    dest[y * width + x] = (255u << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }

}

/* ========================================================================
 * Keyboard input — emulated and raw modes
 * ======================================================================== */

/* Map Windows VK code to RETROK_* for raw mode */
static unsigned vk_to_retrok(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return 'a' + (vk - 'A'); /* RETROK_a..z = 97..122 */
    if (vk >= '0' && vk <= '9') return '0' + (vk - '0'); /* RETROK_0..9 = 48..57 */
    if (vk >= VK_F1 && vk <= VK_F12) return 282 + (vk - VK_F1); /* RETROK_F1=282 */
    switch (vk) {
        case VK_RETURN:  return 13;   /* RETROK_RETURN */
        case VK_ESCAPE:  return 27;   /* RETROK_ESCAPE */
        case VK_SPACE:   return 32;   /* RETROK_SPACE */
        case VK_BACK:    return 8;    /* RETROK_BACKSPACE */
        case VK_TAB:     return 9;    /* RETROK_TAB */
        case VK_DELETE:  return 127;  /* RETROK_DELETE */
        case VK_UP:      return 273;  /* RETROK_UP */
        case VK_DOWN:    return 274;  /* RETROK_DOWN */
        case VK_RIGHT:   return 275;  /* RETROK_RIGHT */
        case VK_LEFT:    return 276;  /* RETROK_LEFT */
        case VK_INSERT:  return 277;
        case VK_HOME:    return 278;
        case VK_END:     return 279;
        case VK_PRIOR:   return 280;  /* Page Up */
        case VK_NEXT:    return 281;  /* Page Down */
        case VK_SHIFT:   return 304;  /* RETROK_LSHIFT */
        case VK_CONTROL: return 306;  /* RETROK_LCTRL */
        case VK_MENU:    return 308;  /* RETROK_LALT */
        default:         return 0;    /* RETROK_UNKNOWN */
    }
}

/* Map VK code to RETRO_DEVICE_ID_JOYPAD_* bit for emulated mode.
 * Returns -1 if no mapping. */
static int vk_to_emulated_joypad(int vk)
{
    switch (vk) {
        case 'W': case VK_UP:    return RETRO_DEVICE_ID_JOYPAD_UP;
        case 'S': case VK_DOWN:  return RETRO_DEVICE_ID_JOYPAD_DOWN;
        case 'A': case VK_LEFT:  return RETRO_DEVICE_ID_JOYPAD_LEFT;
        case 'D': case VK_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
        case VK_RETURN:          return RETRO_DEVICE_ID_JOYPAD_START;
        case VK_SHIFT:           return RETRO_DEVICE_ID_JOYPAD_SELECT;
        case 'J':                return RETRO_DEVICE_ID_JOYPAD_B;
        case 'K':                return RETRO_DEVICE_ID_JOYPAD_A;
        case 'U':                return RETRO_DEVICE_ID_JOYPAD_Y;
        case 'I':                return RETRO_DEVICE_ID_JOYPAD_X;
        case 'Q':                return RETRO_DEVICE_ID_JOYPAD_L;
        case 'E':                return RETRO_DEVICE_ID_JOYPAD_R;
        default:                 return -1;
    }
}

static void libretro_inst_key_down(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (!data->host) return;

    if (data->inputMode == LIBRETRO_INPUT_EMULATED) {
        int bit = vk_to_emulated_joypad(vk_code);
        if (bit >= 0)
            data->emulatedJoypad |= (1 << bit);
    } else {
        unsigned retrok = vk_to_retrok(vk_code);
        if (retrok)
            libretro_host_set_key_state(data->host, retrok, 1);
    }
}

static void libretro_inst_key_up(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (!data->host) return;

    if (data->inputMode == LIBRETRO_INPUT_EMULATED) {
        int bit = vk_to_emulated_joypad(vk_code);
        if (bit >= 0)
            data->emulatedJoypad &= ~(1 << bit);
    } else {
        unsigned retrok = vk_to_retrok(vk_code);
        if (retrok)
            libretro_host_set_key_state(data->host, retrok, 0);
    }
}

static void libretro_inst_key_char(EmbeddedInstance* inst, unsigned int unicode_char, int modifiers)
{
    /* Not used for Libretro — keyboard input uses key_down/key_up */
}

static const EmbeddedInstanceVtable g_libretroVtable = {
    libretro_inst_init,
    libretro_inst_shutdown,
    libretro_inst_update,
    libretro_inst_is_active,
    libretro_inst_render,
    libretro_inst_key_down,
    libretro_inst_key_up,
    libretro_inst_key_char,
    NULL, NULL, NULL, NULL, /* no mouse */
    NULL, /* get_title */
    NULL, NULL, /* get_width, get_height */
    NULL, /* navigate */
    NULL, NULL, NULL, NULL, NULL /* go_back, go_forward, reload, can_go_back, can_go_forward */
};

/* ========================================================================
 * Public API (internal to DLL)
 * ======================================================================== */

/* Used by JS bridge to reach the host of the currently-active core for option queries. */
extern "C" LibretroHost* LibretroInstance_GetHost(EmbeddedInstance* inst)
{
    if (!inst || inst->type != EMBEDDED_LIBRETRO || !inst->user_data) return NULL;
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    return data->host;
}

EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    LibretroInstanceData* data = (LibretroInstanceData*)calloc(1, sizeof(LibretroInstanceData));
    if (!inst || !data) { free(inst); free(data); return NULL; }

    data->core_path = _strdup(core_path);
    data->game_path = _strdup(game_path);

    inst->type = EMBEDDED_LIBRETRO;
    inst->vtable = &g_libretroVtable;
    inst->target_material = material_name;
    inst->user_data = data;
    return inst;
}

/* Capture a snapshot of the core's current frame at its native pixel aspect,
 * resized so the longest side is ≤ 512. Simple (no PAR correction) — frame
 * dimensions as reported by the core drive the output aspect directly. */
extern "C" bool LibretroInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                                 unsigned char** bgraOut,
                                                 int* widthOut, int* heightOut)
{
    if (!inst || inst->type != EMBEDDED_LIBRETRO) return false;
    if (!bgraOut || !widthOut || !heightOut) return false;
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (!data || !data->host) return false;

    unsigned frame_width = 0, frame_height = 0;
    size_t frame_pitch = 0;
    bool is_xrgb8888 = false;
    const void* frame_data = libretro_host_get_frame(data->host,
                                                     &frame_width, &frame_height,
                                                     &frame_pitch, &is_xrgb8888);
    if (!frame_data || frame_width == 0 || frame_height == 0) return false;

    int fw = (int)frame_width;
    int fh = (int)frame_height;

    /* Longest-side ≤ 512, aspect preserved. */
    const int maxDim = 512;
    int targetW = fw, targetH = fh;
    if (fw > maxDim || fh > maxDim) {
        if (fw >= fh) {
            targetW = maxDim;
            targetH = (int)((double)fh * maxDim / fw + 0.5);
        } else {
            targetH = maxDim;
            targetW = (int)((double)fw * maxDim / fh + 0.5);
        }
        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;
    }

    size_t outBytes = (size_t)targetW * targetH * 4;
    unsigned char* out = (unsigned char*)malloc(outBytes);
    if (!out) return false;

    if (is_xrgb8888) {
        const uint32_t* src = (const uint32_t*)frame_data;
        size_t srcStride = frame_pitch / 4;
        for (int ty = 0; ty < targetH; ty++) {
            int sy = ty * fh / targetH;
            if (sy >= fh) sy = fh - 1;
            for (int tx = 0; tx < targetW; tx++) {
                int sx = tx * fw / targetW;
                if (sx >= fw) sx = fw - 1;
                uint32_t color = src[(size_t)sy * srcStride + sx];
                uint8_t r = (color >> 16) & 0xFF;
                uint8_t g = (color >> 8)  & 0xFF;
                uint8_t b = (color >> 0)  & 0xFF;
                unsigned char* dst = out + ((size_t)ty * targetW + tx) * 4;
                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
            }
        }
    } else {
        /* RGB565 */
        const uint16_t* src = (const uint16_t*)frame_data;
        size_t srcStride = frame_pitch / 2;
        for (int ty = 0; ty < targetH; ty++) {
            int sy = ty * fh / targetH;
            if (sy >= fh) sy = fh - 1;
            for (int tx = 0; tx < targetW; tx++) {
                int sx = tx * fw / targetW;
                if (sx >= fw) sx = fw - 1;
                uint16_t pixel = src[(size_t)sy * srcStride + sx];
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5)  & 0x3F) << 2;
                uint8_t b = ((pixel >> 0)  & 0x1F) << 3;
                unsigned char* dst = out + ((size_t)ty * targetW + tx) * 4;
                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
            }
        }
    }

    *bgraOut = out;
    *widthOut = targetW;
    *heightOut = targetH;
    return true;
}

void LibretroInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown) inst->vtable->shutdown(inst);
    if (inst->user_data) {
        LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
        free((void*)data->core_path);
        free((void*)data->game_path);
    }
    free(inst->user_data);
    free(inst);
}
