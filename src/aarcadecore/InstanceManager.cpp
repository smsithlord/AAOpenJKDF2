#include "InstanceManager.h"
#include "aarcadecore_internal.h"
#include <algorithm>
#include <cctype>

/* Forward declarations for embedded instance creation */
EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* url, const char* material_name);
void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst);
extern "C" bool SteamworksWebBrowserInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                                             unsigned char** bgraOut,
                                                             int* widthOut, int* heightOut);
EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name);
extern "C" EmbeddedInstance* VideoPlayerInstance_Create(const char* file_path, const char* material_name);
extern "C" void VideoPlayerInstance_Destroy(EmbeddedInstance* inst);
extern "C" bool VideoPlayerInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                                    unsigned char** bgraOut,
                                                    int* widthOut, int* heightOut);
void LibretroInstance_Destroy(EmbeddedInstance* inst);
extern "C" bool LibretroInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                                 unsigned char** bgraOut,
                                                 int* widthOut, int* heightOut);

/* Exposed from aarcadecore.cpp */
int aarcadecore_addTask(EmbeddedInstance* inst);
void aarcadecore_removeTask(int taskIndex);
void aarcadecore_setFullscreenInstance(EmbeddedInstance* inst);
EmbeddedInstance* aarcadecore_getFullscreenInstance(void);
EmbeddedInstance* aarcadecore_getInputModeInstance(void);
void aarcadecore_setInputModeInstance(EmbeddedInstance* inst);
void UltralightManager_UnloadOverlay(void);
void aarcadecore_clearOverlayAssociation(void);

#include "ImageLoader.h"
#include "SQLiteLibrary.h"
#include "LibretroCoreConfig.h"
extern ImageLoader g_imageLoader;
extern SQLiteLibrary g_library;
std::string UltralightManager_EvalLocalStorage(const char* key);

/* YouTube URL patterns:
 *   https://www.youtube.com/watch?v=VIDEO_ID
 *   https://youtube.com/watch?v=VIDEO_ID&list=...
 *   https://youtu.be/VIDEO_ID
 *   https://www.youtube.com/embed/VIDEO_ID
 */

std::string InstanceManager::extractYouTubeVideoId(const std::string& url)
{
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    size_t pos = lower.find("youtu.be/");
    if (pos != std::string::npos) {
        size_t start = pos + 9;
        size_t end = url.find_first_of("?&#", start);
        if (end == std::string::npos) end = url.size();
        return url.substr(start, end - start);
    }

    if (lower.find("youtube.com") != std::string::npos) {
        pos = url.find("v=");
        if (pos != std::string::npos) {
            size_t start = pos + 2;
            size_t end = url.find_first_of("&#", start);
            if (end == std::string::npos) end = url.size();
            return url.substr(start, end - start);
        }

        pos = lower.find("/embed/");
        if (pos != std::string::npos) {
            size_t start = pos + 7;
            size_t end = url.find_first_of("?&#", start);
            if (end == std::string::npos) end = url.size();
            return url.substr(start, end - start);
        }
    }

    return "";
}

static std::string urlEncode(const std::string& str)
{
    std::string result;
    for (char c : str) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            result += buf;
        }
    }
    return result;
}

std::string InstanceManager::resolveUrl(const std::string& fileUrl, const std::string& previewUrl, const std::string& itemTitle)
{
    std::string url = fileUrl.empty() ? previewUrl : fileUrl;
    if (url.empty()) return url;

    std::string videoId = extractYouTubeVideoId(url);
    if (!videoId.empty()) {
        std::string result = "https://anarchyarcade.com/aarcade/youtube_player.php?id=" + videoId
            + "&plbehavior=shuffle&vbehavior=default&endbehavior=near"
            + "&annotations=0&mixes=0&related=default&autoplay=0";
        if (!itemTitle.empty())
            result += "&title=" + urlEncode(itemTitle);
        return result;
    }

    return url;
}

/* --- Embedded item instance management --- */

/* Check if a URL looks like an image (matches library.js isImageUrl logic) */
static bool isImageUrl(const std::string& url)
{
    if (url.empty()) return false;
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const char* exts[] = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".svg"};
    for (const char* ext : exts) {
        if (lower.find(ext) != std::string::npos) return true;
    }
    const char* keywords[] = {"image", "screenshot", "preview", "thumb"};
    for (const char* kw : keywords) {
        if (lower.find(kw) != std::string::npos) return true;
    }
    return false;
}

/* Check if a file path looks like a video file */
static bool isVideoFile(const std::string& file)
{
    if (file.empty()) return false;
    std::string lower = file;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const char* exts[] = {".mp4", ".mpeg", ".mpg", ".avi", ".mkv", ".mov",
                          ".wmv", ".flv", ".webm", ".m4v", ".ts"};
    for (const char* ext : exts) {
        size_t pos = lower.rfind(ext);
        if (pos != std::string::npos && pos + strlen(ext) == lower.size())
            return true;
    }
    return false;
}

/* Case-insensitive substring search */
static bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
    std::string h = haystack, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

/* URL-like detection: anything that should NOT be probed as a local file */
static bool isUrlLike(const std::string& s)
{
    if (s.empty()) return false;
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("://") != std::string::npos) return true;
    if (lower.rfind("www.", 0) == 0) return true;
    if (lower.rfind("//", 0) == 0) return true;
    return false;
}

/* Resolve item.file to an existing local path, else "".
 * Handles:
 *   - full path (drive letter / UNC) that exists on disk
 *   - bare filename that resolves under one of the Open With app's filepaths
 *     (with extension gating against fp.extensions)
 * Returns "" for URLs, empty paths, broken full paths, and unresolvable filenames. */
