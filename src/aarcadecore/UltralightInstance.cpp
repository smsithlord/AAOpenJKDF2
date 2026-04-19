/*
 * UltralightInstance — EmbeddedInstance for Ultralight HTML renderer
 *
 * Renders local HTML files to in-game textures using Ultralight's
 * CPU rendering mode (BitmapSurface). Includes JS bridge for C++↔JS
 * communication via the `aacore` global object.
 */

#include "aarcadecore_internal.h"
#include "ItemLauncher.h"
#include "VideoPlayerInstance.h"
#include <Ultralight/Ultralight.h>
#include <Ultralight/MouseEvent.h>
#include <Ultralight/ScrollEvent.h>
#include <Ultralight/ConsoleMessage.h>
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

    /* ViewListener override — route JS console output to game console */
    void OnAddConsoleMessage(ultralight::View* caller,
                             const ultralight::ConsoleMessage& message) override {
        if (g_host.host_printf) {
            const char* level = "LOG";
            if (message.level() == kMessageLevel_Warning) level = "WARN";
            else if (message.level() == kMessageLevel_Error) level = "ERROR";
            g_host.host_printf("JS [%s]: %s\n", level, message.message().utf8().data());
        }
    }
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
void UltralightManager_OpenTaskMenu(void);
void UltralightManager_OpenMainMenuPage(void);
void UltralightManager_SetHudInputActive(bool active);

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
extern bool g_deepSleepRequested;
AAPI_CALLBACK(js_manager_requestDeepSleep) {
    g_deepSleepRequested = true;
    return JSValueMakeBoolean(ctx, true);
}
extern bool g_exitGameRequested;
AAPI_CALLBACK(js_manager_exitGame) {
    g_exitGameRequested = true;
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

AAPI_CALLBACK(js_manager_openTaskMenu) {
    UltralightManager_OpenTaskMenu();
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_openMainMenu) {
    UltralightManager_OpenMainMenuPage();
    return JSValueMakeBoolean(ctx, true);
}

#include "InstanceManager.h"
#include "SQLiteLibrary.h"
#include "LibretroCoreConfig.h"
#include "ImageLoader.h"
extern InstanceManager g_instanceManager;
extern SQLiteLibrary g_library;
extern ImageLoader g_imageLoader;

static void jsSetPropStr(JSContextRef ctx, JSObjectRef obj, const char* name, const std::string& value) {
    JSStringRef k = JSStringCreateWithUTF8CString(name);
    JSStringRef v = JSStringCreateWithUTF8CString(value.c_str());
    JSObjectSetProperty(ctx, obj, k, JSValueMakeString(ctx, v), 0, nullptr);
    JSStringRelease(v); JSStringRelease(k);
}

AAPI_CALLBACK(js_manager_getActiveInstances) {
    auto instances = g_instanceManager.getActiveInstances();
    JSValueRef* vals = new JSValueRef[instances.size()];
    for (size_t i = 0; i < instances.size(); i++) {
        JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
        jsSetPropStr(ctx, o, "itemId", instances[i]->itemId);
        jsSetPropStr(ctx, o, "url", instances[i]->url);
        jsSetPropStr(ctx, o, "title", instances[i]->title);
        JSStringRef ak = JSStringCreateWithUTF8CString("active");
        JSObjectSetProperty(ctx, o, ak, JSValueMakeBoolean(ctx, instances[i]->active), 0, nullptr);
        JSStringRelease(ak);
        vals[i] = o;
    }
    JSObjectRef arr = JSObjectMakeArray(ctx, instances.size(), instances.empty() ? nullptr : vals, nullptr);
    delete[] vals;
    return arr;
}

AAPI_CALLBACK(js_manager_getOverlayInstanceInfo) {
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    const EmbeddedItemInstance* inst = target ? g_instanceManager.getInstanceForBrowser(target) : nullptr;
    if (!inst) return JSValueMakeNull(ctx);

    JSObjectRef obj = JSObjectMake(ctx, nullptr, nullptr);
    jsSetPropStr(ctx, obj, "url", inst->url);
    jsSetPropStr(ctx, obj, "title", inst->title);
    jsSetPropStr(ctx, obj, "itemId", inst->itemId);

    /* Instance type */
    const char* itype = "";
    if (target) {
        switch (target->type) {
            case EMBEDDED_STEAMWORKS_BROWSER: itype = "swb"; break;
            case EMBEDDED_LIBRETRO: itype = "libretro"; break;
            case EMBEDDED_ULTRALIGHT: itype = "ultralight"; break;
            case EMBEDDED_VIDEO_PLAYER: itype = "videoplayer"; break;
            default: break;
        }
    }
    jsSetPropStr(ctx, obj, "instanceType", std::string(itype));

    /* Core/game paths for Libretro */
    if (target && target->type == EMBEDDED_LIBRETRO && target->user_data) {
        typedef struct { void* host; const char* core_path; const char* game_path; } LRDataFull;
        LRDataFull* lrd = (LRDataFull*)target->user_data;
        if (lrd->core_path) jsSetPropStr(ctx, obj, "corePath", std::string(lrd->core_path));
        if (lrd->game_path) jsSetPropStr(ctx, obj, "gamePath", std::string(lrd->game_path));
    }

    /* File path for Video Player */
    if (target && target->type == EMBEDDED_VIDEO_PLAYER && target->vtable->get_title) {
        const char* vf = target->vtable->get_title(target);
        if (vf) jsSetPropStr(ctx, obj, "videoFile", std::string(vf));
    }

    return obj;
}

AAPI_CALLBACK(js_manager_getOverlayMode) {
    /* Returns "fullscreen", "input", or null */
    if (aarcadecore_getFullscreenInstance()) {
        JSStringRef s = JSStringCreateWithUTF8CString("fullscreen");
        JSValueRef v = JSValueMakeString(ctx, s);
        JSStringRelease(s);
        return v;
    }
    if (aarcadecore_getInputModeInstance()) {
        JSStringRef s = JSStringCreateWithUTF8CString("input");
        JSValueRef v = JSValueMakeString(ctx, s);
        JSStringRelease(s);
        return v;
    }
    return JSValueMakeNull(ctx);
}

AAPI_CALLBACK(js_manager_goBack) {
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (target && target->vtable->go_back) { target->vtable->go_back(target); return JSValueMakeBoolean(ctx, true); }
    return JSValueMakeBoolean(ctx, false);
}

AAPI_CALLBACK(js_manager_goForward) {
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (target && target->vtable->go_forward) { target->vtable->go_forward(target); return JSValueMakeBoolean(ctx, true); }
    return JSValueMakeBoolean(ctx, false);
}

AAPI_CALLBACK(js_manager_reloadInstance) {
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (target && target->vtable->reload) { target->vtable->reload(target); return JSValueMakeBoolean(ctx, true); }
    return JSValueMakeBoolean(ctx, false);
}

AAPI_CALLBACK(js_manager_setSpawnTransform) {
    if (argumentCount < 10) return JSValueMakeBoolean(ctx, false);
    float pitch = (float)JSValueToNumber(ctx, arguments[0], nullptr);
    float yaw   = (float)JSValueToNumber(ctx, arguments[1], nullptr);
    float roll  = (float)JSValueToNumber(ctx, arguments[2], nullptr);
    bool isWorldRot = JSValueToBoolean(ctx, arguments[3]);
    float offX = (float)JSValueToNumber(ctx, arguments[4], nullptr);
    float offY = (float)JSValueToNumber(ctx, arguments[5], nullptr);
    float offZ = (float)JSValueToNumber(ctx, arguments[6], nullptr);
    bool isWorldOff = JSValueToBoolean(ctx, arguments[7]);
    bool useRaycast = JSValueToBoolean(ctx, arguments[8]);
    float scale = (float)JSValueToNumber(ctx, arguments[9], nullptr);
    g_instanceManager.setSpawnTransform(pitch, yaw, roll, isWorldRot, offX, offY, offZ, isWorldOff, useRaycast, scale);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_getSpawnModelId) {
    std::string id = g_instanceManager.getSpawnModelId();
    if (id.empty()) return JSValueMakeNull(ctx);
    JSStringRef s = JSStringCreateWithUTF8CString(id.c_str());
    JSValueRef v = JSValueMakeString(ctx, s);
    JSStringRelease(s);
    return v;
}

AAPI_CALLBACK(js_manager_getInitialSpawnScale) {
    return JSValueMakeNumber(ctx, g_instanceManager.getInitialSpawnScale());
}

AAPI_CALLBACK(js_manager_getSpawnItemId) {
    std::string id = g_instanceManager.getSpawnItemId();
    if (id.empty()) return JSValueMakeNull(ctx);
    JSStringRef s = JSStringCreateWithUTF8CString(id.c_str());
    JSValueRef v = JSValueMakeString(ctx, s);
    JSStringRelease(s);
    return v;
}

AAPI_CALLBACK(js_manager_setSpawnModeModel) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string modelId = jsValueToString(ctx, arguments[0]);
    g_instanceManager.requestSpawnModelChange(modelId);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_moveAimedObject) {
    const SpawnedObject* obj = g_instanceManager.getAimedObject();
    if (obj) {
        g_instanceManager.requestMove(obj->thingIdx);
        return JSValueMakeBoolean(ctx, true);
    }
    return JSValueMakeBoolean(ctx, false);
}

