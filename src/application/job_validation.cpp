#include "job_validation.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"
#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>

namespace world_explorer::job_detail {

Result<nlohmann::json> read_json(const std::filesystem::path& path,
                                 std::size_t maximum,
                                 const std::string& expected_sha256) {
    try {
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maximum)
            return fail(ErrorCode::RESOURCE_EXHAUSTED,
                        "JSON input missing or exceeds budget: " + path.string());
        std::ifstream input(path, std::ios::binary);
        std::string bytes(maximum + 1, '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const auto K_COUNT = static_cast<std::size_t>(input.gcount());
        if (K_COUNT > maximum || !input.eof())
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "JSON input changed or exceeds budget");
        bytes.resize(K_COUNT);
        if (!expected_sha256.empty() && sha256(std::as_bytes(std::span(bytes))) != expected_sha256)
            return fail(ErrorCode::INVALID_DATA, "Job JSON checksum differs from selection");
        return nlohmann::json::parse(
            bytes, [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 64) throw std::invalid_argument("JSON nesting exceeds 64 levels");
                return true;
            });
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, "Invalid job input: " + std::string(exception.what()));
    }
}

bool valid_digest(const std::string& digest) {
    return digest.size() == 64 && std::ranges::all_of(digest, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

Result<std::filesystem::path> contained_file(const std::filesystem::path& root,
                                             const std::string& relative) {
    const std::filesystem::path K_PATH(relative);
    if (K_PATH.empty() || K_PATH.is_absolute() || K_PATH.has_root_name() ||
        relative.find('\0') != std::string::npos)
        return fail(ErrorCode::INVALID_DATA, "Asset path must be relative");
    for (const auto& component : K_PATH)
        if (component == ".." || component == ".")
            return fail(ErrorCode::INVALID_DATA, "Unsafe asset path");
    auto canonical = std::filesystem::canonical(root / K_PATH);
    if (canonical.lexically_relative(root) != K_PATH ||
        !std::filesystem::is_regular_file(canonical))
        return fail(ErrorCode::INVALID_DATA, "Asset path escapes its directory or uses a symlink");
    return canonical;
}

Result<pose_input> verify_poses(const ReconstructionJob& job,
                                const std::string& source_hash,
                                const std::stop_token& stop) {
    const auto K_ROOT = std::filesystem::canonical(job.m_pose_job);
    const auto K_POINTER = read_json(K_ROOT / "poses.json", 1024uz * 1024);
    const auto K_COMPLETED = read_json(K_ROOT / "result.json", 1024uz * 1024);
    if (!K_POINTER) return std::unexpected(K_POINTER.error());
    if (!K_COMPLETED) return std::unexpected(K_COMPLETED.error());
    const auto K_KEY = K_POINTER->at("key").get<std::string>();
    if (!valid_digest(K_KEY) || K_COMPLETED->at("schema_version") != 1 ||
        K_COMPLETED->at("status") != "complete" || K_COMPLETED->at("stage") != "poses" ||
        K_COMPLETED->at("source_sha256") != source_hash)
        return fail(ErrorCode::INVALID_DATA, "Pose completion does not match the source video");
    const auto K_DIRECTORY = std::filesystem::canonical(K_POINTER->at("output").get<std::string>());
    if (K_DIRECTORY != K_ROOT / "stages" / ("poses-" + K_KEY) ||
        std::filesystem::canonical(K_COMPLETED->at("output").get<std::string>()) != K_DIRECTORY)
        return fail(ErrorCode::INVALID_DATA, "Pose pointer escapes its committed stage");
    const auto K_STAGE = verify_committed_stage(K_DIRECTORY, K_KEY, stop);
    if (!K_STAGE) return std::unexpected(K_STAGE.error());
    if (!K_STAGE->m_assets.contains("training/sparse/cameras.bin") ||
        !K_STAGE->m_assets.contains("training/sparse/images.bin") ||
        !K_STAGE->m_assets.contains("training/sparse/points3D.bin"))
        return fail(ErrorCode::INVALID_DATA, "Pose stage lacks the undistorted model");
    // Reject unsupported/malformed calibration before map admission or optimizer launch.
    const auto K_CAMERA =
        read_colmap_training_camera(K_DIRECTORY / "training/sparse/cameras.bin",
                                    K_STAGE->m_assets.at("training/sparse/cameras.bin").m_sha256,
                                    stop);
    if (!K_CAMERA) return std::unexpected(K_CAMERA.error());
    pose_input poses {K_DIRECTORY, K_DIRECTORY / "training", K_STAGE->m_completion_sha256, {}};
    for (const auto& [K_NAME, K_ASSET] : K_STAGE->m_assets)
        poses.file_hashes.emplace(K_NAME, K_ASSET.m_sha256);
    return poses;
}

Result<std::string> verify_fit(const ReconstructionJob& job,
                               const pose_input& poses,
                               const std::string& source_hash,
                               const CompletedPoseDataset& dataset) {
    const auto K_CAMERA = read_colmap_training_camera(
        poses.training / "sparse/cameras.bin", poses.file_hashes.at("training/sparse/cameras.bin"));
    if (!K_CAMERA) return std::unexpected(K_CAMERA.error());
    const auto K_RATIO = std::min(1.,
                                  static_cast<double>(job.m_max_image_dimension) /
                                      std::max(K_CAMERA->m_width, K_CAMERA->m_height));
    // Match Python's nearest-even integer resize instead of ties-away std::round.
    const auto K_ROUNDED = [](double value) {
        auto whole = static_cast<std::uint32_t>(std::floor(value));
        if (value - whole > .5 || (value - whole == .5 && whole % 2 != 0)) ++whole;
        return whole;
    };
    const auto K_WIDTH = K_ROUNDED(K_CAMERA->m_width * K_RATIO);
    const auto K_HEIGHT = K_ROUNDED(K_CAMERA->m_height * K_RATIO);
    const auto K_SCALE_X = static_cast<double>(K_WIDTH) / K_CAMERA->m_width;
    const auto K_SCALE_Y = static_cast<double>(K_HEIGHT) / K_CAMERA->m_height;
    const std::array<std::array<double, 3>, 3> K_INTRINSIC {
        {{K_CAMERA->m_camera.m_fx * K_SCALE_X, 0, (K_CAMERA->m_camera.m_cx + .5) * K_SCALE_X},
         {0, K_CAMERA->m_camera.m_fy * K_SCALE_Y, (K_CAMERA->m_camera.m_cy + .5) * K_SCALE_Y},
         {0, 0, 1}}};
    const auto K_RESULT_FILE = read_json(job.m_fit_directory / "result.json", 1024uz * 1024);
    const auto K_INPUTS = read_json(job.m_fit_directory / "input_manifest.json", 1024uz * 1024);
    if (!K_RESULT_FILE) return std::unexpected(K_RESULT_FILE.error());
    if (!K_INPUTS) return std::unexpected(K_INPUTS.error());
    if (K_RESULT_FILE->at("recipe") != "local_gsplat_v1" ||
        K_INPUTS->at("recipe") != "local_gsplat_v1" ||
        K_RESULT_FILE->at("source_sha256") != source_hash ||
        K_INPUTS->at("source_sha256") != source_hash || K_RESULT_FILE->at("steps") != job.m_steps ||
        K_INPUTS->at("steps") != job.m_steps ||
        K_INPUTS->at("max_image_dimension") != job.m_max_image_dimension ||
        K_INPUTS->at("max_splats") != job.m_max_splats)
        return fail(ErrorCode::INVALID_DATA,
                    "Completed fit differs from the selected source or configuration");
    if (job.m_action == FitAction::REUSE_COMPLETED &&
        K_RESULT_FILE->at("identity") != job.m_completed_fit_identity)
        return fail(ErrorCode::INVALID_DATA,
                    "Completed fit identity differs from the explicitly selected fit");
    const auto& frames = K_INPUTS->at("dataset").at("frames");
    if (!frames.is_array() || frames.empty() || frames.size() > 120 ||
        frames.size() != dataset.m_poses.size())
        return fail(ErrorCode::INVALID_DATA, "Invalid fit frame inventory");
    std::map<FrameId, const PoseEstimate*> registered;
    for (const auto& pose : dataset.m_poses) registered.emplace(pose.m_frame, &pose);
    std::set<std::string> names;
    for (const auto& frame : frames) {
        if (frame.at("original_width") != K_CAMERA->m_width ||
            frame.at("original_height") != K_CAMERA->m_height || frame.at("width") != K_WIDTH ||
            frame.at("height") != K_HEIGHT)
            return fail(ErrorCode::INVALID_DATA,
                        "Fit dimensions differ from undistorted camera/resize");
        const auto K_RECORDED = frame.at("intrinsic").get<std::array<std::array<double, 3>, 3>>();
        for (std::size_t row { 0uz }; row < 3uz; ++row)
            for (std::size_t column { 0uz }; column < 3uz; ++column) {
                const auto K_EXPECTED = K_INTRINSIC[row][column];
                // The fitting dataset stores float32 intrinsics and per-axis resizes.
                const auto K_TOLERANCE =
                    8 * std::numeric_limits<float>::epsilon() * std::max(1., std::abs(K_EXPECTED));
                if (!std::isfinite(K_RECORDED[row][column]) || !std::isfinite(K_EXPECTED) ||
                    std::abs(K_RECORDED[row][column] - K_EXPECTED) > K_TOLERANCE)
                    return fail(ErrorCode::INVALID_DATA,
                                "Fit intrinsics differ from undistorted camera/resize");
            }
        const auto K_NAME = frame.at("name").get<std::string>();
        const auto K_FOUND = poses.file_hashes.find("training/images/" + K_NAME);
        if (!names.insert(K_NAME).second || K_FOUND == poses.file_hashes.end() ||
            K_FOUND->second != frame.at("sha256").get<std::string>())
            return fail(ErrorCode::INVALID_DATA, "Fit images differ from the committed pose stage");
        const auto K_SOURCE_FRAME = dataset.m_training_frames.find(K_NAME);
        if (K_SOURCE_FRAME == dataset.m_training_frames.end() ||
            !registered.contains(K_SOURCE_FRAME->second))
            return fail(ErrorCode::INVALID_DATA, "Fit image has no registered canonical pose");
        const auto& transform = registered.at(K_SOURCE_FRAME->second)->m_submap_from_camera;
        const auto K_Q = transform.m_rotation;
        const Quatd K_INVERSE { K_Q.m_w, -K_Q.m_x, -K_Q.m_y, -K_Q.m_z };
        // Native COLMAP cam_from_world = canonical cam_from_world * Rx(pi).
        const auto K_X = rotate(K_INVERSE, {1, 0, 0});
        const auto K_Y = rotate(K_INVERSE, {0, -1, 0});
        const auto K_Z = rotate(K_INVERSE, {0, 0, -1});
        const auto K_T = rotate(K_INVERSE, transform.m_translation) * -1;
        const std::array<std::array<double, 4>, 4> K_EXPECTED {
            {{K_X.m_x, K_Y.m_x, K_Z.m_x, K_T.m_x},
             {K_X.m_y, K_Y.m_y, K_Z.m_y, K_T.m_y},
             {K_X.m_z, K_Y.m_z, K_Z.m_z, K_T.m_z},
             {0, 0, 0, 1}}};
        const auto K_VIEW = frame.at("view").get<std::array<std::array<double, 4>, 4>>();
        for (std::size_t row { 0uz }; row < 4uz; ++row)
            for (std::size_t column { 0uz }; column < 4uz; ++column)
                if (!std::isfinite(K_VIEW[row][column]) ||
                    std::abs(K_VIEW[row][column] - K_EXPECTED[row][column]) >
                        5e-6 * std::max(1., std::abs(K_EXPECTED[row][column])))
                    return fail(ErrorCode::INVALID_DATA,
                                "Fit cameras differ from the canonical pose dependency");
    }
    auto identity = K_RESULT_FILE->at("identity").get<std::string>();
    if (!valid_digest(identity)) return fail(ErrorCode::INVALID_DATA, "Invalid fit identity");
    return identity;
}

}  // namespace world_explorer::job_detail