static std::string resolveLocalItemFile(const Arcade::Item& item)
{
    if (item.file.empty()) return "";
    if (isUrlLike(item.file)) return "";

    /* Full path: drive letter (X:) or UNC (\\) */
    bool isFullPath = false;
    if (item.file.size() >= 2 && isalpha((unsigned char)item.file[0]) && item.file[1] == ':')
        isFullPath = true;
    else if (item.file.size() >= 2 && (item.file[0] == '\\' || item.file[0] == '/')
                                    && (item.file[1] == '\\' || item.file[1] == '/'))
        isFullPath = true;

    if (isFullPath) {
        FILE* f = fopen(item.file.c_str(), "rb");
        if (f) { fclose(f); return item.file; }
        return ""; /* broken full path — do NOT try app resolution */
    }

    if (item.app.empty()) return "";

    /* Extract item.file's extension for the gate */
    std::string fileExt;
    {
        size_t dot = item.file.rfind('.');
        if (dot != std::string::npos) {
            fileExt = item.file.substr(dot + 1);
            std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
        }
    }

    /* Normalize item.file's separators for joining */
    std::string relFile = item.file;
    for (char& c : relFile) if (c == '/') c = '\\';

    auto appFilepaths = g_library.getAppFilepaths(item.app);
    for (const auto& fp : appFilepaths) {
        /* Extension gate */
        if (!fp.extensions.empty()) {
            if (fileExt.empty()) continue;
            bool extOk = false;
            std::string exts = fp.extensions;
            size_t start = 0;
            while (start <= exts.size()) {
                size_t comma = exts.find(',', start);
                std::string tok = exts.substr(start, (comma == std::string::npos) ? std::string::npos : comma - start);
                /* trim */
                size_t a = tok.find_first_not_of(" \t");
                size_t b = tok.find_last_not_of(" \t");
                if (a != std::string::npos) tok = tok.substr(a, b - a + 1);
                else tok.clear();
                if (!tok.empty() && tok[0] == '.') tok.erase(0, 1);
                std::transform(tok.begin(), tok.end(), tok.begin(), ::tolower);
                if (!tok.empty() && tok == fileExt) { extOk = true; break; }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            if (!extOk) continue;
        }

        /* Normalize base path */
        std::string base = fp.path;
        for (char& c : base) if (c == '/') c = '\\';
        if (!base.empty() && base.back() != '\\') base += '\\';

        std::string candidate = base + relFile;
        FILE* f = fopen(candidate.c_str(), "rb");
        if (f) { fclose(f); return candidate; }
    }

    return "";
}

void InstanceManager::ensureItemInstance(const Arcade::Item& item, const std::string& resolvedUrl)
{
    auto it = itemInstances_.find(item.id);
    if (it != itemInstances_.end()) {
        if (it->second.active) return; /* already active, nothing to do */
        /* Stale entry (browser destroyed on deactivate) — remove it so we recreate below */
        itemInstances_.erase(it);
    }

    EmbeddedItemInstance inst;
    inst.itemId = item.id;
    inst.url = resolvedUrl;
    inst.active = true;
    inst.browser = nullptr;
    inst.taskIndex = -1;

    /* Decide which embedded instance type to create based on item data.
     * Rule: prefer FILE behavior (video/Libretro) only if item.file resolves to a
     * local file that actually exists. Otherwise prefer PREVIEW in the web browser. */
    EmbeddedInstance* task = nullptr;
    std::string localPath = resolveLocalItemFile(item);

    if (!localPath.empty()) {
        if (isVideoFile(localPath)) {
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: '%s' is a video file — creating VideoPlayer instance (resolved: %s)\n",
                                  item.file.c_str(), localPath.c_str());
            task = VideoPlayerInstance_Create(localPath.c_str(), "DynScreen.mat");
        } else {
            /* Libretro: full-path match first, then app-filepath overlap */
            std::string coreDll = g_coreConfigMgr.findCoreForFile(localPath);
            if (coreDll.empty() && !item.app.empty()) {
                auto appFilepaths = g_library.getAppFilepaths(item.app);
                if (!appFilepaths.empty()) {
                    std::vector<std::pair<std::string, std::string>> appPaths;
                    for (const auto& fp : appFilepaths)
                        appPaths.push_back({fp.path, fp.extensions});
                    coreDll = g_coreConfigMgr.findCoreMatchingAppPaths(appPaths);
                }
            }
            if (!coreDll.empty()) {
                std::string corePath = std::string("aarcadecore/libretro/cores/") + coreDll;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: '%s' matched core '%s' — creating Libretro instance (game: %s)\n",
                                      item.file.c_str(), coreDll.c_str(), localPath.c_str());
                task = LibretroInstance_Create(corePath.c_str(), localPath.c_str(), "DynScreen.mat");
            }
            /* else: local file exists but no handler — fall through to preview/skip */
        }
    }

    if (!task) {
        /* FILE wasn't a usable local file (or was unhandled). Prefer PREVIEW. */
        std::string browserUrl;
        if (!item.preview.empty())      browserUrl = item.preview;
        else if (isUrlLike(item.file))  browserUrl = item.file;
        else if (!item.screen.empty())  browserUrl = item.screen;

        if (!browserUrl.empty()) {
            std::string finalUrl = resolveUrl(browserUrl, "", item.title);
            task = SteamworksWebBrowserInstance_Create(finalUrl.c_str(), "DynScreen.mat");
        } else {
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: no usable file/preview/screen — skipping instance for item=%s\n",
                                  item.id.c_str());
        }
    }

    if (task && task->vtable->init(task)) {
        inst.browser = task;
        inst.taskIndex = aarcadecore_addTask(task);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Created %s instance for item=%s (taskIndex=%d)\n",
                              task->type == EMBEDDED_LIBRETRO ? "Libretro" :
                              task->type == EMBEDDED_VIDEO_PLAYER ? "VideoPlayer" : "SteamworksWebBrowser",
                              item.id.c_str(), inst.taskIndex);
    } else {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: WARNING: Failed to create instance for item=%s\n", item.id.c_str());
        if (task) {
            if (task->vtable->shutdown) task->vtable->shutdown(task);
            free(task);
        }
    }

    itemInstances_[item.id] = inst;
}

/* --- Level management --- */

void InstanceManager::onMapUnloaded()
{
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Map unloaded, deactivating all instances\n");

    /* Deactivate all embedded instances (browsers etc.) */
    for (auto& pair : itemInstances_) {
        if (pair.second.active) {
            deactivateInstance(pair.first);
        }
    }

    /* Clear spawned objects and state */
    objects_.clear();
    selectedObjectIndex_ = -1;
    spawnTransformSet_ = false;
    currentInstanceId_.clear();
    currentMapId_.clear();
}

void InstanceManager::onMapLoaded()
{
    /* Clear any stale state from previous map */
    objects_.clear();
    selectedObjectIndex_ = -1;
    currentInstanceId_.clear();
    currentMapId_.clear();

    /* Query host for current map key */
    if (!g_host.get_current_map) return;
    char mapKey[128] = {0};
    g_host.get_current_map(mapKey, sizeof(mapKey));
    if (!mapKey[0]) return;

    std::string fileKey = mapKey;

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Setting level to '%s'\n", fileKey.c_str());

    /* Find or create the map */
    currentMapId_ = g_library.findMapByPlatformFile(OPENJK_PLATFORM_ID, fileKey);
    if (currentMapId_.empty()) {
        currentMapId_ = g_library.createMap(fileKey, OPENJK_PLATFORM_ID, fileKey);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Created new map '%s' (id=%s)\n", fileKey.c_str(), currentMapId_.c_str());
    } else {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Found existing map (id=%s)\n", currentMapId_.c_str());
    }

    /* Find or create the instance */
    currentInstanceId_ = g_library.findInstanceByMap(currentMapId_);
    if (currentInstanceId_.empty()) {
        currentInstanceId_ = g_library.createInstance("Unnamed Instance", currentMapId_);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Created new instance (id=%s)\n", currentInstanceId_.c_str());
    } else {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Found existing instance (id=%s)\n", currentInstanceId_.c_str());

        /* Restore saved objects */
        auto savedObjects = g_library.getInstanceObjects(currentInstanceId_);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Restoring %d objects\n", (int)savedObjects.size());

        for (const auto& obj : savedObjects) {
            /* Look up the item from the database (may be empty for model-only objects) */
            Arcade::Item item;
            if (!obj.item.empty()) {
                item = g_library.getItemById(obj.item);
                if (item.id.empty()) {
                    if (g_host.host_printf)
                        g_host.host_printf("InstanceManager: WARNING: Item '%s' not found, skipping object\n", obj.item.c_str());
                    continue;
                }
            }

            /* Parse position and rotation from strings */
            SpawnRequest req;
            req.item = item;
            /* Resolve model and template */
            if (!obj.model.empty()) {
                req.modelId = obj.model;
                std::string tmpl = g_library.findModelPlatformFile(obj.model, OPENJK_PLATFORM_ID);
                if (!tmpl.empty()) req.templateName = tmpl;
            }
            if (req.modelId.empty()) {
                /* Old save with no model — find or create the default */
                std::string defaultTemplate = "dyn_videosign";
                req.modelId = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, defaultTemplate);
                if (req.modelId.empty())
                    req.modelId = g_library.createModel("Video Sign", OPENJK_PLATFORM_ID, defaultTemplate);
                req.templateName = defaultTemplate;
            }
            if (req.templateName.empty()) req.templateName = "dyn_videosign";
            req.hasExplicitPosition = true;
            req.objectKey = obj.object_key;

            float px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0;
            int sector = -1;
            sscanf(obj.position.c_str(), "%f %f %f %d", &px, &py, &pz, &sector);
            sscanf(obj.rotation.c_str(), "%f %f %f", &rx, &ry, &rz);
            req.posX = px; req.posY = py; req.posZ = pz;
            req.sectorId = sector;
            req.rotX = rx; req.rotY = ry; req.rotZ = rz;
            req.scale = obj.scale;
            req.slave = obj.slave;

            pendingSpawns_.push(req);
        }
    }
}

