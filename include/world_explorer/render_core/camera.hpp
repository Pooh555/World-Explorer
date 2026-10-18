#ifndef WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#define WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
#include "world_explorer/core/math.hpp"
#include "world_explorer/render_core/timing.hpp"

#include <cstdint>
#include <optional>

namespace world_explorer {

struct CalibratedProjection {
    PinholeCamera m_intrinsics;  // Canonical integer pixel centers.
    std::uint32_t m_width {}, m_height {};
};

// Camera basis is optical (+X right, +Y down, +Z forward), independent of capture poses.
struct CameraView {
    Vec3d m_position {};
    Vec3d m_right { 1, 0, 0 };
    Vec3d m_down { 0, -1, 0 };
    Vec3d m_forward { 0, 0, -1 };
    double m_vertical_fov_radians { 1.0471975511965976 };
    float m_near_plane { 0.01F };
    float m_far_plane { 100000.F };
    // Explicit fixed-camera rendering. Dimensions must match the framebuffer;
    // resizing never silently changes calibration. Spectator views omit this.
    std::optional<CalibratedProjection> m_calibration {};
};

// Validate the optical basis/clip planes and return float raster intrinsics.
// Principal points are converted once to Vulkan/gsplat half-integer centers.
Result<std::array<float, 4>>
raster_intrinsics(const CameraView& camera, std::uint32_t width, std::uint32_t height);

struct RendererDiagnostics {
    std::uint64_t m_submitted_frames {};
    std::uint64_t m_completed_timeline {};
    std::uint64_t m_interactive_device_idle_waits {};
    std::size_t m_resident_splat_count {};
    std::size_t m_tile_reference_count {};
    std::size_t m_allocated_bytes {};
    std::size_t m_staging_bytes {};
    std::size_t m_scratch_bytes {};
    std::size_t m_framebuffer_bytes {};
    std::uint32_t m_framebuffer_width {}, m_framebuffer_height {};
    std::uint32_t m_validation_errors {};
    double m_last_cpu_frame_ms {};
    // One latest completed sample per frame slot; sequence identifies samples
    // across delayed completion/resize. Empty when timing collection is disabled.
    std::array<RendererFrameTiming, 2> m_completed_timings {};
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_RENDER_CORE_CAMERA_HPP
