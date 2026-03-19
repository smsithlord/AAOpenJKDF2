/*
 * LibretroInstance — EmbeddedInstance implementation for Libretro cores
 *
 * Refactored from libretro_integration.c. Wraps LibretroHost with the
 * EmbeddedInstance interface so it can render to in-game surfaces.
 */

#include "LibretroInstance.h"
#include "libretro_host.h"
#include "../../libretro_examples/libretro.h"
#include "../../stdPlatform.h"
#include "globals.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Per-instance state */
typedef struct LibretroInstanceData {
    LibretroHost* host;
    const char* core_path;
    const char* game_path;
    uint32_t frame_counter;
} LibretroInstanceData;

/* ========================================================================
 * Input helpers (same as before)
 * ======================================================================== */

static int16_t libretro_build_joypad_state(void)
{
    int16_t buttons = 0;

    if (stdControl_aKeyInfo[KEY_JOY1_B1])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (stdControl_aKeyInfo[KEY_JOY1_B2])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (stdControl_aKeyInfo[KEY_JOY1_B3])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (stdControl_aKeyInfo[KEY_JOY1_B4])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (stdControl_aKeyInfo[KEY_JOY1_B5])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (stdControl_aKeyInfo[KEY_JOY1_B7])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (stdControl_aKeyInfo[KEY_JOY1_B10])    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (stdControl_aKeyInfo[KEY_JOY1_B11])    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (stdControl_aKeyInfo[KEY_JOY1_B16])    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (stdControl_aKeyInfo[KEY_JOY1_B17])    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    if (stdControl_aKeyInfo[KEY_JOY1_B8])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_L3);
    if (stdControl_aKeyInfo[KEY_JOY1_B9])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_R3);
    if (stdControl_aKeyInfo[KEY_JOY1_HUP])    buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (stdControl_aKeyInfo[KEY_JOY1_HDOWN])  buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (stdControl_aKeyInfo[KEY_JOY1_HLEFT])  buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (stdControl_aKeyInfo[KEY_JOY1_HRIGHT]) buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);

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

    stdPlatform_Printf("LibretroInstance: Initializing...\n");

    data->host = libretro_host_create(data->core_path);
    if (!data->host) {
        stdPlatform_Printf("LibretroInstance: Failed to create host\n");
        return false;
    }

    libretro_host_get_system_info(data->host, &core_name, &core_version);
    stdPlatform_Printf("LibretroInstance: Loaded %s v%s\n", core_name, core_version);

    if (!libretro_host_load_game(data->host, data->game_path)) {
        stdPlatform_Printf("LibretroInstance: Failed to load game\n");
        libretro_host_destroy(data->host);
        data->host = NULL;
        return false;
    }

    EmbeddedInstance_RegisterTexture(inst);

    stdPlatform_Printf("LibretroInstance: Initialized successfully\n");
    return true;
}

static void libretro_inst_shutdown(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (data->host) {
        stdPlatform_Printf("LibretroInstance: Shutting down...\n");
        libretro_host_destroy(data->host);
        data->host = NULL;
    }
}

static void libretro_inst_update(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    if (!data->host)
        return;

    int16_t joypad_buttons = libretro_build_joypad_state();
    if (joypad_buttons) {
        stdPlatform_Printf("Libretro: Joypad input=0x%04X\n", (unsigned)(uint16_t)joypad_buttons);
    }
    libretro_host_set_input(data->host, 0, joypad_buttons);

    libretro_host_run_frame(data->host);

    data->frame_counter++;
    if (data->frame_counter % 600 == 0) {
        stdPlatform_Printf("Libretro: Frame %u\n", data->frame_counter);
    }
}

static bool libretro_inst_is_active(EmbeddedInstance* inst)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    return data->host != NULL;
}

static void libretro_inst_render(EmbeddedInstance* inst,
    rdMaterial* material, rdTexture* texture, int mipLevel,
    void* pixelData, int width, int height, rdTexFormat format)
{
    LibretroInstanceData* data = (LibretroInstanceData*)inst->user_data;
    const void* frame_data;
    unsigned frame_width, frame_height;
    size_t frame_pitch;
    bool is_xrgb8888;

    if (!data->host)
        return;

    frame_data = libretro_host_get_frame(data->host,
                                          &frame_width, &frame_height,
                                          &frame_pitch, &is_xrgb8888);
    if (!frame_data)
        return;

    if (format.is16bit) {
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
            int x = i % width;
            int y = i / width;
            dest[i] = (uint8_t)(((x + y) / 8) & 0xFF);
        }
    }
}

/* ========================================================================
 * Vtable
 * ======================================================================== */

static const EmbeddedInstanceVtable g_libretroVtable = {
    libretro_inst_init,
    libretro_inst_shutdown,
    libretro_inst_update,
    libretro_inst_is_active,
    libretro_inst_render
};

/* ========================================================================
 * Public API
 * ======================================================================== */

EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    LibretroInstanceData* data = (LibretroInstanceData*)calloc(1, sizeof(LibretroInstanceData));

    if (!inst || !data) {
        free(inst);
        free(data);
        return NULL;
    }

    data->core_path = core_path;
    data->game_path = game_path;
    data->frame_counter = 0;

    inst->type = EMBEDDED_LIBRETRO;
    inst->vtable = &g_libretroVtable;
    inst->target_material = material_name;
    inst->user_data = data;

    return inst;
}

void LibretroInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown)
        inst->vtable->shutdown(inst);
    free(inst->user_data);
    free(inst);
}
