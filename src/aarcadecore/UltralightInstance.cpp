/*
 * UltralightInstance — EmbeddedInstance for Ultralight HTML renderer
 *
 * Renders local HTML files to in-game textures using Ultralight's
 * CPU rendering mode (BitmapSurface).
 */

#include "aarcadecore_internal.h"
#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

using namespace ultralight;

#define UL_DEFAULT_WIDTH  512
#define UL_DEFAULT_HEIGHT 512

/* Per-instance state */
struct UltralightData {
    RefPtr<Renderer> renderer;
    RefPtr<View> view;
    const char* htmlPath;
    bool initialized;
};

/* ========================================================================
 * EmbeddedInstance vtable implementations
 * ======================================================================== */

static bool ul_init(EmbeddedInstance* inst)
{
    UltralightData* data = (UltralightData*)inst->user_data;

    if (g_host.host_printf) g_host.host_printf("Ultralight: Initializing...\n");

    /* Setup Platform handlers */
    Config config;
    config.resource_path_prefix = "./resources/";
    Platform::instance().set_config(config);
    Platform::instance().set_font_loader(GetPlatformFontLoader());
    Platform::instance().set_file_system(GetPlatformFileSystem("./"));

    /* Create renderer */
    data->renderer = Renderer::Create();
    if (!data->renderer) {
        if (g_host.host_printf) g_host.host_printf("Ultralight: Failed to create renderer\n");
        return false;
    }

    /* Create view with CPU rendering */
    ViewConfig viewConfig;
    viewConfig.is_accelerated = false;
    data->view = data->renderer->CreateView(UL_DEFAULT_WIDTH, UL_DEFAULT_HEIGHT, viewConfig, nullptr);
    if (!data->view) {
        if (g_host.host_printf) g_host.host_printf("Ultralight: Failed to create view\n");
        return false;
    }

    /* Load the HTML file and give the view keyboard focus */
    data->view->LoadURL(data->htmlPath);
    data->view->Focus();
    data->initialized = true;

    if (g_host.host_printf) g_host.host_printf("Ultralight: Initialized, loading %s\n", data->htmlPath);
    return true;
}

static void ul_shutdown(EmbeddedInstance* inst)
{
    UltralightData* data = (UltralightData*)inst->user_data;

    data->view = nullptr;
    data->renderer = nullptr;
    data->initialized = false;

    if (g_host.host_printf) g_host.host_printf("Ultralight: Shutdown complete\n");
}

static void ul_update(EmbeddedInstance* inst)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->renderer)
        return;

    data->renderer->Update();
    data->renderer->RefreshDisplay(0);
    data->renderer->Render();
}

static bool ul_is_active(EmbeddedInstance* inst)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    return data->initialized;
}