/* --- Spawn pipeline --- */

void InstanceManager::requestSpawn(const Arcade::Item& item)
{
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Spawn requested - item=%s title=%s\n",
                          item.id.c_str(), item.title.c_str());

    SpawnRequest req;
    req.item = item;

    /* Check localStorage for best model: per-item first, then global, then default */
    std::string bestModelId;
    if (!item.id.empty()) {
        std::string key = std::string("lastSpawnModelId_") + item.id;
        bestModelId = UltralightManager_EvalLocalStorage(key.c_str());
    }
    if (bestModelId.empty())
        bestModelId = UltralightManager_EvalLocalStorage("lastSpawnModelId");

    if (!bestModelId.empty()) {
        std::string tmpl = g_library.findModelPlatformFile(bestModelId, OPENJK_PLATFORM_ID);
        if (!tmpl.empty()) {
            req.modelId = bestModelId;
            req.templateName = tmpl;
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Using remembered model %s (%s)\n",
                                  bestModelId.c_str(), tmpl.c_str());
        }
    }

    /* Fallback to default if no remembered model found */
    if (req.templateName.empty()) {
        std::string defaultTemplate = "aaojk_movie_stand_standard";
        req.modelId = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, defaultTemplate);
        if (req.modelId.empty())
            req.modelId = g_library.createModel("Movie Stand Standard", OPENJK_PLATFORM_ID, defaultTemplate);
        req.templateName = defaultTemplate;
    }
    req.hasExplicitPosition = false;
    req.posX = req.posY = req.posZ = 0;
    req.sectorId = -1;
    req.rotX = req.rotY = req.rotZ = 0;
    req.scale = 1.0f;
    req.slave = 0;
    pendingSpawns_.push(req);
}

int InstanceManager::countItemUsage(const std::string& itemId) const
{
    if (itemId.empty()) return 0;
    int count = 0;
    for (const auto& obj : objects_) {
        if (obj.itemId == itemId) count++;
    }
    return count;
}

int InstanceManager::countModelUsage(const std::string& modelId) const
{
    if (modelId.empty()) return 0;
    int count = 0;
    for (const auto& obj : objects_)
        if (obj.modelId == modelId) count++;
    return count;
}

void InstanceManager::requestSpawnModel(const std::string& modelId)
{
    std::string tmpl = g_library.findModelPlatformFile(modelId, OPENJK_PLATFORM_ID);
    if (tmpl.empty()) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: No template found for model %s\n", modelId.c_str());
        return;
    }

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Model spawn - model=%s template=%s\n",
                          modelId.c_str(), tmpl.c_str());

    SpawnRequest req;
    // Empty item — no embedded content, no dynamic textures
    req.modelId = modelId;
    req.templateName = tmpl;
    req.hasExplicitPosition = false;
    req.posX = req.posY = req.posZ = 0;
    req.sectorId = -1;
    req.rotX = req.rotY = req.rotZ = 0;
    req.scale = 1.0f;
    req.slave = 0;
    pendingSpawns_.push(req);
}

bool InstanceManager::isModelCabinet(const std::string& modelId)
{
    if (!g_host.is_template_cabinet) return false;
    std::string tmpl = g_library.findModelPlatformFile(modelId, OPENJK_PLATFORM_ID);
    if (tmpl.empty()) return false;
    return g_host.is_template_cabinet(tmpl.c_str()) != 0;
}

void InstanceManager::requestSpawn(const Arcade::Item& item, const std::string& modelId, float scale)
{
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Clone spawn - item=%s model=%s scale=%.1f\n",
                          item.id.c_str(), modelId.c_str(), scale);

    SpawnRequest req;
    req.item = item;
    req.modelId = modelId;
    std::string tmpl = g_library.findModelPlatformFile(modelId, OPENJK_PLATFORM_ID);
    if (tmpl.empty()) {
        /* Fallback to default if model template not found */
        tmpl = "aaojk_movie_stand_standard";
        req.modelId = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, tmpl);
        if (req.modelId.empty())
            req.modelId = g_library.createModel("Movie Stand Standard", OPENJK_PLATFORM_ID, tmpl);
    }
    req.templateName = tmpl;
    req.hasExplicitPosition = false;
    req.posX = req.posY = req.posZ = 0;
    req.sectorId = -1;
    req.rotX = req.rotY = req.rotZ = 0;
    req.scale = scale;
    req.slave = 0;
    pendingSpawns_.push(req);
}

bool InstanceManager::hasPendingSpawn() const
{
    return !pendingSpawns_.empty();
}

SpawnRequest InstanceManager::popPendingSpawn()
{
    lastPopped_ = pendingSpawns_.front();
    pendingSpawns_.pop();
    return lastPopped_;
}

/* Kick off an item-channel image request. JS walks the full legacy
 * (field × variation) priority list and reports back success/failure. */
void InstanceManager::requestChannelImage(int objIdx, int thingIdx, const Arcade::Item& item, bool isScreen)
{
    if (!g_imageLoader.isInitialized()) return;

    ImageLoader::ItemChannelFields fields;
    fields.id = item.id;
    fields.marquee = item.marquee;
    fields.screen = item.screen;
    fields.preview = item.preview;
    fields.file = item.file;

    std::string channel = isScreen ? "screen" : "marquee";
    std::string requestId = std::to_string(thingIdx) + "|" + channel;

    if (isScreen)
        objects_[objIdx].screenImageRequested = true;
    else
        objects_[objIdx].marqueeImageRequested = true;

    g_imageLoader.requestItemChannel(requestId, fields, channel,
        [this, objIdx, thingIdx, isScreen](const ImageLoadResult& result) {
            if (objIdx >= (int)objects_.size() || objects_[objIdx].thingIdx != thingIdx) return;
            if (result.success) {
                if (isScreen)
                    objects_[objIdx].screenImagePath = result.filePath;
                else
                    objects_[objIdx].marqueeImagePath = result.filePath;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: %s image ready for thingIdx=%d: %s\n",
                                      isScreen ? "Screen" : "Marquee", thingIdx, result.filePath.c_str());
            } else {
                if (isScreen)
                    objects_[objIdx].screenImageRequested = false;
                else
                    objects_[objIdx].marqueeImageRequested = false;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: All %s image candidates failed for thingIdx=%d\n",
                                      isScreen ? "screen" : "marquee", thingIdx);
            }
        });
}

