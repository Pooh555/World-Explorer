#include "world_explorer/application/scenes.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace world_explorer {

Result<SourceInspectionView> source_view(const SceneState& scene) {
    const auto K_FOUND = scene.m_provenance.find("source_inspection_view");
    if (K_FOUND == scene.m_provenance.end()) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION, "This scene has no recorded source view");
    }
    try {
        if (K_FOUND->second.size() > 4096) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Source view metadata exceeds its budget");
        }
        const auto K_VALUE = nlohmann::json::parse(K_FOUND->second);
        const auto& version = K_VALUE.at("schema_version");
        if (!version.is_number_integer() || (version != 1 && version != 2)) {
            return fail(ErrorCode::INVALID_DATA, "Unknown source view schema");
        }
        const auto K_POSITION = K_VALUE.at("position").get<std::array<double, 3>>();
        const auto K_DIRECTION = K_VALUE.at("forward").get<std::array<double, 3>>();
        SourceInspectionView result;
        result.m_camera.m_position = { K_POSITION[0], K_POSITION[1], K_POSITION[2] };
        Vec3d forward { K_DIRECTION[0], K_DIRECTION[1], K_DIRECTION[2] };
        result.m_vertical_fov_radians = K_VALUE.at("vertical_fov_radians").get<double>();
        result.m_camera.m_speed = K_VALUE.at("speed").get<double>();
        result.m_frame_name = K_VALUE.at("frame_name").get<std::string>();
        if (version == 2) {
            if (K_VALUE.at("up_reference") != "CAPTURE_ORIENTATION") {
                return fail(ErrorCode::INVALID_DATA, "Unknown source view up reference");
            }
            const auto K_UP = K_VALUE.at("up").get<std::array<double, 3>>();
            const Vec3d K_AXIS { K_UP[0], K_UP[1], K_UP[2] };
            if (!finite(K_AXIS) || std::abs(dot(K_AXIS, K_AXIS) - 1) > 1e-5 ||
                std::abs(dot(K_AXIS, forward)) > 1e-5) {
                return fail(ErrorCode::INVALID_DATA, "Invalid source view up direction");
            }
            result.m_camera.m_up_axis = K_AXIS * (1 / std::sqrt(dot(K_AXIS, K_AXIS)));
        }
        if (!finite(result.m_camera.m_position) || !finite(forward) ||
            std::abs(dot(forward, forward) - 1) > 1e-5 ||
            !std::isfinite(result.m_vertical_fov_radians) ||
            result.m_vertical_fov_radians <= 0.05 || result.m_vertical_fov_radians >= 3.1 ||
            !std::isfinite(result.m_camera.m_speed) || result.m_camera.m_speed < 0.01 ||
            result.m_camera.m_speed > 10000 || result.m_frame_name.size() > 128) {
            return fail(ErrorCode::INVALID_DATA, "Invalid source view camera");
        }
        if (scene.m_world_frame) {
            const auto K_VALID = validate_world_frame(scene);
            if (!K_VALID) {
                return std::unexpected(K_VALID.error());
            }
            const auto& transform = scene.m_world_frame->m_world_from_source;
            result.m_camera.m_position = transform_point(transform, result.m_camera.m_position);
            result.m_camera.m_up_axis = rotate(transform.m_rotation, result.m_camera.m_up_axis);
            forward = rotate(transform.m_rotation, forward);
            result.m_camera.m_speed =
                std::clamp(result.m_camera.m_speed * transform.m_scale, .01, 10000.);
            if (!finite(result.m_camera.m_position)) {
                return fail(ErrorCode::INVALID_DATA, "Aligned source camera overflows");
            }
        }
        result.m_camera.m_yaw = std::atan2(dot(forward, result.m_camera.reference_right()),
                                           dot(forward, result.m_camera.reference_forward()));
        result.m_camera.m_pitch =
            std::asin(std::clamp(dot(forward, result.m_camera.m_up_axis), -1., 1.));
        return result;
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, std::string("Source view invalid: ") + error.what());
    }
}

}  // namespace world_explorer
