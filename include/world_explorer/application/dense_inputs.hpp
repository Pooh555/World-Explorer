#ifndef WORLD_EXPLORER_APPLICATION_DENSE_INPUTS_HPP
#define WORLD_EXPLORER_APPLICATION_DENSE_INPUTS_HPP
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include "world_explorer/scene_store/depth_asset.hpp"
namespace world_explorer {
struct dense_input_request {
    completed_pose_config completed;
    std::filesystem::path canonical_poses;
    std::string pose_sha256;
    // Exact selected depth-producing recipe bytes. Preparation checks identity
    // and input profile only; it does not qualify or execute that recipe.
    std::filesystem::path depth_recipe;
    std::string depth_recipe_sha256;
};
struct prepared_dense_frame {
    std::string image_name;
    std::filesystem::path calibrated_image;
    depth_asset_metadata expected_depth;
};
struct prepared_dense_inputs {
    std::vector<prepared_dense_frame> frames;
    std::string manifest_json;
};
// Read-only checked preparation of the retained native undistorted PNG grid.
// No crop, resize, solve or depth production. Image bytes remain in the committed
// pose stage. Preprocessing and selected depth recipe have separate hashes.
result<prepared_dense_inputs> prepare_dense_inputs(const dense_input_request& request,
                                                   const std::stop_token& stop = {});
// Exclusive immutable manifest publication; destination must be outside inputs.
result<std::string> write_dense_inputs(const dense_input_request& request,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop = {});
// Rechecks independently supplied inputs and compares the pinned manifest with
// recomputed metadata. Never derives a depth expectation from a depth file.
result<prepared_dense_inputs> load_dense_inputs(const dense_input_request& request,
                                                const std::filesystem::path& manifest,
                                                const std::string& manifest_sha256,
                                                const std::stop_token& stop = {});
} // namespace world_explorer
#endif
