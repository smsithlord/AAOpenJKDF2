#ifndef ITEM_LAUNCHER_H
#define ITEM_LAUNCHER_H

#include <string>

class ItemLauncher {
public:
    /* Launch an item by ID. Returns true on success. */
    static bool launch(const std::string& itemId);

private:
    static bool launchWithoutApp(const std::string& file);
    static bool launchWithApp(const std::string& file, const std::string& appId);

    /* Security checks */
    static bool alphabetSafe(const std::string& text);
    static bool prefixSafe(const std::string& text);
    static bool directorySafe(const std::string& text);
    static bool isNumericString(const std::string& text);
    static std::string findSteamExe();
};

#endif // ITEM_LAUNCHER_H
