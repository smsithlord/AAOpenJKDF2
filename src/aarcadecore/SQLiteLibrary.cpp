#include "SQLiteLibrary.h"
#include "sqlite3.h"
#include "aarcadecore_internal.h"

std::string SQLiteLibrary::getStr(sqlite3_stmt* stmt, int col)
{
    const char* text = (const char*)sqlite3_column_text(stmt, col);
    return text ? text : "";
}

bool SQLiteLibrary::open(const char* dbPath)
{
    if (db_) close();

    int rc = sqlite3_open_v2(dbPath, &db_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        if (g_host.host_printf)
            g_host.host_printf("SQLiteLibrary: Failed to open '%s': %s\n", dbPath, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    if (g_host.host_printf)
        g_host.host_printf("SQLiteLibrary: Opened '%s'\n", dbPath);
    return true;
}

void SQLiteLibrary::close()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// --- Items ---

std::vector<Arcade::Item> SQLiteLibrary::getItems(int offset, int limit)
{
    std::vector<Arcade::Item> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, app, description, file, marquee, screen, title, type FROM items ORDER BY id LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Item item;
        item.id          = getStr(stmt, 0);
        item.app         = getStr(stmt, 1);
        item.description = getStr(stmt, 2);
        item.file        = getStr(stmt, 3);
        item.marquee     = getStr(stmt, 4);
        item.screen      = getStr(stmt, 5);
        item.title       = getStr(stmt, 6);
        item.type        = getStr(stmt, 7);
        results.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Arcade::Item> SQLiteLibrary::searchItems(const std::string& query, int limit)
{
    std::vector<Arcade::Item> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, app, description, file, marquee, screen, title, type FROM items WHERE title LIKE ? LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Item item;
        item.id          = getStr(stmt, 0);
        item.app         = getStr(stmt, 1);
        item.description = getStr(stmt, 2);
        item.file        = getStr(stmt, 3);
        item.marquee     = getStr(stmt, 4);
        item.screen      = getStr(stmt, 5);
        item.title       = getStr(stmt, 6);
        item.type        = getStr(stmt, 7);
        results.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Types ---

std::vector<Arcade::Type> SQLiteLibrary::getTypes()
{
    std::vector<Arcade::Type> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, priority FROM types ORDER BY priority";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Type t;
        t.id       = getStr(stmt, 0);
        t.title    = getStr(stmt, 1);
        t.priority = sqlite3_column_int(stmt, 2);
        results.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Models ---

std::vector<Arcade::Model> SQLiteLibrary::getModels(int offset, int limit)
{
    std::vector<Arcade::Model> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, screen FROM models ORDER BY id LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Model m;
        m.id     = getStr(stmt, 0);
        m.title  = getStr(stmt, 1);
        m.screen = getStr(stmt, 2);
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Arcade::Model> SQLiteLibrary::searchModels(const std::string& query, int limit)
{
    std::vector<Arcade::Model> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, screen FROM models WHERE title LIKE ? LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Model m;
        m.id     = getStr(stmt, 0);
        m.title  = getStr(stmt, 1);
        m.screen = getStr(stmt, 2);
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Apps ---

std::vector<Arcade::App> SQLiteLibrary::getApps(int offset, int limit)
{
    std::vector<Arcade::App> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, type, screen FROM apps ORDER BY id LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::App a;
        a.id     = getStr(stmt, 0);
        a.title  = getStr(stmt, 1);
        a.type   = getStr(stmt, 2);
        a.screen = getStr(stmt, 3);
        results.push_back(std::move(a));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Arcade::App> SQLiteLibrary::searchApps(const std::string& query, int limit)
{
    std::vector<Arcade::App> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, type, screen FROM apps WHERE title LIKE ? LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::App a;
        a.id     = getStr(stmt, 0);
        a.title  = getStr(stmt, 1);
        a.type   = getStr(stmt, 2);
        a.screen = getStr(stmt, 3);
        results.push_back(std::move(a));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Maps ---

std::vector<Arcade::Map> SQLiteLibrary::getMaps(int offset, int limit)
{
    std::vector<Arcade::Map> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title FROM maps ORDER BY id LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Map m;
        m.id    = getStr(stmt, 0);
        m.title = getStr(stmt, 1);
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Arcade::Map> SQLiteLibrary::searchMaps(const std::string& query, int limit)
{
    std::vector<Arcade::Map> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title FROM maps WHERE title LIKE ? LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Map m;
        m.id    = getStr(stmt, 0);
        m.title = getStr(stmt, 1);
        results.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Platforms ---

std::vector<Arcade::Platform> SQLiteLibrary::getPlatforms()
{
    std::vector<Arcade::Platform> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title FROM platforms ORDER BY title";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Platform p;
        p.id    = getStr(stmt, 0);
        p.title = getStr(stmt, 1);
        results.push_back(std::move(p));
    }
    sqlite3_finalize(stmt);
    return results;
}

// --- Instances ---

std::vector<Arcade::Instance> SQLiteLibrary::getInstances(int offset, int limit)
{
    std::vector<Arcade::Instance> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id FROM instances ORDER BY id LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Instance inst;
        inst.id = getStr(stmt, 0);
        results.push_back(std::move(inst));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Arcade::Instance> SQLiteLibrary::searchInstances(const std::string& query, int limit)
{
    std::vector<Arcade::Instance> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id FROM instances WHERE id LIKE ? LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::Instance inst;
        inst.id = getStr(stmt, 0);
        results.push_back(std::move(inst));
    }
    sqlite3_finalize(stmt);
    return results;
}
