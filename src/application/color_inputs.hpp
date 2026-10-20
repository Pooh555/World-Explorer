#pragma once

#include "world_explorer/scene_store/color_projection.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <nlohmann/json.hpp>

namespace world_explorer::color_inputs {

struct Cache {
    std::filesystem::path m_root;
    nlohmann::json m_receipt;
};

struct Inputs {
    SceneSnapshot m_scene;
    std::filesystem::path m_cache_root, m_output;
    nlohmann::json m_receipt;
    double m_voxel_size {};
    bool m_sequence {};
    std::vector<Cache> m_caches;
    std::vector<std::size_t> m_frame_caches;
};

// Read-only validation of every child and encoded dependency before projection.
Result<std::vector<Cache>> admit_caches(const nlohmann::json& receipt,
                                        const std::filesystem::path& root,
                                        bool sequence,
                                        const std::stop_token& stop = {});

Result<Inputs> load(const std::filesystem::path& request,
                    const std::string& expected_sha256,
                    const std::filesystem::path& destination,
                    const std::stop_token& stop);
Result<ColorProjectionView>
view(const Inputs& inputs, std::size_t index, const std::stop_token& stop);

}  // namespace world_explorer::color_inputs
