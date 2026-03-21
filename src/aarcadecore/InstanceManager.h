#ifndef INSTANCE_MANAGER_H
#define INSTANCE_MANAGER_H

#include <string>
#include <vector>
#include <queue>
#include <map>

struct SpawnRequest {
    std::string itemId;
    std::string resolvedUrl;
};

/* Per-item embedded instance — shared by all objects using the same item */
struct EmbeddedItemInstance {
    std::string itemId;
    std::string url;
    bool active;       /* stays active once activated, even when deselected */
    // Future: EmbeddedInstance* browser; // Steamworks/Ultralight task
};

/* A spawned object in the game world */
struct SpawnedObject {
    std::string itemId;   /* links to EmbeddedItemInstance */
    std::string url;
    int thingIdx;         /* engine sithThing index */
};

class InstanceManager {
public:
    InstanceManager() : selectedObjectIndex_(-1) {}

    /* Spawn pipeline */
    void requestSpawn(const std::string& itemId, const std::string& fileUrl, const std::string& previewUrl);
    bool hasPendingSpawn() const;
    SpawnRequest popPendingSpawn();
    void confirmSpawn(int thingIdx);

    /* Selection */
    void selectObject(int index);
    int getSelectedObjectIndex() const { return selectedObjectIndex_; }
    const SpawnedObject* getSelectedObject() const;
    const EmbeddedItemInstance* getSelectedItemInstance() const;
    int getSelectedThingIdx() const;

    /* Queries */
    int getSpawnedCount() const { return (int)objects_.size(); }
    const SpawnedObject* getSpawned(int index) const;
    int getActiveInstanceCount() const;
    const EmbeddedItemInstance* getItemInstance(const std::string& itemId) const;

    /* URL resolution */
    static std::string resolveUrl(const std::string& fileUrl, const std::string& previewUrl);
    static std::string extractYouTubeVideoId(const std::string& url);

private:
    std::vector<SpawnedObject> objects_;
    std::map<std::string, EmbeddedItemInstance> itemInstances_; /* itemId → per-item instance */
    int selectedObjectIndex_;

    std::queue<SpawnRequest> pendingSpawns_;
    SpawnRequest lastPopped_;

    void ensureItemInstance(const std::string& itemId, const std::string& url);
};

#endif
