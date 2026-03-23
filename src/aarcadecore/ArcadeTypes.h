#ifndef ARCADE_TYPES_H
#define ARCADE_TYPES_H

#include <string>
#include <chrono>
#include <random>

namespace Arcade {

/* Firebase-style push ID generator (ported from aarcade-core) */
inline std::string generateFirebasePushId() {
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string id = "-";
    uint64_t timestamp = static_cast<uint64_t>(ms);
    for (int i = 7; i >= 0; i--)
        id += chars[(timestamp >> (i * 6)) & 0x3F];
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<> dis(0, 63);
    for (int i = 0; i < 11; i++)
        id += chars[dis(gen)];
    return id;
}

struct Item {
    std::string id, app, description, file, marquee, preview, screen, title, type;
};

struct Type {
    std::string id, title;
    int priority = 0;
};

struct Model {
    std::string id, title, screen;
};

struct App {
    std::string id, title, type, screen;
    std::string commandformat, description, download, file, reference;
};

struct AppFilepath {
    std::string app_id, filepath_key, path, extensions;
};

struct Map {
    std::string id, title;
};

struct MapPlatform {
    std::string map_id, platform_key, file;
};

struct Platform {
    std::string id, title;
};

struct Instance {
    std::string id, title, map; /* map = maps.id reference */
};

struct InstanceObject {
    std::string instance_id, object_key;
    std::string item;      /* item ID reference */
    std::string model;     /* model ID reference */
    std::string position;  /* "X Y Z" space-separated */
    std::string rotation;  /* "Pitch Yaw Roll" space-separated */
    float scale = 1.0f;
};

} // namespace Arcade

#define OPENJK_PLATFORM_ID "-qy1Xi800ElhAZksOIc8"
#define OPENJK_PLATFORM_TITLE "AArcade: OpenJK"

#endif // ARCADE_TYPES_H
