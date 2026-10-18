#ifndef WORLD_EXPLORER_APPLICATION_SCENES_HPP
#define WORLD_EXPLORER_APPLICATION_SCENES_HPP
#include "world_explorer/navigation/fly_camera.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <optional>
#include <stop_token>

namespace world_explorer {

// Synthetic diagnostic geometry is never presented as a reconstructed capture.
SceneSnapshot make_diagnostic_scene();

struct SceneImportRequest {
    std::filesystem::path m_input;
    std::filesystem::path m_destination;
    std::stop_token m_stop {};
};

Result<void> compile_splat_scene(const SceneImportRequest& request);

// Validates a local fit completion manifest and binds the immutable PLY checksum
// and reconstruction provenance to the resulting scene. It grants no quality certification.
struct FitPoseDependency {
    std::string m_asset_sha256;
    std::string m_pose_completion_sha256;
    WorldEpoch m_epoch;
    MapRevisionId m_revision;
    // Conservative dependency on the whole registered initial solve.
    std::vector<FrameId> m_source_frames;
};

struct FitImportExpectation {
    std::string m_capture_sha256;
    std::string m_fit_identity;
    std::optional<FitPoseDependency> m_poses {};
    std::string m_inspection_completion_sha256 {};
};

Result<void> compile_fit_scene(const SceneImportRequest& request,
                               const FitImportExpectation& expectation = {});

struct SourceInspectionView {
    FlyCamera m_camera;
    double m_vertical_fov_radians {};
    std::string m_frame_name;
};

// An explicit spectator reset using a recorded capture up hint when available.
// Gravity remains unknown. This is not a calibrated render or capture tracker.
Result<SourceInspectionView> source_view(const SceneState& scene);
Bounds3d world_bounds(const SceneState& scene);

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_APPLICATION_SCENES_HPP
