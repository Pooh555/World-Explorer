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
        const auto& version = value.at("schema_version");
        if (!version.is_number_integer() || (version != 1 && version != 2))
            return fail(error_code::INVALID_DATA, "Unknown source view schema");
        const auto position = value.at("position").get<std::array<double, 3>>();
        const auto direction = value.at("forward").get<std::array<double, 3>>();
        source_inspection_view result;
        result.camera.position = {position[0], position[1], position[2]};
        const vec3d forward{direction[0], direction[1], direction[2]};
        result.vertical_fov_radians = value.at("vertical_fov_radians").get<double>();
        result.camera.speed = value.at("speed").get<double>();
        result.frame_name = value.at("frame_name").get<std::string>();
        if (version == 2) {
            if (value.at("up_reference") != "CAPTURE_ORIENTATION")
                return fail(error_code::INVALID_DATA, "Unknown source view up reference");
            const auto up = value.at("up").get<std::array<double, 3>>();
            const vec3d axis{up[0], up[1], up[2]};
            if (!finite(axis) || std::abs(dot(axis, axis) - 1) > 1e-5 ||
                std::abs(dot(axis, forward)) > 1e-5)
                return fail(error_code::INVALID_DATA, "Invalid source view up direction");
            result.camera.up_axis = axis * (1 / std::sqrt(dot(axis, axis)));
        }
        if (!finite(result.camera.position) || !finite(forward) ||
            std::abs(dot(forward, forward) - 1) > 1e-5 ||
            !std::isfinite(result.vertical_fov_radians) || result.vertical_fov_radians <= 0.05 ||
            result.vertical_fov_radians >= 3.1 || !std::isfinite(result.camera.speed) ||
            result.camera.speed < 0.01 || result.camera.speed > 10000 ||
            result.frame_name.size() > 128)
            return fail(error_code::INVALID_DATA, "Invalid source view camera");
        result.camera.yaw = std::atan2(dot(forward, result.camera.reference_right()),
                                       dot(forward, result.camera.reference_forward()));
        result.camera.pitch = std::asin(std::clamp(dot(forward, result.camera.up_axis), -1., 1.));
        return result;
    } catch (const std::exception& error) {
        return fail(error_code::INVALID_DATA, std::string("Source view invalid: ") + error.what());
    }
}
} // namespace world_explorer
