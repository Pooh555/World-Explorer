#include "world_explorer/application/reconstruction_job.hpp"

#include "job_validation.hpp"
#include "pose_job.hpp"
#include "pose_pipeline.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"

#include <array>
#include <cmath>
#include <set>

namespace world_explorer {
namespace {

std::filesystem::path pose_map_directory(const ReconstructionJob& job) {
    if (!job.m_map_directory.empty()) {
        return job.m_map_directory;
    }
    auto path = job.m_fit_directory;
    path += ".map";
    return path;
}

Result<void> validate_job(const ReconstructionJob& job) {
    if (job.m_video.empty() || job.m_pose_job.empty() || job.m_fit_directory.empty() ||
        job.m_scene_output.empty() || (job.m_steps != 7000 && job.m_steps != 30000) ||
        job.m_max_image_dimension < 64 || job.m_max_image_dimension > 4096 ||
        job.m_max_splats < 4 || job.m_max_splats > 2000000) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid reconstruction paths or resource limits");
    }
    const auto K_OUTPUT = std::filesystem::weakly_canonical(job.m_scene_output);
    const auto K_FIT = std::filesystem::weakly_canonical(job.m_fit_directory);
    const auto K_MAP = std::filesystem::weakly_canonical(pose_map_directory(job));
    // Publication must not overwrite its own inputs, including via an ancestor path.
    const std::array K_INPUTS { std::filesystem::weakly_canonical(job.m_video),
                               std::filesystem::weakly_canonical(job.m_pose_job),
                               K_FIT };
    for (const auto& input : K_INPUTS) {
        const auto K_RELATIVE = input.lexically_relative(K_OUTPUT);
        const auto K_REVERSE = K_OUTPUT.lexically_relative(input);
        if (input == K_OUTPUT || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") ||
            (!K_REVERSE.empty() && *K_REVERSE.begin() != "..")) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Scene output overlaps reconstruction inputs");
        }
    }
    for (const auto& input : { K_INPUTS[0], K_INPUTS[1] }) {
        const auto K_RELATIVE = input.lexically_relative(K_FIT);
        const auto K_REVERSE = K_FIT.lexically_relative(input);
        if (input == K_FIT || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") ||
            (!K_REVERSE.empty() && *K_REVERSE.begin() != "..")) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Fit output overlaps pose or video input");
        }
    }
    for (const auto& input : { K_INPUTS[0], K_INPUTS[1], K_FIT, K_OUTPUT }) {
        const auto K_RELATIVE = input.lexically_relative(K_MAP);
        const auto K_REVERSE = K_MAP.lexically_relative(input);
        if (input == K_MAP || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") ||
            (!K_REVERSE.empty() && *K_REVERSE.begin() != "..")) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Pose map overlaps reconstruction inputs or outputs");
        }
    }
    if (job.m_inspection && (job.m_inspection->m_stage.empty() ||
                             !job_detail::valid_digest(job.m_inspection->m_completion_sha256) ||
                             !job.m_inspection->m_minimum_free_bytes ||
                             job.m_inspection->m_minimum_free_bytes > 32ULL * 1024 * 1024 * 1024)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid retained video inspection selection");
    }
    if (job.m_inspection && job.m_automatic_inspection) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Select one inspection action");
    }
    if (!job.m_fresh_poses && (job.m_automatic_decode || job.m_selected_decode)) {
        return fail(ErrorCode::INVALID_ARGUMENT,
                    "Decode actions require an explicit fresh pose action");
    }
    if (job.m_fresh_poses) {
        const auto& config = *job.m_fresh_poses;
        if (std::filesystem::is_symlink(job.m_pose_job)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Fresh pose job may not be a symlink");
        }
        if ((!job.m_inspection && !job.m_automatic_inspection) || !job.m_decode_stage.empty() ||
            (job.m_automatic_decode.has_value() == job.m_selected_decode.has_value()) ||
            config.m_worker.m_script.empty() || config.m_worker.m_python.empty() ||
            config.m_pose_python.empty() || config.m_worker.m_python.size() > 4096 ||
            config.m_pose_python.size() > 4096 ||
            config.m_worker.m_python.find('\0') != std::string::npos ||
            config.m_pose_python.find('\0') != std::string::npos ||
            !config.m_worker.m_framed_protocol || !config.m_cpu_threads ||
            config.m_cpu_threads > 4 || config.m_journal_bytes.m_bytes < 4uz * 1024 * 1024 ||
            config.m_journal_bytes.m_bytes > 2uz * 1024 * 1024 * 1024 ||
            (config.m_recipe != PoseRecipe::SEQUENTIAL &&
             config.m_recipe != PoseRecipe::EXHAUSTIVE_GUIDED)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid explicit fresh pose action");
        }
        auto plan = std::filesystem::weakly_canonical(job.m_pose_job);
        plan += ".plan";
        std::vector<std::filesystem::path> read_only { job.m_video,
                                                      job.m_request_file,
                                                      job.m_gsplat_source,
                                                      job.m_fitting_worker.m_script,
                                                      config.m_worker.m_script };
        if (std::filesystem::path(config.m_worker.m_python).has_parent_path()) {
            read_only.emplace_back(config.m_worker.m_python);
        }
        if (std::filesystem::path(config.m_pose_python).has_parent_path()) {
            read_only.emplace_back(config.m_pose_python);
        }
        if (job.m_inspection) {
            read_only.push_back(job.m_inspection->m_stage.parent_path().parent_path());
        }
        if (job.m_automatic_inspection) {
            read_only.push_back(job.m_automatic_inspection->m_job_directory);
            read_only.push_back(job.m_automatic_inspection->m_worker.m_script);
            if (std::filesystem::path(job.m_automatic_inspection->m_worker.m_python)
                    .has_parent_path()) {
                read_only.emplace_back(job.m_automatic_inspection->m_worker.m_python);
            }
        }
        if (std::filesystem::path(job.m_fitting_worker.m_python).has_parent_path()) {
            read_only.emplace_back(job.m_fitting_worker.m_python);
        }
        if (job.m_selected_decode) {
            if (!job_detail::valid_digest(job.m_selected_decode->m_completion_sha256) ||
                job.m_selected_decode->m_stage.empty()) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Invalid retained decode selection");
            }
            read_only.push_back(job.m_selected_decode->m_stage.parent_path().parent_path());
        }
        if (job.m_automatic_decode) {
            const auto& decode = *job.m_automatic_decode;
            if (decode.m_job_directory.empty() || decode.m_worker.m_script.empty() ||
                decode.m_worker.m_python.empty() || !decode.m_worker.m_framed_protocol ||
                decode.m_worker.m_python.size() > 4096 ||
                decode.m_worker.m_python.find('\0') != std::string::npos ||
                !std::isfinite(decode.m_sample_fps) || decode.m_sample_fps < .1 ||
                decode.m_sample_fps > 4 || decode.m_max_frames < 3 || decode.m_max_frames > 120 ||
                !decode.m_cpu_threads || decode.m_cpu_threads > 4 || !decode.m_max_output_bytes ||
                decode.m_max_output_bytes > 32ULL * 1024 * 1024 * 1024) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Invalid automatic decode action");
            }
            read_only.push_back(decode.m_worker.m_script);
            if (std::filesystem::path(decode.m_worker.m_python).has_parent_path()) {
                read_only.emplace_back(decode.m_worker.m_python);
            }
        }
        std::vector<std::filesystem::path> writable { K_INPUTS[1], plan, K_MAP, K_FIT, K_OUTPUT };
        if (job.m_automatic_decode) {
            writable.push_back(
                std::filesystem::weakly_canonical(job.m_automatic_decode->m_job_directory));
        }
        const auto K_CHECKPOINT = job.m_checkpoint.empty()
                                      ? std::filesystem::path {}
                                      : std::filesystem::weakly_canonical(job.m_checkpoint);
        for (std::size_t index { 0uz }; index < writable.size(); ++index) {
            for (std::size_t other { index + 1uz }; other < writable.size(); ++other) {
                if (video_detail::overlaps(writable[index], writable[other])) {
                    return fail(ErrorCode::INVALID_ARGUMENT,
                                "Fresh reconstruction jobs/outputs overlap");
                }
            }
            for (const auto& input : read_only) {
                if (!input.empty() &&
                    video_detail::overlaps(writable[index],
                                           std::filesystem::weakly_canonical(input))) {
                    return fail(ErrorCode::INVALID_ARGUMENT,
                                "Fresh reconstruction output overlaps a protected input");
                }
            }
            // Resume reads and later replaces the worker's canonical checkpoint.
            // All other fit files and other writable destinations stay protected.
            if (!K_CHECKPOINT.empty() && video_detail::overlaps(writable[index], K_CHECKPOINT) &&
                !(job.m_action == FitAction::RESUME && writable[index] == K_FIT &&
                  K_CHECKPOINT == K_FIT / "checkpoint.pt")) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Fresh reconstruction output overlaps a protected input");
            }
        }
    }
    if (!job.m_request_file.empty()) {
        for (const auto& destination : { K_FIT, K_MAP, K_OUTPUT }) {
            if (video_detail::overlaps(destination,
                                       std::filesystem::weakly_canonical(job.m_request_file))) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Reconstruction output overlaps its request file");
            }
        }
    }
    if (job.m_automatic_inspection) {
        const auto& config = *job.m_automatic_inspection;
        if (config.m_job_directory.empty() || config.m_worker.m_script.empty() ||
            config.m_worker.m_python.empty() || config.m_worker.m_python.size() > 4096 ||
            config.m_worker.m_python.find('\0') != std::string::npos ||
            !config.m_worker.m_framed_protocol || !config.m_minimum_free_bytes ||
            config.m_minimum_free_bytes > 32ULL * 1024 * 1024 * 1024) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid automatic inspection configuration");
        }
        const auto K_DIRECTORY = std::filesystem::weakly_canonical(config.m_job_directory);
        auto protected_paths = std::vector<std::filesystem::path> { K_INPUTS[0],
                                                                   K_INPUTS[1],
                                                                   K_FIT,
                                                                   K_OUTPUT,
                                                                   K_MAP,
                                                                   job.m_request_file,
                                                                   job.m_gsplat_source,
                                                                   job.m_fitting_worker.m_script,
                                                                   job.m_checkpoint,
                                                                   job.m_decode_stage,
                                                                   config.m_worker.m_script };
        if (std::filesystem::path(config.m_worker.m_python).has_parent_path()) {
            protected_paths.emplace_back(config.m_worker.m_python);
        }
        for (const auto& input : protected_paths) {
            if (!input.empty() &&
                video_detail::overlaps(K_DIRECTORY, std::filesystem::weakly_canonical(input))) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Metadata job overlaps reconstruction inputs/outputs");
            }
        }
    }
    if (job.m_action == FitAction::REUSE_COMPLETED) {
        if (!job_detail::valid_digest(job.m_completed_fit_identity) || !job.m_checkpoint.empty()) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Completed reuse requires a fit identity and no checkpoint");
        }
    } else if (job.m_action == FitAction::START || job.m_action == FitAction::RESUME) {
        if (job.m_gsplat_source.empty() || job.m_fitting_worker.m_script.empty() ||
            job.m_fitting_worker.m_python.empty()) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Fitting requires explicit local backend paths");
        }
        if (std::filesystem::exists(K_FIT / "result.json")) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Completed fit exists; select explicit reuse or a fresh directory");
        }
        if (job.m_action == FitAction::RESUME &&
            !std::filesystem::is_regular_file(job.m_checkpoint)) {
            return fail(ErrorCode::NOT_FOUND, "Resume requires a trusted local checkpoint");
        }
        if (job.m_action == FitAction::START &&
            (!job.m_checkpoint.empty() || std::filesystem::exists(K_FIT / "checkpoint.pt"))) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Checkpoint exists; select explicit resume or a fresh directory");
        }
        if (!job.m_completed_fit_identity.empty()) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Fit identity is only valid for completed reuse");
        }
    } else {
        return fail(ErrorCode::INVALID_ARGUMENT, "Unknown fit action");
    }
    return {};
}

}  // namespace

