#ifndef SQLITE_LIBRARY_H
#define SQLITE_LIBRARY_H

#include "ArcadeTypes.h"
#include <vector>
#include <string>

struct sqlite3;

class SQLiteLibrary {
public:
    SQLiteLibrary() : db_(nullptr) {}
    ~SQLiteLibrary() { close(); }

    void setPlatformKey(const std::string& key) { platformKey_ = key; }

    bool open(const char* dbPath);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    /* Schema migration — call after open */
    void ensureSchema();

    // Items (typeFilter: empty string = all types, otherwise filter by type column)
    Arcade::Item getItemById(const std::string& id);
    std::vector<Arcade::Item> getItems(int offset, int limit, const std::string& typeFilter = "");
    std::vector<Arcade::Item> searchItems(const std::string& query, int limit, const std::string& typeFilter = "");

    // Types
    std::vector<Arcade::Type> getTypes();

    // Models
    Arcade::Model getModelById(const std::string& id);
    std::vector<Arcade::Model> getModels(int offset, int limit);
    std::vector<Arcade::Model> searchModels(const std::string& query, int limit);

    // Apps
    std::vector<Arcade::App> getApps(int offset, int limit);
    std::vector<Arcade::App> searchApps(const std::string& query, int limit);

    // Maps
    std::vector<Arcade::Map> getMaps(int offset, int limit);
    std::vector<Arcade::Map> searchMaps(const std::string& query, int limit);

    // Platforms
    std::vector<Arcade::Platform> getPlatforms();

    // Instances
    std::vector<Arcade::Instance> getInstances(int offset, int limit);
    std::vector<Arcade::Instance> searchInstances(const std::string& query, int limit);

    /* Instance/map management for auto-save/restore */
    std::string findMapByPlatformFile(const std::string& platformKey, const std::string& file);
    std::string findModelPlatformFile(const std::string& modelId, const std::string& platformKey);
    std::string findModelByPlatformFile(const std::string& platformKey, const std::string& file);
    std::string createModel(const std::string& title, const std::string& platformKey, const std::string& file);
    std::string createMap(const std::string& title, const std::string& platformKey, const std::string& file);
    std::string findInstanceByMap(const std::string& mapId);
    std::string createInstance(const std::string& title, const std::string& mapId);
    std::vector<Arcade::InstanceObject> getInstanceObjects(const std::string& instanceId);
    void saveInstanceObject(const Arcade::InstanceObject& obj);
    void deleteInstanceObject(const std::string& instanceId, const std::string& objectKey);

    /* Merge another library.db into this one using ATTACH DATABASE.
     * strategy: "skip" = INSERT OR IGNORE, "overwrite" = INSERT OR REPLACE */
    std::string mergeFrom(const std::string& sourceDbPath, const std::string& strategy = "skip");

private:
    sqlite3* db_;
    std::string platformKey_;
    static std::string getStr(struct sqlite3_stmt* stmt, int col);
    void execSQL(const char* sql);
};

#endif // SQLITE_LIBRARY_H