AAPI_CALLBACK(js_manager_navigateInstance) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string url = jsValueToString(ctx, arguments[0]);
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (target && target->vtable->navigate) {
        target->vtable->navigate(target, url.c_str());
        return JSValueMakeBoolean(ctx, true);
    }
    return JSValueMakeBoolean(ctx, false);
}

AAPI_CALLBACK(js_manager_setHudInputActive) {
    if (argumentCount < 1) return JSValueMakeUndefined(ctx);
    bool active = JSValueToBoolean(ctx, arguments[0]);
    UltralightManager_SetHudInputActive(active);
    return JSValueMakeUndefined(ctx);
}

AAPI_CALLBACK(js_manager_deactivateInstance) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string itemId = jsValueToString(ctx, arguments[0]);
    g_instanceManager.deactivateInstance(itemId);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_rememberItem) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string itemId = jsValueToString(ctx, arguments[0]);
    g_instanceManager.setRememberedItemId(itemId);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_getRememberedItemId) {
    const std::string& id = g_instanceManager.getRememberedItemId();
    JSStringRef s = JSStringCreateWithUTF8CString(id.c_str());
    JSValueRef v = JSValueMakeString(ctx, s);
    JSStringRelease(s);
    return v;
}

static JSObjectRef itemToJS(JSContextRef ctx, const Arcade::Item& item);

