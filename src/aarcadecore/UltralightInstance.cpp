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
struct UltralightData : public LoadListener, public ViewListener {
    RefPtr<Renderer> renderer;
    RefPtr<View> view;
    const char* htmlPath;
    bool initialized;
    bool closeRequested;  /* set by JS closeMenu() callback */
    int lastMouseX;
    int lastMouseY;

    /* ViewListener override — fires before any page scripts run */
    void OnWindowObjectReady(ultralight::View* caller, uint64_t frame_id,
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
void UltralightManager_OpenLibraryBrowser(void);

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

/* Macro for JSC callback signature */
#define AAPI_CALLBACK(name) static JSValueRef name(JSContextRef ctx, JSObjectRef function, \
    JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)

/* ========================================================================
 * aapi.manager.* callbacks — host/engine communication
 * ======================================================================== */

AAPI_CALLBACK(js_manager_closeMenu) {
    if (g_currentULData) g_currentULData->closeRequested = true;
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_openEngineMenu) {
    UltralightManager_RequestEngineMenu();
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_startLibretro) {
    UltralightManager_RequestStartLibretro();
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_openLibraryBrowser) {
    UltralightManager_OpenLibraryBrowser();
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_getVersion) {
    JSStringRef str = JSStringCreateWithUTF8CString("AArcadeCore 1.0");
    JSValueRef result = JSValueMakeString(ctx, str);
    JSStringRelease(str);
    return result;
}

/* ========================================================================
 * aapi JS bridge — typed library database access
 * ======================================================================== */

#include "SQLiteLibrary.h"
extern SQLiteLibrary g_library;

/* Helper: create a JS string property on an object */
static void jsSetProp(JSContextRef ctx, JSObjectRef obj, const char* name, const std::string& value)
{
    JSStringRef key = JSStringCreateWithUTF8CString(name);
    JSStringRef val = JSStringCreateWithUTF8CString(value.c_str());
    JSObjectSetProperty(ctx, obj, key, JSValueMakeString(ctx, val), 0, nullptr);
    JSStringRelease(val);
    JSStringRelease(key);
}

static void jsSetInt(JSContextRef ctx, JSObjectRef obj, const char* name, int value)
{
    JSStringRef key = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(ctx, obj, key, JSValueMakeNumber(ctx, (double)value), 0, nullptr);
    JSStringRelease(key);
}

/* Convert typed structs to JS objects */
static JSObjectRef itemToJS(JSContextRef ctx, const Arcade::Item& item) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", item.id);
    jsSetProp(ctx, o, "app", item.app);
    jsSetProp(ctx, o, "description", item.description);
    jsSetProp(ctx, o, "file", item.file);
    jsSetProp(ctx, o, "marquee", item.marquee);
    jsSetProp(ctx, o, "screen", item.screen);
    jsSetProp(ctx, o, "title", item.title);
    jsSetProp(ctx, o, "type", item.type);
    return o;
}

static JSObjectRef typeToJS(JSContextRef ctx, const Arcade::Type& t) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", t.id);
    jsSetProp(ctx, o, "title", t.title);
    jsSetInt(ctx, o, "priority", t.priority);
    return o;
}

static JSObjectRef modelToJS(JSContextRef ctx, const Arcade::Model& m) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", m.id);
    jsSetProp(ctx, o, "title", m.title);
    jsSetProp(ctx, o, "screen", m.screen);
    return o;
}

static JSObjectRef appToJS(JSContextRef ctx, const Arcade::App& a) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", a.id);
    jsSetProp(ctx, o, "title", a.title);
    jsSetProp(ctx, o, "type", a.type);
    jsSetProp(ctx, o, "screen", a.screen);
    return o;
}

static JSObjectRef mapToJS(JSContextRef ctx, const Arcade::Map& m) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", m.id);
    jsSetProp(ctx, o, "title", m.title);
    return o;
}

static JSObjectRef platformToJS(JSContextRef ctx, const Arcade::Platform& p) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", p.id);
    jsSetProp(ctx, o, "title", p.title);
    return o;
}

static JSObjectRef instanceToJS(JSContextRef ctx, const Arcade::Instance& inst) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", inst.id);
    return o;
}

