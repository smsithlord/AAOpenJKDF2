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

    int rc = sqlite3_open_v2(dbPath, &db_, SQLITE_OPEN_READWRITE, nullptr);
    if (rc != SQLITE_OK) {
        if (g_host.host_printf)
            g_host.host_printf("SQLiteLibrary: Failed to open '%s': %s\n", dbPath, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    /* WAL mode + relaxed sync for faster writes (safe against app crashes, not OS crashes) */
    execSQL("PRAGMA journal_mode=WAL");
    execSQL("PRAGMA synchronous=NORMAL");

    if (g_host.host_printf)
        g_host.host_printf("SQLiteLibrary: Opened '%s' (WAL mode)\n", dbPath);
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

Arcade::Item SQLiteLibrary::getItemById(const std::string& id)
{
    Arcade::Item item;
    if (!db_) return item;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, app, description, file, marquee, preview, screen, title, type FROM items WHERE id = ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return item;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.id          = getStr(stmt, 0);
        item.app         = getStr(stmt, 1);
        item.description = getStr(stmt, 2);
        item.file        = getStr(stmt, 3);
        item.marquee     = getStr(stmt, 4);
        item.preview     = getStr(stmt, 5);
        item.screen      = getStr(stmt, 6);
        item.title       = getStr(stmt, 7);
        item.type        = getStr(stmt, 8);
    }
    sqlite3_finalize(stmt);
    return item;
}

std::vector<Arcade::Item> SQLiteLibrary::getItems(int offset, int limit)
{
    std::vector<Arcade::Item> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, app, description, file, marquee, preview, screen, title, type FROM items ORDER BY id LIMIT ? OFFSET ?";
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
        item.preview     = getStr(stmt, 5);
        item.screen      = getStr(stmt, 6);
        item.title       = getStr(stmt, 7);
        item.type        = getStr(stmt, 8);
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
    const char* sql = "SELECT id, app, description, file, marquee, preview, screen, title, type FROM items WHERE title LIKE ? LIMIT ?";
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
        item.preview     = getStr(stmt, 5);
        item.screen      = getStr(stmt, 6);
        item.title       = getStr(stmt, 7);
        item.type        = getStr(stmt, 8);
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

// --- Schema migration + Instance/Map management ---

void SQLiteLibrary::execSQL(const char* sql)
{
    if (!db_) return;
    char* err = nullptr;
    sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

void SQLiteLibrary::ensureSchema()
{
    if (!db_) return;

    /* Ensure platform exists */
    execSQL("INSERT OR IGNORE INTO platforms (id, title) VALUES ('" OPENJK_PLATFORM_ID "', '" OPENJK_PLATFORM_TITLE "')");

    /* Ensure instances table has title and map columns (ALTER TABLE is safe to call if already exists — will just fail silently) */
    execSQL("ALTER TABLE instances ADD COLUMN title TEXT");
    execSQL("ALTER TABLE instances ADD COLUMN map TEXT");

    /* Ensure maps table exists */
    execSQL("CREATE TABLE IF NOT EXISTS maps (id TEXT PRIMARY KEY, title TEXT)");
    execSQL("CREATE INDEX IF NOT EXISTS idx_maps_title ON maps(title)");
    execSQL("CREATE TABLE IF NOT EXISTS map_platforms (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "map_id TEXT NOT NULL, platform_key TEXT NOT NULL, file TEXT, "
            "FOREIGN KEY (map_id) REFERENCES maps(id) ON DELETE CASCADE, "
            "UNIQUE (map_id, platform_key))");

    /* Model platform files — maps model IDs to platform-specific template names */
    execSQL("CREATE TABLE IF NOT EXISTS model_platforms (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "model_id TEXT NOT NULL, platform_key TEXT NOT NULL, file TEXT, "
            "UNIQUE (model_id, platform_key))");

    /* Ensure instance_objects table exists */
    execSQL("CREATE TABLE IF NOT EXISTS instance_objects (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "instance_id TEXT NOT NULL, object_key TEXT NOT NULL, "
            "anim TEXT, body INTEGER, child INTEGER, item TEXT, model TEXT, "
            "position TEXT, rotation TEXT, scale REAL, skin INTEGER, slave INTEGER, "
            "FOREIGN KEY (instance_id) REFERENCES instances(id) ON DELETE CASCADE, "
            "UNIQUE (instance_id, object_key))");
}

std::string SQLiteLibrary::findMapByPlatformFile(const std::string& platformKey, const std::string& file)
{
    if (!db_) return "";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT map_id FROM map_platforms WHERE platform_key = ? AND file = ? LIMIT 1";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (g_host.host_printf)
            g_host.host_printf("SQLiteLibrary: findMapByPlatformFile prepare failed: %s\n", sqlite3_errmsg(db_));
        return "";
    }
    sqlite3_bind_text(stmt, 1, platformKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.c_str(), -1, SQLITE_TRANSIENT);
    std::string result;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        result = getStr(stmt, 0);
    if (g_host.host_printf)
        g_host.host_printf("SQLiteLibrary: findMapByPlatformFile('%s', '%s') → '%s' (rc=%d)\n",
                          platformKey.c_str(), file.c_str(), result.c_str(), rc);
    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteLibrary::createMap(const std::string& title, const std::string& platformKey, const std::string& file)
{
    if (!db_) return "";
    std::string mapId = Arcade::generateFirebasePushId();

    sqlite3_stmt* stmt = nullptr;
    const char* sql1 = "INSERT INTO maps (id, title) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db_, sql1, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mapId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* sql2 = "INSERT INTO map_platforms (map_id, platform_key, file) VALUES (?, ?, ?)";
    if (sqlite3_prepare_v2(db_, sql2, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mapId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, platformKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return mapId;
}

std::string SQLiteLibrary::findInstanceByMap(const std::string& mapId)
{
    if (!db_) return "";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id FROM instances WHERE map = ? LIMIT 1";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (g_host.host_printf)
            g_host.host_printf("SQLiteLibrary: findInstanceByMap prepare failed: %s\n", sqlite3_errmsg(db_));
        return "";
    }
    sqlite3_bind_text(stmt, 1, mapId.c_str(), -1, SQLITE_TRANSIENT);
    std::string result;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        result = getStr(stmt, 0);
    if (g_host.host_printf)
        g_host.host_printf("SQLiteLibrary: findInstanceByMap('%s') → '%s' (rc=%d)\n",
                          mapId.c_str(), result.c_str(), rc);
    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteLibrary::createInstance(const std::string& title, const std::string& mapId)
{
    if (!db_) return "";
    std::string instId = Arcade::generateFirebasePushId();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO instances (id, title, map) VALUES (?, ?, ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, instId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, mapId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return instId;
}

std::vector<Arcade::InstanceObject> SQLiteLibrary::getInstanceObjects(const std::string& instanceId)
{
    std::vector<Arcade::InstanceObject> results;
    if (!db_) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT instance_id, object_key, item, model, position, rotation, scale FROM instance_objects WHERE instance_id = ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;
    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Arcade::InstanceObject obj;
        obj.instance_id = getStr(stmt, 0);
        obj.object_key  = getStr(stmt, 1);
        obj.item        = getStr(stmt, 2);
        obj.model       = getStr(stmt, 3);
        obj.position    = getStr(stmt, 4);
        obj.rotation    = getStr(stmt, 5);
        obj.scale       = (float)sqlite3_column_double(stmt, 6);
        results.push_back(std::move(obj));
    }
    sqlite3_finalize(stmt);
    return results;
}

void SQLiteLibrary::saveInstanceObject(const Arcade::InstanceObject& obj)
{
    if (!db_) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO instance_objects (instance_id, object_key, item, model, position, rotation, scale) VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, obj.instance_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, obj.object_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, obj.item.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, obj.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, obj.position.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, obj.rotation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, obj.scale);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SQLiteLibrary::deleteInstanceObject(const std::string& instanceId, const std::string& objectKey)
{
    if (!db_) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM instance_objects WHERE instance_id = ? AND object_key = ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, objectKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string SQLiteLibrary::findModelPlatformFile(const std::string& modelId, const std::string& platformKey)
{
    if (!db_ || modelId.empty()) return "";

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT file FROM model_platforms WHERE model_id = ? AND platform_key = ? LIMIT 1";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, platformKey.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = getStr(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteLibrary::findModelByPlatformFile(const std::string& platformKey, const std::string& file)
{
    if (!db_) return "";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT model_id FROM model_platforms WHERE platform_key = ? AND file = ? LIMIT 1";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, platformKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = getStr(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteLibrary::createModel(const std::string& title, const std::string& platformKey, const std::string& file)
{
    if (!db_) return "";
    std::string modelId = Arcade::generateFirebasePushId();

    sqlite3_stmt* stmt = nullptr;
    const char* sql1 = "INSERT INTO models (id, title) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db_, sql1, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, modelId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* sql2 = "INSERT INTO model_platforms (model_id, platform_key, file) VALUES (?, ?, ?)";
    if (sqlite3_prepare_v2(db_, sql2, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, modelId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, platformKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (g_host.host_printf)
        g_host.host_printf("SQLiteLibrary: Created model '%s' (id=%s) with platform file '%s'\n",
                          title.c_str(), modelId.c_str(), file.c_str());
    return modelId;
}
