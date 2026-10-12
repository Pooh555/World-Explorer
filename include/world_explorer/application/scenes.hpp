#ifndef WORLD_EXPLORER_APPLICATION_SCENES_HPP
#define WORLD_EXPLORER_APPLICATION_SCENES_HPP
#include "world_explorer/navigation/fly_camera.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <optional>
#include <stop_token>
namespace world_explorer {
// Synthetic diagnostic geometry is never presented as a reconstructed capture.
scene_snapshot make_diagnostic_scene();
struct scene_import_request {
    std::filesystem::path input;
    std::filesystem::path destination;
    std::stop_token stop{};
};
result<void> compile_splat_scene(const scene_import_request& request);
// Validates a local fit completion manifest and binds the immutable PLY checksum
// and reconstruction provenance to the resulting scene. It grants no quality certification.
struct fit_pose_dependency {
    std::string asset_sha256;
    std::string pose_completion_sha256;
    world_epoch epoch;
    map_revision_id revision;
    // Conservative dependency on the whole registered initial solve.
    std::vector<frame_id> source_frames;
};
struct fit_import_expectation {
    std::string capture_sha256;
    std::string fit_identity;
    std::optional<fit_pose_dependency> poses{};
};
result<void> compile_fit_scene(const scene_import_request& request,
                               const fit_import_expectation& expectation = {});
struct source_inspection_view {
    fly_camera camera;
    double vertical_fov_radians{};
    std::string frame_name;
};
// An explicit spectator reset toward a recorded source view, with world-up roll
// removed. This is not a calibrated reference render or a reconstruction tracker.
result<source_inspection_view> source_view(const scene_state& scene);
bounds3d world_bounds(const scene_state& scene);
} // namespace world_explorer

#endif // WORLD_EXPLORER_APPLICATION_SCENES_HPP
