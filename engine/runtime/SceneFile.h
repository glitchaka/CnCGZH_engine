#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cncgzh {

struct SceneObject {
    std::string asset;
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float rotationY{0.0f};
    float scale{1.0f};
};

inline bool saveScene(const std::filesystem::path& path, const std::vector<SceneObject>& objects) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "CNCGZH_SCENE_V1\n";
    for (const auto& object : objects) {
        out << object.asset << '|' << object.x << '|' << object.y << '|' << object.z << '|'
            << object.rotationY << '|' << object.scale << '\n';
    }
    return static_cast<bool>(out);
}

inline bool loadScene(const std::filesystem::path& path, std::vector<SceneObject>& objects) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line) || line != "CNCGZH_SCENE_V1") return false;
    objects.clear();
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '|')) fields.push_back(field);
        if (fields.size() != 6) continue;
        SceneObject object;
        object.asset = fields[0];
        try {
            object.x = std::stof(fields[1]);
            object.y = std::stof(fields[2]);
            object.z = std::stof(fields[3]);
            object.rotationY = std::stof(fields[4]);
            object.scale = std::stof(fields[5]);
        } catch (...) {
            continue;
        }
        objects.push_back(object);
    }
    return true;
}

} // namespace cncgzh