/* Template: convert vector to JS array */
template<typename T, typename ConvFn>
static JSObjectRef vectorToJSArray(JSContextRef ctx, const std::vector<T>& vec, ConvFn conv) {
    JSValueRef* vals = new JSValueRef[vec.size()];
    for (size_t i = 0; i < vec.size(); i++)
        vals[i] = conv(ctx, vec[i]);
    JSObjectRef arr = JSObjectMakeArray(ctx, vec.size(), vec.empty() ? nullptr : vals, nullptr);
    delete[] vals;
    return arr;
}

/* Parse (offset, limit) arguments */
static void parseOffsetLimit(JSContextRef ctx, size_t argc, const JSValueRef args[], int& offset, int& limit) {
    offset = (argc > 0) ? (int)JSValueToNumber(ctx, args[0], nullptr) : 0;
    limit  = (argc > 1) ? (int)JSValueToNumber(ctx, args[1], nullptr) : 50;
}

/* Parse (query, limit) arguments */
static std::string parseQueryLimit(JSContextRef ctx, size_t argc, const JSValueRef args[], int& limit) {
    std::string query = (argc > 0) ? jsValueToString(ctx, args[0]) : "";
    limit = (argc > 1) ? (int)JSValueToNumber(ctx, args[1], nullptr) : 50;
    return query;
}

/* --- aapi.library.* callback implementations --- */

AAPI_CALLBACK(js_aapi_getItemsTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    return vectorToJSArray(ctx, g_library.getItems(offset, limit), itemToJS);
}
AAPI_CALLBACK(js_aapi_searchItemsTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    return vectorToJSArray(ctx, g_library.searchItems(q, limit), itemToJS);
}
AAPI_CALLBACK(js_aapi_getTypesTyped) {
    return vectorToJSArray(ctx, g_library.getTypes(), typeToJS);
}
AAPI_CALLBACK(js_aapi_getModelsTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    return vectorToJSArray(ctx, g_library.getModels(offset, limit), modelToJS);
}
AAPI_CALLBACK(js_aapi_searchModelsTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    return vectorToJSArray(ctx, g_library.searchModels(q, limit), modelToJS);
}
AAPI_CALLBACK(js_aapi_getAppsTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    return vectorToJSArray(ctx, g_library.getApps(offset, limit), appToJS);
}
AAPI_CALLBACK(js_aapi_searchAppsTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    return vectorToJSArray(ctx, g_library.searchApps(q, limit), appToJS);
}
AAPI_CALLBACK(js_aapi_getMapsTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    return vectorToJSArray(ctx, g_library.getMaps(offset, limit), mapToJS);
}
AAPI_CALLBACK(js_aapi_searchMapsTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    return vectorToJSArray(ctx, g_library.searchMaps(q, limit), mapToJS);
}
AAPI_CALLBACK(js_aapi_getPlatformsTyped) {
    return vectorToJSArray(ctx, g_library.getPlatforms(), platformToJS);
}
AAPI_CALLBACK(js_aapi_getInstancesTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    return vectorToJSArray(ctx, g_library.getInstances(offset, limit), instanceToJS);
}
AAPI_CALLBACK(js_aapi_searchInstancesTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    return vectorToJSArray(ctx, g_library.searchInstances(q, limit), instanceToJS);
}

/* --- aapi.images.* callback implementations --- */

#include "ImageLoader.h"
extern ImageLoader g_imageLoader;

/*
 * Promise-like completion system for getCacheImage.
 *
 * getCacheImage(url) returns a JS object with then() and catch() methods.
 * When the image is cached, processCompletions() invokes the stored resolve callback.
 */

struct PendingImageRequest {
    JSContextRef ctx;
    JSObjectRef promiseObj;  /* GC-protected */
    std::string url;
};
static std::vector<PendingImageRequest> g_pendingImages;
static std::mutex g_pendingImagesMutex;

/* then() method on promise-like object — stores resolve callback as _resolve property */
static JSValueRef js_promise_then(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argc, const JSValueRef args[], JSValueRef* exc)
{
    if (argc >= 1) {
        JSStringRef key = JSStringCreateWithUTF8CString("_resolve");
        JSObjectSetProperty(ctx, thisObject, key, args[0], 0, nullptr);
        JSStringRelease(key);
    }
    return thisObject; /* chainable */
}

