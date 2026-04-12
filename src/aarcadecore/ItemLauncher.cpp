/*
 * ItemLauncher — resolve an item into a process call and launch it.
 *
 * An item has a file (what to launch) and optionally an app (what to launch it with).
 * Security checks validate paths before process creation.
 */

#include "ItemLauncher.h"
#include "SQLiteLibrary.h"
#include "ArcadeTypes.h"
#include "aarcadecore_internal.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <algorithm>
#include <cctype>

extern SQLiteLibrary g_library;
extern AACoreHostCallbacks g_host;

/* ======================================================================== */

bool ItemLauncher::isNumericString(const std::string& text)
{
    if (text.empty()) return false;
    for (char c : text) {
        if (!isdigit((unsigned char)c)) return false;
    }
    return true;
}

bool ItemLauncher::alphabetSafe(const std::string& text)
{
    static const std::string safe =
        " 1234567890abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "`-=[]\\;',./~!@#$%^&*()_+{}|:<>?";
    for (char c : text) {
        if (safe.find(c) == std::string::npos) return false;
    }
    return true;
}

bool ItemLauncher::prefixSafe(const std::string& text)
{
    if (text.empty()) return false;
    if (text.find("http://") == 0 || text.find("https://") == 0 || text.find("steam://") == 0)
        return true;
    if (isNumericString(text))
        return true;
    if (text.size() >= 3 && isalpha((unsigned char)text[0]) && text[1] == ':' && (text[2] == '\\' || text[2] == '/'))
        return true;
    return false;
}

bool ItemLauncher::directorySafe(const std::string& text)
{
    if (text.empty()) return false;
    if (text[0] == ' ' || text[0] == '%') return false;
    if (text.find("/..") != std::string::npos || text.find("\\..") != std::string::npos)
        return false;
    /* Block access to Windows directory */
    if (text.size() > 10) {
        std::string sub = text.substr(3, 7);
        std::string lower;
        for (char c : sub) lower += (char)tolower((unsigned char)c);
        if (lower == "windows" && (text.size() == 10 || text[10] == '\\' || text[10] == '/'))
            return false;
    }
    return true;
}

std::string ItemLauncher::findSteamExe()
{
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        DWORD size = sizeof(buf);
        DWORD type;
        if (RegQueryValueExA(hKey, "SteamExe", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ) {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
#endif
    return "";
}

/* ======================================================================== */

bool ItemLauncher::launchWithoutApp(const std::string& file)
{
    std::string resolved = file;

    /* www. prefix → prepend http:// */
    if (resolved.find("www.") == 0)
        resolved = "http://" + resolved;

    /* Pure numeric → Steam App ID: launch via bat matching old app's format.
     * The old ArcadeCreateProcess wrote a bat with drive letter, cd, and START,
     * then ran it. The bat window must be VISIBLE (not hidden) for the START
     * command to properly route steam:// to the Steam client. */
    if (isNumericString(resolved)) {
#ifdef _WIN32
        std::string steamUrl = "steam://run/" + resolved;
        std::string batPath = "aa_steam_launch.bat";
        std::string goodExe = "\"" + steamUrl + "\"";

        FILE* f = fopen(batPath.c_str(), "w");
        if (f) {
            /* Match old app's bat format exactly */
            fprintf(f, "%c:\n", steamUrl[0]);
            std::string dir = steamUrl.substr(0, steamUrl.find_last_of("/\\"));
            fprintf(f, "cd \"%s\"\n", dir.c_str());
            fprintf(f, "START \"Launching item...\" %s\n", goodExe.c_str());
            fclose(f);

            if (g_host.host_printf)
                g_host.host_printf("ItemLauncher: Launching Steam app %s via bat\n", resolved.c_str());

            /* Use CreateProcessA with VISIBLE window — DO NOT use
             * CREATE_NO_WINDOW or SW_HIDE, the old app showed a brief
             * cmd window and this is required for proper protocol routing. */
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            char cmdBuf[1024];
            snprintf(cmdBuf, sizeof(cmdBuf), "cmd.exe /c \"%s\"", batPath.c_str());

            BOOL ok = CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            if (ok) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return true;
            }
        }

        /* If bat approach fails, fall through to protocol URL (may open browser) */
        resolved = steamUrl;
#endif
    }

    /* Alternative approaches (commented out for reference):
     *
     * 1. Find Steam.exe via registry and use -applaunch:
     *    std::string steamExe = findSteamExe();
     *    if (!steamExe.empty()) {
     *        std::string args = "-applaunch " + resolved;
     *        std::string steamDir = steamExe.substr(0, steamExe.find_last_of("/\\") + 1);
     *        ShellExecuteA(NULL, "open", steamExe.c_str(), args.c_str(), steamDir.c_str(), SW_SHOWNORMAL);
     *    }
     *
     * 2. CreateProcessA with cmd.exe /c batfile (didn't work — still opened browser):
     *    CreateProcessA(NULL, "cmd.exe /c batfile", ..., CREATE_NO_WINDOW, ...);
     */

    /* Protocol URLs: launch directly */
    if (resolved.find("http") == 0 || resolved.find("steam://") == 0) {
#ifdef _WIN32
        HINSTANCE r = ShellExecuteA(NULL, "open", resolved.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return (intptr_t)r > 32;
#endif
        return false;
    }

    /* Local file: verify it exists */
    if (!prefixSafe(resolved)) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Prefix not safe: %s\n", resolved.c_str());
        return false;
    }
    if (!alphabetSafe(resolved) || !directorySafe(resolved)) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Path not safe: %s\n", resolved.c_str());
        return false;
    }

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(resolved.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: File not found: %s\n", resolved.c_str());
        return false;
    }
    HINSTANCE r = ShellExecuteA(NULL, "open", resolved.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return (intptr_t)r > 32;
#endif
    return false;
}

/* ======================================================================== */

bool ItemLauncher::launchWithApp(const std::string& file, const std::string& appId)
{
    Arcade::App app = g_library.getAppById(appId);
    if (app.id.empty() || app.file.empty()) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: App not found or no executable: %s\n", appId.c_str());
        return false;
    }

#ifdef _WIN32
    /* Verify app executable exists */
    DWORD attrs = GetFileAttributesA(app.file.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: App executable not found: %s\n", app.file.c_str());
        return false;
    }

    /* Get app filepaths and try to find the item's file */
    std::vector<Arcade::AppFilepath> filepaths = g_library.getAppFilepaths(appId);
    std::string composedFile = file;
    std::string shortFileOnly = file;
    bool fileResolved = false;

    for (const auto& fp : filepaths) {
        std::string testPath = fp.path;
        /* Remove trailing slash */
        if (!testPath.empty() && (testPath.back() == '/' || testPath.back() == '\\'))
            testPath.pop_back();

        /* Check if directory exists */
        DWORD dirAttrs = GetFileAttributesA(testPath.c_str());
        if (dirAttrs == INVALID_FILE_ATTRIBUTES || !(dirAttrs & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        /* Determine slash style */
        char slash = (testPath.find('\\') != std::string::npos) ? '\\' : '/';

        /* Build test short file — strip the path prefix if it matches */
        std::string testShort = shortFileOnly;
        std::replace(testShort.begin(), testShort.end(), (slash == '\\') ? '/' : '\\', slash);

        if (testShort.find(testPath) == 0)
            testShort = testShort.substr(testPath.length() + 1);
        else if (testShort.find(':') != std::string::npos) {
            size_t lastSlash = testShort.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                testShort = testShort.substr(lastSlash + 1);
        }

        std::string testFile = testPath + slash + testShort;
        DWORD fileAttrs = GetFileAttributesA(testFile.c_str());
        if (fileAttrs != INVALID_FILE_ATTRIBUTES) {
            composedFile = testFile;
            shortFileOnly = testShort;
            fileResolved = true;
            break;
        }
    }

    /* If not resolved via filepaths, try stripping to filename only */
    if (!fileResolved && shortFileOnly.find(':') != std::string::npos) {
        size_t lastSlash = shortFileOnly.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            shortFileOnly = shortFileOnly.substr(lastSlash + 1);
    }

    /* Security check on composed file before substitution */
    if (!alphabetSafe(composedFile) || !directorySafe(composedFile)) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Composed file path not safe: %s\n", composedFile.c_str());
        return false;
    }

    /* Build command line from command format */
    std::string commands = app.commandformat;
    if (!commands.empty()) {
        /* Replace $FILE with composedFile */
        size_t pos;
        while ((pos = commands.find("$FILE")) != std::string::npos)
            commands.replace(pos, 5, composedFile);
        while ((pos = commands.find("$SHORTFILE")) != std::string::npos)
            commands.replace(pos, 10, shortFileOnly);
        while ((pos = commands.find("$QUOTE")) != std::string::npos)
            commands.replace(pos, 6, "\"");
    } else {
        commands = "\"" + composedFile + "\"";
    }

    /* Get executable directory and short exe name */
    std::string exeDir;
    std::string shortExe = app.file;
    size_t lastSlash = app.file.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        exeDir = app.file.substr(0, lastSlash + 1);
        shortExe = app.file.substr(lastSlash + 1);
    }

    /* Validate executable path */
    if (!alphabetSafe(app.file) || !prefixSafe(app.file)) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Executable path not safe: %s\n", app.file.c_str());
        return false;
    }

    /* Build final command line: "exe_name" commands */
    std::string cmdLine = "\"" + shortExe + "\" " + commands;

    if (g_host.host_printf)
        g_host.host_printf("ItemLauncher: Launching\n  Executable: %s\n  Directory: %s\n  Commands: %s\n",
                          app.file.c_str(), exeDir.c_str(), cmdLine.c_str());

    /* Create process */
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char cmdBuf[4096];
    strncpy(cmdBuf, cmdLine.c_str(), sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = '\0';

    BOOL ok = CreateProcessA(
        app.file.c_str(),
        cmdBuf,
        NULL, NULL, FALSE, 0, NULL,
        exeDir.empty() ? NULL : exeDir.c_str(),
        &si, &pi
    );

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    if (g_host.host_printf)
        g_host.host_printf("ItemLauncher: CreateProcess failed (error %lu)\n", GetLastError());
    return false;
#endif
    return false;
}

/* ======================================================================== */

bool ItemLauncher::launch(const std::string& itemId)
{
    Arcade::Item item = g_library.getItemById(itemId);
    if (item.id.empty()) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Item not found: %s\n", itemId.c_str());
        return false;
    }

    if (item.file.empty()) {
        if (g_host.host_printf) g_host.host_printf("ItemLauncher: Item has no file: %s\n", itemId.c_str());
        return false;
    }

    if (g_host.host_printf)
        g_host.host_printf("ItemLauncher: Launching item '%s' file='%s' app='%s'\n",
                          item.title.c_str(), item.file.c_str(), item.app.c_str());

    if (item.app.empty() || item.app == "Windows (default)") {
        return launchWithoutApp(item.file);
    } else {
        return launchWithApp(item.file, item.app);
    }
}
