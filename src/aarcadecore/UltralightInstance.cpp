/*
 * UltralightInstance — EmbeddedInstance for Ultralight HTML renderer
 *
 * Renders local HTML files to in-game textures using Ultralight's
 * CPU rendering mode (BitmapSurface). Includes JS bridge for C++↔JS
 * communication via the `aacore` global object.
 */

#include "aarcadecore_internal.h"
#include <Ultralight/Ultralight.h>
#include <Ultralight/MouseEvent.h>
#include <AppCore/Platform.h>
#include <JavaScriptCore/JavaScript.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdint.h>

using namespace ultralight;

#define UL_DEFAULT_WIDTH  1920
#define UL_DEFAULT_HEIGHT 1080

/* Per-instance state */
struct UltralightData : public LoadListener {
    RefPtr<Renderer> renderer;
    RefPtr<View> view;
    const char* htmlPath;
    bool initialized;
    bool closeRequested;  /* set by JS closeMenu() callback */
    int lastMouseX;
    int lastMouseY;

    /* LoadListener overrides */
    void OnDOMReady(ultralight::View* caller, uint64_t frame_id,
                    bool is_main_frame, const String& url) override;
};

/* Global pointer for JS callback access */
static UltralightData* g_currentULData = nullptr;

/* ========================================================================
 * JS Bridge: generic command dispatcher
 *
 * JS calls:  aacore.call("commandName")      — fire-and-forget (async)
 *            aacore.callSync("commandName")   — returns a value (sync)
 * ======================================================================== */

/* Forward declarations for command handlers */
void UltralightManager_RequestEngineMenu(void);
void UltralightManager_RequestStartLibretro(void);

/* Helper: extract UTF-8 string from JSValue */
static std::string jsValueToString(JSContextRef ctx, JSValueRef val)
{
    JSStringRef jsStr = JSValueToStringCopy(ctx, val, nullptr);
    if (!jsStr) return "";
    size_t maxLen = JSStringGetMaximumUTF8CStringSize(jsStr);
    std::string result(maxLen, '\0');
    size_t len = JSStringGetUTF8CString(jsStr, &result[0], maxLen);
    JSStringRelease(jsStr);
    result.resize(len > 0 ? len - 1 : 0); /* remove null terminator */
    return result;
}

/* aacore.call("command") — async, fire-and-forget */
static JSValueRef js_call(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argumentCount,
    const JSValueRef arguments[], JSValueRef* exception)
{
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);

    std::string cmd = jsValueToString(ctx, arguments[0]);
    if (g_host.host_printf) g_host.host_printf("UL: call('%s')\n", cmd.c_str());

    if (cmd == "closeMenu") {
        if (g_currentULData) g_currentULData->closeRequested = true;
    } else if (cmd == "openEngineMenu") {
        UltralightManager_RequestEngineMenu();
    } else if (cmd == "startLibretro") {
        UltralightManager_RequestStartLibretro();
    } else {
        if (g_host.host_printf) g_host.host_printf("UL: unknown command '%s'\n", cmd.c_str());
        return JSValueMakeBoolean(ctx, false);
    }

    return JSValueMakeBoolean(ctx, true);
}

/* aacore.callSync("command") — sync, returns a value */
static JSValueRef js_callSync(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argumentCount,
    const JSValueRef arguments[], JSValueRef* exception)
{
    if (argumentCount < 1) return JSValueMakeNull(ctx);

    std::string cmd = jsValueToString(ctx, arguments[0]);

    if (cmd == "getVersion") {
        JSStringRef str = JSStringCreateWithUTF8CString("AArcadeCore 1.0");
        JSValueRef result = JSValueMakeString(ctx, str);
        JSStringRelease(str);
        return result;
    }

    if (g_host.host_printf) g_host.host_printf("UL: unknown sync command '%s'\n", cmd.c_str());
    return JSValueMakeNull(ctx);
}

