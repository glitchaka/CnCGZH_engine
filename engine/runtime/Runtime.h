#pragma once

#include <cstdint>
#include <vector>

namespace cncgzh {

struct Vec2 {
    float x{};
    float y{};
};

struct Entity {
    std::uint32_t id{};
    Vec2 position{};
    Vec2 velocity{};
    Vec2 target{};
    float radius{14.0f};
    float speed{85.0f};
    int health{100};
    bool hasTarget{false};
    bool selected{false};
};

struct Camera {
    Vec2 position{};
    float zoom{1.0f};
};

class World {
public:
    Entity& spawn(Vec2 position);
    void update(float dt);
    void clearSelection();
    Entity* pick(Vec2 worldPosition);
    void commandMoveSelected(Vec2 worldPosition);

    std::vector<Entity>& entities() { return entities_; }
    const std::vector<Entity>& entities() const { return entities_; }

private:
    std::vector<Entity> entities_;
    std::uint32_t nextId_{1};
};

} // namespace cncgzh
