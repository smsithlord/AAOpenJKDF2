#include "InstanceManager.h"
#include "aarcadecore_internal.h"
#include <algorithm>
#include <cctype>

/* Forward declarations for embedded instance creation */
EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* url, const char* material_name);
void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst);
EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name);
void LibretroInstance_Destroy(EmbeddedInstance* inst);

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
extern ImageLoader g_imageLoader;
extern SQLiteLibrary g_library;

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

/* Get the best screen image URL (priority: screen → preview → file → marquee fallback) */
static std::string getBestScreenUrl(const Arcade::Item& item)
{
    if (isImageUrl(item.screen)) return item.screen;
    if (isImageUrl(item.preview)) return item.preview;
    if (isImageUrl(item.file)) return item.file;
    if (isImageUrl(item.marquee)) return item.marquee; /* fallback */
    return "";
}

/* Get the best marquee image URL (priority: marquee → screen → preview → file fallback) */
static std::string getBestMarqueeUrl(const Arcade::Item& item)
{
    if (isImageUrl(item.marquee)) return item.marquee;
    if (isImageUrl(item.screen)) return item.screen;
    if (isImageUrl(item.preview)) return item.preview;
    if (isImageUrl(item.file)) return item.file; /* fallback */
    return "";
}

/* Case-insensitive substring search */
static bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
    std::string h = haystack, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
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

    /* Decide which embedded instance type to create based on item data */
    EmbeddedInstance* task = nullptr;

    if (containsIgnoreCase(item.title, "mario")) {
        /* TEST: Libretro instance for items with "Mario" in the title */
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Title '%s' contains 'Mario' — creating Libretro instance\n", item.title.c_str());
        task = LibretroInstance_Create("bsnes_libretro.dll", "testgame.zip", "DynScreen.mat");
    } else {
        /* Default: Steamworks Web Browser */
        task = SteamworksWebBrowserInstance_Create(resolvedUrl.c_str(), "DynScreen.mat");
    }

    if (task && task->vtable->init(task)) {
        inst.browser = task;
        inst.taskIndex = aarcadecore_addTask(task);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Created %s instance for item=%s (taskIndex=%d)\n",
                              task->type == EMBEDDED_LIBRETRO ? "Libretro" : "SteamworksWebBrowser",
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
            /* Look up the item from the database */
            Arcade::Item item = g_library.getItemById(obj.item);
            if (item.id.empty()) {
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: WARNING: Item '%s' not found, skipping object\n", obj.item.c_str());
                continue;
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
    /* Find or create the default model for OpenJK */
    std::string defaultTemplate = "aaojk_movie_stand_standard";
    req.modelId = g_library.findModelByPlatformFile(OPENJK_PLATFORM_ID, defaultTemplate);
    if (req.modelId.empty())
        req.modelId = g_library.createModel("Movie Stand Standard", OPENJK_PLATFORM_ID, defaultTemplate);
    req.templateName = defaultTemplate;
    req.hasExplicitPosition = false;
    req.posX = req.posY = req.posZ = 0;
    req.sectorId = -1;
    req.rotX = req.rotY = req.rotZ = 0;
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

void InstanceManager::initSpawnedObject(int thingIdx)
{
    const Arcade::Item& item = lastPopped_.item;
    std::string resolvedUrl = resolveUrl(item.file, item.screen, item.title);

    /* Add spawned object */
    SpawnedObject obj;
    obj.itemId = item.id;
    obj.modelId = lastPopped_.modelId;
    obj.objectKey = lastPopped_.objectKey.empty() ? Arcade::generateFirebasePushId() : lastPopped_.objectKey;
    obj.url = resolvedUrl;
    obj.thingIdx = thingIdx;
    obj.screenImageRequested = false;
    obj.marqueeImageRequested = false;
    objects_.push_back(obj);

    /* Request screen image from ImageLoader */
    int objIdx = (int)objects_.size() - 1;
    std::string screenUrl = getBestScreenUrl(item);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Screen image for item=%s: '%s'\n",
                          item.id.c_str(), screenUrl.empty() ? "(none)" : screenUrl.c_str());

    if (!screenUrl.empty() && g_imageLoader.isInitialized()) {
        objects_[objIdx].screenImageRequested = true;
        g_imageLoader.loadAndCacheImage(screenUrl, [this, objIdx, thingIdx](const ImageLoadResult& result) {
            if (result.success && objIdx < (int)objects_.size() && objects_[objIdx].thingIdx == thingIdx) {
                objects_[objIdx].screenImagePath = result.filePath;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: Screen image ready for thingIdx=%d: %s\n",
                                      thingIdx, result.filePath.c_str());
            }
        });
    }

    /* Request marquee image */
    std::string marqueeUrl = getBestMarqueeUrl(item);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Marquee image for item=%s: '%s'\n",
                          item.id.c_str(), marqueeUrl.empty() ? "(none)" : marqueeUrl.c_str());

    if (!marqueeUrl.empty() && g_imageLoader.isInitialized()) {
        objects_[objIdx].marqueeImageRequested = true;
        g_imageLoader.loadAndCacheImage(marqueeUrl, [this, objIdx, thingIdx](const ImageLoadResult& result) {
            if (result.success && objIdx < (int)objects_.size() && objects_[objIdx].thingIdx == thingIdx) {
                objects_[objIdx].marqueeImagePath = result.filePath;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: Marquee image ready for thingIdx=%d: %s\n",
                                      thingIdx, result.filePath.c_str());
            }
        });
    }

    int newIndex = (int)objects_.size() - 1;

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: initSpawnedObject - thingIdx=%d item=%s url=%s (total objects: %d)\n",
                          thingIdx, obj.itemId.c_str(), obj.url.c_str(), (int)objects_.size());
}

