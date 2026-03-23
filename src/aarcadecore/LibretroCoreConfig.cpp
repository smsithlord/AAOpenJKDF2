#include "LibretroCoreConfig.h"
#include "aarcadecore_internal.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

LibretroCoreConfigManager g_coreConfigMgr;

static const char* CORES_DIR = "aarcadecore/libretro/cores/";
static const char* CONFIG_FILE = "aarcadecore/libretro_cores.json";

/* Minimal JSON helpers — no external dependency */
static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

/* Simple JSON string value extraction */
static std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++; /* skip opening quote */
    size_t end = pos;
    while (end < json.size() && json[end] != '"') {
        if (json[end] == '\\') end++; /* skip escaped char */
        end++;
    }
    return json.substr(pos, end - pos);
}

static bool jsonGetBool(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    return json.find("true", pos) < json.find('\n', pos);
}

static int jsonGetInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    return atoi(json.c_str() + pos);
}

void LibretroCoreConfigManager::scanCores()
{
    std::vector<std::string> dllFiles;

#ifdef _WIN32
    std::string searchPath = std::string(CORES_DIR) + "*_libretro.dll";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                dllFiles.push_back(fd.cFileName);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(CORES_DIR);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name.size() > 14 && name.substr(name.size() - 14) == "_libretro.dll")
                dllFiles.push_back(name);
        }
        closedir(dir);
    }
#endif

    std::sort(dllFiles.begin(), dllFiles.end());

    /* Merge with existing config: keep config for known cores, add new ones */
    std::vector<LibretroCoreConfig> merged;
    for (const auto& dll : dllFiles) {
        bool found = false;
        for (const auto& existing : cores_) {
            if (existing.file == dll) {
                merged.push_back(existing);
                found = true;
                break;
            }
        }
        if (!found) {
            LibretroCoreConfig cfg;
            cfg.file = dll;
            merged.push_back(cfg);
        }
    }
    cores_ = merged;
}

void LibretroCoreConfigManager::loadConfig()
{
    configPath_ = CONFIG_FILE;
    std::ifstream f(configPath_);
    if (!f.is_open()) return;

    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    /* Parse array of core objects — simple manual parse */
    size_t pos = 0;
    while ((pos = json.find("{\"file\"", pos)) != std::string::npos) {
        size_t end = json.find('}', pos);
        /* Find the matching closing brace (handle nested paths array) */
        int depth = 0;
        for (size_t i = pos; i < json.size(); i++) {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') { depth--; if (depth == 0) { end = i; break; } }
        }
        std::string obj = json.substr(pos, end - pos + 1);

        LibretroCoreConfig cfg;
        cfg.file = jsonGetString(obj, "file");
        cfg.enabled = jsonGetBool(obj, "enabled");
        cfg.cartSaves = jsonGetBool(obj, "cartSaves");
        cfg.stateSaves = jsonGetBool(obj, "stateSaves");
        cfg.priority = jsonGetInt(obj, "priority");

        /* Parse paths array */
        size_t pathsPos = obj.find("\"paths\"");
        if (pathsPos != std::string::npos) {
            size_t arrStart = obj.find('[', pathsPos);
            size_t arrEnd = obj.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arrStr = obj.substr(arrStart, arrEnd - arrStart + 1);
                size_t ppos = 0;
                while ((ppos = arrStr.find('{', ppos)) != std::string::npos) {
                    size_t pend = arrStr.find('}', ppos);
                    std::string pobj = arrStr.substr(ppos, pend - ppos + 1);
                    CoreContentPath cp;
                    cp.path = jsonGetString(pobj, "path");
                    cp.extensions = jsonGetString(pobj, "extensions");
                    cfg.paths.push_back(cp);
                    ppos = pend + 1;
                }
            }
        }

        if (!cfg.file.empty())
            cores_.push_back(cfg);
        pos = end + 1;
    }
}