void InstanceManager::initSpawnedObject(int thingIdx)
{
    const Arcade::Item& item = lastPopped_.item;
    std::string resolvedUrl = resolveUrl(item.file, !item.preview.empty() ? item.preview : item.screen, item.title);

    /* Add spawned object */
    SpawnedObject obj;
    obj.itemId = item.id;
    obj.modelId = lastPopped_.modelId;
    obj.objectKey = lastPopped_.objectKey.empty() ? Arcade::generateFirebasePushId() : lastPopped_.objectKey;
    obj.url = resolvedUrl;
    obj.scale = lastPopped_.scale;
    obj.slave = lastPopped_.slave != 0;
    obj.thingIdx = thingIdx;
    obj.screenImageRequested = false;
    obj.marqueeImageRequested = false;
    objects_.push_back(obj);

    /* Request screen + marquee images via item-channel loader (JS walks the full
     * legacy field × variation fallback list and reports a winning URL). */
    int objIdx = (int)objects_.size() - 1;
    requestChannelImage(objIdx, thingIdx, item, true);
    requestChannelImage(objIdx, thingIdx, item, false);

    int newIndex = (int)objects_.size() - 1;

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: initSpawnedObject - thingIdx=%d item=%s url=%s (total objects: %d)\n",
                          thingIdx, obj.itemId.c_str(), obj.url.c_str(), (int)objects_.size());
}

void InstanceManager::confirmSpawn(int thingIdx)
{
    /* Update scale from spawn transform before position is reported and saved.
     * Skip for restores (objectKey set) — they already have the correct saved scale. */
    if (spawnTransformSet_) {
        for (auto& obj : objects_) {
            if (obj.thingIdx == thingIdx) {
                if (lastPopped_.objectKey.empty()) {
                    obj.scale = spawnScale_;
                }
                break;
            }
        }
    }
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Spawn confirmed - thingIdx=%d\n", thingIdx);
}

/* --- Selection --- */

void InstanceManager::selectObject(int index)
{
    if (index < -1 || index >= (int)objects_.size()) return;

    /* Deselect previous (but leave its embedded instance active) */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        const SpawnedObject& prev = objects_[selectedObjectIndex_];
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Deselected object #%d (thingIdx=%d, item=%s) - instance stays active\n",
                              selectedObjectIndex_, prev.thingIdx, prev.itemId.c_str());
    }

    selectedObjectIndex_ = index;

    if (index >= 0) {
        const SpawnedObject& obj = objects_[index];
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Selected object #%d (thingIdx=%d, item=%s)\n",
                              index, obj.thingIdx, obj.itemId.c_str());
    }
}

void InstanceManager::objectUsed(int thingIdx)
{
    /* Redirect slave objects to their master */
    if (thingIdx >= 0) thingIdx = resolveMasterThingIdx(thingIdx);

    /* thingIdx == -1 means "deselect current" (e.g. LMB on empty space) */
    if (thingIdx < 0) {
        if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
            const SpawnedObject& prev = objects_[selectedObjectIndex_];
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: objectUsed — deselecting object #%d (item=%s)\n",
                                  selectedObjectIndex_, prev.itemId.c_str());
            if (aarcadecore_getFullscreenInstance())
                aarcadecore_setFullscreenInstance(NULL);
            deactivateInstance(prev.itemId);
            selectedObjectIndex_ = -1;
        }
        return;
    }

    /* Find the object by thingIdx */
    int newIndex = -1;
    for (int i = 0; i < (int)objects_.size(); i++) {
        if (objects_[i].thingIdx == thingIdx) {
            newIndex = i;
            break;
        }
    }

    if (newIndex < 0) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: objectUsed — thingIdx=%d not found in spawned objects\n", thingIdx);
        return;
    }

    /* If this object is already selected, toggle fullscreen overlay */
    if (newIndex == selectedObjectIndex_) {
        const SpawnedObject& obj = objects_[newIndex];
        const EmbeddedItemInstance* inst = getItemInstance(obj.itemId);

        if (aarcadecore_getFullscreenInstance()) {
            /* Already fullscreen — exit fullscreen (instance stays active on sithThing) */
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: objectUsed — exiting fullscreen for object #%d (thingIdx=%d)\n",
                                  newIndex, thingIdx);
            aarcadecore_setFullscreenInstance(NULL);
        } else if (inst && inst->active && inst->browser) {
            /* Instance is active — enter fullscreen overlay */
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: objectUsed — entering fullscreen for object #%d (thingIdx=%d)\n",
                                  newIndex, thingIdx);
            aarcadecore_setFullscreenInstance(inst->browser);
        }
        return;
    }

    /* If switching to a different object, exit fullscreen first */
    if (aarcadecore_getFullscreenInstance()) {
        aarcadecore_setFullscreenInstance(NULL);
    }

    /* Select the new object */
    const SpawnedObject& obj = objects_[newIndex];

    /* Deactivate the previously selected object's instance — but only if the
     * new object uses a different item (shared items reuse the same instance) */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        const SpawnedObject& prev = objects_[selectedObjectIndex_];
        if (prev.itemId != obj.itemId) {
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: objectUsed — deactivating previous object #%d (item=%s)\n",
                                  selectedObjectIndex_, prev.itemId.c_str());
            deactivateInstance(prev.itemId);
        } else {
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: objectUsed — keeping shared instance for item=%s\n", prev.itemId.c_str());
        }
    }

    selectedObjectIndex_ = newIndex;

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: objectUsed — selected object #%d (thingIdx=%d, item=%s)\n",
                          newIndex, thingIdx, obj.itemId.c_str());

    /* Activate its embedded instance */
    Arcade::Item item = g_library.getItemById(obj.itemId);
    if (item.id.empty()) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: objectUsed — WARNING: item '%s' not found in library\n", obj.itemId.c_str());
        return;
    }

    std::string resolvedUrl = resolveUrl(item.file, !item.preview.empty() ? item.preview : item.screen, item.title);
    ensureItemInstance(item, resolvedUrl);
}

/* --- Selector ray --- */

void InstanceManager::setAimedThing(int thingIdx)
{
    if (thingIdx == aimedThingIdx_) return;
    aimedThingIdx_ = thingIdx;

    if (thingIdx < 0) return;

    /* Find the spawned object and look up the item */
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            Arcade::Item item = g_library.getItemById(obj.itemId);
            if (!item.id.empty() && g_host.host_printf)
                g_host.host_printf("InstanceManager: Aiming at '%s'\n", item.title.c_str());
            return;
        }
    }
}

const SpawnedObject* InstanceManager::getAimedObject() const
{
    if (aimedThingIdx_ < 0) return nullptr;
    for (const auto& obj : objects_) {
        if (obj.thingIdx == aimedThingIdx_) return &obj;
    }
    return nullptr;
}

/* --- Destroy --- */

