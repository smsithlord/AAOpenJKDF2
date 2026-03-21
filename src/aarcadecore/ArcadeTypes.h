#ifndef ARCADE_TYPES_H
#define ARCADE_TYPES_H

#include <string>

namespace Arcade {

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
};

struct Map {
    std::string id, title;
};

struct Platform {
    std::string id, title;
};

struct Instance {
    std::string id;
};

} // namespace Arcade

#endif // ARCADE_TYPES_H