AAPI_CALLBACK(js_manager_getAimedObjectInfo) {
    const SpawnedObject* obj = g_instanceManager.getAimedObject();
    if (!obj) return JSValueMakeNull(ctx);

    Arcade::Item item = g_library.getItemById(obj->itemId);
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetPropStr(ctx, o, "itemId", obj->itemId);
    jsSetPropStr(ctx, o, "modelId", obj->modelId);
    jsSetPropStr(ctx, o, "objectKey", obj->objectKey);
    jsSetPropStr(ctx, o, "url", obj->url);
    jsSetPropStr(ctx, o, "title", item.title.empty() ? obj->modelId : item.title);

    if (!item.id.empty()) {
        JSObjectRef itemObj = itemToJS(ctx, item);
        JSStringRef k = JSStringCreateWithUTF8CString("item");
        JSObjectSetProperty(ctx, o, k, itemObj, 0, nullptr);
        JSStringRelease(k);
    }

    JSStringRef tk = JSStringCreateWithUTF8CString("thingIdx");
    JSObjectSetProperty(ctx, o, tk, JSValueMakeNumber(ctx, obj->thingIdx), 0, nullptr);
    JSStringRelease(tk);

    return o;
}

AAPI_CALLBACK(js_manager_destroyAimedObject) {
    int thingIdx = g_instanceManager.getAimedThingIdx();
    if (thingIdx < 0) return JSValueMakeBoolean(ctx, false);
    g_instanceManager.destroyObject(thingIdx);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_toggleSlaveAimedObject) {
    const SpawnedObject* obj = g_instanceManager.getAimedObject();
    if (!obj) return JSValueMakeBoolean(ctx, false);
    bool newState = g_instanceManager.toggleSlave(obj->thingIdx);
    return JSValueMakeBoolean(ctx, newState);
}

AAPI_CALLBACK(js_manager_isAimedObjectSlave) {
    const SpawnedObject* obj = g_instanceManager.getAimedObject();
    if (!obj) return JSValueMakeBoolean(ctx, false);
    return JSValueMakeBoolean(ctx, obj->slave);
}

AAPI_CALLBACK(js_manager_launchItem) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string itemId = jsValueToString(ctx, arguments[0]);
    bool ok = ItemLauncher::launch(itemId);
    return JSValueMakeBoolean(ctx, ok);
}

AAPI_CALLBACK(js_manager_getVideoTimeInfo) {
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (!target || target->type != EMBEDDED_VIDEO_PLAYER) return JSValueMakeNull(ctx);

    double pos = 0, dur = 0;
    VideoPlayerInstance_GetTimeInfo(target, &pos, &dur);

    JSObjectRef obj = JSObjectMake(ctx, nullptr, nullptr);
    JSStringRef kPos = JSStringCreateWithUTF8CString("position");
    JSObjectSetProperty(ctx, obj, kPos, JSValueMakeNumber(ctx, pos), 0, nullptr);
    JSStringRelease(kPos);
    JSStringRef kDur = JSStringCreateWithUTF8CString("duration");
    JSObjectSetProperty(ctx, obj, kDur, JSValueMakeNumber(ctx, dur), 0, nullptr);
    JSStringRelease(kDur);
    return obj;
}

AAPI_CALLBACK(js_manager_seekVideo) {
    if (argumentCount < 1) return JSValueMakeUndefined(ctx);
    double pos = JSValueToNumber(ctx, arguments[0], nullptr);
    EmbeddedInstance* target = aarcadecore_getInputModeInstance();
    if (!target) target = aarcadecore_getFullscreenInstance();
    if (target) VideoPlayerInstance_Seek(target, pos);
    return JSValueMakeUndefined(ctx);
}

AAPI_CALLBACK(js_manager_refreshItemTextures) {
    if (argumentCount < 1) return JSValueMakeUndefined(ctx);
    std::string itemId = jsValueToString(ctx, arguments[0]);
    bool deleteDiskCache = (argumentCount >= 2) && JSValueToBoolean(ctx, arguments[1]);
    g_instanceManager.refreshItemTextures(itemId, deleteDiskCache);
    return JSValueMakeUndefined(ctx);
}

AAPI_CALLBACK(js_manager_cloneAimedObject) {
    const SpawnedObject* obj = g_instanceManager.getAimedObject();
    if (!obj) return JSValueMakeBoolean(ctx, false);
    if (obj->itemId.empty()) {
        if (!obj->modelId.empty()) {
            g_instanceManager.requestSpawnModel(obj->modelId);
            return JSValueMakeBoolean(ctx, true);
        }
        return JSValueMakeBoolean(ctx, false);
    }
    Arcade::Item item = g_library.getItemById(obj->itemId);
    if (item.id.empty()) return JSValueMakeBoolean(ctx, false);
    g_instanceManager.requestSpawn(item, obj->modelId, obj->scale);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_importDefaultLibrary) {
    int created = g_instanceManager.importDefaultLibrary();
    /* Return { created: N, total: 3 } */
    JSObjectRef result = JSObjectMake(ctx, nullptr, nullptr);
    JSStringRef createdKey = JSStringCreateWithUTF8CString("created");
    JSStringRef totalKey = JSStringCreateWithUTF8CString("total");
    JSObjectSetProperty(ctx, result, createdKey, JSValueMakeNumber(ctx, created), 0, nullptr);
    JSObjectSetProperty(ctx, result, totalKey, JSValueMakeNumber(ctx, 4), 0, nullptr);
    JSStringRelease(createdKey);
    JSStringRelease(totalKey);
    return result;
}

