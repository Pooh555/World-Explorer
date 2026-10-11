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
std::filesystem::path stage_path(const std::filesystem::path& root, const char* name,
                                 const nlohmann::json& pointer) {
    const auto key = pointer.at("key").get<std::string>();
    const auto text = pointer.at("output").get<std::string>();
    if (!adapter_detail::valid_digest(key) || text.empty() || text.size() > 4096 ||
        text.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid committed pose/decode pointer");
    const auto directory =
        std::filesystem::canonical(std::filesystem::path(std::u8string(text.begin(), text.end())));
    if (directory != root / "stages" / (std::string(name) + "-" + key))
        throw std::invalid_argument("Committed pose/decode pointer escapes the job");
    return directory;
}
double number(const nlohmann::json& value) {
    if (!value.is_number())
        throw std::invalid_argument("Expected numeric pose/calibration value");
    const auto result = value.get<double>();
    if (!std::isfinite(result))
        throw std::invalid_argument("Nonfinite pose/calibration value");
    return result;
}
template <std::size_t count> std::array<double, count> numbers(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != count)
        throw std::invalid_argument("Invalid pose/calibration vector length");
    std::array<double, count> result;
    for (std::size_t index = 0; index < count; ++index)
        result[index] = number(value[index]);
    return result;
}
} // namespace
result<std::shared_ptr<const completed_pose_dataset>>
load_completed_poses(const completed_pose_config& config, const std::stop_token& stop) {
    try {
        constexpr std::size_t MAX_JSON = std::size_t{1024} * 1024;
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Completed pose import cancelled");
        if (!adapter_detail::valid_digest(config.source_sha256))
            return fail(error_code::INVALID_ARGUMENT,
                        "Completed pose import requires source SHA-256");
        const auto root = std::filesystem::canonical(config.pose_job);
        const auto completion = adapter_detail::read_json(root / "result.json", MAX_JSON);
        if (completion.at("schema_version") != 1 || completion.at("status") != "complete" ||
            completion.at("stage") != "poses" ||
            completion.at("source_sha256") != config.source_sha256)
            return fail(error_code::INVALID_DATA,
                        "Completed poses differ from the selected source");
        const auto pointer = adapter_detail::read_json(root / "poses.json", MAX_JSON);
        const auto directory = stage_path(root, "poses", pointer);
        const auto output = completion.at("output").get<std::string>();
        if (output.size() > 4096 || output.find('\0') != std::string::npos ||
            std::filesystem::canonical(
                std::filesystem::path(std::u8string(output.begin(), output.end()))) != directory)
            return fail(error_code::INVALID_DATA, "Pose completion/pointer mismatch");
        const auto stage =
            verify_committed_stage(directory, pointer.at("key").get<std::string>(), stop);
        if (!stage)
            return std::unexpected(stage.error());
        if (!stage->assets.contains("canonical_poses.json") ||
            !stage->assets.contains("result.json"))
            return fail(error_code::INVALID_DATA, "Pose stage lacks canonical metadata");
        const auto stage_result = adapter_detail::read_json(directory / "result.json", MAX_JSON,
                                                            stage->assets.at("result.json").sha256);
        if (stage_result.at("canonical_poses") != "canonical_poses.json" ||
            stage_result.at("scale_status") != "ARBITRARY" ||
            stage_result.at("geometry") != "UNAVAILABLE")
            return fail(error_code::UNSUPPORTED_OPERATION, "Unsupported completed pose profile");
        auto decode_pointer = nlohmann::json::object();
        if (config.decode_stage.empty()) {
            decode_pointer = adapter_detail::read_json(root / "decode.json", MAX_JSON);
        } else {
            const auto selected = std::filesystem::canonical(config.decode_stage);
            const auto name = selected.filename().string();
            if (!name.starts_with("decode-"))
                return fail(error_code::INVALID_DATA, "Explicit decode stage has invalid basename");
            const auto encoded = selected.generic_u8string();
            decode_pointer = {{"key", name.substr(7)},
                              {"output", std::string(encoded.begin(), encoded.end())}};
        }
        const auto decoded = stage_path(root, "decode", decode_pointer);
        const auto catalog = inspect_decoded_frame_source(
            {.stage_directory = decoded, .source_sha256 = config.source_sha256}, stop);
        if (!catalog)
            return std::unexpected(catalog.error());
        const auto value =
            adapter_detail::read_json(directory / "canonical_poses.json", MAX_JSON,
                                      stage->assets.at("canonical_poses.json").sha256);
        if (value.at("schema_version") != 1 || value.at("epoch") != 0 ||
            value.at("revision") != 1 || value.at("scale_status") != "ARBITRARY" ||
            value.at("gravity") != "unknown" || value.at("world_up") != "+y" ||
            numbers<4>(value.at("backend_world_rotation_wxyz")) != std::array{0., 1., 0., 0.})
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Unsupported canonical coordinate/provenance profile");
        const auto& cameras = value.at("cameras");
        const auto& poses = value.at("poses");
        if (!cameras.is_array() || cameras.size() != 1 || !poses.is_array() || poses.size() < 3 ||
            poses.size() > catalog->observations.size() ||
            poses.size() * 10 < catalog->observations.size() * 9 ||
            adapter_detail::unsigned_value(value.at("registered_frames")) != poses.size() ||
            adapter_detail::unsigned_value(stage_result.at("registered_frames")) != poses.size() ||
            adapter_detail::unsigned_value(value.at("points")) < 10 ||
            number(value.at("mean_reprojection_error")) < 0)
            return fail(error_code::INVALID_DATA,
                        "Disconnected or inconsistent canonical pose inventory");
        auto dataset = std::make_shared<completed_pose_dataset>();
        dataset->epoch = {0};
        dataset->revision = {1};
        dataset->submap = {adapter_detail::identity(config.source_sha256, "submap", "0")};
        dataset->source_sha256 = config.source_sha256;
        dataset->pose_completion_sha256 = stage->completion_sha256;
        dataset->decode_completion_sha256 = catalog->completion_sha256;
        dataset->decode_directory = decoded;
        const auto& camera = cameras[0];
        const auto camera_id = adapter_detail::unsigned_value(camera.at("camera_id"));
        const auto width = adapter_detail::unsigned_value(camera.at("width"));
        const auto height = adapter_detail::unsigned_value(camera.at("height"));
        if (!camera_id || !width || !height || width > 8192 || height > 8192 ||
            camera.at("revision") != 1 || camera.at("backend_params_centers") != "half_pixel" ||
            camera.at("canonical_pixel_centers") != "integer")
            return fail(error_code::INVALID_DATA, "Invalid camera identity/dimensions/centers");
        const auto model = camera.at("model").get<std::string>();
        if (model != "OPENCV" && model != "PINHOLE")
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Unsupported canonical camera model: " + model);
        const auto& matrix = camera.at("calibration_matrix");
        if (!matrix.is_array() || matrix.size() != 3)
            return fail(error_code::INVALID_DATA, "Invalid calibration matrix shape");
        const auto first = numbers<3>(matrix[0]);
        const auto second = numbers<3>(matrix[1]);
        const auto third = numbers<3>(matrix[2]);
        if (first[1] != 0 || second[0] != 0 || third != std::array{0., 0., 1.})
            return fail(error_code::INVALID_DATA, "Unsupported calibration matrix entries");
        calibration_record calibration{catalog->observations.front().calibration,
                                       {first[0], second[1], first[2], second[2]},
                                       static_cast<std::uint32_t>(width),
                                       static_cast<std::uint32_t>(height)};
        const auto& params = camera.at("backend_params");
        if (!params.is_array() || params.size() != (model == "OPENCV" ? 8U : 4U))
            return fail(error_code::INVALID_DATA, "Invalid camera coefficient count");
        if (number(params[0]) != calibration.camera.fx ||
            number(params[1]) != calibration.camera.fy ||
            number(params[2]) - 0.5 != calibration.camera.cx ||
            number(params[3]) - 0.5 != calibration.camera.cy)
            return fail(error_code::INVALID_DATA, "Backend/canonical intrinsic matrix mismatch");
        if (model == "OPENCV") {
            calibration.model = calibration_model::OPENCV;
            for (std::size_t index = 0; index < 4; ++index)
                calibration.distortion[index] = number(params[index + 4]);
        }
        if (!valid(calibration))
            return fail(error_code::INVALID_DATA, "Invalid imported camera calibration");
        dataset->calibrations.push_back(calibration);
        std::map<std::string, const decoded_observation*> frames;
        for (const auto& frame : catalog->observations) {
            if (frame.width != width || frame.height != height ||
                frame.calibration != calibration.id ||
                !frames.emplace(frame.external_frame_id, &frame).second)
                return fail(error_code::INVALID_DATA, "Decode/calibration inventory mismatch");
            const auto hash = sha256_file(frame.image_path, frame.encoded_bytes, stop);
            if (!hash)
                return std::unexpected(hash.error());
            if (*hash != frame.image_sha256 ||
                std::filesystem::file_size(frame.image_path) != frame.encoded_bytes)
                return fail(error_code::INVALID_DATA, "Decoded observation image changed");
            dataset->observations.push_back({frame.id, catalog->capture, frame.calibration,
                                             frame.original_timestamp, frame.capture_time,
                                             frame.image_sha256});
        }
        std::set<frame_id> registered;
        std::set<std::uint64_t> image_ids, rig_frame_ids;
        for (const auto& pose : poses) {
            const auto found = frames.find(pose.at("frame_id").get<std::string>());
            if (found == frames.end())
                return fail(error_code::INVALID_DATA, "Pose references an unknown decoded frame");
            const auto& frame = *found->second;
            const auto image_id = adapter_detail::unsigned_value(pose.at("image_id"));
            const auto rig_frame_id =
                adapter_detail::unsigned_value(pose.at("backend_rig_frame_id"));
            const auto time_base = std::to_string(frame.original_timestamp.time_base_numerator) +
                                   "/" +
                                   std::to_string(frame.original_timestamp.time_base_denominator);
            if (!registered.insert(frame.id).second || !image_id || !rig_frame_id ||
                !image_ids.insert(image_id).second || !rig_frame_ids.insert(rig_frame_id).second ||
                pose.at("calibration_id") != frame.external_calibration_id ||
                adapter_detail::unsigned_value(pose.at("camera_id")) != camera_id ||
                pose.at("pose_revision") != 1 || pose.at("state") != "final" ||
                adapter_detail::unsigned_value(pose.at("pts")) !=
                    static_cast<std::uint64_t>(frame.original_timestamp.pts) ||
                pose.at("time_base") != time_base)
                return fail(error_code::INVALID_DATA, "Pose identity/revision/timestamp mismatch");
            const auto translation = numbers<3>(pose.at("world_from_camera").at("translation"));
            const auto rotation = numbers<4>(pose.at("world_from_camera").at("rotation_wxyz"));
            similarity_transform transform{{rotation[0], rotation[1], rotation[2], rotation[3]},
                                           {translation[0], translation[1], translation[2]},
                                           1};
            if (!valid(transform))
                return fail(error_code::INVALID_DATA, "Invalid world-from-camera transform");
            dataset->poses.push_back({frame.id,
                                      dataset->submap,
                                      transform,
                                      frame.capture_time,
                                      dataset->revision,
                                      tracking_state::TRACKING,
                                      {}});
        }
        std::ranges::sort(dataset->poses, {}, &pose_estimate::capture_time);
        const auto pose_hash =
            sha256_file(directory / "completion.json", std::size_t{16} * MAX_JSON, stop);
        const auto decode_hash = sha256_file(decoded / "completion.json", MAX_JSON, stop);
        const auto canonical_hash = sha256_file(directory / "canonical_poses.json", MAX_JSON, stop);
        if (!pose_hash || !decode_hash || !canonical_hash) {
            if (!pose_hash)
                return std::unexpected(pose_hash.error());
            if (!decode_hash)
                return std::unexpected(decode_hash.error());
            return std::unexpected(canonical_hash.error());
        }
        if (*pose_hash != stage->completion_sha256 || *decode_hash != catalog->completion_sha256 ||
            *canonical_hash != stage->assets.at("canonical_poses.json").sha256)
            return fail(error_code::INVALID_DATA, "Pose/decode metadata changed during import");
        return std::shared_ptr<const completed_pose_dataset>(std::move(dataset));
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Completed pose import allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Completed pose import failed: ") + exception.what());
    }
}
} // namespace world_explorer