/* catch() method on promise-like object — stores reject callback as _reject property */
static JSValueRef js_promise_catch(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argc, const JSValueRef args[], JSValueRef* exc)
{
    if (argc >= 1) {
        JSStringRef key = JSStringCreateWithUTF8CString("_reject");
        JSObjectSetProperty(ctx, thisObject, key, args[0], 0, nullptr);
        JSStringRelease(key);
    }
    return thisObject; /* chainable */
}

AAPI_CALLBACK(js_images_getCacheImage) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string url = jsValueToString(ctx, arguments[0]);

    /* Create promise-like object with then() and catch() */
    JSObjectRef promiseObj = JSObjectMake(ctx, nullptr, nullptr);

    JSStringRef thenName = JSStringCreateWithUTF8CString("then");
    JSObjectRef thenFn = JSObjectMakeFunctionWithCallback(ctx, thenName, js_promise_then);
    JSObjectSetProperty(ctx, promiseObj, thenName, thenFn, 0, nullptr);
    JSStringRelease(thenName);

    JSStringRef catchName = JSStringCreateWithUTF8CString("catch");
    JSObjectRef catchFn = JSObjectMakeFunctionWithCallback(ctx, catchName, js_promise_catch);
    JSObjectSetProperty(ctx, promiseObj, catchName, catchFn, 0, nullptr);
    JSStringRelease(catchName);

    /* Protect from GC until completion */
    JSValueProtect(ctx, promiseObj);

    /* Queue the image load — the callback fires from processCompletions */
    PendingImageRequest req;
    req.ctx = ctx;
    req.promiseObj = promiseObj;
    req.url = url;

    {
        std::lock_guard<std::mutex> lock(g_pendingImagesMutex);
        g_pendingImages.push_back(req);
    }

    g_imageLoader.loadAndCacheImage(url, [url](const ImageLoadResult& result) {
        /* This fires on the main thread from processCompletions.
         * Find and resolve the matching pending request. */
        std::lock_guard<std::mutex> lock(g_pendingImagesMutex);
        for (auto it = g_pendingImages.begin(); it != g_pendingImages.end(); ++it) {
            if (it->url == url) {
                JSContextRef c = it->ctx;
                JSObjectRef p = it->promiseObj;

                if (result.success) {
                    /* Convert file path to file:// URL */
                    std::string fileUrl = "file:///./" + result.filePath;
                    for (char& ch : fileUrl) { if (ch == '\\') ch = '/'; }

                    JSStringRef resolveKey = JSStringCreateWithUTF8CString("_resolve");
                    JSValueRef resolveVal = JSObjectGetProperty(c, p, resolveKey, nullptr);
                    JSStringRelease(resolveKey);

                    if (JSValueIsObject(c, resolveVal)) {
                        JSObjectRef resultObj = JSObjectMake(c, nullptr, nullptr);
                        jsSetProp(c, resultObj, "filePath", fileUrl);
                        JSStringRef sk = JSStringCreateWithUTF8CString("success");
                        JSObjectSetProperty(c, resultObj, sk, JSValueMakeBoolean(c, true), 0, nullptr);
                        JSStringRelease(sk);
                        JSValueRef callArgs[] = { resultObj };
                        JSObjectCallAsFunction(c, (JSObjectRef)resolveVal, nullptr, 1, callArgs, nullptr);
                    }
                } else {
                    /* Call _reject so arcadeHud shows the error placeholder */
                    JSStringRef rejectKey = JSStringCreateWithUTF8CString("_reject");
                    JSValueRef rejectVal = JSObjectGetProperty(c, p, rejectKey, nullptr);
                    JSStringRelease(rejectKey);

                    if (JSValueIsObject(c, rejectVal)) {
                        JSStringRef errStr = JSStringCreateWithUTF8CString("Image load failed");
                        JSValueRef errArg = JSValueMakeString(c, errStr);
                        JSStringRelease(errStr);
                        JSValueRef callArgs[] = { errArg };
                        JSObjectCallAsFunction(c, (JSObjectRef)rejectVal, nullptr, 1, callArgs, nullptr);
                    }
                }

                JSValueUnprotect(c, p);
                g_pendingImages.erase(it);
                break;
            }
        }
    });

    return promiseObj;
}

AAPI_CALLBACK(js_images_processCompletions) {
    g_imageLoader.processCompletions();
    return JSValueMakeUndefined(ctx);
}

