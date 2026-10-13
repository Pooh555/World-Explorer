#ifndef WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#define WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#include "world_explorer/core/math.hpp"
#include "world_explorer/render_core/timing.hpp"
#include <cstdint>
#include <optional>

namespace world_explorer {
struct calibrated_projection {
    pinhole_camera intrinsics; // Canonical integer pixel centers.
    std::uint32_t width{}, height{};
};
// Camera basis is optical (+X right, +Y down, +Z forward), independent of capture poses.
struct camera_view {
    vec3d position{};
    vec3d right{1, 0, 0};
    vec3d down{0, -1, 0};
    vec3d forward{0, 0, -1};
    double vertical_fov_radians{1.0471975511965976};
    float near_plane{0.01F};
    float far_plane{100000.F};
    // Explicit fixed-camera rendering. Dimensions must match the framebuffer;
    // resizing never silently changes calibration. Spectator views omit this.
    std::optional<calibrated_projection> calibration{};
};
// Validate the optical basis/clip planes and return float raster intrinsics.
// Principal points are converted once to Vulkan/gsplat half-integer centers.
result<std::array<float, 4>> raster_intrinsics(const camera_view& camera, std::uint32_t width,
                                               std::uint32_t height);
struct renderer_diagnostics {
    std::uint64_t submitted_frames{};
    std::uint64_t completed_timeline{};
    std::size_t resident_splat_count{};
    std::size_t tile_reference_count{};
    std::size_t allocated_bytes{};
    std::size_t staging_bytes{};
    std::size_t scratch_bytes{};
    std::size_t framebuffer_bytes{};
    std::uint32_t framebuffer_width{}, framebuffer_height{};
    std::uint32_t validation_errors{};
    double last_cpu_frame_ms{};
    // One latest completed sample per frame slot; sequence identifies samples
    // across delayed completion/resize. Empty when timing collection is disabled.
    std::array<renderer_frame_timing, 2> completed_timings{};
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
