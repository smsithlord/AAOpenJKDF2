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

    bool open(const char* dbPath);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    /* Schema migration — call after open */
    void ensureSchema();

    // Items
    Arcade::Item getItemById(const std::string& id);
    std::vector<Arcade::Item> getItems(int offset, int limit);
    std::vector<Arcade::Item> searchItems(const std::string& query, int limit);

    // Types
    std::vector<Arcade::Type> getTypes();

    // Models
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
    std::string createMap(const std::string& title, const std::string& platformKey, const std::string& file);
    std::string findInstanceByMap(const std::string& mapId);
    std::string createInstance(const std::string& title, const std::string& mapId);
    std::vector<Arcade::InstanceObject> getInstanceObjects(const std::string& instanceId);
    void saveInstanceObject(const Arcade::InstanceObject& obj);
    void deleteInstanceObject(const std::string& instanceId, const std::string& objectKey);

private:
    sqlite3* db_;
    static std::string getStr(struct sqlite3_stmt* stmt, int col);
    void execSQL(const char* sql);
};

#endif // SQLITE_LIBRARY_H