AAPI_CALLBACK(js_manager_importAdoptedTemplates) {
    auto result = g_instanceManager.importAdoptedTemplates();
    JSObjectRef obj = JSObjectMake(ctx, nullptr, nullptr);
    JSStringRef createdKey = JSStringCreateWithUTF8CString("created");
    JSStringRef totalKey = JSStringCreateWithUTF8CString("total");
    JSObjectSetProperty(ctx, obj, createdKey, JSValueMakeNumber(ctx, result.created), 0, nullptr);
    JSObjectSetProperty(ctx, obj, totalKey, JSValueMakeNumber(ctx, result.total), 0, nullptr);
    JSStringRelease(createdKey);
    JSStringRelease(totalKey);
    return obj;
}

AAPI_CALLBACK(js_manager_mergeLibrary) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string sourcePath = jsValueToString(ctx, arguments[0]);
    std::string strategy = (argumentCount >= 2) ? jsValueToString(ctx, arguments[1]) : "skip";
    std::string stats = g_instanceManager.mergeLibrary(sourcePath, strategy);
    JSObjectRef result = JSObjectMake(ctx, nullptr, nullptr);
    JSStringRef k;
    k = JSStringCreateWithUTF8CString("success");
    JSObjectSetProperty(ctx, result, k, JSValueMakeBoolean(ctx, true), 0, nullptr);
    JSStringRelease(k);
    k = JSStringCreateWithUTF8CString("stats");
    JSStringRef v = JSStringCreateWithUTF8CString(stats.c_str());
    JSObjectSetProperty(ctx, result, k, JSValueMakeString(ctx, v), 0, nullptr);
    JSStringRelease(v); JSStringRelease(k);
    return result;
}

/* Libretro core config JS bridge */
AAPI_CALLBACK(js_manager_getAllLibretroCores) {
    g_coreConfigMgr.scanCores();
    std::string json = g_coreConfigMgr.toJson();
    JSStringRef str = JSStringCreateWithUTF8CString(json.c_str());
    JSValueRef parsed = JSValueMakeFromJSONString(ctx, str);
    JSStringRelease(str);
    return parsed ? parsed : JSValueMakeNull(ctx);
}

AAPI_CALLBACK(js_manager_updateLibretroCore) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    /* Argument is a JSON object: {file, enabled, cartSaves, stateSaves, priority, paths:[{path,extensions}]} */
    JSStringRef jsonStr = JSValueCreateJSONString(ctx, arguments[0], 0, nullptr);
    if (!jsonStr) return JSValueMakeBoolean(ctx, false);
    size_t len = JSStringGetMaximumUTF8CStringSize(jsonStr);
    std::string json(len, '\0');
    JSStringGetUTF8CString(jsonStr, &json[0], len);
    JSStringRelease(jsonStr);
    json.resize(strlen(json.c_str()));

    /* Extract fields from the JS object directly */
    JSObjectRef obj = JSValueToObject(ctx, arguments[0], nullptr);
    if (!obj) return JSValueMakeBoolean(ctx, false);

    auto getProp = [&](const char* name) -> JSValueRef {
        JSStringRef k = JSStringCreateWithUTF8CString(name);
        JSValueRef v = JSObjectGetProperty(ctx, obj, k, nullptr);
        JSStringRelease(k);
        return v;
    };

    std::string file = jsValueToString(ctx, getProp("file"));
    bool enabled = JSValueToBoolean(ctx, getProp("enabled"));
    bool cartSaves = JSValueToBoolean(ctx, getProp("cartSaves"));
    bool stateSaves = JSValueToBoolean(ctx, getProp("stateSaves"));
    int priority = (int)JSValueToNumber(ctx, getProp("priority"), nullptr);

    std::vector<CoreContentPath> paths;
    JSValueRef pathsVal = getProp("paths");
    if (pathsVal && JSValueIsObject(ctx, pathsVal)) {
        JSObjectRef pathsArr = JSValueToObject(ctx, pathsVal, nullptr);
        JSStringRef lenKey = JSStringCreateWithUTF8CString("length");
        int pathCount = (int)JSValueToNumber(ctx, JSObjectGetProperty(ctx, pathsArr, lenKey, nullptr), nullptr);
        JSStringRelease(lenKey);
        for (int i = 0; i < pathCount; i++) {
            JSObjectRef pObj = JSValueToObject(ctx, JSObjectGetPropertyAtIndex(ctx, pathsArr, i, nullptr), nullptr);
            if (!pObj) continue;
            CoreContentPath cp;
            JSStringRef pk = JSStringCreateWithUTF8CString("path");
            cp.path = jsValueToString(ctx, JSObjectGetProperty(ctx, pObj, pk, nullptr));
            JSStringRelease(pk);
            pk = JSStringCreateWithUTF8CString("extensions");
            cp.extensions = jsValueToString(ctx, JSObjectGetProperty(ctx, pObj, pk, nullptr));
            JSStringRelease(pk);
            paths.push_back(cp);
        }
    }

    g_coreConfigMgr.updateCore(file, enabled, cartSaves, stateSaves, priority, paths);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_resetLibretroCoreOptions) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string coreFile = jsValueToString(ctx, arguments[0]);
    g_coreConfigMgr.resetCoreOptions(coreFile);
    return JSValueMakeBoolean(ctx, true);
}