void InstanceManager::destroyObject(int thingIdx)
{
    if (thingIdx < 0) return;

    /* Find the object */
    int objIdx = -1;
    for (int i = 0; i < (int)objects_.size(); i++) {
        if (objects_[i].thingIdx == thingIdx) { objIdx = i; break; }
    }
    if (objIdx < 0) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: destroyObject — thingIdx=%d not found\n", thingIdx);
        return;
    }

    const SpawnedObject& obj = objects_[objIdx];
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Destroying object #%d (thingIdx=%d, item=%s, key=%s)\n",
                          objIdx, thingIdx, obj.itemId.c_str(), obj.objectKey.c_str());

    /* Clear aimed-at if this is the aimed object */
    if (aimedThingIdx_ == thingIdx) aimedThingIdx_ = -1;

    /* Exit fullscreen if this object's instance is fullscreen */
    if (aarcadecore_getFullscreenInstance()) {
        const EmbeddedItemInstance* inst = getItemInstance(obj.itemId);
        if (inst && inst->browser == aarcadecore_getFullscreenInstance())
            aarcadecore_setFullscreenInstance(NULL);
    }

    /* Deselect if this is the selected object */
    if (selectedObjectIndex_ == objIdx)
        selectedObjectIndex_ = -1;

    /* Deactivate embedded instance if active */
    deactivateInstance(obj.itemId);

    /* Delete from database */
    if (!currentInstanceId_.empty())
        g_library.deleteInstanceObject(currentInstanceId_, obj.objectKey);

    /* Queue thingIdx for host to destroy the sithThing */
    pendingDestroys_.push(thingIdx);

    /* Remove from objects_ vector */
    objects_.erase(objects_.begin() + objIdx);

    /* Fix selectedObjectIndex_ after erasure */
    if (selectedObjectIndex_ > objIdx)
        selectedObjectIndex_--;
}

bool InstanceManager::hasPendingDestroy() const
{
    return !pendingDestroys_.empty();
}

int InstanceManager::popPendingDestroy()
{
    int idx = pendingDestroys_.front();
    pendingDestroys_.pop();
    return idx;
}

void InstanceManager::requestMove(int thingIdx)
{
    pendingMoves_.push(thingIdx);
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Move requested for thingIdx=%d\n", thingIdx);
}

bool InstanceManager::hasPendingMove() const { return !pendingMoves_.empty(); }

int InstanceManager::popPendingMove()
{
    int idx = pendingMoves_.front();
    pendingMoves_.pop();
    return idx;
}

void InstanceManager::setSpawnTransform(float pitch, float yaw, float roll, bool isWorldRot,
                                         float offX, float offY, float offZ, bool isWorldOffset, bool useRaycastOffset,
                                         float scale)
{
    spawnPitch_ = pitch; spawnYaw_ = yaw; spawnRoll_ = roll; spawnRotIsWorld_ = isWorldRot;
    spawnOffX_ = offX; spawnOffY_ = offY; spawnOffZ_ = offZ;
    spawnOffIsWorld_ = isWorldOffset; spawnUseRaycast_ = useRaycastOffset;
    spawnScale_ = scale;
    spawnTransformSet_ = true;
}

void InstanceManager::getSpawnTransform(float* p, float* y, float* r, bool* isWorldRot,
                                         float* ox, float* oy, float* oz, bool* isWorldOff, bool* useRaycast,
                                         float* scale) const
{
    if (p) *p = spawnPitch_; if (y) *y = spawnYaw_; if (r) *r = spawnRoll_;
    if (isWorldRot) *isWorldRot = spawnRotIsWorld_;
    if (ox) *ox = spawnOffX_; if (oy) *oy = spawnOffY_; if (oz) *oz = spawnOffZ_;
    if (isWorldOff) *isWorldOff = spawnOffIsWorld_;
    if (useRaycast) *useRaycast = spawnUseRaycast_;
    if (scale) *scale = spawnScale_;
}

float InstanceManager::getInitialSpawnScale() const
{
    /* For move mode: return the scale of the object being moved */
    if (spawnPreviewThingIdx_ >= 0) {
        for (const auto& obj : objects_) {
            if (obj.thingIdx == spawnPreviewThingIdx_) return obj.scale;
        }
    }
    /* For clone/new spawn: use the lastPopped scale */
    return lastPopped_.scale;
}

float InstanceManager::getObjectScale(int thingIdx) const
{
    /* During spawn/move mode, use live slider value for the preview thing */
    if (thingIdx == spawnPreviewThingIdx_ && spawnTransformSet_)
        return spawnScale_;

    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) return obj.scale;
    }
    return 1.0f;
}

std::string InstanceManager::getSpawnModelId() const
{
    if (spawnPreviewThingIdx_ >= 0) {
        for (const auto& obj : objects_) {
            if (obj.thingIdx == spawnPreviewThingIdx_)
                return obj.modelId;
        }
    }
    return lastPopped_.modelId;
}

std::string InstanceManager::getSpawnItemId() const
{
    if (spawnPreviewThingIdx_ >= 0) {
        for (const auto& obj : objects_) {
            if (obj.thingIdx == spawnPreviewThingIdx_)
                return obj.itemId;
        }
    }
    return lastPopped_.item.id;
}

void InstanceManager::updateThingIdx(int oldIdx, int newIdx)
{
    for (auto& obj : objects_) {
        if (obj.thingIdx == oldIdx) {
            obj.thingIdx = newIdx;
            /* Reset image loaded state so images reload for new thing */
            obj.screenImageRequested = false;
            obj.marqueeImageRequested = false;
            obj.screenImagePath.clear();
            obj.marqueeImagePath.clear();
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Updated thingIdx %d -> %d\n", oldIdx, newIdx);
            return;
        }
    }
}

void InstanceManager::reloadImagesForThing(int thingIdx)
{
    int objIdx = -1;
    for (int i = 0; i < (int)objects_.size(); i++) {
        if (objects_[i].thingIdx == thingIdx) { objIdx = i; break; }
    }
    if (objIdx < 0) return;

    SpawnedObject& obj = objects_[objIdx];
    Arcade::Item item = g_library.getItemById(obj.itemId);
    if (item.id.empty()) return;

    obj.screenImagePath.clear();
    obj.marqueeImagePath.clear();
    requestChannelImage(objIdx, thingIdx, item, true);
    requestChannelImage(objIdx, thingIdx, item, false);
}