Result<ReconstructionJob> read_reconstruction_job(const std::filesystem::path& request,
                                                  const std::string& expected_sha256) {
    try {
        if (!expected_sha256.empty() && !job_detail::valid_digest(expected_sha256)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid reviewed request digest");
        }
        const auto K_PARSED = job_detail::read_json(request, 64uz * 1024, expected_sha256);
        if (!K_PARSED) {
            return std::unexpected(K_PARSED.error());
        }
        const auto& value = *K_PARSED;
        const std::set<std::string> K_FIELDS { "schema_version",
                                              "recipe",
                                              "action",
                                              "video",
                                              "pose_job",
                                              "gsplat_source",
                                              "fit_directory",
                                              "scene_output",
                                              "fit_python",
                                              "fit_worker_script",
                                              "steps",
                                              "max_image_dimension",
                                              "max_splats",
                                              "checkpoint",
                                              "completed_fit_identity",
                                              "map_directory",
                                              "decode_stage",
                                              "pose_action",
                                              "inspection",
                                              "decode",
                                              "pose_worker" };
        if (!value.is_object() || !value.at("schema_version").is_number_unsigned() ||
            (value.at("schema_version") != 1 && value.at("schema_version") != 2) ||
            value.at("recipe") != "local_gsplat_v1") {
            return fail(ErrorCode::INVALID_DATA,
                        "Only explicit local_gsplat_v1 development jobs are supported");
        }
        for (const auto& item : value.items()) {
            if (!K_FIELDS.contains(item.key())) {
                return fail(ErrorCode::INVALID_DATA, "Unknown reconstruction field: " + item.key());
            }
        }
        const auto K_BASE = std::filesystem::canonical(request).parent_path();
        const auto K_PATH = [&](const char* name, bool required = true) -> std::filesystem::path {
            if (!required && !value.contains(name)) {
                return {};
            }
            const auto K_TEXT = value.at(name).get<std::string>();
            if (K_TEXT.empty() || K_TEXT.size() > 4096 || K_TEXT.find('\0') != std::string::npos) {
                throw std::invalid_argument("Invalid local path");
            }
            const std::filesystem::path K_SELECTED(K_TEXT);
            return K_SELECTED.is_absolute() ? K_SELECTED : K_BASE / K_SELECTED;
        };
        ReconstructionJob job;
        job.m_request_file = std::filesystem::canonical(request);
        if (value.at("schema_version") == 2) {
            if (value.at("pose_action") != "adopt_completed" &&
                value.at("pose_action") != "solve") {
                return fail(ErrorCode::INVALID_DATA,
                            "Only explicit retained pose adoption is supported");
            }
            const auto& inspection = value.at("inspection");
            if (!inspection.is_object() ||
                !inspection.at("minimum_free_bytes").is_number_unsigned()) {
                return fail(ErrorCode::INVALID_DATA, "Invalid inspection request");
            }
            const auto K_NESTED_PATH = [&](const char* name) {
                const auto K_TEXT = inspection.at(name).get<std::string>();
                if (K_TEXT.empty() || K_TEXT.size() > 4096 ||
                    K_TEXT.find('\0') != std::string::npos) {
                    throw std::invalid_argument("Invalid inspection path");
                }
                const std::filesystem::path K_SELECTED(K_TEXT);
                return K_SELECTED.is_absolute() ? K_SELECTED : K_BASE / K_SELECTED;
            };
            if (inspection.at("action") == "adopt_completed" && inspection.size() == 4) {
                job.m_inspection = RetainedVideoInspection {
                    K_NESTED_PATH("stage"),
                    inspection.at("completion_sha256").get<std::string>(),
                    inspection.at("minimum_free_bytes").get<std::uint64_t>() };
            } else if (inspection.at("action") == "inspect" && inspection.size() == 5) {
                auto python = inspection.at("python").get<std::string>();
                if (std::filesystem::path(python).has_parent_path()) {
                    python = K_NESTED_PATH("python").string();
                }
                job.m_automatic_inspection = AutomaticVideoInspection {
                    K_NESTED_PATH("job_directory"),
                    { K_NESTED_PATH("worker_script"), std::move(python) },
                    inspection.at("minimum_free_bytes").get<std::uint64_t>() };
            } else {
                return fail(ErrorCode::INVALID_DATA, "Unknown or malformed inspection action");
            }
        } else if (value.contains("pose_action") || value.contains("inspection") ||
                   value.contains("decode") || value.contains("pose_worker")) {
            return fail(ErrorCode::INVALID_DATA, "Inspection adoption requires schema 2");
        }
        job.m_video = K_PATH("video");
        job.m_pose_job = K_PATH("pose_job");
        job.m_fit_directory = K_PATH("fit_directory");
        job.m_scene_output = K_PATH("scene_output");
        job.m_map_directory = K_PATH("map_directory", false);
        job.m_decode_stage = K_PATH("decode_stage", false);
        job.m_gsplat_source = K_PATH("gsplat_source", false);
        job.m_fitting_worker.m_script = K_PATH("fit_worker_script", false);
        if (value.contains("fit_python")) {
            job.m_fitting_worker.m_python = value.at("fit_python").get<std::string>();
            if (job.m_fitting_worker.m_python.empty() ||
                job.m_fitting_worker.m_python.size() > 4096 ||
                job.m_fitting_worker.m_python.find('\0') != std::string::npos) {
                return fail(ErrorCode::INVALID_DATA, "Invalid fitting executable");
            }
            if (std::filesystem::path(job.m_fitting_worker.m_python).has_parent_path()) {
                job.m_fitting_worker.m_python = K_PATH("fit_python").string();
            }
        }
        for (const auto* name : { "steps", "max_image_dimension", "max_splats" }) {
            if (!value.at(name).is_number_unsigned() ||
                value.at(name).get<std::uint64_t>() > UINT32_MAX) {
                return fail(ErrorCode::INVALID_DATA, "Invalid integer resource limit");
            }
        }
        job.m_steps = value.at("steps").get<std::uint32_t>();
        job.m_max_image_dimension = value.at("max_image_dimension").get<std::uint32_t>();
        job.m_max_splats = value.at("max_splats").get<std::uint32_t>();
        const auto K_ACTION = value.at("action").get<std::string>();
        if (K_ACTION == "start") {
            job.m_action = FitAction::START;
        } else if (K_ACTION == "resume") {
            job.m_action = FitAction::RESUME;
        } else if (K_ACTION == "reuse_completed") {
            job.m_action = FitAction::REUSE_COMPLETED;
        } else {
            return fail(ErrorCode::INVALID_DATA, "Unknown fit action");
        }
        if (job.m_action != FitAction::REUSE_COMPLETED && !value.contains("fit_python")) {
            return fail(ErrorCode::INVALID_DATA,
                        "Fitting requires an explicit local Python executable");
        }
        job.m_checkpoint = K_PATH("checkpoint", false);
        job.m_completed_fit_identity = value.value("completed_fit_identity", std::string {});
        const auto K_LOCAL_PATH = [&](const nlohmann::json& object, const char* name) {
            const auto K_TEXT = object.at(name).get<std::string>();
            if (K_TEXT.empty() || K_TEXT.size() > 4096 || K_TEXT.find('\0') != std::string::npos) {
                throw std::invalid_argument("Invalid stage path/executable");
            }
            const std::filesystem::path K_SELECTED(K_TEXT);
            return K_SELECTED.is_absolute() ? K_SELECTED : K_BASE / K_SELECTED;
        };
        const auto K_EXECUTABLE = [&](const nlohmann::json& object, const char* name) {
            auto text = object.at(name).get<std::string>();
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos) {
                throw std::invalid_argument("Invalid explicit stage executable");
            }
            return std::filesystem::path(text).has_parent_path()
                       ? K_LOCAL_PATH(object, name).string()
                       : text;
        };
        const auto K_INTEGER =
            [&](const nlohmann::json& object, const char* name, std::uint64_t maximum) {
                const auto& field = object.at(name);
                if (!field.is_number_unsigned() || field.get<std::uint64_t>() > maximum) {
                    throw std::invalid_argument("Invalid stage integer limit");
                }
                return field.get<std::uint64_t>();
            };
        if (value.at("schema_version") == 2 && value.at("pose_action") == "solve") {
            const auto& pose = value.at("pose_worker");
            if (!pose.is_object() || pose.size() != 6) {
                return fail(ErrorCode::INVALID_DATA,
                            "Fresh solving requires explicit local worker/recipe/limits");
            }
            FreshPoseConfiguration config;
            config.m_worker = { K_LOCAL_PATH(pose, "worker_script"),
                               K_EXECUTABLE(pose, "worker_python") };
            config.m_pose_python = K_EXECUTABLE(pose, "pose_python");
            const auto K_RECIPE = pose.at("recipe").get<std::string>();
            if (K_RECIPE != "sequential" && K_RECIPE != "exhaustive_guided") {
                return fail(ErrorCode::INVALID_DATA, "Unknown explicit pose recipe");
            }
            config.m_recipe =
                K_RECIPE == "sequential" ? PoseRecipe::SEQUENTIAL : PoseRecipe::EXHAUSTIVE_GUIDED;
            config.m_cpu_threads = static_cast<std::uint32_t>(K_INTEGER(pose, "cpu_threads", 4));
            config.m_journal_bytes = { static_cast<std::size_t>(
                K_INTEGER(pose, "journal_bytes", 2uz * 1024 * 1024 * 1024)) };
            job.m_fresh_poses = std::move(config);
            const auto& decode = value.at("decode");
            if (!decode.is_object()) {
                return fail(ErrorCode::INVALID_DATA, "Invalid explicit decode action");
            }
            if (decode.at("action") == "adopt_completed" && decode.size() == 3) {
                job.m_selected_decode =
                    RetainedVideoDecode { K_LOCAL_PATH(decode, "stage"),
                                         decode.at("completion_sha256").get<std::string>() };
            } else if (decode.at("action") == "decode" && decode.size() == 8) {
                AutomaticVideoDecode config_decode;
                config_decode.m_job_directory = K_LOCAL_PATH(decode, "job_directory");
                config_decode.m_worker = { K_LOCAL_PATH(decode, "worker_script"),
                                          K_EXECUTABLE(decode, "python") };
                if (!decode.at("sample_fps").is_number()) {
                    return fail(ErrorCode::INVALID_DATA, "Invalid frame selection rate");
                }
                config_decode.m_sample_fps = decode.at("sample_fps").get<double>();
                config_decode.m_max_frames =
                    static_cast<std::uint32_t>(K_INTEGER(decode, "max_frames", 120));
                config_decode.m_cpu_threads =
                    static_cast<std::uint32_t>(K_INTEGER(decode, "cpu_threads", 4));
                config_decode.m_max_output_bytes =
                    K_INTEGER(decode, "max_output_bytes", 32ULL * 1024 * 1024 * 1024);
                job.m_automatic_decode = std::move(config_decode);
            } else {
                return fail(ErrorCode::INVALID_DATA, "Unknown or malformed decode action");
            }
        } else if (value.contains("decode") || value.contains("pose_worker")) {
            return fail(ErrorCode::INVALID_DATA,
                        "Decode/pose worker configuration requires a solve action");
        }
        const auto K_VALID = validate_job(job);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        return job;
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Reconstruction request invalid: " + std::string(exception.what()));
    }
}