void UltralightManager_OpenTabMenu(void);
int UltralightManager_ConsumeRequestedTab(void);
AAPI_CALLBACK(js_manager_openTabMenu) {
    UltralightManager_OpenTabMenu();
    return JSValueMakeBoolean(ctx, true);
}
AAPI_CALLBACK(js_manager_getRequestedTab) {
    int idx = UltralightManager_ConsumeRequestedTab();
    if (idx < 0) return JSValueMakeNull(ctx);
    return JSValueMakeNumber(ctx, idx);
}

void UltralightManager_OpenBuildContextMenu(void);
AAPI_CALLBACK(js_manager_openBuildContextMenu) {
    UltralightManager_OpenBuildContextMenu();
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_spawnItemObject) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string itemId = jsValueToString(ctx, arguments[0]);

    /* Look up the full item from the database */
    Arcade::Item item = g_library.getItemById(itemId);
    if (item.id.empty()) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Item not found in database: %s\n", itemId.c_str());
        return JSValueMakeBoolean(ctx, false);
    }

    g_instanceManager.requestSpawn(item);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_manager_isModelCabinet) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string modelId = jsValueToString(ctx, arguments[0]);
    return JSValueMakeBoolean(ctx, g_instanceManager.isModelCabinet(modelId));
}

AAPI_CALLBACK(js_manager_captureThumbnail) {
    /* captureThumbnail(modelId, x, y, w, h) — capture game pixels and save as model thumbnail */
    if (argumentCount < 5) return JSValueMakeBoolean(ctx, false);
    std::string modelId = jsValueToString(ctx, arguments[0]);
    int x = (int)JSValueToNumber(ctx, arguments[1], nullptr);
    int y = (int)JSValueToNumber(ctx, arguments[2], nullptr);
    int w = (int)JSValueToNumber(ctx, arguments[3], nullptr);
    int h = (int)JSValueToNumber(ctx, arguments[4], nullptr);

    if (modelId.empty() || w <= 0 || h <= 0) return JSValueMakeBoolean(ctx, false);

    /* Call host to capture game framebuffer pixels */
    if (!g_host.capture_rect_pixels) return JSValueMakeBoolean(ctx, false);
    void* pixels = nullptr;
    int capturedW = 0, capturedH = 0;
    if (!g_host.capture_rect_pixels(x, y, w, h, &pixels, &capturedW, &capturedH))
        return JSValueMakeBoolean(ctx, false);

    /* Save as thumbnail (resized to max 512) */
    bool ok = g_imageLoader.saveThumbnail(modelId, (const uint8_t*)pixels, capturedW, capturedH);
    free(pixels);
    return JSValueMakeBoolean(ctx, ok);
}

AAPI_CALLBACK(js_manager_spawnModelObject) {
    if (argumentCount < 1) return JSValueMakeBoolean(ctx, false);
    std::string modelId = jsValueToString(ctx, arguments[0]);
    g_instanceManager.requestSpawnModel(modelId);
    return JSValueMakeBoolean(ctx, true);
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
    jsSetProp(ctx, o, "preview", item.preview);
    jsSetProp(ctx, o, "screen", item.screen);
    jsSetProp(ctx, o, "title", item.title);
    jsSetProp(ctx, o, "type", item.type);
    int usage = g_instanceManager.countItemUsage(item.id);
    if (usage > 0) jsSetInt(ctx, o, "count", usage);
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
    int usage = g_instanceManager.countModelUsage(m.id);
    if (usage > 0) jsSetInt(ctx, o, "count", usage);
    return o;
}

