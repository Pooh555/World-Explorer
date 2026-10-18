#include "world_explorer/application/dense_inputs.hpp"

#include "job_validation.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/colmap_images.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"

#include <algorithm>
#include <array>
#include <new>

namespace world_explorer {
namespace {

bool close(double first, double second) {
    return std::isfinite(first) && std::isfinite(second) &&
           std::abs(first - second) <= 1e-8 * std::max({1., std::abs(first), std::abs(second)});
}

bool same_pose(const SimilarityTransform& first, const SimilarityTransform& second) {
    if (!close(first.m_scale, second.m_scale)) return false;
    for (const auto K_POINT : {Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 }}) {
        const auto K_A = transform_point(first, K_POINT);
        const auto K_B = transform_point(second, K_POINT);
        if (!close(K_A.m_x, K_B.m_x) || !close(K_A.m_y, K_B.m_y) || !close(K_A.m_z, K_B.m_z))
            return false;
    }
    return true;
}

Result<void> check_png(const std::filesystem::path& file,
                       const std::string& hash,
                       const TrainingCamera& camera,
                       const std::stop_token& stop) {
    const auto K_BYTES = asset_files::read_bytes(file, 64uz * 1024 * 1024, stop);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    constexpr std::array<unsigned char, 16> K_HEADER {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
    if (sha256(*K_BYTES) != hash || K_BYTES->size() < 33 ||
        !std::equal(K_HEADER.begin(), K_HEADER.end(), K_BYTES->begin(), [](auto a, auto b) {
            return a == std::to_integer<unsigned char>(b);
        }))
        return fail(ErrorCode::INVALID_DATA, "Dense image checksum/PNG header differs");
    const auto K_NUMBER = [&](std::size_t offset) {
        std::uint32_t value {};
        for (std::size_t i { offset }; i < offset + 4uz; ++i)
            value = (value << 8U) | std::to_integer<unsigned char>((*K_BYTES)[i]);
        return value;
    };
    if (K_NUMBER(16) != camera.m_width || K_NUMBER(20) != camera.m_height ||
        (*K_BYTES)[24] != std::byte { 8 } || (*K_BYTES)[25] != std::byte { 2 } ||
        (*K_BYTES)[26] != std::byte { 0 } || (*K_BYTES)[27] != std::byte { 0 } ||
        (*K_BYTES)[28] != std::byte { 0 })
        return fail(ErrorCode::INVALID_DATA, "Dense PNG must match native RGB8 calibrated grid");
    // Full pixel decoding belongs to the consuming isolated worker. This checks
    // encoded identity/header, not PNG decompression or photometric correctness.
    return {};
}

bool within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto K_RELATIVE = child.lexically_relative(parent);
    return child == parent || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..");
}

}  // namespace

