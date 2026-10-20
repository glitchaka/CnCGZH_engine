#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cncstudio {

enum class ObjectKind {
    Empty,
    Mesh,
    Camera,
    Light
};

struct Transform {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct CameraComponent {
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    bool active = false;
};

struct LightComponent {
    float intensity = 1.0f;
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
};

enum class PinType {
    Flow,
    Bool,
    Number,
    Text,
    Object,
    Vector3
};

struct ScriptPin {
    std::uint64_t id = 0;
    std::string name;
    PinType type = PinType::Flow;
};

struct ScriptNode {
    std::uint64_t id = 0;
    std::string type;
    std::string title;
    std::string category;
    float x = 40.0f;
    float y = 40.0f;
    std::vector<ScriptPin> inputs;
    std::vector<ScriptPin> outputs;
    std::unordered_map<std::string, std::string> properties;
};

struct ScriptLink {
    std::uint64_t fromPin = 0;
    std::uint64_t toPin = 0;
};

struct ScriptGraph {
    std::vector<ScriptNode> nodes;
    std::vector<ScriptLink> links;
};

struct SceneObject {
    std::uint64_t id = 0;
    std::string name;
    ObjectKind kind = ObjectKind::Empty;
    Transform transform;
    std::filesystem::path assetPath;
    CameraComponent camera;
    LightComponent light;
    ScriptGraph script;
    bool visible = true;
};

struct ApiSymbol {
    std::string library;
    std::string header;
    std::string name;
    std::string signature;
    bool callableAdapter = false;
};

} // namespace cncstudio