static JSObjectRef appToJS(JSContextRef ctx, const Arcade::App& a) {
    JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
    jsSetProp(ctx, o, "id", a.id);
    jsSetProp(ctx, o, "title", a.title);
    jsSetProp(ctx, o, "type", a.type);
    jsSetProp(ctx, o, "screen", a.screen);
    jsSetProp(ctx, o, "commandformat", a.commandformat);
    jsSetProp(ctx, o, "description", a.description);
    jsSetProp(ctx, o, "download", a.download);
    jsSetProp(ctx, o, "file", a.file);
    jsSetProp(ctx, o, "reference", a.reference);
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

AAPI_CALLBACK(js_aapi_getItemById) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string id = jsValueToString(ctx, arguments[0]);
    Arcade::Item item = g_library.getItemById(id);
    if (item.id.empty()) return JSValueMakeNull(ctx);
    return itemToJS(ctx, item);
}
AAPI_CALLBACK(js_aapi_getModelById) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string id = jsValueToString(ctx, arguments[0]);
    Arcade::Model model = g_library.getModelById(id);
    if (model.id.empty()) return JSValueMakeNull(ctx);
    return modelToJS(ctx, model);
}
AAPI_CALLBACK(js_aapi_getModelPlatformFile) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string modelId = jsValueToString(ctx, arguments[0]);
    std::string file = g_library.findModelPlatformFile(modelId, OPENJK_PLATFORM_ID);
    JSStringRef str = JSStringCreateWithUTF8CString(file.c_str());
    JSValueRef val = JSValueMakeString(ctx, str);
    JSStringRelease(str);
    return val;
}
AAPI_CALLBACK(js_aapi_updateModel) {
    if (argumentCount < 3) return JSValueMakeBoolean(ctx, false);
    std::string id = jsValueToString(ctx, arguments[0]);
    std::string field = jsValueToString(ctx, arguments[1]);
    std::string value = jsValueToString(ctx, arguments[2]);
    bool ok = g_library.updateModel(id, field, value);
    return JSValueMakeBoolean(ctx, ok);
}
AAPI_CALLBACK(js_aapi_updateItem) {
    if (argumentCount < 3) return JSValueMakeBoolean(ctx, false);
    std::string id = jsValueToString(ctx, arguments[0]);
    std::string field = jsValueToString(ctx, arguments[1]);
    std::string value = jsValueToString(ctx, arguments[2]);
    bool ok = g_library.updateItem(id, field, value);
    return JSValueMakeBoolean(ctx, ok);
}
AAPI_CALLBACK(js_aapi_createItem) {
    if (argumentCount < 3) return JSValueMakeNull(ctx);
    std::string title = jsValueToString(ctx, arguments[0]);
    std::string type = jsValueToString(ctx, arguments[1]);
    std::string file = jsValueToString(ctx, arguments[2]);
    std::string newId = g_library.createItem(title, type, file);
    if (newId.empty()) return JSValueMakeNull(ctx);
    JSStringRef str = JSStringCreateWithUTF8CString(newId.c_str());
    JSValueRef val = JSValueMakeString(ctx, str);
    JSStringRelease(str);
    return val;
}
AAPI_CALLBACK(js_aapi_findItemByFile) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string file = jsValueToString(ctx, arguments[0]);
    std::string id = g_library.findItemByFile(file);
    if (id.empty()) return JSValueMakeNull(ctx);
    JSStringRef str = JSStringCreateWithUTF8CString(id.c_str());
    JSValueRef val = JSValueMakeString(ctx, str);
    JSStringRelease(str);
    return val;
}
AAPI_CALLBACK(js_aapi_getItemsTyped) {
    int offset, limit; parseOffsetLimit(ctx, argumentCount, arguments, offset, limit);
    std::string typeFilter = (argumentCount > 2) ? jsValueToString(ctx, arguments[2]) : "";
    return vectorToJSArray(ctx, g_library.getItems(offset, limit, typeFilter), itemToJS);
}
AAPI_CALLBACK(js_aapi_searchItemsTyped) {
    int limit; std::string q = parseQueryLimit(ctx, argumentCount, arguments, limit);
    std::string typeFilter = (argumentCount > 2) ? jsValueToString(ctx, arguments[2]) : "";
    return vectorToJSArray(ctx, g_library.searchItems(q, limit, typeFilter), itemToJS);
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

AAPI_CALLBACK(js_aapi_getAppById) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string id = jsValueToString(ctx, arguments[0]);
    Arcade::App app = g_library.getAppById(id);
    if (app.id.empty()) return JSValueMakeNull(ctx);
    return appToJS(ctx, app);
}

AAPI_CALLBACK(js_aapi_getAppFilepaths) {
    if (argumentCount < 1) return JSValueMakeNull(ctx);
    std::string appId = jsValueToString(ctx, arguments[0]);
    auto fps = g_library.getAppFilepaths(appId);
    JSObjectRef arr = JSObjectMakeArray(ctx, 0, nullptr, nullptr);
    for (size_t i = 0; i < fps.size(); i++) {
        JSObjectRef o = JSObjectMake(ctx, nullptr, nullptr);
        jsSetProp(ctx, o, "app_id", fps[i].app_id);
        jsSetProp(ctx, o, "filepath_key", fps[i].filepath_key);
        jsSetProp(ctx, o, "path", fps[i].path);
        jsSetProp(ctx, o, "extensions", fps[i].extensions);
        JSObjectSetPropertyAtIndex(ctx, arr, (unsigned)i, o, nullptr);
    }
    return arr;
}

AAPI_CALLBACK(js_aapi_saveAppAttribute) {
    if (argumentCount < 3) return JSValueMakeBoolean(ctx, false);
    std::string appId = jsValueToString(ctx, arguments[0]);
    std::string field = jsValueToString(ctx, arguments[1]);
    std::string value = jsValueToString(ctx, arguments[2]);
    g_library.updateAppField(appId, field, value);
    return JSValueMakeBoolean(ctx, true);
}

