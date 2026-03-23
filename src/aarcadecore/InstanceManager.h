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
    std::string modelId;      /* model ID from media library */
    std::string templateName; /* engine template (default: aaojk_movie_stand_standard) */
    bool hasExplicitPosition;
    float posX, posY, posZ;
    int sectorId;
    float rotX, rotY, rotZ;
    float scale;
    std::string objectKey; /* for restored objects */
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
    std::string itemId;       /* links to EmbeddedItemInstance */
    std::string modelId;      /* model ID from media library */
    std::string objectKey;    /* Firebase push ID for DB storage */
    std::string url;
    float scale;              /* uniform scale factor (1.0 = normal) */
    int thingIdx;             /* engine sithThing index */
    std::string screenImagePath;
    std::string marqueeImagePath;
    bool screenImageRequested;
    bool marqueeImageRequested;
};

class InstanceManager {
public:
    InstanceManager() : selectedObjectIndex_(-1), aimedThingIdx_(-1),
        spawnPreviewThingIdx_(-1),
        spawnPitch_(0), spawnYaw_(0), spawnRoll_(0), spawnRotIsWorld_(false),
        spawnOffX_(0), spawnOffY_(0), spawnOffZ_(0), spawnOffIsWorld_(false),
        spawnUseRaycast_(false), spawnScale_(1.0f), spawnTransformSet_(false) {}

    /* Map lifecycle — called by host when maps load/unload */
    void onMapLoaded();
    void onMapUnloaded();
    std::string getCurrentInstanceId() const { return currentInstanceId_; }

    /* Spawn pipeline */
    void requestSpawn(const Arcade::Item& item);
    bool hasPendingSpawn() const;
    SpawnRequest popPendingSpawn();
    void initSpawnedObject(int thingIdx);
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

    /* Report position after spawn (triggers auto-save) */
    void reportThingTransform(int thingIdx, float px, float py, float pz, int sectorId, float rx, float ry, float rz);

    /* Object "used" by player — select and activate its embedded instance */
    void objectUsed(int thingIdx);

    /* Selector ray — update which thing the player is aiming at */
    void setAimedThing(int thingIdx);
    int getAimedThingIdx() const { return aimedThingIdx_; }
    const SpawnedObject* getAimedObject() const;

    /* Destroy an object — cleans up DLL state, queues thingIdx for host to destroy */
    void destroyObject(int thingIdx);
    bool hasPendingDestroy() const;
    int popPendingDestroy();

    /* Move mode — queues a thingIdx for the host to enter move mode */
    void requestMove(int thingIdx);
    bool hasPendingMove() const;
    int popPendingMove();

    /* Spawn transform override (rotation + position offset + scale) */
    void setSpawnTransform(float pitch, float yaw, float roll, bool isWorldRot,
                           float offX, float offY, float offZ, bool isWorldOffset, bool useRaycastOffset,
                           float scale);
    bool hasSpawnTransform() const { return spawnTransformSet_; }
    void getSpawnTransform(float* p, float* y, float* r, bool* isWorldRot,
                           float* ox, float* oy, float* oz, bool* isWorldOff, bool* useRaycast,
                           float* scale) const;

    /* Get uniform scale for a spawned object by thingIdx */
    float getObjectScale(int thingIdx) const;
    void clearSpawnTransform() { spawnTransformSet_ = false; }

    /* Current spawn model ID (for localStorage persistence in JS) */
    void setSpawnPreviewThingIdx(int thingIdx) { spawnPreviewThingIdx_ = thingIdx; }
    std::string getSpawnModelId() const;

    /* Update thingIdx after destroy+recreate (template swap) */
    void updateThingIdx(int oldIdx, int newIdx);

    /* Re-request images for an existing SpawnedObject */
    void reloadImagesForThing(int thingIdx);

    /* Get the template name for a spawned object by thingIdx */
    std::string getTemplateForThing(int thingIdx) const;

    /* Remove a spawned object by thingIdx (for spawn cancel cleanup) */
    void removeSpawnedByThingIdx(int thingIdx);

    /* Import default model entries into the library */
    int importDefaultLibrary();

    /* Merge another library.db into the active library.
     * strategy: "skip" = INSERT OR IGNORE, "overwrite" = INSERT OR REPLACE */
    std::string mergeLibrary(const std::string& sourcePath, const std::string& strategy = "skip");

    /* Spawn mode model change — queues a template name for the host */
    void requestSpawnModelChange(const std::string& modelId);
    bool hasPendingModelChange() const;
    std::string popPendingModelChange();

    /* Deactivate/manage instances */
    void deactivateInstance(const std::string& itemId);
    void deselectOnly();
    void rememberObject(int thingIdx);
    EmbeddedInstance* getInputTarget() const;
    const EmbeddedItemInstance* getInstanceForBrowser(EmbeddedInstance* browser) const;
    std::vector<const EmbeddedItemInstance*> getActiveInstances() const;

    /* URL resolution */
    static std::string resolveUrl(const std::string& fileUrl, const std::string& previewUrl, const std::string& itemTitle = "");
    static std::string extractYouTubeVideoId(const std::string& url);

private:
    std::vector<SpawnedObject> objects_;
    std::map<std::string, EmbeddedItemInstance> itemInstances_; /* itemId → per-item instance */
    int selectedObjectIndex_;
    int aimedThingIdx_;

    std::string currentInstanceId_;
    std::string currentMapId_;

    std::queue<SpawnRequest> pendingSpawns_;
    std::queue<int> pendingDestroys_;
    std::queue<int> pendingMoves_;
    std::queue<std::string> pendingModelChanges_; /* template names for spawn mode */
    int spawnPreviewThingIdx_;
    float spawnPitch_, spawnYaw_, spawnRoll_;
    bool spawnRotIsWorld_;
    float spawnOffX_, spawnOffY_, spawnOffZ_;
    bool spawnOffIsWorld_;
    bool spawnUseRaycast_;
    float spawnScale_;
    bool spawnTransformSet_;
    SpawnRequest lastPopped_;

    void ensureItemInstance(const Arcade::Item& item, const std::string& resolvedUrl);
};

#endif