void InstanceManager::refreshItemTextures(const std::string& itemId, bool deleteDiskCache)
{
    if (itemId.empty()) return;

    /* Always clear the in-memory pixel cache so the next capture re-reads from disk. */
    for (auto& obj : objects_) {
        if (obj.itemId != itemId) continue;
        if (!obj.screenImagePath.empty())
            g_imageLoader.clearPixelCache(obj.screenImagePath);
        if (!obj.marqueeImagePath.empty())
            g_imageLoader.clearPixelCache(obj.marqueeImagePath);
    }

    /* Optionally nuke every on-disk cache file this item could resolve to, forcing
     * a full re-download. Mirrors image-loader.html's buildCandidateList: four
     * field URLs + their YouTube thumbnail variants + the item snapshot. */
    if (deleteDiskCache) {
        Arcade::Item item = g_library.getItemById(itemId);
        auto wipeUrl = [](const std::string& url) {
            if (url.empty()) return;
            g_imageLoader.deleteCacheForUrl(url, "url");
            std::string ytId = InstanceManager::extractYouTubeVideoId(url);
            if (!ytId.empty())
                g_imageLoader.deleteCacheForUrl(
                    "https://img.youtube.com/vi/" + ytId + "/0.jpg", "url");
        };
        wipeUrl(item.marquee);
        wipeUrl(item.screen);
        wipeUrl(item.preview);
        wipeUrl(item.file);
        if (!item.id.empty()) g_imageLoader.deleteCacheForUrl(item.id, "snapshot");

        /* Also nuke the currently-loaded paths. In the legacy URL flow a capture
         * could have been saved under the snapshot key instead of the URL hash. */
        for (auto& obj : objects_) {
            if (obj.itemId != itemId) continue;
            if (!obj.screenImagePath.empty()) g_imageLoader.deleteCacheFile(obj.screenImagePath);
            if (!obj.marqueeImagePath.empty()) g_imageLoader.deleteCacheFile(obj.marqueeImagePath);
        }
    }

    for (auto& obj : objects_) {
        if (obj.itemId != itemId) continue;
        reloadImagesForThing(obj.thingIdx);
        if (g_host.reset_thing_texture)
            g_host.reset_thing_texture(obj.thingIdx);
    }

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Refreshing textures for item %s%s\n",
                          itemId.c_str(), deleteDiskCache ? " (disk cache wiped)" : "");
}

std::string InstanceManager::getTemplateForThing(int thingIdx) const
{
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            return g_library.findModelPlatformFile(obj.modelId, OPENJK_PLATFORM_ID);
        }
    }
    return "";
}

void InstanceManager::removeSpawnedByThingIdx(int thingIdx)
{
    for (auto it = objects_.begin(); it != objects_.end(); ++it) {
        if (it->thingIdx == thingIdx) {
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Removed spawned object for thingIdx=%d\n", thingIdx);
            objects_.erase(it);
            return;
        }
    }
}

int InstanceManager::importDefaultLibrary()
{
    struct { const char* tmpl; const char* title; } defaults[] = {
        { "aaojk_movie_stand_standard", "Movie Stand Standard" },
        { "aaojk_movie_display_wallmount", "Movie Display Wallmount" },
        { "dyn_videosign", "Video Sign" },
        { "aaojk_wall_pad_w", "Wall Pad W" },
        { "aaojk_pic_wide_l", "Pic Wide L" },
        { "aaojk_pic_tall_l", "Pic Tall L" },
        { "aaojk_big_movie_wallmount", "Big Movie Wallmount" },
        { "aaojk_big_movie_wallmount_no_banner", "Big Movie Wallmount No Banner" },
    };
    int created = 0;
    for (auto& d : defaults) {
        std::string existing = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, d.tmpl);
        if (existing.empty()) {
            g_library.createModel(d.title, OPENJK_PLATFORM_ID, d.tmpl);
            created++;
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Created default model '%s' (%s)\n", d.title, d.tmpl);
        }
    }
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: importDefaultLibrary created %d of %d models\n",
            created, (int)(sizeof(defaults) / sizeof(defaults[0])));
    return created;
}

bool InstanceManager::registerAdoptedTemplate(const char* templateName)
{
    if (!templateName || !templateName[0]) return false;

    std::string tmpl(templateName);
    std::string existing = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, tmpl);
    if (!existing.empty()) return false; /* already in library */

    /* Derive title: strip "aaojk_" prefix, replace '_' with ' ', title-case */
    std::string title = tmpl;
    if (title.find("aaojk_") == 0) title = title.substr(6);
    for (size_t i = 0; i < title.size(); i++) {
        if (title[i] == '_') title[i] = ' ';
    }
    bool capitalize = true;
    for (size_t i = 0; i < title.size(); i++) {
        if (capitalize && title[i] >= 'a' && title[i] <= 'z') title[i] -= 32;
        capitalize = (title[i] == ' ');
    }

    g_library.createModel(title, OPENJK_PLATFORM_ID, tmpl);
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Registered adopted template '%s' as '%s'\n", templateName, title.c_str());
    return true;
}

InstanceManager::ImportResult InstanceManager::importAdoptedTemplates()
{
    ImportResult result = { 0, 0 };

    FILE* f = fopen("resource/jkl/addon-static.jkl", "r");
    if (!f) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: No addon-static.jkl found\n");
        return result;
    }

    // Parse template names from TEMPLATES section
    std::vector<std::string> templateNames;
    char line[512];
    bool inTemplates = false;

    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (trimmed[0] == '\0') continue;

        if (strstr(trimmed, "SECTION: TEMPLATES") || strstr(trimmed, "section: templates")) {
            inTemplates = true;
            // Skip the "world templates N" header line
            if (fgets(line, sizeof(line), f)) {}
            continue;
        }

        if (inTemplates) {
            if (strncmp(trimmed, "end", 3) == 0 &&
                (trimmed[3] == '\0' || trimmed[3] == ' ' || trimmed[3] == '\t' || trimmed[3] == '\r')) {
                break;
            }
            // First token is the template name
            char tmplName[128];
            if (sscanf(trimmed, "%127s", tmplName) == 1) {
                templateNames.push_back(tmplName);
            }
        }
    }
    fclose(f);

    result.total = (int)templateNames.size();

    for (const auto& tmpl : templateNames) {
        std::string existing = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, tmpl);
        if (existing.empty()) {
            // Derive title: strip "aaojk_" prefix, replace '_' with ' ', title-case
            std::string title = tmpl;
            if (title.find("aaojk_") == 0) title = title.substr(6);
            for (size_t i = 0; i < title.size(); i++) {
                if (title[i] == '_') title[i] = ' ';
            }
            // Title-case each word
            bool capitalize = true;
            for (size_t i = 0; i < title.size(); i++) {
                if (capitalize && title[i] >= 'a' && title[i] <= 'z') {
                    title[i] -= 32;
                }
                capitalize = (title[i] == ' ');
            }

            g_library.createModel(title, OPENJK_PLATFORM_ID, tmpl);
            result.created++;
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Created adopted model '%s' (%s)\n", title.c_str(), tmpl.c_str());
        }
    }

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: importAdoptedTemplates created %d of %d models\n",
            result.created, result.total);
    return result;
}

std::string InstanceManager::mergeLibrary(const std::string& sourcePath, const std::string& strategy)
{
    return g_library.mergeFrom(sourcePath, strategy);
}

void InstanceManager::requestSpawnModelChange(const std::string& modelId)
{
    /* Look up the platform file (template name) for this model */
    std::string tmpl = g_library.findModelPlatformFile(modelId, OPENJK_PLATFORM_ID);
    if (tmpl.empty()) {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: No OpenJK template for model %s\n", modelId.c_str());
        return;
    }
    /* Update lastPopped_ so confirmSpawn uses the correct model */
    lastPopped_.modelId = modelId;
    lastPopped_.templateName = tmpl;
    pendingModelChanges_.push(tmpl);

    /* Also update the actual SpawnedObject in objects_ so reportThingTransform saves the new model */
    int previewIdx = spawnPreviewThingIdx_;
    for (auto& obj : objects_) {
        if (obj.thingIdx == previewIdx) {
            obj.modelId = modelId;
            break;
        }
    }
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Queued model change to template '%s' (model=%s)\n",
            tmpl.c_str(), modelId.c_str());
}

