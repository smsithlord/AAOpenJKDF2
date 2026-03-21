#ifndef INSTANCE_MANAGER_H
#define INSTANCE_MANAGER_H

#include <string>
#include <vector>
#include <queue>
#include <map>
#include "ArcadeTypes.h"

struct EmbeddedInstance; /* forward decl from aarcadecore_internal.h */

struct SpawnRequest {
    Arcade::Item item;  /* full item data from the library */
};

/* Per-item embedded instance — shared by all objects using the same item */
struct EmbeddedItemInstance {
    std::string itemId;
    std::string url;
    std::string title;          /* page title from browser (updated each frame) */
    bool active;
    EmbeddedInstance* browser;  /* Steamworks web browser task (owned) */
    int taskIndex;              /* index in g_tasks[] for rendering */
};

/* A spawned object in the game world */
struct SpawnedObject {
    std::string itemId;   /* links to EmbeddedItemInstance */
    std::string url;
    int thingIdx;         /* engine sithThing index */
    std::string screenImagePath;  /* cached PNG path (empty = not ready) */
    std::string marqueeImagePath; /* cached PNG path (empty = not ready) */
    bool screenImageRequested;
    bool marqueeImageRequested;
};

class InstanceManager {
public:
    InstanceManager() : selectedObjectIndex_(-1) {}

    /* Spawn pipeline */
    void requestSpawn(const Arcade::Item& item);
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

    /* Task index lookup for per-thing texture rendering */
    int getTaskIndexForThing(int thingIdx) const;

    /* Image paths for thing (empty string = not ready yet) */
    std::string getScreenImagePath(int thingIdx) const;
    std::string getMarqueeImagePath(int thingIdx) const;

    /* Per-frame update — sync titles from browsers */
    void updateTitles();

    /* Deactivate/manage instances */
    void deactivateInstance(const std::string& itemId);
    std::vector<const EmbeddedItemInstance*> getActiveInstances() const;

    /* URL resolution */
    static std::string resolveUrl(const std::string& fileUrl, const std::string& previewUrl, const std::string& itemTitle = "");
    static std::string extractYouTubeVideoId(const std::string& url);

private:
    std::vector<SpawnedObject> objects_;
    std::map<std::string, EmbeddedItemInstance> itemInstances_; /* itemId → per-item instance */
    int selectedObjectIndex_;

    std::queue<SpawnRequest> pendingSpawns_;
    SpawnRequest lastPopped_;

    void ensureItemInstance(const Arcade::Item& item, const std::string& resolvedUrl);
};

#endif
