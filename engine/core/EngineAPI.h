#pragma once

#include <cstdint>
#include <string>

namespace cncgzh {

using EntityId = std::uint32_t;

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

class EngineAPI {
public:
    virtual ~EngineAPI() = default;

    virtual EntityId spawn(const std::string& archetype, Vec3 position) = 0;
    virtual void destroy(EntityId entity) = 0;
    virtual void moveTo(EntityId entity, Vec3 destination) = 0;
    virtual void setHealth(EntityId entity, float health) = 0;
    virtual void giveResource(std::uint32_t player, const std::string& resource, int amount) = 0;
    virtual void showMessage(const std::string& message) = 0;
};

} // namespace cncgzh