void InstanceManager::confirmSpawn(int thingIdx)
{
    /* Called when user actually confirms placement (LMB) — just logs for now.
     * Position saving is handled by report_thing_transform from the host. */
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

    std::string resolvedUrl = resolveUrl(item.file, item.screen, item.title);
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
                                         float offX, float offY, float offZ, bool isWorldOffset, bool useRaycastOffset)
{
    spawnPitch_ = pitch; spawnYaw_ = yaw; spawnRoll_ = roll; spawnRotIsWorld_ = isWorldRot;
    spawnOffX_ = offX; spawnOffY_ = offY; spawnOffZ_ = offZ;
    spawnOffIsWorld_ = isWorldOffset; spawnUseRaycast_ = useRaycastOffset;
    spawnTransformSet_ = true;
}

void InstanceManager::getSpawnTransform(float* p, float* y, float* r, bool* isWorldRot,
                                         float* ox, float* oy, float* oz, bool* isWorldOff, bool* useRaycast) const
{
    if (p) *p = spawnPitch_; if (y) *y = spawnYaw_; if (r) *r = spawnRoll_;
    if (isWorldRot) *isWorldRot = spawnRotIsWorld_;
    if (ox) *ox = spawnOffX_; if (oy) *oy = spawnOffY_; if (oz) *oz = spawnOffZ_;
    if (isWorldOff) *isWorldOff = spawnOffIsWorld_;
    if (useRaycast) *useRaycast = spawnUseRaycast_;
}

std::string InstanceManager::getSpawnModelId() const
{
    /* Look up by current preview thingIdx if available */
    if (spawnPreviewThingIdx_ >= 0) {
        for (const auto& obj : objects_) {
            if (obj.thingIdx == spawnPreviewThingIdx_)
                return obj.modelId;
        }
    }
    return lastPopped_.modelId;
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

    std::string screenUrl = getBestScreenUrl(item);
    if (!screenUrl.empty() && g_imageLoader.isInitialized()) {
        obj.screenImageRequested = true;
        obj.screenImagePath.clear();
        g_imageLoader.loadAndCacheImage(screenUrl, [this, objIdx, thingIdx](const ImageLoadResult& result) {
            if (result.success && objIdx < (int)objects_.size() && objects_[objIdx].thingIdx == thingIdx) {
                objects_[objIdx].screenImagePath = result.filePath;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: Screen image ready for thingIdx=%d: %s\n",
                                      thingIdx, result.filePath.c_str());
            }
        });
    }

    std::string marqueeUrl = getBestMarqueeUrl(item);
    if (!marqueeUrl.empty() && g_imageLoader.isInitialized()) {
        obj.marqueeImageRequested = true;
        obj.marqueeImagePath.clear();
        g_imageLoader.loadAndCacheImage(marqueeUrl, [this, objIdx, thingIdx](const ImageLoadResult& result) {
            if (result.success && objIdx < (int)objects_.size() && objects_[objIdx].thingIdx == thingIdx) {
                objects_[objIdx].marqueeImagePath = result.filePath;
                if (g_host.host_printf)
                    g_host.host_printf("InstanceManager: Marquee image ready for thingIdx=%d: %s\n",
                                      thingIdx, result.filePath.c_str());
            }
        });
    }
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
            dbObj.scale = 1.0f;

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

    /* Remove from task list, then destroy the browser */
    if (inst.taskIndex >= 0) {
        aarcadecore_removeTask(inst.taskIndex);
    }
    if (inst.browser) {
        SteamworksWebBrowserInstance_Destroy(inst.browser);
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

    /* Check if it already has an active instance */
    const EmbeddedItemInstance* existing = getItemInstance(obj.itemId);
    if (existing && existing->active) return; /* already running */

    /* Look up the item and activate its instance */
    Arcade::Item item = g_library.getItemById(obj.itemId);
    if (item.id.empty()) return;

    std::string resolvedUrl = resolveUrl(item.file, item.screen, item.title);
    ensureItemInstance(item, resolvedUrl);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: rememberObject — activated instance for thingIdx=%d (item=%s) without selecting\n",
                          thingIdx, obj.itemId.c_str());
}

EmbeddedInstance* InstanceManager::getInputTarget() const
{
    /* Priority 1: selected object's active instance */
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < (int)objects_.size()) {
        const SpawnedObject& obj = objects_[selectedObjectIndex_];
        auto it = itemInstances_.find(obj.itemId);
        if (it != itemInstances_.end() && it->second.active && it->second.browser)
            return it->second.browser;
    }
    /* Priority 2: aimed-at object's active instance */
    if (aimedThingIdx_ >= 0) {
        for (const auto& obj : objects_) {
            if (obj.thingIdx == aimedThingIdx_) {
                auto it = itemInstances_.find(obj.itemId);
                if (it != itemInstances_.end() && it->second.active && it->second.browser)
                    return it->second.browser;
                break;
            }
        }
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

int InstanceManager::getTaskIndexForThing(int thingIdx) const
{
    /* Find the object with this thingIdx */
    for (const auto& obj : objects_) {
        if (obj.thingIdx == thingIdx) {
            /* Find its item instance */
            const EmbeddedItemInstance* inst = getItemInstance(obj.itemId);
            if (inst && inst->active) return inst->taskIndex;
            return -1;
        }
    }
    return -1;
}