/* Helper to register a method on a JS object */
static void addJSMethod(JSContextRef ctx, JSObjectRef obj, const char* name, JSObjectCallAsFunctionCallback fn) {
    JSStringRef methodName = JSStringCreateWithUTF8CString(name);
    JSObjectRef methodFunc = JSObjectMakeFunctionWithCallback(ctx, methodName, fn);
    JSObjectSetProperty(ctx, obj, methodName, methodFunc, 0, nullptr);
    JSStringRelease(methodName);
}

void UltralightData::OnWindowObjectReady(ultralight::View* caller, uint64_t frame_id,
                                          bool is_main_frame, const String& url)
{
    if (!is_main_frame) return;

    if (g_host.host_printf) g_host.host_printf("UL: OnWindowObjectReady, setting up JS bridge\n");

    auto scoped_context = caller->LockJSContext();
    JSContextRef ctx = (*scoped_context);

    JSObjectRef globalObj = JSContextGetGlobalObject(ctx);

    /* Create unified aapi bridge with namespaces */
    JSObjectRef aapiObj = JSObjectMake(ctx, nullptr, nullptr);

    /* aapi.manager — host/engine communication */
    JSObjectRef managerObj = JSObjectMake(ctx, nullptr, nullptr);
    addJSMethod(ctx, managerObj, "closeMenu", js_manager_closeMenu);
    addJSMethod(ctx, managerObj, "openEngineMenu", js_manager_openEngineMenu);
    addJSMethod(ctx, managerObj, "startLibretro", js_manager_startLibretro);
    addJSMethod(ctx, managerObj, "openLibraryBrowser", js_manager_openLibraryBrowser);
    addJSMethod(ctx, managerObj, "getVersion", js_manager_getVersion);

    JSStringRef managerName = JSStringCreateWithUTF8CString("manager");
    JSObjectSetProperty(ctx, aapiObj, managerName, managerObj, 0, nullptr);
    JSStringRelease(managerName);

    /* aapi.library — database queries */
    JSObjectRef libraryObj = JSObjectMake(ctx, nullptr, nullptr);
    addJSMethod(ctx, libraryObj, "getItems", js_aapi_getItemsTyped);
    addJSMethod(ctx, libraryObj, "searchItems", js_aapi_searchItemsTyped);
    addJSMethod(ctx, libraryObj, "getTypes", js_aapi_getTypesTyped);
    addJSMethod(ctx, libraryObj, "getModels", js_aapi_getModelsTyped);
    addJSMethod(ctx, libraryObj, "searchModels", js_aapi_searchModelsTyped);
    addJSMethod(ctx, libraryObj, "getApps", js_aapi_getAppsTyped);
    addJSMethod(ctx, libraryObj, "searchApps", js_aapi_searchAppsTyped);
    addJSMethod(ctx, libraryObj, "getMaps", js_aapi_getMapsTyped);
    addJSMethod(ctx, libraryObj, "searchMaps", js_aapi_searchMapsTyped);
    addJSMethod(ctx, libraryObj, "getPlatforms", js_aapi_getPlatformsTyped);
    addJSMethod(ctx, libraryObj, "getInstances", js_aapi_getInstancesTyped);
    addJSMethod(ctx, libraryObj, "searchInstances", js_aapi_searchInstancesTyped);

    JSStringRef libraryName = JSStringCreateWithUTF8CString("library");
    JSObjectSetProperty(ctx, aapiObj, libraryName, libraryObj, 0, nullptr);
    JSStringRelease(libraryName);

    /* aapi.images — image caching */
    JSObjectRef imagesObj = JSObjectMake(ctx, nullptr, nullptr);
    addJSMethod(ctx, imagesObj, "getCacheImage", js_images_getCacheImage);
    addJSMethod(ctx, imagesObj, "processCompletions", js_images_processCompletions);

    JSStringRef imagesName = JSStringCreateWithUTF8CString("images");
    JSObjectSetProperty(ctx, aapiObj, imagesName, imagesObj, 0, nullptr);
    JSStringRelease(imagesName);

    /* Attach aapi to global window */
    JSStringRef aapiName = JSStringCreateWithUTF8CString("aapi");
    JSObjectSetProperty(ctx, globalObj, aapiName, aapiObj, 0, nullptr);
    JSStringRelease(aapiName);
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

    /* Set view listener for JS bridge setup (OnWindowObjectReady fires before scripts) */
    data->view->set_view_listener(data);
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
