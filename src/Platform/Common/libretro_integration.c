/*
 * Libretro Integration for OpenJKDF2
 *
 * Connects Libretro emulator output to in-game dynamic textures
 * Specifically displays emulator output on DynScreen.mat
 */

#include "libretro_host.h"
#include "../../libretro_examples/libretro.h"
#include "../../Engine/rdDynamicTexture.h"
#include "../../Engine/rdMaterial.h"
#include "../../stdPlatform.h"
#include <stdint.h>
#include <string.h>

/* Global libretro host instance */
static LibretroHost* g_libretro = NULL;

/* Configuration */
#define LIBRETRO_CORE_PATH "bsnes_libretro.dll"
#define LIBRETRO_GAME_PATH "testgame.zip"

/* Frame counter for update control */
static uint32_t g_frame_counter = 0;

/* ========================================================================
 * Dynamic Texture Callback for DynScreen.mat
 * ======================================================================== */

static void libretro_compscreen_callback(
    rdMaterial* material,
    rdTexture* texture,
    int mipLevel,
    void* pixelData,
    int width,
    int height,
    rdTexFormat format,
    void* userData)
{
    const void* frame_data;
    unsigned frame_width, frame_height;
    size_t frame_pitch;
    bool is_xrgb8888;

    /* Only update the highest mip level */
    if (mipLevel != 0) {
        return;
    }

    /* Check if libretro is initialized and has a frame */
    if (!g_libretro) {
        return;
    }

    frame_data = libretro_host_get_frame(g_libretro,
                                          &frame_width,
                                          &frame_height,
                                          &frame_pitch,
                                          &is_xrgb8888);

    if (!frame_data) {
        return;
    }

    /* Convert and copy frame to texture based on format */
    if (format.is16bit) {
        /* 16-bit RGB565 texture */
        uint16_t* dest = (uint16_t*)pixelData;

        /* Calculate scaling factors (fixed point with 16-bit fraction) */
        int scale_x = ((int)frame_width << 16) / width;
        int scale_y = ((int)frame_height << 16) / height;

        if (is_xrgb8888) {
            /* Convert XRGB8888 to RGB565 with scaling */
            const uint32_t* src = (const uint32_t*)frame_data;
            int y, x;

            for (y = 0; y < height; y++) {
                int src_y = (y * scale_y) >> 16;
                if (src_y >= (int)frame_height) src_y = frame_height - 1;

                for (x = 0; x < width; x++) {
                    int src_x = (x * scale_x) >> 16;
                    if (src_x >= (int)frame_width) src_x = frame_width - 1;

                    uint32_t color = src[src_y * (frame_pitch / 4) + src_x];

                    /* Extract RGB components */
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;

                    /* Convert to RGB565 */
                    uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

                    dest[y * width + x] = rgb565;
                }
            }
        } else {
            /* Direct copy RGB565 to RGB565 with scaling */
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
        /* 8-bit paletted texture - not ideal for video output */
        /* For now, just show a placeholder pattern */
        uint8_t* dest = (uint8_t*)pixelData;
        int i;

        /* Create a simple test pattern to indicate libretro is active */
        for (i = 0; i < width * height; i++) {
            int x = i % width;
            int y = i / width;
            dest[i] = (uint8_t)(((x + y) / 8) & 0xFF);
        }
    }
}

/* ========================================================================
 * Public Integration Functions
 * ======================================================================== */

void libretro_integration_init(void)
{
    const char* core_name;
    const char* core_version;

    stdPlatform_Printf("Libretro: Initializing integration...\n");

    /* Create libretro host and load core */
    g_libretro = libretro_host_create(LIBRETRO_CORE_PATH);
    if (!g_libretro) {
        stdPlatform_Printf("Libretro: Failed to create host\n");
        return;
    }

    /* Get core info */
    libretro_host_get_system_info(g_libretro, &core_name, &core_version);
    stdPlatform_Printf("Libretro: Loaded %s v%s\n", core_name, core_version);

    /* Load game */
    if (!libretro_host_load_game(g_libretro, LIBRETRO_GAME_PATH)) {
        stdPlatform_Printf("Libretro: Failed to load game\n");
        libretro_host_destroy(g_libretro);
        g_libretro = NULL;
        return;
    }

    /* Register dynamic texture callback for DynScreen.mat */
    rdDynamicTexture_Register("DynScreen.mat", libretro_compscreen_callback, NULL);

    stdPlatform_Printf("Libretro: Integration initialized successfully\n");
    stdPlatform_Printf("Libretro: Emulator output will appear on DynScreen.mat\n");
}

static int16_t libretro_build_joypad_state(void)
{
    int16_t buttons = 0;

    /* Map OpenJKDF2 gamepad keys to Libretro RETRO_DEVICE_JOYPAD bits.
     * stdControl_aKeyInfo[] is nonzero when a key is pressed.
     * SDL button A = KEY_JOY1_B1, B = B2, X = B3, Y = B4, etc.
     * See stdControl_ReadGamepad() in SDL2/stdControl.c for the mapping. */
    /* Map by physical position: Xbox bottom=SNES bottom, etc. */
    if (stdControl_aKeyInfo[KEY_JOY1_B1])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_B);      /* Xbox A (bottom) → SNES B (bottom) */
    if (stdControl_aKeyInfo[KEY_JOY1_B2])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_A);      /* Xbox B (right)  → SNES A (right)  */
    if (stdControl_aKeyInfo[KEY_JOY1_B3])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);      /* Xbox X (left)   → SNES Y (left)   */
    if (stdControl_aKeyInfo[KEY_JOY1_B4])     buttons |= (1 << RETRO_DEVICE_ID_JOYPAD_X);      /* Xbox Y (top)    → SNES X (top)    */
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

void libretro_integration_update(void)
{
    if (!g_libretro) {
        return;
    }

    /* Forward gamepad input to the Libretro core */
    int16_t joypad_buttons = libretro_build_joypad_state();
    if (joypad_buttons) {
        stdPlatform_Printf("Libretro: Joypad input=0x%04X\n", (unsigned)(uint16_t)joypad_buttons);
    }
    libretro_host_set_input(g_libretro, 0, joypad_buttons);

    /* Run one frame of emulation */
    libretro_host_run_frame(g_libretro);

    g_frame_counter++;

    /* Optional: Log progress periodically */
    if (g_frame_counter % 600 == 0) {
        stdPlatform_Printf("Libretro: Frame %u\n", g_frame_counter);
    }
}

void libretro_integration_shutdown(void)
{
    if (g_libretro) {
        stdPlatform_Printf("Libretro: Shutting down integration...\n");
        libretro_host_destroy(g_libretro);
        g_libretro = NULL;
    }
}

bool libretro_integration_is_active(void)
{
    return g_libretro != NULL;
}