AAPI_CALLBACK(js_aapi_saveAppFilepaths) {
    if (argumentCount < 2) return JSValueMakeBoolean(ctx, false);
    std::string appId = jsValueToString(ctx, arguments[0]);
    /* arguments[1] is an array of {path, extensions} objects */
    JSObjectRef arr = JSValueToObject(ctx, arguments[1], nullptr);
    if (!arr) return JSValueMakeBoolean(ctx, false);
    JSStringRef lenKey = JSStringCreateWithUTF8CString("length");
    int count = (int)JSValueToNumber(ctx, JSObjectGetProperty(ctx, arr, lenKey, nullptr), nullptr);
    JSStringRelease(lenKey);
    std::vector<Arcade::AppFilepath> paths;
    for (int i = 0; i < count; i++) {
        JSObjectRef o = JSValueToObject(ctx, JSObjectGetPropertyAtIndex(ctx, arr, i, nullptr), nullptr);
        if (!o) continue;
        Arcade::AppFilepath fp;
        fp.app_id = appId;
        JSStringRef pk = JSStringCreateWithUTF8CString("path");
        fp.path = jsValueToString(ctx, JSObjectGetProperty(ctx, o, pk, nullptr));
        JSStringRelease(pk);
        pk = JSStringCreateWithUTF8CString("extensions");
        fp.extensions = jsValueToString(ctx, JSObjectGetProperty(ctx, o, pk, nullptr));
        JSStringRelease(pk);
        paths.push_back(fp);
    }
    g_library.saveAppFilepaths(appId, paths);
    return JSValueMakeBoolean(ctx, true);
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
    addJSMethod(ctx, managerObj, "requestDeepSleep", js_manager_requestDeepSleep);
    addJSMethod(ctx, managerObj, "exitGame", js_manager_exitGame);
    addJSMethod(ctx, managerObj, "startLibretro", js_manager_startLibretro);
    addJSMethod(ctx, managerObj, "openLibraryBrowser", js_manager_openLibraryBrowser);
    addJSMethod(ctx, managerObj, "getVersion", js_manager_getVersion);
    addJSMethod(ctx, managerObj, "spawnItemObject", js_manager_spawnItemObject);
    addJSMethod(ctx, managerObj, "spawnModelObject", js_manager_spawnModelObject);
    addJSMethod(ctx, managerObj, "captureThumbnail", js_manager_captureThumbnail);
    addJSMethod(ctx, managerObj, "isModelCabinet", js_manager_isModelCabinet);
    addJSMethod(ctx, managerObj, "openTaskMenu", js_manager_openTaskMenu);
    addJSMethod(ctx, managerObj, "openMainMenu", js_manager_openMainMenu);
    addJSMethod(ctx, managerObj, "getActiveInstances", js_manager_getActiveInstances);
    addJSMethod(ctx, managerObj, "deactivateInstance", js_manager_deactivateInstance);
    addJSMethod(ctx, managerObj, "rememberItem", js_manager_rememberItem);
    addJSMethod(ctx, managerObj, "getRememberedItemId", js_manager_getRememberedItemId);
    addJSMethod(ctx, managerObj, "getOverlayInstanceInfo", js_manager_getOverlayInstanceInfo);
    addJSMethod(ctx, managerObj, "setHudInputActive", js_manager_setHudInputActive);
    addJSMethod(ctx, managerObj, "navigateInstance", js_manager_navigateInstance);
    addJSMethod(ctx, managerObj, "goBack", js_manager_goBack);
    addJSMethod(ctx, managerObj, "goForward", js_manager_goForward);
    addJSMethod(ctx, managerObj, "reloadInstance", js_manager_reloadInstance);
    addJSMethod(ctx, managerObj, "moveAimedObject", js_manager_moveAimedObject);
    addJSMethod(ctx, managerObj, "setSpawnModeModel", js_manager_setSpawnModeModel);
    addJSMethod(ctx, managerObj, "setSpawnTransform", js_manager_setSpawnTransform);
    addJSMethod(ctx, managerObj, "getSpawnModelId", js_manager_getSpawnModelId);
    addJSMethod(ctx, managerObj, "getInitialSpawnScale", js_manager_getInitialSpawnScale);
    addJSMethod(ctx, managerObj, "getSpawnItemId", js_manager_getSpawnItemId);
    addJSMethod(ctx, managerObj, "getOverlayMode", js_manager_getOverlayMode);
    addJSMethod(ctx, managerObj, "getAimedObjectInfo", js_manager_getAimedObjectInfo);
    addJSMethod(ctx, managerObj, "openBuildContextMenu", js_manager_openBuildContextMenu);
    addJSMethod(ctx, managerObj, "openTabMenu", js_manager_openTabMenu);
    addJSMethod(ctx, managerObj, "getRequestedTab", js_manager_getRequestedTab);
    addJSMethod(ctx, managerObj, "destroyAimedObject", js_manager_destroyAimedObject);
    addJSMethod(ctx, managerObj, "toggleSlaveAimedObject", js_manager_toggleSlaveAimedObject);
    addJSMethod(ctx, managerObj, "isAimedObjectSlave", js_manager_isAimedObjectSlave);
    addJSMethod(ctx, managerObj, "cloneAimedObject", js_manager_cloneAimedObject);
    addJSMethod(ctx, managerObj, "launchItem", js_manager_launchItem);
    addJSMethod(ctx, managerObj, "refreshItemTextures", js_manager_refreshItemTextures);
    addJSMethod(ctx, managerObj, "getVideoTimeInfo", js_manager_getVideoTimeInfo);
    addJSMethod(ctx, managerObj, "seekVideo", js_manager_seekVideo);
    addJSMethod(ctx, managerObj, "importDefaultLibrary", js_manager_importDefaultLibrary);
    addJSMethod(ctx, managerObj, "importAdoptedTemplates", js_manager_importAdoptedTemplates);
    addJSMethod(ctx, managerObj, "mergeLibrary", js_manager_mergeLibrary);
    addJSMethod(ctx, managerObj, "getAllLibretroCores", js_manager_getAllLibretroCores);
    addJSMethod(ctx, managerObj, "updateLibretroCore", js_manager_updateLibretroCore);
    addJSMethod(ctx, managerObj, "resetLibretroCoreOptions", js_manager_resetLibretroCoreOptions);

    JSStringRef managerName = JSStringCreateWithUTF8CString("manager");
    JSObjectSetProperty(ctx, aapiObj, managerName, managerObj, 0, nullptr);
    JSStringRelease(managerName);

    /* aapi.library — database queries */
    JSObjectRef libraryObj = JSObjectMake(ctx, nullptr, nullptr);
    addJSMethod(ctx, libraryObj, "getItemById", js_aapi_getItemById);
    addJSMethod(ctx, libraryObj, "getModelById", js_aapi_getModelById);
    addJSMethod(ctx, libraryObj, "getModelPlatformFile", js_aapi_getModelPlatformFile);
    addJSMethod(ctx, libraryObj, "updateModel", js_aapi_updateModel);
    addJSMethod(ctx, libraryObj, "updateItem", js_aapi_updateItem);
    addJSMethod(ctx, libraryObj, "createItem", js_aapi_createItem);
    addJSMethod(ctx, libraryObj, "findItemByFile", js_aapi_findItemByFile);
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
    addJSMethod(ctx, libraryObj, "getAppById", js_aapi_getAppById);
    addJSMethod(ctx, libraryObj, "getAppFilepaths", js_aapi_getAppFilepaths);
    addJSMethod(ctx, libraryObj, "saveAppAttribute", js_aapi_saveAppAttribute);
    addJSMethod(ctx, libraryObj, "saveAppFilepaths", js_aapi_saveAppFilepaths);

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
 * Clipboard implementation for Ultralight (Win32)
 * ======================================================================== */

class ArcadeClipboard : public ultralight::Clipboard {
public:
    void Clear() override {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    String ReadPlainText() override {
        if (!OpenClipboard(NULL)) return String();
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) { CloseClipboard(); return String(); }
        const Char16* text = (const Char16*)GlobalLock(h);
        if (!text) { CloseClipboard(); return String(); }
        size_t len = wcslen((const wchar_t*)text);
        String result(text, len);
        GlobalUnlock(h);
        CloseClipboard();
        return result;
    }

    void WritePlainText(const String& text) override {
        if (!OpenClipboard(NULL)) return;
        EmptyClipboard();
        String16 utf16 = text.utf16();
        size_t len = utf16.length();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(Char16));
        if (h) {
            Char16* dst = (Char16*)GlobalLock(h);
            memcpy(dst, utf16.data(), len * sizeof(Char16));
            dst[len] = 0;
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
    }
};

static ArcadeClipboard g_clipboard;

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
    config.cache_path = "./cache/ultralight/";
    Platform::instance().set_config(config);
    Platform::instance().set_font_loader(GetPlatformFontLoader());
    Platform::instance().set_file_system(GetPlatformFileSystem("./"));
    Platform::instance().set_clipboard(&g_clipboard);

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

    /* Suppress character input when Ctrl is held — accelerators (Ctrl+C/V/X/A)
       are already handled by kType_RawKeyDown, don't also type the letter */
    if (modifiers & AACORE_MOD_CTRL) return;

    KeyEvent evt;
    evt.type = KeyEvent::kType_Char;
    evt.virtual_key_code = 0;
    evt.native_key_code = 0;
    evt.modifiers = 0;
    if (modifiers & AACORE_MOD_ALT)   evt.modifiers |= KeyEvent::kMod_AltKey;
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

static void ul_mouse_wheel(EmbeddedInstance* inst, int delta)
{
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    ScrollEvent evt;
    evt.type = ScrollEvent::kType_ScrollByPixel;
    evt.delta_x = 0;
    evt.delta_y = delta;
    data->view->FireScrollEvent(evt);
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
    ul_mouse_wheel,
    NULL, /* get_title */
    NULL, NULL, /* get_width, get_height */
    NULL, /* navigate */
    NULL, NULL, NULL, NULL, NULL /* go_back, go_forward, reload, can_go_back, can_go_forward */
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

void UltralightInstance_EvaluateScript(EmbeddedInstance* inst, const char* script)
{
    if (!inst || !inst->user_data) return;
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return;
    ultralight::String exception;
    data->view->EvaluateScript(ultralight::String(script), &exception);
    if (!exception.empty() && g_host.host_printf)
        g_host.host_printf("UL EvaluateScript error: %s\n", ultralight::String(exception).utf8().data());
}

std::string UltralightInstance_EvalScriptString(EmbeddedInstance* inst, const char* script)
{
    if (!inst || !inst->user_data) return "";
    UltralightData* data = (UltralightData*)inst->user_data;
    if (!data->initialized || !data->view) return "";
    ultralight::String exception;
    ultralight::String result = data->view->EvaluateScript(ultralight::String(script), &exception);
    if (!exception.empty()) return "";
    if (result.empty()) return "";
    /* "null" and "undefined" come back as literal strings */
    std::string out = result.utf8().data();
    if (out == "null" || out == "undefined") return "";
    return out;
}