Result<std::filesystem::path> reconstruct(const ReconstructionJob& job,
                                          const std::stop_token& stop,
                                          const LogCallback& log,
                                          const ReconstructionProgressCallback& progress) {
    ReconstructionStage stage = ReconstructionStage::VALIDATING;
    const auto K_BEGIN_STAGE = [&](ReconstructionStage next) {
        stage = next;
        if (progress) {
            progress({ stage, {}, {}, 0 });
        }
    };
    const auto K_OBSERVE = [&](const WorkerEvent& event) {
        if (progress) {
            if (const auto K_CHECKED = checked_reconstruction_progress(event, stage, job.m_steps)) {
                progress(*K_CHECKED);
            }
        }
    };
    try {
        K_BEGIN_STAGE(ReconstructionStage::VALIDATING);
        const auto K_VALID = validate_job(job);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Reconstruction cancelled");
        }
        if (log) {
            log("Recipe: local_gsplat_v1 (development; quality unqualified).\n");
        }
        std::optional<VideoPreflightReport> preflight;
        auto selected_inspection = job.m_inspection;
        if (job.m_automatic_inspection) {
            K_BEGIN_STAGE(ReconstructionStage::INSPECTING);
            const auto K_INSPECTED =
                inspect_video({ job.m_video,
                               *job.m_automatic_inspection,
                               { pose_map_directory(job), job.m_fit_directory, job.m_scene_output } },
                              stop,
                              log,
                              K_OBSERVE);
            if (!K_INSPECTED) {
                return std::unexpected(K_INSPECTED.error());
            }
            selected_inspection = K_INSPECTED->m_inspection;
            preflight = K_INSPECTED->m_report;
        } else if (selected_inspection) {
            K_BEGIN_STAGE(ReconstructionStage::INSPECTING);
            const auto K_INSPECTED = preflight_video(
                { job.m_video,
                 *selected_inspection,
                 { pose_map_directory(job), job.m_fit_directory, job.m_scene_output } },
                stop,
                log);
            if (!K_INSPECTED) {
                return std::unexpected(K_INSPECTED.error());
            }
            preflight = *K_INSPECTED;
        }
        const auto K_SOURCE_HASH = preflight
                                       ? Result<std::string>(preflight->m_source_sha256)
                                       : sha256_file(job.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_SOURCE_HASH) {
            return std::unexpected(K_SOURCE_HASH.error());
        }
        std::unique_ptr<FileLock> pose_intent_lock;
        std::optional<DecodedVideo> selected_decode;
        if (job.m_fresh_poses) {
            if (!selected_inspection || !preflight) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Fresh poses require checked video preflight");
            }
            K_BEGIN_STAGE(ReconstructionStage::DECODING);
            Result<DecodedVideo> decoded =
                fail(ErrorCode::INVALID_ARGUMENT, "Missing decode action");
            if (job.m_automatic_decode) {
                decoded = decode_video(
                    { job.m_video,
                     *selected_inspection,
                     *job.m_automatic_decode,
                     { pose_map_directory(job), job.m_fit_directory, job.m_scene_output } },
                    stop,
                    log,
                    K_OBSERVE);
            } else if (job.m_selected_decode) {
                decoded = adopt_video_decode(*job.m_selected_decode, *K_SOURCE_HASH, stop);
            }
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            selected_decode = *decoded;
            K_BEGIN_STAGE(ReconstructionStage::POSES);
            auto ready =
                job_detail::prepare_pose_job(job, *decoded, *preflight, stop, log, K_OBSERVE);
            if (!ready) {
                return std::unexpected(ready.error());
            }
            pose_intent_lock = std::move(*ready);
        }
        if (!job.m_fresh_poses) {
            K_BEGIN_STAGE(ReconstructionStage::POSES);
        }
        if (log) {
            log("Verifying completed poses...\n");
        }
        const auto K_POSES = job_detail::verify_poses(job, *K_SOURCE_HASH, stop);
        if (!K_POSES) {
            return std::unexpected(K_POSES.error());
        }
        K_BEGIN_STAGE(ReconstructionStage::MAP);
        auto prepared = prepare_completed_pose_map(
            { { job.m_pose_job, job.m_video, job.m_decode_stage }, pose_map_directory(job) },
            stop,
            log);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        if (prepared->m_provenance.m_source_sha256 != *K_SOURCE_HASH ||
            prepared->m_provenance.m_pose_completion_sha256 != K_POSES->completion_hash) {
            return fail(ErrorCode::INVALID_DATA, "Pose inputs changed during map preparation");
        }
        if (log) {
            log("Durable pose map ready at epoch 0/revision 1.\n");
        }
        if (job.m_action != FitAction::REUSE_COMPLETED) {
            K_BEGIN_STAGE(ReconstructionStage::FITTING);
            if (log) {
                log("Fitting locally; ETA unknown.\n");
            }
            std::vector<std::string> arguments { "--source",
                                                job.m_gsplat_source.string(),
                                                "--data",
                                                K_POSES->training.string(),
                                                "--video",
                                                job.m_video.string(),
                                                "--output",
                                                job.m_fit_directory.string(),
                                                "--steps",
                                                std::to_string(job.m_steps),
                                                "--max-image-dimension",
                                                std::to_string(job.m_max_image_dimension),
                                                "--max-splats",
                                                std::to_string(job.m_max_splats) };
            if (job.m_action == FitAction::RESUME) {
                arguments.emplace_back("--resume");
                arguments.push_back(job.m_checkpoint.string());
            }
            const auto K_FITTING =
                run_worker(job.m_fitting_worker, arguments, stop, log, K_OBSERVE);
            if (!K_FITTING) {
                return std::unexpected(K_FITTING.error());
            }
            if (*K_FITTING != 0) {
                return fail(ErrorCode::BACKEND_FAILURE,
                            "Fit worker failed with exit code " + std::to_string(*K_FITTING));
            }
        } else if (log) {
            log("Reusing the explicitly selected completed fit; optimization skipped.\n");
        }
        K_BEGIN_STAGE(ReconstructionStage::VERIFYING_FIT);
        const auto K_FINAL_SOURCE = sha256_file(job.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_FINAL_SOURCE) {
            return std::unexpected(K_FINAL_SOURCE.error());
        }
        const auto K_FINAL_POSES = job_detail::verify_poses(job, *K_SOURCE_HASH, stop);
        if (!K_FINAL_POSES) {
            return std::unexpected(K_FINAL_POSES.error());
        }
        if (*K_SOURCE_HASH != *K_FINAL_SOURCE ||
            K_POSES->completion_hash != K_FINAL_POSES->completion_hash) {
            return fail(ErrorCode::INVALID_DATA,
                        "Reconstruction input changed; previous scene retained");
        }
        const auto K_DATASET =
            load_completed_poses({ job.m_pose_job, *K_SOURCE_HASH, job.m_decode_stage }, stop);
        if (!K_DATASET) {
            return std::unexpected(K_DATASET.error());
        }
        const auto K_CHECKED_FIT =
            job_detail::verify_fit(job, *K_POSES, *K_SOURCE_HASH, **K_DATASET);
        if (!K_CHECKED_FIT) {
            return std::unexpected(K_CHECKED_FIT.error());
        }
        const auto K_CANONICAL = pose_pipeline::canonical(**K_DATASET, stop);
        if (!K_CANONICAL) {
            return std::unexpected(K_CANONICAL.error());
        }
        const auto K_BYTES = encode_pose_asset(*K_CANONICAL, {}, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        if (sha256(*K_BYTES) != prepared->m_asset_sha256) {
            return fail(ErrorCode::INVALID_DATA,
                        "Canonical pose dependency changed; previous scene retained");
        }
        const auto K_SNAPSHOT = prepared->m_store->acquire_snapshot();
        FitPoseDependency dependency { prepared->m_asset_sha256,
                                      prepared->m_provenance.m_pose_completion_sha256,
                                      K_SNAPSHOT->m_epoch,
                                      K_SNAPSHOT->m_revision,
                                      {} };
        for (const auto& [K_FRAME, K_POSE] : K_SNAPSHOT->m_poses) {
            (void)K_POSE;
            dependency.m_source_frames.push_back(K_FRAME);
        }
        if (selected_inspection) {
            const auto K_RECHECKED = preflight_video(
                { job.m_video,
                 *selected_inspection,
                 { pose_map_directory(job), job.m_fit_directory, job.m_scene_output } },
                stop);
            if (!K_RECHECKED) {
                return std::unexpected(K_RECHECKED.error());
            }
            if (K_RECHECKED->m_source_sha256 != *K_SOURCE_HASH) {
                return fail(ErrorCode::INVALID_DATA,
                            "Video changed after preflight; previous scene retained");
            }
        }
        if (selected_decode) {
            const auto K_RECHECKED =
                adopt_video_decode(selected_decode->m_selected, *K_SOURCE_HASH, stop);
            if (!K_RECHECKED) {
                return std::unexpected(K_RECHECKED.error());
            }
        }
        K_BEGIN_STAGE(ReconstructionStage::COMPILING);
        if (log) {
            log("Compiling checked fit and publishing scene...\n");
        }
        const auto K_PUBLISHED = compile_fit_scene(
            { .m_input = job.m_fit_directory / "result.json",
             .m_destination = job.m_scene_output,
             .m_stop = stop },
            { *K_SOURCE_HASH,
             *K_CHECKED_FIT,
             std::move(dependency),
             preflight ? preflight->m_inspection_completion_sha256 : std::string {} });
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        if (log) {
            log("Scene published. Scale arbitrary; geometry and walking unavailable.\n");
        }
        return job.m_scene_output;
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Reconstruction failed: " + std::string(exception.what()));
    }
}

}  // namespace world_explorer
