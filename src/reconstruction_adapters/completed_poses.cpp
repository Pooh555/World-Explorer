#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include "artifact_io.hpp"
#include "observation_identity.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"

#include <algorithm>
#include <map>
#include <new>
#include <set>

namespace world_explorer {
namespace {

std::filesystem::path
stage_path(const std::filesystem::path& root, const char* name, const nlohmann::json& pointer) {
    const auto K_KEY = pointer.at("key").get<std::string>();
    const auto K_TEXT = pointer.at("output").get<std::string>();
    if (!adapter_detail::valid_digest(K_KEY) || K_TEXT.empty() || K_TEXT.size() > 4096 ||
        K_TEXT.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid committed pose/decode pointer");
    const auto K_DIRECTORY = std::filesystem::canonical(
        std::filesystem::path(std::u8string(K_TEXT.begin(), K_TEXT.end())));
    if (K_DIRECTORY != root / "stages" / (std::string(name) + "-" + K_KEY))
        throw std::invalid_argument("Committed pose/decode pointer escapes the job");
    return K_DIRECTORY;
}

double number(const nlohmann::json& value) {
    if (!value.is_number()) throw std::invalid_argument("Expected numeric pose/calibration value");
    const auto K_RESULT = value.get<double>();
    if (!std::isfinite(K_RESULT)) throw std::invalid_argument("Nonfinite pose/calibration value");
    return K_RESULT;
}

template <std::size_t Count> std::array<double, Count> numbers(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != Count)
        throw std::invalid_argument("Invalid pose/calibration vector length");
    std::array<double, Count> result;
    for (std::size_t index { 0uz }; index < Count; ++index) result[index] = number(value[index]);
    return result;
}

}  // namespace

Result<std::shared_ptr<const CompletedPoseDataset>>
load_completed_poses(const CompletedPoseConfig& config, const std::stop_token& stop) {
    try {
        constexpr std::size_t K_MAX_JSON { 1024uz * 1024uz };
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Completed pose import cancelled");
        if (!adapter_detail::valid_digest(config.m_source_sha256))
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Completed pose import requires source SHA-256");
        const auto K_ROOT = std::filesystem::canonical(config.m_pose_job);
        const auto K_COMPLETION = adapter_detail::read_json(K_ROOT / "result.json", K_MAX_JSON);
        if (K_COMPLETION.at("schema_version") != 1 || K_COMPLETION.at("status") != "complete" ||
            K_COMPLETION.at("stage") != "poses" ||
            K_COMPLETION.at("source_sha256") != config.m_source_sha256)
            return fail(ErrorCode::INVALID_DATA, "Completed poses differ from the selected source");
        const auto K_POINTER = adapter_detail::read_json(K_ROOT / "poses.json", K_MAX_JSON);
        const auto K_DIRECTORY = stage_path(K_ROOT, "poses", K_POINTER);
        const auto K_OUTPUT = K_COMPLETION.at("output").get<std::string>();
        if (K_OUTPUT.size() > 4096 || K_OUTPUT.find('\0') != std::string::npos ||
            std::filesystem::canonical(std::filesystem::path(
                std::u8string(K_OUTPUT.begin(), K_OUTPUT.end()))) != K_DIRECTORY)
            return fail(ErrorCode::INVALID_DATA, "Pose completion/pointer mismatch");
        const auto K_STAGE =
            verify_committed_stage(K_DIRECTORY, K_POINTER.at("key").get<std::string>(), stop);
        if (!K_STAGE) return std::unexpected(K_STAGE.error());
        if (!K_STAGE->m_assets.contains("canonical_poses.json") ||
            !K_STAGE->m_assets.contains("result.json"))
            return fail(ErrorCode::INVALID_DATA, "Pose stage lacks canonical metadata");
        const auto K_STAGE_RESULT = adapter_detail::read_json(
            K_DIRECTORY / "result.json", K_MAX_JSON, K_STAGE->m_assets.at("result.json").m_sha256);
        if (K_STAGE_RESULT.at("canonical_poses") != "canonical_poses.json" ||
            K_STAGE_RESULT.at("scale_status") != "ARBITRARY" ||
            K_STAGE_RESULT.at("geometry") != "UNAVAILABLE")
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported completed pose profile");
        auto decode_pointer = nlohmann::json::object();
        if (config.m_decode_stage.empty()) {
            decode_pointer = adapter_detail::read_json(K_ROOT / "decode.json", K_MAX_JSON);
        } else {
            const auto K_SELECTED = std::filesystem::canonical(config.m_decode_stage);
            const auto K_NAME = K_SELECTED.filename().string();
            if (!K_NAME.starts_with("decode-"))
                return fail(ErrorCode::INVALID_DATA, "Explicit decode stage has invalid basename");
            const auto K_ENCODED = K_SELECTED.generic_u8string();
            decode_pointer = { { "key", K_NAME.substr(7) },
                              { "output", std::string(K_ENCODED.begin(), K_ENCODED.end()) } };
        }
        const auto K_DECODED = stage_path(K_ROOT, "decode", decode_pointer);
        const auto K_CATALOG = inspect_decoded_frame_source(
            {.m_stage_directory = K_DECODED, .m_source_sha256 = config.m_source_sha256}, stop);
        if (!K_CATALOG) return std::unexpected(K_CATALOG.error());
        if (K_CATALOG->m_journal_sha256) {
            if (!K_COMPLETION.contains("journal_sha256") ||
                K_COMPLETION.at("journal_sha256") != *K_CATALOG->m_journal_sha256)
                return fail(ErrorCode::INVALID_DATA, "Pose/decode journal provenance mismatch");
        } else if (K_COMPLETION.contains("journal_sha256"))
            return fail(ErrorCode::INVALID_DATA, "Pose journal absent from selected decode stage");
        const auto K_VALUE =
            adapter_detail::read_json(K_DIRECTORY / "canonical_poses.json",
                                      K_MAX_JSON,
                                      K_STAGE->m_assets.at("canonical_poses.json").m_sha256);
        if (K_VALUE.at("schema_version") != 1 || K_VALUE.at("epoch") != 0 ||
            K_VALUE.at("revision") != 1 || K_VALUE.at("scale_status") != "ARBITRARY" ||
            K_VALUE.at("gravity") != "unknown" || K_VALUE.at("world_up") != "+y" ||
            numbers<4>(K_VALUE.at("backend_world_rotation_wxyz")) != std::array { 0., 1., 0., 0. })
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Unsupported canonical coordinate/provenance profile");
        const auto& cameras = K_VALUE.at("cameras");
        const auto& poses = K_VALUE.at("poses");
        if (!cameras.is_array() || cameras.size() != 1 || !poses.is_array() || poses.size() < 3 ||
            poses.size() > K_CATALOG->m_observations.size() ||
            poses.size() * 10 < K_CATALOG->m_observations.size() * 9 ||
            adapter_detail::unsigned_value(K_VALUE.at("registered_frames")) != poses.size() ||
            adapter_detail::unsigned_value(K_STAGE_RESULT.at("registered_frames")) !=
                poses.size() ||
            adapter_detail::unsigned_value(K_VALUE.at("points")) < 10 ||
            number(K_VALUE.at("mean_reprojection_error")) < 0)
            return fail(ErrorCode::INVALID_DATA,
                        "Disconnected or inconsistent canonical pose inventory");
        auto dataset = std::make_shared<CompletedPoseDataset>();
        dataset->m_epoch = { 0 };
        dataset->m_revision = { 1 };
        dataset->m_submap = { adapter_detail::identity(config.m_source_sha256, "submap", "0") };
        dataset->m_source_sha256 = config.m_source_sha256;
        dataset->m_pose_completion_sha256 = K_STAGE->m_completion_sha256;
        dataset->m_decode_completion_sha256 = K_CATALOG->m_completion_sha256;
        dataset->m_journal_sha256 = K_CATALOG->m_journal_sha256;
        dataset->m_decode_directory = K_DECODED;
        const auto& camera = cameras[0];
        const auto K_CAMERA_ID = adapter_detail::unsigned_value(camera.at("camera_id"));
        const auto K_WIDTH = adapter_detail::unsigned_value(camera.at("width"));
        const auto K_HEIGHT = adapter_detail::unsigned_value(camera.at("height"));
        if (!K_CAMERA_ID || !K_WIDTH || !K_HEIGHT || K_WIDTH > 8192 || K_HEIGHT > 8192 ||
            camera.at("revision") != 1 || camera.at("backend_params_centers") != "half_pixel" ||
            camera.at("canonical_pixel_centers") != "integer")
            return fail(ErrorCode::INVALID_DATA, "Invalid camera identity/dimensions/centers");
        const auto K_MODEL = camera.at("model").get<std::string>();
        if (K_MODEL != "OPENCV" && K_MODEL != "PINHOLE")
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Unsupported canonical camera model: " + K_MODEL);
        const auto& matrix = camera.at("calibration_matrix");
        if (!matrix.is_array() || matrix.size() != 3)
            return fail(ErrorCode::INVALID_DATA, "Invalid calibration matrix shape");
        const auto K_FIRST = numbers<3>(matrix[0]);
        const auto K_SECOND = numbers<3>(matrix[1]);
        const auto K_THIRD = numbers<3>(matrix[2]);
        if (K_FIRST[1] != 0 || K_SECOND[0] != 0 || K_THIRD != std::array { 0., 0., 1. })
            return fail(ErrorCode::INVALID_DATA, "Unsupported calibration matrix entries");
        CalibrationRecord calibration {K_CATALOG->m_observations.front().m_calibration,
                                       {K_FIRST[0], K_SECOND[1], K_FIRST[2], K_SECOND[2]},
                                       static_cast<std::uint32_t>(K_WIDTH),
                                       static_cast<std::uint32_t>(K_HEIGHT)};
        const auto& params = camera.at("backend_params");
        if (!params.is_array() || params.size() != (K_MODEL == "OPENCV" ? 8U : 4U))
            return fail(ErrorCode::INVALID_DATA, "Invalid camera coefficient count");
        if (number(params[0]) != calibration.m_camera.m_fx ||
            number(params[1]) != calibration.m_camera.m_fy ||
            number(params[2]) - 0.5 != calibration.m_camera.m_cx ||
            number(params[3]) - 0.5 != calibration.m_camera.m_cy)
            return fail(ErrorCode::INVALID_DATA, "Backend/canonical intrinsic matrix mismatch");
        if (K_MODEL == "OPENCV") {
            calibration.m_model = CalibrationModel::OPENCV;
            for (std::size_t index { 0uz }; index < 4uz; ++index)
                calibration.m_distortion[index] = number(params[index + 4]);
        }
        if (!valid(calibration))
            return fail(ErrorCode::INVALID_DATA, "Invalid imported camera calibration");
        dataset->m_calibrations.push_back(calibration);
        std::map<std::string, const DecodedObservation*> frames;
        for (const auto& frame : K_CATALOG->m_observations) {
            if (frame.m_width != K_WIDTH || frame.m_height != K_HEIGHT ||
                frame.m_calibration != calibration.m_id ||
                !frames.emplace(frame.m_external_frame_id, &frame).second)
                return fail(ErrorCode::INVALID_DATA, "Decode/calibration inventory mismatch");
            const auto K_HASH = sha256_file(frame.m_image_path, frame.m_encoded_bytes, stop);
            if (!K_HASH) return std::unexpected(K_HASH.error());
            if (*K_HASH != frame.m_image_sha256 ||
                std::filesystem::file_size(frame.m_image_path) != frame.m_encoded_bytes)
                return fail(ErrorCode::INVALID_DATA, "Decoded observation image changed");
            dataset->m_observations.push_back({frame.m_id,
                                               K_CATALOG->m_capture,
                                               frame.m_calibration,
                                               frame.m_original_timestamp,
                                               frame.m_capture_time,
                                               frame.m_image_sha256});
        }
        std::set<FrameId> registered;
        std::set<std::uint64_t> image_ids, rig_frame_ids;
        for (const auto& pose : poses) {
            const auto K_FOUND = frames.find(pose.at("frame_id").get<std::string>());
            if (K_FOUND == frames.end())
                return fail(ErrorCode::INVALID_DATA, "Pose references an unknown decoded frame");
            const auto& frame = *K_FOUND->second;
            const auto K_IMAGE_ID = adapter_detail::unsigned_value(pose.at("image_id"));
            const auto K_RIG_FRAME_ID =
                adapter_detail::unsigned_value(pose.at("backend_rig_frame_id"));
            const auto K_TIME_BASE =
                std::to_string(frame.m_original_timestamp.m_time_base_numerator) + "/" +
                std::to_string(frame.m_original_timestamp.m_time_base_denominator);
            if (!registered.insert(frame.m_id).second || !K_IMAGE_ID || !K_RIG_FRAME_ID ||
                !image_ids.insert(K_IMAGE_ID).second ||
                !rig_frame_ids.insert(K_RIG_FRAME_ID).second ||
                pose.at("calibration_id") != frame.m_external_calibration_id ||
                adapter_detail::unsigned_value(pose.at("camera_id")) != K_CAMERA_ID ||
                pose.at("pose_revision") != 1 || pose.at("state") != "final" ||
                adapter_detail::unsigned_value(pose.at("pts")) !=
                    static_cast<std::uint64_t>(frame.m_original_timestamp.m_pts) ||
                pose.at("time_base") != K_TIME_BASE)
                return fail(ErrorCode::INVALID_DATA, "Pose identity/revision/timestamp mismatch");
            const auto K_TRANSLATION = numbers<3>(pose.at("world_from_camera").at("translation"));
            const auto K_ROTATION = numbers<4>(pose.at("world_from_camera").at("rotation_wxyz"));
            SimilarityTransform transform {
                {K_ROTATION[0], K_ROTATION[1], K_ROTATION[2], K_ROTATION[3]},
                {K_TRANSLATION[0], K_TRANSLATION[1], K_TRANSLATION[2]},
                1};
            if (!valid(transform))
                return fail(ErrorCode::INVALID_DATA, "Invalid world-from-camera transform");
            dataset->m_poses.push_back({frame.m_id,
                                        dataset->m_submap,
                                        transform,
                                        frame.m_capture_time,
                                        dataset->m_revision,
                                        TrackingState::TRACKING,
                                        {}});
            if (!dataset->m_training_frames
                     .emplace(frame.m_image_path.filename().string(), frame.m_id)
                     .second)
                return fail(ErrorCode::INVALID_DATA, "Duplicate training image name");
        }
        std::ranges::sort(dataset->m_poses, {}, &PoseEstimate::m_capture_time);
        const auto K_POSE_HASH =
            sha256_file(K_DIRECTORY / "completion.json", 16uz * K_MAX_JSON, stop);
        const auto K_DECODE_HASH = sha256_file(K_DECODED / "completion.json", K_MAX_JSON, stop);
        const auto K_CANONICAL_HASH =
            sha256_file(K_DIRECTORY / "canonical_poses.json", K_MAX_JSON, stop);
        if (!K_POSE_HASH || !K_DECODE_HASH || !K_CANONICAL_HASH) {
            if (!K_POSE_HASH) return std::unexpected(K_POSE_HASH.error());
            if (!K_DECODE_HASH) return std::unexpected(K_DECODE_HASH.error());
            return std::unexpected(K_CANONICAL_HASH.error());
        }
        if (*K_POSE_HASH != K_STAGE->m_completion_sha256 ||
            *K_DECODE_HASH != K_CATALOG->m_completion_sha256 ||
            *K_CANONICAL_HASH != K_STAGE->m_assets.at("canonical_poses.json").m_sha256)
            return fail(ErrorCode::INVALID_DATA, "Pose/decode metadata changed during import");
        return std::shared_ptr<const CompletedPoseDataset>(std::move(dataset));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Completed pose import allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Completed pose import failed: ") + exception.what());
    }
}

}  // namespace world_explorer
