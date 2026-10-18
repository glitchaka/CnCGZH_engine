#pragma once

#include "core/EngineAPI.h"

namespace cncgzh {

// Boundary between the reusable editor/runtime and the preserved EA code.
// The implementation is intentionally thin: every Generals-specific include
// must stay on the .cpp side so projects made with the editor remain generic.
class GeneralsAdapter final : public EngineAPI {
public:
    EntityId spawn(const std::string& archetype, Vec3 position) override;
    void destroy(EntityId entity) override;
    void moveTo(EntityId entity, Vec3 destination) override;
    void setHealth(EntityId entity, float health) override;
    void giveResource(std::uint32_t player, const std::string& resource, int amount) override;
    void showMessage(const std::string& message) override;
};

} // namespace cncgzh