Result<PreparedDenseInputs> prepare_dense_inputs(const DenseInputRequest& request,
                                                 const std::stop_token& stop) {
    try {
        const auto K_SELECTED_RECIPE =
            asset_files::read_bytes(request.m_depth_recipe, 1024uz * 1024, stop);
        if (!K_SELECTED_RECIPE) return std::unexpected(K_SELECTED_RECIPE.error());
        if (!job_detail::valid_digest(request.m_depth_recipe_sha256) ||
            sha256(*K_SELECTED_RECIPE) != request.m_depth_recipe_sha256)
            return fail(ErrorCode::INVALID_DATA, "Dense depth recipe checksum differs");
        // libc++ JSON adapters require character iterators; preserve the hashed byte extent.
        const auto K_DEPTH_RECIPE = nlohmann::json::parse(
            reinterpret_cast<const char*>(K_SELECTED_RECIPE->data()),
            reinterpret_cast<const char*>(K_SELECTED_RECIPE->data() + K_SELECTED_RECIPE->size()),
            [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 32) throw std::invalid_argument("Dense recipe nesting exceeds profile");
                return true;
            });
        if (K_DEPTH_RECIPE.at("schema_version") != 1 ||
            K_DEPTH_RECIPE.at("input_profile") != "colmap_native_dense_input_v1" ||
            K_DEPTH_RECIPE.at("depth_producer") != "MULTIVIEW_STEREO")
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Unsupported dense depth recipe input profile");
        const auto K_CANONICAL =
            read_pose_asset(request.m_canonical_poses, request.m_pose_sha256, {}, stop);
        if (!K_CANONICAL) return std::unexpected(K_CANONICAL.error());
        const auto K_DATASET = load_completed_poses(request.m_completed, stop);
        if (!K_DATASET) return std::unexpected(K_DATASET.error());
        const auto K_RECONSTRUCTED = pose_pipeline::canonical(**K_DATASET, stop);
        if (!K_RECONSTRUCTED) return std::unexpected(K_RECONSTRUCTED.error());
        const auto K_ENCODED = encode_pose_asset(*K_RECONSTRUCTED, {}, stop);
        if (!K_ENCODED) return std::unexpected(K_ENCODED.error());
        if (sha256(*K_ENCODED) != request.m_pose_sha256)
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Canonical poses differ from retained pose/decode inputs");
        ReconstructionJob job;
        job.m_pose_job = request.m_completed.m_pose_job;
        const auto K_POSES =
            job_detail::verify_poses(job, request.m_completed.m_source_sha256, stop);
        if (!K_POSES) return std::unexpected(K_POSES.error());
        if (K_POSES->completion_hash != (*K_DATASET)->m_pose_completion_sha256)
            return fail(ErrorCode::RESYNC_REQUIRED, "Dense pose stage changed during preparation");
        const auto K_CAMERA =
            read_colmap_training_camera(K_POSES->training / "sparse/cameras.bin",
                                        K_POSES->file_hashes.at("training/sparse/cameras.bin"),
                                        stop);
        if (!K_CAMERA) return std::unexpected(K_CAMERA.error());
        const auto K_IMAGES =
            read_colmap_image_poses(K_POSES->training / "sparse/images.bin",
                                    K_POSES->file_hashes.at("training/sparse/images.bin"),
                                    stop);
        if (!K_IMAGES) return std::unexpected(K_IMAGES.error());
        if (K_IMAGES->size() != (*K_DATASET)->m_training_frames.size())
            return fail(ErrorCode::INVALID_DATA, "Dense model image inventory differs");
        const nlohmann::json K_RECIPE {{"recipe", "colmap_native_dense_input_v1"},
                                       {"crop", "none"},
                                       {"resize", "none"},
                                       {"pixel_centers", "integer"},
                                       {"image_encoding", "PNG_RGB8"}};
        const auto K_RECIPE_BYTES = K_RECIPE.dump();
        const auto K_RECIPE_HASH = sha256(std::as_bytes(std::span(K_RECIPE_BYTES)));
        const auto& source_camera = (*K_DATASET)->m_calibrations.front();
        const nlohmann::json K_SOURCE_CALIBRATION {
            {"calibration", source_camera.m_id.m_value},
            {"width", source_camera.m_width},
            {"height", source_camera.m_height},
            {"model", source_camera.m_model == CalibrationModel::OPENCV ? "OPENCV" : "PINHOLE"},
            {"intrinsics",
             {source_camera.m_camera.m_fx,
              source_camera.m_camera.m_fy,
              source_camera.m_camera.m_cx,
              source_camera.m_camera.m_cy}},
            {"distortion_k1_k2_p1_p2", source_camera.m_distortion},
            {"image_transform", source_camera.m_image_transform}};
        PreparedDenseInputs prepared;
        auto frames = nlohmann::json::array();
        const auto& map = *K_CANONICAL->m_snapshot;
        auto ordered = *K_IMAGES;
        std::ranges::sort(ordered, {}, &ColmapImagePose::m_name);
        for (const auto& image : ordered) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Dense preparation cancelled");
            const auto K_FRAME = (*K_DATASET)->m_training_frames.find(image.m_name);
            if (K_FRAME == (*K_DATASET)->m_training_frames.end() ||
                image.m_camera_id != K_CAMERA->m_backend_camera_id)
                return fail(ErrorCode::INVALID_DATA, "Dense image/calibration identity differs");
            const auto& observation = map.m_observations.at(K_FRAME->second);
            const auto& pose = map.m_poses.at(K_FRAME->second);
            if (map.m_invalidated_frames.contains(K_FRAME->second) ||
                (pose.m_tracking != TrackingState::TRACKING &&
                 pose.m_tracking != TrackingState::FINISHED) ||
                !same_pose(image.m_world_from_camera, pose.m_submap_from_camera))
                return fail(ErrorCode::RESYNC_REQUIRED,
                            "Dense model pose differs from canonical pose");
            const auto K_NAME = "training/images/" + image.m_name;
            const auto K_HASH = K_POSES->file_hashes.at(K_NAME);
            const auto K_PATH = job_detail::contained_file(K_POSES->directory, K_NAME);
            if (!K_PATH) return std::unexpected(K_PATH.error());
            const auto K_PNG = check_png(*K_PATH, K_HASH, *K_CAMERA, stop);
            if (!K_PNG) return std::unexpected(K_PNG.error());
            DepthAssetMetadata metadata;
            metadata.m_dependency = { map.m_epoch,
                                     map.m_revision,
                                     K_FRAME->second,
                                     observation.m_calibration,
                                     request.m_pose_sha256,
                                     observation.m_image_sha256 };
            metadata.m_width = K_CAMERA->m_width;
            metadata.m_height = K_CAMERA->m_height;
            metadata.m_camera = K_CAMERA->m_camera;
            metadata.m_calibrated_image_sha256 = K_HASH;
            metadata.m_recipe_sha256 = request.m_depth_recipe_sha256;
            metadata.m_producer = DepthProducerKind::MULTIVIEW_STEREO;
            prepared.m_frames.push_back({image.m_name, *K_PATH, metadata});
            const auto& transform = pose.m_submap_from_camera;
            const auto& q = transform.m_rotation;
            const auto& t = transform.m_translation;
            frames.push_back({{"frame", K_FRAME->second.m_value},
                              {"calibration", observation.m_calibration.m_value},
                              {"backend_image_id", image.m_image_id},
                              {"backend_camera_id", image.m_camera_id},
                              {"image", K_NAME},
                              {"calibrated_image_sha256", K_HASH},
                              {"observation_image_sha256", observation.m_image_sha256},
                              {"width", metadata.m_width},
                              {"height", metadata.m_height},
                              {"intrinsics",
                               {metadata.m_camera.m_fx,
                                metadata.m_camera.m_fy,
                                metadata.m_camera.m_cx,
                                metadata.m_camera.m_cy}},
                              {"world_from_camera",
                               {{"rotation_wxyz", {q.m_w, q.m_x, q.m_y, q.m_z}},
                                {"translation", {t.m_x, t.m_y, t.m_z}},
                                {"scale", transform.m_scale}}}});
        }
        // Input identities are checked again after assembling the manifest. The
        // caller must recheck before consumption; paths are not immutable leases.
        const auto K_FINAL_POSE = sha256_file(request.m_canonical_poses, 256uz * 1024 * 1024, stop);
        const auto K_FINAL_COMPLETION =
            sha256_file(K_POSES->directory / "completion.json", 16uz * 1024 * 1024, stop);
        if (!K_FINAL_POSE) return std::unexpected(K_FINAL_POSE.error());
        if (!K_FINAL_COMPLETION) return std::unexpected(K_FINAL_COMPLETION.error());
        if (*K_FINAL_POSE != request.m_pose_sha256 ||
            *K_FINAL_COMPLETION != K_POSES->completion_hash)
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Dense dependencies changed during preparation");
        prepared.m_manifest_json =
            nlohmann::json {
                {"schema_version", 1},
                {"kind", "DENSE_INPUTS_NATIVE_GRID"},
                {"scope",
                 "checked input identities and calibration; no depth or geometry produced"},
                {"world_epoch", map.m_epoch.m_value},
                {"pose_revision", map.m_revision.m_value},
                {"source_sha256", request.m_completed.m_source_sha256},
                {"source_calibration", K_SOURCE_CALIBRATION},
                {"pose_asset_sha256", request.m_pose_sha256},
                {"pose_completion_sha256", K_POSES->completion_hash},
                {"decode_completion_sha256", (*K_DATASET)->m_decode_completion_sha256},
                {"cameras_sha256", K_POSES->file_hashes.at("training/sparse/cameras.bin")},
                {"images_sha256", K_POSES->file_hashes.at("training/sparse/images.bin")},
                {"units", "ARBITRARY_POSE_CAMERA_UNITS"},
                {"camera_axes", "+x_right,+y_down,+z_forward"},
                {"depth_convention", "AXIAL_CAMERA_Z"},
                {"preparation_recipe", K_RECIPE},
                {"preparation_recipe_sha256", K_RECIPE_HASH},
                {"depth_recipe_sha256", request.m_depth_recipe_sha256},
                {"frames", frames}}
                .dump(2) +
            "\n";
        return prepared;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Dense preparation allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Dense preparation failed: ") + exception.what());
    }
}

