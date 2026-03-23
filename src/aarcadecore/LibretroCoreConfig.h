#ifndef LIBRETRO_CORE_CONFIG_H
#define LIBRETRO_CORE_CONFIG_H

#include <string>
#include <vector>

struct CoreContentPath {
    std::string path;
    std::string extensions;
};

struct LibretroCoreConfig {
    std::string file;
    bool enabled = false;
    bool cartSaves = false;
    bool stateSaves = false;
    int priority = 0;
    std::vector<CoreContentPath> paths;
};

class LibretroCoreConfigManager {
public:
    void scanCores();
    void loadConfig();
    void saveConfig();
    const std::vector<LibretroCoreConfig>& getAllCores() const { return cores_; }
    void updateCore(const std::string& file, bool enabled, bool cartSaves, bool stateSaves,
                    int priority, const std::vector<CoreContentPath>& paths);
    void resetCoreOptions(const std::string& coreFile);

    /* Find an enabled core whose content paths match the given file.
     * Returns the core DLL filename, or empty string if no match. */
    std::string findCoreForFile(const std::string& filePath) const;

    /* Find an enabled core that shares content paths with the given app file paths.
     * Returns the core DLL filename, or empty string if no match. */
    std::string findCoreMatchingAppPaths(const std::vector<std::pair<std::string, std::string>>& appPaths) const;

    /* Resolve a filename within a matched core's content paths.
     * Returns the full path if found on disk, or empty string. */
    std::string resolveFileInCorePaths(const std::string& coreDll, const std::string& filename) const;

    /* Serialize all cores to JSON string for JS bridge */
    std::string toJson() const;

private:
    std::vector<LibretroCoreConfig> cores_;
    std::string configPath_;
};

extern LibretroCoreConfigManager g_coreConfigMgr;

#endif