bool InstanceManager::hasPendingModelChange() const { return !pendingModelChanges_.empty(); }

std::string InstanceManager::popPendingModelChange()
{
    std::string tmpl = pendingModelChanges_.front();
    pendingModelChanges_.pop();
    return tmpl;
}

const SpawnedObject* InstanceManager::getSelectedObject() const
{
    return getSpawned(selectedObjectIndex_);
}

const EmbeddedItemInstance* InstanceManager::getSelectedItemInstance() const
{
    const SpawnedObject* obj = getSelectedObject();
    if (!obj) return nullptr;
    return getItemInstance(obj->itemId);
}

int InstanceManager::getSelectedThingIdx() const
{
    const SpawnedObject* obj = getSelectedObject();
    return obj ? obj->thingIdx : -1;
}

/* --- Queries --- */

const SpawnedObject* InstanceManager::getSpawned(int index) const
{
    if (index < 0 || index >= (int)objects_.size()) return nullptr;
    return &objects_[index];
}

int InstanceManager::getActiveInstanceCount() const
{
    int count = 0;
    for (auto& pair : itemInstances_) {
        if (pair.second.active) count++;
    }
    return count;
}

const EmbeddedItemInstance* InstanceManager::getItemInstance(const std::string& itemId) const
{
    auto it = itemInstances_.find(itemId);
    if (it == itemInstances_.end()) return nullptr;
    return &it->second;
}

void InstanceManager::updateTitles()
{
    for (auto& pair : itemInstances_) {
        EmbeddedItemInstance& inst = pair.second;
        if (inst.active && inst.browser && inst.browser->vtable->get_title) {
            const char* t = inst.browser->vtable->get_title(inst.browser);
            inst.title = t ? t : "";
        }
    }
}

void InstanceManager::reportThingTransform(int thingIdx, float px, float py, float pz, int sectorId, float rx, float ry, float rz)
{
    if (currentInstanceId_.empty()) return;

    for (auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            /* Save to database */
            char posBuf[128], rotBuf[128];
            snprintf(posBuf, sizeof(posBuf), "%.10f %.10f %.10f %d", px, py, pz, sectorId);
            snprintf(rotBuf, sizeof(rotBuf), "%.10f %.10f %.10f", rx, ry, rz);

            Arcade::InstanceObject dbObj;
            dbObj.instance_id = currentInstanceId_;
            dbObj.object_key = obj.objectKey;
            dbObj.item = obj.itemId;
            dbObj.model = obj.modelId;
            dbObj.position = posBuf;
            dbObj.rotation = rotBuf;
            dbObj.scale = obj.scale;
            dbObj.slave = obj.slave ? 1 : 0;

            g_library.saveInstanceObject(dbObj);

            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Saved object %s (thingIdx=%d) to instance %s\n",
                                  obj.objectKey.c_str(), thingIdx, currentInstanceId_.c_str());
            return;
        }
    }
}

void InstanceManager::deactivateInstance(const std::string& itemId)
{
    auto it = itemInstances_.find(itemId);
    if (it == itemInstances_.end()) return;

    EmbeddedItemInstance& inst = it->second;
    if (!inst.active) return;

    inst.active = false;

    /* If this browser was the input/fullscreen target, clear and unload overlay */
    if (inst.browser) {
        if (aarcadecore_getInputModeInstance() == inst.browser)
            aarcadecore_setInputModeInstance(NULL);
        if (aarcadecore_getFullscreenInstance() == inst.browser)
            aarcadecore_setFullscreenInstance(NULL);
        if (!aarcadecore_getInputModeInstance() && !aarcadecore_getFullscreenInstance()) {
            UltralightManager_UnloadOverlay();
            aarcadecore_clearOverlayAssociation();
        }
    }

    /* Capture snapshot of last rendered frame before removing task */
    if (inst.taskIndex >= 0 && inst.browser && inst.browser->vtable->render) {
        /* Use itemId as the snapshot key — matches getBestScreenUrl lookup */
        /* Only save if no snapshot exists yet for this item */
        std::string snapshotKey = itemId;
        if (!snapshotKey.empty() && g_imageLoader.isInitialized()
            && g_imageLoader.getSnapshotPath(snapshotKey).empty()) {

            if (inst.browser->type == EMBEDDED_VIDEO_PLAYER) {
                /* Video player: crop the front buffer to the video's true
                 * aspect ratio (strips the letterbox mpv added for the
                 * square texture). */
                unsigned char* snapBuf = nullptr;
                int snapW = 0, snapH = 0;
                if (VideoPlayerInstance_CaptureSnapshot(inst.browser, &snapBuf, &snapW, &snapH)) {
                    g_imageLoader.saveSnapshot(snapshotKey, snapBuf, snapW, snapH);
                    free(snapBuf);
                }
            } else if (inst.browser->type == EMBEDDED_STEAMWORKS_BROWSER) {
                /* Web browser: the internal pixelBuffer is already at the
                 * browser's native aspect — resize to max 512 longest side. */
                unsigned char* snapBuf = nullptr;
                int snapW = 0, snapH = 0;
                if (SteamworksWebBrowserInstance_CaptureSnapshot(inst.browser, &snapBuf, &snapW, &snapH)) {
                    g_imageLoader.saveSnapshot(snapshotKey, snapBuf, snapW, snapH);
                    free(snapBuf);
                }
            } else if (inst.browser->type == EMBEDDED_LIBRETRO) {
                /* Libretro: snapshot from the core's native frame at the
                 * reported pixel dimensions (no PAR correction). */
                unsigned char* snapBuf = nullptr;
                int snapW = 0, snapH = 0;
                if (LibretroInstance_CaptureSnapshot(inst.browser, &snapBuf, &snapW, &snapH)) {
                    g_imageLoader.saveSnapshot(snapshotKey, snapBuf, snapW, snapH);
                    free(snapBuf);
                }
            } else {
                const int snapW = 512, snapH = 512;
                uint8_t* snapBuf = (uint8_t*)malloc(snapW * snapH * 4);
                if (snapBuf) {
                    inst.browser->vtable->render(inst.browser, snapBuf, snapW, snapH, 0, 32);
                    g_imageLoader.saveSnapshot(snapshotKey, snapBuf, snapW, snapH);
                    free(snapBuf);
                }
            }
        }
    }

    /* Remove from task list, then destroy the browser */
    if (inst.taskIndex >= 0) {
        aarcadecore_removeTask(inst.taskIndex);
    }
    if (inst.browser) {
        if (inst.browser->type == EMBEDDED_LIBRETRO) {
            LibretroInstance_Destroy(inst.browser);
        } else if (inst.browser->type == EMBEDDED_VIDEO_PLAYER) {
            VideoPlayerInstance_Destroy(inst.browser);
        } else {
            SteamworksWebBrowserInstance_Destroy(inst.browser);
        }
        inst.browser = nullptr;
    }

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Deactivated instance for item=%s\n", itemId.c_str());

    inst.taskIndex = -1;

    /* Clear selection if the deactivated item was the selected object's item */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        if (objects_[selectedObjectIndex_].itemId == itemId)
            selectedObjectIndex_ = -1;
    }
}

