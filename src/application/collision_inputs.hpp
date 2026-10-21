#pragma once

#include "world_explorer/scene_store/collision_support.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace world_explorer::collision_inputs {

using Json = nlohmann::json;

void fields(const Json& value, std::initializer_list<const char*> names);
double real(const Json& value);
Vec3d vector(const Json& value);
Result<Json> read_request(const std::filesystem::path& path,
                          std::string_view expected_sha256,
                          const std::stop_token& stop);

struct Inputs {
    std::filesystem::path m_source_path;
    SceneSnapshot m_source;
    std::shared_ptr<const CollisionSupport> m_support;
};

Result<Inputs>
load(const std::filesystem::path& request, const Json& document, const std::stop_token& stop);

}  // namespace world_explorer::collision_inputs