static void ul_render(EmbeddedInstance* inst,
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view)
        return;

    Surface* surface = data->view->surface();
    if (!surface)
        return;

    BitmapSurface* bitmapSurface = static_cast<BitmapSurface*>(surface);
    RefPtr<Bitmap> bitmap = bitmapSurface->bitmap();
    if (!bitmap)
        return;

    auto pixels = bitmap->LockPixelsSafe();
    if (!pixels || !pixels.data())
        return;

    uint8_t* src = (uint8_t*)pixels.data();
    uint32_t srcWidth = bitmap->width();
    uint32_t srcHeight = bitmap->height();
    uint32_t srcRowBytes = bitmap->row_bytes();

    if (is16bit) {
        /* Convert BGRA -> RGB565 with scaling */
        uint16_t* dest = (uint16_t*)pixelData;
        int scale_x = ((int)srcWidth << 16) / width;
        int scale_y = ((int)srcHeight << 16) / height;

        for (int y = 0; y < height; y++) {
            int sy = (y * scale_y) >> 16;
            if (sy >= (int)srcHeight) sy = srcHeight - 1;

            for (int x = 0; x < width; x++) {
                int sx = (x * scale_x) >> 16;
                if (sx >= (int)srcWidth) sx = srcWidth - 1;

                int idx = sy * srcRowBytes + sx * 4;
                uint8_t b = src[idx + 0];
                uint8_t g = src[idx + 1];
                uint8_t r = src[idx + 2];

                dest[y * width + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    }
}

static void ul_key_down(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;

    if (g_host.host_printf) g_host.host_printf("UL: key_down vk=0x%X mod=%d\n", vk_code, modifiers);

    KeyEvent evt;
    evt.type = KeyEvent::kType_RawKeyDown;
    evt.virtual_key_code = vk_code;
    evt.native_key_code = vk_code;
    evt.modifiers = 0;
    if (modifiers & AACORE_MOD_ALT)   evt.modifiers |= KeyEvent::kMod_AltKey;
    if (modifiers & AACORE_MOD_CTRL)  evt.modifiers |= KeyEvent::kMod_CtrlKey;
    if (modifiers & AACORE_MOD_SHIFT) evt.modifiers |= KeyEvent::kMod_ShiftKey;
    data->view->FireKeyEvent(evt);
}

static void ul_key_up(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;

    KeyEvent evt;
    evt.type = KeyEvent::kType_KeyUp;
    evt.virtual_key_code = vk_code;
    evt.native_key_code = vk_code;
    evt.modifiers = 0;
    if (modifiers & AACORE_MOD_ALT)   evt.modifiers |= KeyEvent::kMod_AltKey;
    if (modifiers & AACORE_MOD_CTRL)  evt.modifiers |= KeyEvent::kMod_CtrlKey;
    if (modifiers & AACORE_MOD_SHIFT) evt.modifiers |= KeyEvent::kMod_ShiftKey;
    data->view->FireKeyEvent(evt);
}

static void ul_key_char(EmbeddedInstance* inst, unsigned int unicode_char, int modifiers)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;

    if (g_host.host_printf) g_host.host_printf("UL: key_char U+%04X ('%c') mod=%d\n",
        unicode_char, (unicode_char >= 32 && unicode_char < 127) ? (char)unicode_char : '?', modifiers);

    KeyEvent evt;
    evt.type = KeyEvent::kType_Char;
    evt.virtual_key_code = 0;
    evt.native_key_code = 0;
    evt.modifiers = 0;
    if (modifiers & AACORE_MOD_ALT)   evt.modifiers |= KeyEvent::kMod_AltKey;
    if (modifiers & AACORE_MOD_CTRL)  evt.modifiers |= KeyEvent::kMod_CtrlKey;
    if (modifiers & AACORE_MOD_SHIFT) evt.modifiers |= KeyEvent::kMod_ShiftKey;

    /* Convert unicode codepoint to UTF-16 string */
    unsigned short buf[3] = {0};
    size_t len = 1;
    if (unicode_char <= 0xFFFF) {
        buf[0] = (unsigned short)unicode_char;
    } else {
        unicode_char -= 0x10000;
        buf[0] = (unsigned short)(0xD800 + (unicode_char >> 10));
        buf[1] = (unsigned short)(0xDC00 + (unicode_char & 0x3FF));
        len = 2;
    }
    evt.text = String16(buf, len);
    evt.unmodified_text = evt.text;
    data->view->FireKeyEvent(evt);
}

static const EmbeddedInstanceVtable g_ulVtable = {
    ul_init,
    ul_shutdown,
    ul_update,
    ul_is_active,
    ul_render,
    ul_key_down,
    ul_key_up,
    ul_key_char
};

/* ========================================================================
 * Public API (internal to DLL)
 * ======================================================================== */

EmbeddedInstance* UltralightInstance_Create(const char* htmlPath, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    UltralightData* data = new UltralightData();
    if (!inst || !data) { free(inst); delete data; return NULL; }

    data->htmlPath = htmlPath;
    data->initialized = false;

    inst->type = EMBEDDED_ULTRALIGHT;
    inst->vtable = &g_ulVtable;
    inst->target_material = material_name;
    inst->user_data = data;

    return inst;
}

void UltralightInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown)
        inst->vtable->shutdown(inst);
    if (inst->user_data)
        delete (UltralightData*)inst->user_data;
    free(inst);
}