void InstanceManager::deselectOnly()
{
    if (selectedObjectIndex_ < 0) return;

    if (aarcadecore_getFullscreenInstance())
        aarcadecore_setFullscreenInstance(NULL);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: deselectOnly — deselected object #%d (instance kept alive)\n",
                          selectedObjectIndex_);
    selectedObjectIndex_ = -1;
}

void InstanceManager::rememberObject(int thingIdx)
{
    /* Redirect slave objects to their master */
    thingIdx = resolveMasterThingIdx(thingIdx);

    /* Find the object */
    int index = -1;
    for (int i = 0; i < (int)objects_.size(); i++) {
        if (objects_[i].thingIdx == thingIdx) {
            index = i;
            break;
        }
    }
    if (index < 0) return;

    const SpawnedObject& obj = objects_[index];

    /* Always update remembered item for mirror priority */
    rememberedItemId_ = obj.itemId;

    /* Check if it already has an active instance */
    const EmbeddedItemInstance* existing = getItemInstance(obj.itemId);
    if (existing && existing->active) return; /* already running */

    /* Look up the item and activate its instance */
    Arcade::Item item = g_library.getItemById(obj.itemId);
    if (item.id.empty()) return;

    std::string resolvedUrl = resolveUrl(item.file, !item.preview.empty() ? item.preview : item.screen, item.title);
    ensureItemInstance(item, resolvedUrl);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: rememberObject — activated instance for thingIdx=%d (item=%s) without selecting\n",
                          thingIdx, obj.itemId.c_str());
}

EmbeddedInstance* InstanceManager::getInputTarget() const
{
    /* Helper: find browser for a thingIdx, resolving slave→master */
    auto findBrowserForThing = [this](int thingIdx) -> EmbeddedInstance* {
        int resolved = resolveMasterThingIdx(thingIdx);
        for (const auto& obj : objects_) {
            if (obj.thingIdx == resolved) {
                auto it = itemInstances_.find(obj.itemId);
                if (it != itemInstances_.end() && it->second.active && it->second.browser)
                    return it->second.browser;
                break;
            }
        }
        return nullptr;
    };

    /* Priority 1: selected object's active instance (with slave resolve) */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        EmbeddedInstance* b = findBrowserForThing(objects_[selectedObjectIndex_].thingIdx);
        if (b) return b;
    }
    /* Priority 2: aimed-at object's active instance (with slave resolve) */
    if (aimedThingIdx_ >= 0) {
        EmbeddedInstance* b = findBrowserForThing(aimedThingIdx_);
        if (b) return b;
    }
    return nullptr;
}

const EmbeddedItemInstance* InstanceManager::getInstanceForBrowser(EmbeddedInstance* browser) const
{
    if (!browser) return nullptr;
    for (const auto& pair : itemInstances_) {
        if (pair.second.browser == browser)
            return &pair.second;
    }
    return nullptr;
}

std::vector<const EmbeddedItemInstance*> InstanceManager::getActiveInstances() const
{
    std::vector<const EmbeddedItemInstance*> result;
    for (auto& pair : itemInstances_) {
        if (pair.second.active)
            result.push_back(&pair.second);
    }
    return result;
}

std::string InstanceManager::getScreenImagePath(int thingIdx) const
{
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx)
            return obj.screenImagePath;
    }
    return "";
}

std::string InstanceManager::getMarqueeImagePath(int thingIdx) const
{
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx)
            return obj.marqueeImagePath;
    }
    return "";
}

int InstanceManager::getScreenImageStatus(int thingIdx) const
{
    for (const auto& obj : objects_) {
        if (obj.thingIdx != thingIdx) continue;
        if (!obj.screenImagePath.empty()) return 1;
        if (obj.screenImageRequested) return 0;
        return -1;
    }
    return 0;
}

int InstanceManager::getMarqueeImageStatus(int thingIdx) const
{
    for (const auto& obj : objects_) {
        if (obj.thingIdx != thingIdx) continue;
        if (!obj.marqueeImagePath.empty()) return 1;
        if (obj.marqueeImageRequested) return 0;
        return -1;
    }
    return 0;
}

int InstanceManager::resolveMasterThingIdx(int thingIdx) const
{
    /* Find the object */
    const SpawnedObject* obj = nullptr;
    for (const auto& o : objects_) {
        if (o.thingIdx == thingIdx) { obj = &o; break; }
    }
    if (!obj || !obj->slave) return thingIdx;

    /* Slave with own active instance — no redirect */
    const EmbeddedItemInstance* ownInst = getItemInstance(obj->itemId);
    if (ownInst && ownInst->active) return thingIdx;

    /* Find the master: same priority as getSlaveTaskIndex */
    auto findThingForItem = [this](const std::string& itemId) -> int {
        for (const auto& o : objects_) {
            if (o.itemId == itemId) return o.thingIdx;
        }
        return -1;
    };

    /* Priority 1: selected object */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        const SpawnedObject& sel = objects_[selectedObjectIndex_];
        const EmbeddedItemInstance* inst = getItemInstance(sel.itemId);
        if (inst && inst->active) return sel.thingIdx;
    }

    /* Priority 2: remembered instance */
    if (!rememberedItemId_.empty()) {
        const EmbeddedItemInstance* inst = getItemInstance(rememberedItemId_);
        if (inst && inst->active) {
            int t = findThingForItem(rememberedItemId_);
            if (t >= 0) return t;
        }
    }

    /* Priority 3: first active instance */
    for (const auto& pair : itemInstances_) {
        if (pair.second.active) {
            int t = findThingForItem(pair.first);
            if (t >= 0) return t;
        }
    }

    return thingIdx;
}

bool InstanceManager::toggleSlave(int thingIdx)
{
    for (auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            obj.slave = !obj.slave;
            if (!currentInstanceId_.empty())
                g_library.updateInstanceObjectSlave(currentInstanceId_, obj.objectKey, obj.slave ? 1 : 0);
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: toggleSlave thingIdx=%d → slave=%d\n", thingIdx, obj.slave ? 1 : 0);
            return obj.slave;
        }
    }
    return false;
}

int InstanceManager::getSlaveTaskIndex() const
{
    /* Priority 1: selected object's instance */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        const SpawnedObject& sel = objects_[selectedObjectIndex_];
        const EmbeddedItemInstance* inst = getItemInstance(sel.itemId);
        if (inst && inst->active && inst->taskIndex >= 0) return inst->taskIndex;
    }

    /* Priority 2: remembered instance */
    if (!rememberedItemId_.empty()) {
        const EmbeddedItemInstance* inst = getItemInstance(rememberedItemId_);
        if (inst && inst->active && inst->taskIndex >= 0) return inst->taskIndex;
    }

    /* Priority 3: first active instance */
    for (const auto& pair : itemInstances_) {
        if (pair.second.active && pair.second.taskIndex >= 0)
            return pair.second.taskIndex;
    }

    return -1;
}

int InstanceManager::getTaskIndexForThing(int thingIdx) const
{
    /* Find the object with this thingIdx */
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            /* Check own item instance first (highest priority even for slaves) */
            const EmbeddedItemInstance* inst = getItemInstance(obj.itemId);
            if (inst && inst->active) return inst->taskIndex;

            /* If slave and no own instance, mirror another */
            if (obj.slave) return getSlaveTaskIndex();

            return -1;
        }
    }
    return -1;
}
