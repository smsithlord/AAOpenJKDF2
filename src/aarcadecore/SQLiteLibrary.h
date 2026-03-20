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

    // Items
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

private:
    sqlite3* db_;

    // Helper to get a string column (returns "" if NULL)
    static std::string getStr(struct sqlite3_stmt* stmt, int col);
};

#endif // SQLITE_LIBRARY_H
