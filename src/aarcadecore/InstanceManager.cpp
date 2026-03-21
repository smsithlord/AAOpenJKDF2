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

#include "ImageLoader.h"
extern ImageLoader g_imageLoader;

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

/* Get the best image URL from an item (priority: marquee → screen → file, image URLs only) */
static std::string getBestImageUrl(const Arcade::Item& item)
{
    if (isImageUrl(item.marquee)) return item.marquee;
    if (isImageUrl(item.screen)) return item.screen;
    if (isImageUrl(item.file)) return item.file;
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
        /* Already exists — ensure active */
        if (!it->second.active) {
            it->second.active = true;
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Activated embedded instance for item=%s\n", item.id.c_str());
        }
        return;
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

/* --- Spawn pipeline --- */

void InstanceManager::requestSpawn(const Arcade::Item& item)
{
    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Spawn requested - item=%s title=%s\n",
                          item.id.c_str(), item.title.c_str());

    SpawnRequest req;
    req.item = item;
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

void InstanceManager::confirmSpawn(int thingIdx)
{
    const Arcade::Item& item = lastPopped_.item;
    std::string resolvedUrl = resolveUrl(item.file, item.screen, item.title);

    /* Ensure the item has an embedded instance (creates + activates if new) */
    ensureItemInstance(item, resolvedUrl);

    /* Add spawned object */
    SpawnedObject obj;
    obj.itemId = item.id;
    obj.url = resolvedUrl;
    obj.thingIdx = thingIdx;
    obj.screenImageRequested = false;
    objects_.push_back(obj);

    /* Request screen image from ImageLoader — same logic as library.js getBestImage */
    int objIdx = (int)objects_.size() - 1;
    std::string screenUrl = getBestImageUrl(item);

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

    int newIndex = (int)objects_.size() - 1;

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Spawn confirmed - thingIdx=%d item=%s url=%s (total objects: %d)\n",
                          thingIdx, obj.itemId.c_str(), obj.url.c_str(), (int)objects_.size());

    /* Select the newly spawned object (deselects previous) */
    selectObject(newIndex);
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

void InstanceManager::deactivateInstance(const std::string& itemId)
{
    auto it = itemInstances_.find(itemId);
    if (it == itemInstances_.end()) return;

    EmbeddedItemInstance& inst = it->second;
    if (!inst.active) return;

    inst.active = false;

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
