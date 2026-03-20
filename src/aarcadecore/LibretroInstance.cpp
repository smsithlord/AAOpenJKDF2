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

/* Key indices matching OpenJKDF2's KEY_JOY1_* defines.
 * These are the values the host's get_key_state expects. */
#define AACORE_KEY_JOY1_B1   0x100
#define AACORE_KEY_JOY1_B2   0x101
#define AACORE_KEY_JOY1_B3   0x102
#define AACORE_KEY_JOY1_B4   0x103
#define AACORE_KEY_JOY1_B5   0x104
#define AACORE_KEY_JOY1_B7   0x106
#define AACORE_KEY_JOY1_B8   0x107
#define AACORE_KEY_JOY1_B10  0x12D
#define AACORE_KEY_JOY1_B11  0x12E
#define AACORE_KEY_JOY1_B16  0x133
#define AACORE_KEY_JOY1_B17  0x134
#define AACORE_KEY_JOY1_B9   0x12C
#define AACORE_KEY_JOY1_HUP  0x109
#define AACORE_KEY_JOY1_HDOWN 0x10B
#define AACORE_KEY_JOY1_HLEFT 0x108
#define AACORE_KEY_JOY1_HRIGHT 0x10A

static int16_t libretro_build_joypad_state(void)
{
    int16_t buttons = 0;
    if (!g_host.get_key_state) return 0;

    if (g_host.get_key_state(AACORE_KEY_JOY1_B1))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B2))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B3))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B4))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B5))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B7))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B10))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B11))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B16))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B17))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B8))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L3);
    if (g_host.get_key_state(AACORE_KEY_JOY1_B9))     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R3);
    if (g_host.get_key_state(AACORE_KEY_JOY1_HUP))    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (g_host.get_key_state(AACORE_KEY_JOY1_HDOWN))  buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (g_host.get_key_state(AACORE_KEY_JOY1_HLEFT))  buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (g_host.get_key_state(AACORE_KEY_JOY1_HRIGHT)) buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);

    return buttons;
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

    int16_t joypad_buttons = libretro_build_joypad_state();
    /* OR physical gamepad with emulated keyboard joypad */
    joypad_buttons |= data->emulatedJoypad;
    libretro_host_set_input(data->host, 0, joypad_buttons);
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
        uint8_t* dest = (uint8_t*)pixelData;
        int i;
        for (i = 0; i < width * height; i++) {
            dest[i] = (uint8_t)((((i % width) + (i / width)) / 8) & 0xFF);
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
    libretro_inst_key_char
};

/* ========================================================================
 * Public API (internal to DLL)
 * ======================================================================== */

EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    LibretroInstanceData* data = (LibretroInstanceData*)calloc(1, sizeof(LibretroInstanceData));
    if (!inst || !data) { free(inst); free(data); return NULL; }

    data->core_path = core_path;
    data->game_path = game_path;

    inst->type = EMBEDDED_LIBRETRO;
    inst->vtable = &g_libretroVtable;
    inst->target_material = material_name;
    inst->user_data = data;
    return inst;
}

void LibretroInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown) inst->vtable->shutdown(inst);
    free(inst->user_data);
    free(inst);
}
