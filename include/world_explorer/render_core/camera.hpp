#ifndef WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#define WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#include "world_explorer/core/math.hpp"
#include <cstdint>

namespace world_explorer {
// Camera basis is optical (+X right, +Y down, +Z forward), independent of capture poses.
struct camera_view {
    vec3d position{};
    vec3d right{1, 0, 0};
    vec3d down{0, -1, 0};
    vec3d forward{0, 0, -1};
    double vertical_fov_radians{1.0471975511965976};
    float near_plane{0.01F};
    float far_plane{100000.F};
};
struct renderer_diagnostics {
    std::uint64_t submitted_frames{};
    std::uint64_t completed_timeline{};
    std::size_t resident_splat_count{};
    std::size_t allocated_bytes{};
    std::size_t staging_bytes{};
    std::size_t scratch_bytes{};
    std::uint32_t validation_errors{};
    double last_cpu_frame_ms{};
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