void LibretroCoreConfigManager::saveConfig()
{
    std::ofstream f(configPath_.empty() ? CONFIG_FILE : configPath_.c_str());
    if (!f.is_open()) return;

    f << "[\n";
    for (size_t i = 0; i < cores_.size(); i++) {
        const auto& c = cores_[i];
        f << "  {\"file\":\"" << jsonEscape(c.file) << "\""
          << ",\"enabled\":" << (c.enabled ? "true" : "false")
          << ",\"cartSaves\":" << (c.cartSaves ? "true" : "false")
          << ",\"stateSaves\":" << (c.stateSaves ? "true" : "false")
          << ",\"priority\":" << c.priority
          << ",\"paths\":[";
        for (size_t j = 0; j < c.paths.size(); j++) {
            if (j > 0) f << ",";
            f << "{\"path\":\"" << jsonEscape(c.paths[j].path)
              << "\",\"extensions\":\"" << jsonEscape(c.paths[j].extensions) << "\"}";
        }
        f << "]}";
        if (i + 1 < cores_.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

void LibretroCoreConfigManager::updateCore(const std::string& file, bool enabled, bool cartSaves,
                                            bool stateSaves, int priority,
                                            const std::vector<CoreContentPath>& paths)
{
    for (auto& c : cores_) {
        if (c.file == file) {
            c.enabled = enabled;
            c.cartSaves = cartSaves;
            c.stateSaves = stateSaves;
            c.priority = priority;
            c.paths = paths;
            saveConfig();
            return;
        }
    }
}

void LibretroCoreConfigManager::resetCoreOptions(const std::string& coreFile)
{
    /* Core runtime options are stored in aarcadecore/libretro/config/<corename>.opt */
    std::string baseName = coreFile;
    size_t dot = baseName.rfind('.');
    if (dot != std::string::npos) baseName = baseName.substr(0, dot);
    std::string optPath = std::string("aarcadecore/libretro/config/") + baseName + ".opt";
    remove(optPath.c_str());
    if (g_host.host_printf)
        g_host.host_printf("LibretroCoreConfig: Reset options for %s (%s)\n", coreFile.c_str(), optPath.c_str());
}

static std::string toLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

static bool startsWithIgnoreCase(const std::string& str, const std::string& prefix) {
    if (prefix.empty() || str.size() < prefix.size()) return false;
    return toLowerStr(str.substr(0, prefix.size())) == toLowerStr(prefix);
}

static std::string getFileExtension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    return toLowerStr(path.substr(dot + 1));
}

std::string LibretroCoreConfigManager::findCoreForFile(const std::string& filePath) const
{
    if (filePath.empty()) return "";

    std::string fileExt = getFileExtension(filePath);
    /* Normalize path separators for comparison */
    std::string normPath = filePath;
    for (auto& c : normPath) { if (c == '/') c = '\\'; }

    for (const auto& core : cores_) {
        if (!core.enabled) continue;

        for (const auto& cp : core.paths) {
            /* Normalize content path */
            std::string normContentPath = cp.path;
            for (auto& c : normContentPath) { if (c == '/') c = '\\'; }
            /* Ensure trailing backslash */
            if (!normContentPath.empty() && normContentPath.back() != '\\')
                normContentPath += '\\';

            /* Check if file is within this content path */
            if (!normContentPath.empty() && startsWithIgnoreCase(normPath, normContentPath)) {
                /* Check extension filter if specified */
                if (!cp.extensions.empty() && !fileExt.empty()) {
                    /* Parse comma-separated extensions */
                    bool extMatch = false;
                    std::string exts = cp.extensions;
                    size_t pos = 0;
                    while (pos < exts.size()) {
                        size_t comma = exts.find(',', pos);
                        if (comma == std::string::npos) comma = exts.size();
                        std::string ext = exts.substr(pos, comma - pos);
                        /* Trim whitespace */
                        while (!ext.empty() && ext.front() == ' ') ext.erase(ext.begin());
                        while (!ext.empty() && ext.back() == ' ') ext.pop_back();
                        if (toLowerStr(ext) == fileExt) { extMatch = true; break; }
                        pos = comma + 1;
                    }
                    if (!extMatch) continue;
                }
                if (g_host.host_printf)
                    g_host.host_printf("LibretroCoreConfig: File '%s' matched core '%s' (path: '%s')\n",
                                      filePath.c_str(), core.file.c_str(), cp.path.c_str());
                return core.file;
            }

            /* If content path is empty (any folder), just check extension */
            if (normContentPath.empty() && !cp.extensions.empty() && !fileExt.empty()) {
                std::string exts = cp.extensions;
                size_t pos = 0;
                while (pos < exts.size()) {
                    size_t comma = exts.find(',', pos);
                    if (comma == std::string::npos) comma = exts.size();
                    std::string ext = exts.substr(pos, comma - pos);
                    while (!ext.empty() && ext.front() == ' ') ext.erase(ext.begin());
                    while (!ext.empty() && ext.back() == ' ') ext.pop_back();
                    if (toLowerStr(ext) == fileExt) {
                        if (g_host.host_printf)
                            g_host.host_printf("LibretroCoreConfig: File '%s' matched core '%s' (ext: '%s')\n",
                                              filePath.c_str(), core.file.c_str(), ext.c_str());
                        return core.file;
                    }
                    pos = comma + 1;
                }
            }
        }
    }
    return "";
}

std::string LibretroCoreConfigManager::findCoreMatchingAppPaths(
    const std::vector<std::pair<std::string, std::string>>& appPaths) const
{
    for (const auto& core : cores_) {
        if (!core.enabled) continue;
        for (const auto& cp : core.paths) {
            std::string normCorePath = cp.path;
            for (auto& c : normCorePath) { if (c == '/') c = '\\'; }
            if (!normCorePath.empty() && normCorePath.back() != '\\') normCorePath += '\\';

            for (const auto& ap : appPaths) {
                std::string normAppPath = ap.first;
                for (auto& c : normAppPath) { if (c == '/') c = '\\'; }
                if (!normAppPath.empty() && normAppPath.back() != '\\') normAppPath += '\\';

                if (!normCorePath.empty() && !normAppPath.empty() &&
                    toLowerStr(normCorePath) == toLowerStr(normAppPath)) {
                    if (g_host.host_printf)
                        g_host.host_printf("LibretroCoreConfig: App path '%s' matched core '%s'\n",
                                          ap.first.c_str(), core.file.c_str());
                    return core.file;
                }
            }
        }
    }
    return "";
}

std::string LibretroCoreConfigManager::resolveFileInCorePaths(
    const std::string& coreDll, const std::string& filename) const
{
    for (const auto& core : cores_) {
        if (core.file != coreDll) continue;
        for (const auto& cp : core.paths) {
            std::string normPath = cp.path;
            for (auto& c : normPath) { if (c == '/') c = '\\'; }
            if (!normPath.empty() && normPath.back() != '\\') normPath += '\\';

            std::string fullPath = normPath + filename;
            /* Check if file exists */
            FILE* f = fopen(fullPath.c_str(), "rb");
            if (f) {
                fclose(f);
                if (g_host.host_printf)
                    g_host.host_printf("LibretroCoreConfig: Resolved '%s' to '%s'\n",
                                      filename.c_str(), fullPath.c_str());
                return fullPath;
            }
        }
        break;
    }
    return "";
}

std::string LibretroCoreConfigManager::toJson() const
{
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < cores_.size(); i++) {
        const auto& c = cores_[i];
        if (i > 0) ss << ",";
        ss << "{\"file\":\"" << jsonEscape(c.file) << "\""
           << ",\"enabled\":" << (c.enabled ? "true" : "false")
           << ",\"cartSaves\":" << (c.cartSaves ? "true" : "false")
           << ",\"stateSaves\":" << (c.stateSaves ? "true" : "false")
           << ",\"priority\":" << c.priority
           << ",\"paths\":[";
        for (size_t j = 0; j < c.paths.size(); j++) {
            if (j > 0) ss << ",";
            ss << "{\"path\":\"" << jsonEscape(c.paths[j].path)
               << "\",\"extensions\":\"" << jsonEscape(c.paths[j].extensions) << "\"}";
        }
        ss << "]}";
    }
    ss << "]";
    return ss.str();
}
