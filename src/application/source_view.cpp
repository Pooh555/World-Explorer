#include "world_explorer/application/scenes.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace world_explorer {
result<source_inspection_view> source_view(const scene_state& scene) {
    const auto found = scene.provenance.find("source_inspection_view");
    if (found == scene.provenance.end())
        return fail(error_code::UNSUPPORTED_OPERATION, "This scene has no recorded source view");
    try {
        if (found->second.size() > 4096)
            return fail(error_code::RESOURCE_EXHAUSTED, "Source view metadata exceeds its budget");
        const auto value = nlohmann::json::parse(found->second);
        if (value.at("schema_version") != 1)
            return fail(error_code::INVALID_DATA, "Unknown source view schema");
        const auto position = value.at("position").get<std::array<double, 3>>();
        const auto direction = value.at("forward").get<std::array<double, 3>>();
        source_inspection_view result;
        result.camera.position = {position[0], position[1], position[2]};
        const vec3d forward{direction[0], direction[1], direction[2]};
        result.vertical_fov_radians = value.at("vertical_fov_radians").get<double>();
        result.camera.speed = value.at("speed").get<double>();
        result.frame_name = value.at("frame_name").get<std::string>();
        if (!finite(result.camera.position) || !finite(forward) ||
            std::abs(dot(forward, forward) - 1) > 1e-5 ||
            !std::isfinite(result.vertical_fov_radians) || result.vertical_fov_radians <= 0.05 ||
            result.vertical_fov_radians >= 3.1 || !std::isfinite(result.camera.speed) ||
            result.camera.speed < 0.01 || result.camera.speed > 10000 ||
            result.frame_name.size() > 128)
            return fail(error_code::INVALID_DATA, "Invalid source view camera");
        result.camera.yaw = std::atan2(forward.x, -forward.z);
        result.camera.pitch = std::asin(std::clamp(forward.y, -1., 1.));
        return result;
    } catch (const std::exception& error) {
        return fail(error_code::INVALID_DATA, std::string("Source view invalid: ") + error.what());
    }
}
} // namespace world_explorer
