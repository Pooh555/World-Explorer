#ifndef WORLD_EXPLORER_APPLICATION_DENSE_INPUTS_HPP
#define WORLD_EXPLORER_APPLICATION_DENSE_INPUTS_HPP
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include "world_explorer/scene_store/depth_asset.hpp"

namespace world_explorer {

struct DenseInputRequest {
    CompletedPoseConfig m_completed;
    std::filesystem::path m_canonical_poses;
    std::string m_pose_sha256;
    // Exact selected depth-producing recipe bytes. Preparation checks identity
    // and input profile only; it does not qualify or execute that recipe.
    std::filesystem::path m_depth_recipe;
    std::string m_depth_recipe_sha256;
};

struct PreparedDenseFrame {
    std::string m_image_name;
    std::filesystem::path m_calibrated_image;
    DepthAssetMetadata m_expected_depth;
};

struct PreparedDenseInputs {
    std::vector<PreparedDenseFrame> m_frames;
    std::string m_manifest_json;
};

struct ResizedDenseInputRequest {
    DenseInputRequest m_native;
    std::filesystem::path m_depth_recipe;
    std::string m_depth_recipe_sha256;
};

// Read-only checked preparation of the retained native undistorted PNG grid.
// No crop, resize, solve or depth production. Image bytes remain in the committed
// pose stage. Preprocessing and selected depth recipe have separate hashes.
Result<PreparedDenseInputs> prepare_dense_inputs(const DenseInputRequest& request,
                                                 const std::stop_token& stop = {});
// Exclusive immutable manifest publication; destination must be outside inputs.
Result<std::string> write_dense_inputs(const DenseInputRequest& request,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop = {});
// Rechecks independently supplied inputs and compares the pinned manifest with
// recomputed metadata. Never derives a depth expectation from a depth file.
Result<PreparedDenseInputs> load_dense_inputs(const DenseInputRequest& request,
                                              const std::filesystem::path& manifest,
                                              const std::string& manifest_sha256,
                                              const std::stop_token& stop = {});

// Independently rechecks native inputs, selected resize/depth recipe, transformed
// calibration and actual resized PNG hashes/headers. Pixel resampling/decoding
// remains in the isolated preparer/consumer; no backend model is resized here.
Result<PreparedDenseInputs> load_resized_dense_inputs(const ResizedDenseInputRequest& request,
                                                      const std::filesystem::path& manifest,
                                                      const std::string& manifest_sha256,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
