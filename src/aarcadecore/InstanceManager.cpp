#include "InstanceManager.h"
#include "aarcadecore_internal.h"
#include <algorithm>
#include <cctype>

/* Forward declarations for Steamworks browser creation */
EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* url, const char* material_name);
void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst);

/* Exposed from aarcadecore.cpp */
int aarcadecore_addTask(EmbeddedInstance* inst);

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

void InstanceManager::ensureItemInstance(const std::string& itemId, const std::string& url)
{
    auto it = itemInstances_.find(itemId);
    if (it != itemInstances_.end()) {
        /* Already exists — ensure active */
        if (!it->second.active) {
            it->second.active = true;
            if (g_host.host_printf)
                g_host.host_printf("InstanceManager: Activated embedded instance for item=%s\n", itemId.c_str());
        }
        return;
    }

    /* Create new embedded item instance with a Steamworks browser */
    EmbeddedItemInstance inst;
    inst.itemId = itemId;
    inst.url = url;
    inst.active = true;
    inst.browser = nullptr;
    inst.taskIndex = -1;

    /* Create and initialize the browser */
    EmbeddedInstance* browser = SteamworksWebBrowserInstance_Create(url.c_str(), "compscreen.mat");
    if (browser && browser->vtable->init(browser)) {
        inst.browser = browser;
        inst.taskIndex = aarcadecore_addTask(browser);
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: Created browser instance for item=%s (taskIndex=%d, url=%s)\n",
                              itemId.c_str(), inst.taskIndex, url.c_str());
    } else {
        if (g_host.host_printf)
            g_host.host_printf("InstanceManager: WARNING: Failed to create browser for item=%s\n", itemId.c_str());
        if (browser) SteamworksWebBrowserInstance_Destroy(browser);
    }

    itemInstances_[itemId] = inst;
}

/* --- Spawn pipeline --- */

void InstanceManager::requestSpawn(const std::string& itemId, const std::string& fileUrl, const std::string& previewUrl, const std::string& itemTitle)
{
    std::string resolved = resolveUrl(fileUrl, previewUrl, itemTitle);

    if (g_host.host_printf)
        g_host.host_printf("InstanceManager: Spawn requested - item=%s url=%s\n",
                          itemId.c_str(), resolved.c_str());

    SpawnRequest req;
    req.itemId = itemId;
    req.resolvedUrl = resolved;
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
    /* Ensure the item has an embedded instance (creates + activates if new) */
    ensureItemInstance(lastPopped_.itemId, lastPopped_.resolvedUrl);

    /* Add spawned object */
    SpawnedObject obj;
    obj.itemId = lastPopped_.itemId;
    obj.url = lastPopped_.resolvedUrl;
    obj.thingIdx = thingIdx;
    objects_.push_back(obj);

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

    /* Shut down and destroy the browser */
    if (inst.browser) {
        if (inst.browser->vtable->shutdown)
            inst.browser->vtable->shutdown(inst.browser);
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
