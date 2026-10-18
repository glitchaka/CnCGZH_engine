#include "runtime/Runtime.h"

#include <algorithm>
#include <cmath>

namespace cncgzh {

Entity& World::spawn(Vec2 position) {
    Entity entity;
    entity.id = nextId_++;
    entity.position = position;
    entity.target = position;
    entities_.push_back(entity);
    return entities_.back();
}

void World::update(float dt) {
    for (auto& entity : entities_) {
        if (!entity.hasTarget) {
            entity.velocity = {};
            continue;
        }

        const float dx = entity.target.x - entity.position.x;
        const float dy = entity.target.y - entity.position.y;
        const float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < 2.0f) {
            entity.position = entity.target;
            entity.velocity = {};
            entity.hasTarget = false;
            continue;
        }

        const float invDistance = 1.0f / distance;
        entity.velocity = {dx * invDistance * entity.speed, dy * invDistance * entity.speed};
        const float step = std::min(distance, entity.speed * dt);
        entity.position.x += dx * invDistance * step;
        entity.position.y += dy * invDistance * step;
    }
}

void World::clearSelection() {
    for (auto& entity : entities_) entity.selected = false;
}

Entity* World::pick(Vec2 worldPosition) {
    for (auto it = entities_.rbegin(); it != entities_.rend(); ++it) {
        const float dx = worldPosition.x - it->position.x;
        const float dy = worldPosition.y - it->position.y;
        if (dx * dx + dy * dy <= it->radius * it->radius) return &*it;
    }
    return nullptr;
}

void World::commandMoveSelected(Vec2 worldPosition) {
    for (auto& entity : entities_) {
        if (!entity.selected) continue;
        entity.target = worldPosition;
        entity.hasTarget = true;
    }
}

} // namespace cncgzh
