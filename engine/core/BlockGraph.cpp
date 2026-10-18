#include "core/BlockGraph.h"

#include <algorithm>
#include <cstdlib>

namespace cncgzh {

namespace {
float number(const Block& b, const char* key, float fallback = 0.0f) {
    auto it = b.args.find(key);
    if (it == b.args.end()) return fallback;
    return std::strtof(it->second.c_str(), nullptr);
}

std::string text(const Block& b, const char* key, const char* fallback = "") {
    auto it = b.args.find(key);
    return it == b.args.end() ? fallback : it->second;
}
}

bool BlockGraph::add(Block block) {
    if (block.id == 0 || find(block.id)) return false;
    blocks_.push_back(std::move(block));
    return true;
}

const Block* BlockGraph::find(std::uint32_t id) const {
    auto it = std::find_if(blocks_.begin(), blocks_.end(), [id](const Block& b) { return b.id == id; });
    return it == blocks_.end() ? nullptr : &*it;
}

void BlockGraph::executeStart(EngineAPI& engine) const {
    for (const auto& block : blocks_) {
        if (block.type == BlockType::OnStart) execute(block.id, engine, 0);
    }
}

void BlockGraph::execute(std::uint32_t id, EngineAPI& engine, int depth) const {
    if (depth > 1024) return; // malformed cyclic graph guard
    const Block* b = find(id);
    if (!b) return;

    static EntityId lastEntity = 0;
    switch (b->type) {
        case BlockType::OnStart: break;
        case BlockType::Spawn:
            lastEntity = engine.spawn(text(*b, "archetype", "unit"),
                {number(*b, "x"), number(*b, "y"), number(*b, "z")});
            break;
        case BlockType::Destroy:
            if (lastEntity) engine.destroy(lastEntity);
            break;
        case BlockType::MoveTo:
            if (lastEntity) engine.moveTo(lastEntity,
                {number(*b, "x"), number(*b, "y"), number(*b, "z")});
            break;
        case BlockType::SetHealth:
            if (lastEntity) engine.setHealth(lastEntity, number(*b, "health", 100.0f));
            break;
        case BlockType::GiveResource:
            engine.giveResource(static_cast<std::uint32_t>(number(*b, "player")),
                text(*b, "resource", "money"), static_cast<int>(number(*b, "amount")));
            break;
        case BlockType::ShowMessage:
            engine.showMessage(text(*b, "message", "Hello from blocks"));
            break;
        case BlockType::Wait:
            // Runtime adapters schedule waits; the core graph deliberately owns no clock.
            break;
        case BlockType::Repeat: {
            int count = static_cast<int>(number(*b, "count", 1));
            count = std::clamp(count, 0, 10000);
            for (int i = 0; i < count; ++i)
                for (auto next : b->next) execute(next, engine, depth + 1);
            return;
        }
    }

    for (auto next : b->next) execute(next, engine, depth + 1);
}

const char* blockTypeName(BlockType type) noexcept {
    switch (type) {
        case BlockType::OnStart: return "When game starts";
        case BlockType::Spawn: return "Spawn entity";
        case BlockType::Destroy: return "Destroy entity";
        case BlockType::MoveTo: return "Move entity";
        case BlockType::SetHealth: return "Set health";
        case BlockType::GiveResource: return "Give resource";
        case BlockType::ShowMessage: return "Show message";
        case BlockType::Wait: return "Wait";
        case BlockType::Repeat: return "Repeat";
    }
    return "Unknown";
}

} // namespace cncgzh