Result<std::string> write_dense_inputs(const DenseInputRequest& request,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop) {
    try {
        if (destination.empty())
            return fail(ErrorCode::INVALID_ARGUMENT, "Dense manifest destination is empty");
        const auto K_OUTPUT = std::filesystem::weakly_canonical(destination);
        for (const auto& input : {request.m_completed.m_pose_job,
                                  request.m_completed.m_decode_stage,
                                  request.m_canonical_poses,
                                  request.m_depth_recipe}) {
            if (!input.empty()) {
                const auto K_SELECTED = std::filesystem::canonical(input);
                if (within(K_OUTPUT, K_SELECTED) || within(K_SELECTED, K_OUTPUT))
                    return fail(ErrorCode::INVALID_ARGUMENT, "Dense manifest overlaps an input");
            }
        }
        // Refuse existing destinations before expensive verification.
        if (std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return fail(ErrorCode::INVALID_ARGUMENT, "Dense manifest destination already exists");
        const auto K_PREPARED = prepare_dense_inputs(request, stop);
        if (!K_PREPARED) return std::unexpected(K_PREPARED.error());
        return asset_files::publish_asset(
            destination, std::as_bytes(std::span(K_PREPARED->m_manifest_json)), stop);
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<PreparedDenseInputs> load_dense_inputs(const DenseInputRequest& request,
                                              const std::filesystem::path& manifest,
                                              const std::string& manifest_sha256,
                                              const std::stop_token& stop) {
    const auto K_BYTES = asset_files::read_bytes(manifest, 1024uz * 1024, stop);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    if (sha256(*K_BYTES) != manifest_sha256)
        return fail(ErrorCode::INVALID_DATA, "Dense manifest checksum differs");
    auto prepared = prepare_dense_inputs(request, stop);
    if (!prepared) return std::unexpected(prepared.error());
    const auto K_EXPECTED = std::as_bytes(std::span(prepared->m_manifest_json));
    if (!std::ranges::equal(*K_BYTES, K_EXPECTED))
        return fail(ErrorCode::RESYNC_REQUIRED, "Dense manifest differs from checked inputs");
    return prepared;
}

}  // namespace world_explorer
