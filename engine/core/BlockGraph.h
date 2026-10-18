#pragma once

#include "core/EngineAPI.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cncgzh {

enum class BlockType {
    OnStart,
    Spawn,
    Destroy,
    MoveTo,
    SetHealth,
    GiveResource,
    ShowMessage,
    Wait,
    Repeat
};

struct Block {
    std::uint32_t id{};
    BlockType type{BlockType::OnStart};
    std::unordered_map<std::string, std::string> args;
    std::vector<std::uint32_t> next;
};

class BlockGraph {
public:
    bool add(Block block);
    const Block* find(std::uint32_t id) const;
    const std::vector<Block>& blocks() const noexcept { return blocks_; }
    void executeStart(EngineAPI& engine) const;

private:
    void execute(std::uint32_t id, EngineAPI& engine, int depth) const;
    std::vector<Block> blocks_;
};

const char* blockTypeName(BlockType type) noexcept;

} // namespace cncgzh