void UltralightData::OnDOMReady(ultralight::View* caller, uint64_t frame_id,
                                 bool is_main_frame, const String& url)
{
    if (!is_main_frame) return;

    if (g_host.host_printf) g_host.host_printf("UL: OnDOMReady, setting up JS bridge\n");

    auto scoped_context = caller->LockJSContext();
    JSContextRef ctx = (*scoped_context);

    /* Create aacore namespace with two generic methods: call() and callSync() */
    JSObjectRef globalObj = JSContextGetGlobalObject(ctx);
    JSObjectRef aacoreObj = JSObjectMake(ctx, nullptr, nullptr);

    JSStringRef methodName;
    JSObjectRef methodFunc;

    methodName = JSStringCreateWithUTF8CString("call");
    methodFunc = JSObjectMakeFunctionWithCallback(ctx, methodName, js_call);
    JSObjectSetProperty(ctx, aacoreObj, methodName, methodFunc, 0, nullptr);
    JSStringRelease(methodName);

    methodName = JSStringCreateWithUTF8CString("callSync");
    methodFunc = JSObjectMakeFunctionWithCallback(ctx, methodName, js_callSync);
    JSObjectSetProperty(ctx, aacoreObj, methodName, methodFunc, 0, nullptr);
    JSStringRelease(methodName);

    /* Attach aacore to global window */
    JSStringRef nsName = JSStringCreateWithUTF8CString("aacore");
    JSObjectSetProperty(ctx, globalObj, nsName, aacoreObj, 0, nullptr);
    JSStringRelease(nsName);
}

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
    viewConfig.is_transparent = true;
    data->view = data->renderer->CreateView(UL_DEFAULT_WIDTH, UL_DEFAULT_HEIGHT, viewConfig, nullptr);
    if (!data->view) {
        if (g_host.host_printf) g_host.host_printf("Ultralight: Failed to create view\n");
        return false;
    }

    /* Set load listener for JS bridge setup */
    data->view->set_load_listener(data);
    g_currentULData = data;

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

    if (g_currentULData == data)
        g_currentULData = nullptr;

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

    if (!is16bit && bpp == 32) {
        /* Direct BGRA copy with scaling (for fullscreen overlay) */
        uint32_t* dest = (uint32_t*)pixelData;
        int scale_x = ((int)srcWidth << 16) / width;
        int scale_y = ((int)srcHeight << 16) / height;

        for (int y = 0; y < height; y++) {
            int sy = (y * scale_y) >> 16;
            if (sy >= (int)srcHeight) sy = srcHeight - 1;
            const uint32_t* srcRow = (const uint32_t*)(src + sy * srcRowBytes);

            for (int x = 0; x < width; x++) {
                int sx = (x * scale_x) >> 16;
                if (sx >= (int)srcWidth) sx = srcWidth - 1;
                dest[y * width + x] = srcRow[sx];
            }
        }
    } else if (is16bit) {
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

/* ========================================================================
 * Keyboard input
 * ======================================================================== */

static void ul_key_down(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;

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

    KeyEvent evt;
    evt.type = KeyEvent::kType_Char;
    evt.virtual_key_code = 0;
    evt.native_key_code = 0;
    evt.modifiers = 0;
    if (modifiers & AACORE_MOD_ALT)   evt.modifiers |= KeyEvent::kMod_AltKey;
    if (modifiers & AACORE_MOD_CTRL)  evt.modifiers |= KeyEvent::kMod_CtrlKey;
    if (modifiers & AACORE_MOD_SHIFT) evt.modifiers |= KeyEvent::kMod_ShiftKey;

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

static void ul_mouse_move(EmbeddedInstance* inst, int x, int y)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    data->lastMouseX = x;
    data->lastMouseY = y;
    MouseEvent evt;
    evt.type = MouseEvent::kType_MouseMoved;
    evt.x = x;
    evt.y = y;
    evt.button = MouseEvent::kButton_None;
    data->view->FireMouseEvent(evt);
}

static void ul_mouse_down(EmbeddedInstance* inst, int button)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    MouseEvent evt;
    evt.type = MouseEvent::kType_MouseDown;
    evt.x = data->lastMouseX;
    evt.y = data->lastMouseY;
    evt.button = (button == AACORE_MOUSE_RIGHT) ? MouseEvent::kButton_Right :
                 (button == AACORE_MOUSE_MIDDLE) ? MouseEvent::kButton_Middle :
                 MouseEvent::kButton_Left;
    data->view->FireMouseEvent(evt);
}

static void ul_mouse_up(EmbeddedInstance* inst, int button)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    MouseEvent evt;
    evt.type = MouseEvent::kType_MouseUp;
    evt.x = data->lastMouseX;
    evt.y = data->lastMouseY;
    evt.button = (button == AACORE_MOUSE_RIGHT) ? MouseEvent::kButton_Right :
                 (button == AACORE_MOUSE_MIDDLE) ? MouseEvent::kButton_Middle :
                 MouseEvent::kButton_Left;
    data->view->FireMouseEvent(evt);
}

static const EmbeddedInstanceVtable g_ulVtable = {
    ul_init,
    ul_shutdown,
    ul_update,
    ul_is_active,
    ul_render,
    ul_key_down,
    ul_key_up,
    ul_key_char,
    ul_mouse_move,
    ul_mouse_down,
    ul_mouse_up,
    NULL  /* mouse_wheel — not needed for Ultralight currently */
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
    data->closeRequested = false;
    data->lastMouseX = 0;
    data->lastMouseY = 0;

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

bool UltralightInstance_IsCloseRequested(EmbeddedInstance* inst)
{
    if (!inst || !inst->user_data) return false;
    return ((UltralightData*)inst->user_data)->closeRequested;
}

void UltralightInstance_LoadURL(EmbeddedInstance* inst, const char* url)
{
    if (!inst || !inst->user_data) return;
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    data->closeRequested = false;
    data->view->LoadURL(url);
    data->view->Focus();
    if (g_host.host_printf) g_host.host_printf("UL: Loading %s\n", url);
}
