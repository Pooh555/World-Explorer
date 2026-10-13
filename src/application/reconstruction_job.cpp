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
std::filesystem::path pose_map_directory(const reconstruction_job& job) {
    if (!job.map_directory.empty())
        return job.map_directory;
    auto path = job.fit_directory;
    path += ".map";
    return path;
}
result<void> validate_job(const reconstruction_job& job) {
    if (job.video.empty() || job.pose_job.empty() || job.fit_directory.empty() ||
        job.scene_output.empty() || (job.steps != 7000 && job.steps != 30000) ||
        job.max_image_dimension < 64 || job.max_image_dimension > 4096 || job.max_splats < 4 ||
        job.max_splats > 2000000)
        return fail(error_code::INVALID_ARGUMENT,
                    "Invalid reconstruction paths or resource limits");
    const auto output = std::filesystem::weakly_canonical(job.scene_output);
    const auto fit = std::filesystem::weakly_canonical(job.fit_directory);
    const auto map = std::filesystem::weakly_canonical(pose_map_directory(job));
    // Publication must not overwrite its own inputs, including via an ancestor path.
    const std::array inputs{std::filesystem::weakly_canonical(job.video),
                            std::filesystem::weakly_canonical(job.pose_job), fit};
    for (const auto& input : inputs) {
        const auto relative = input.lexically_relative(output);
        const auto reverse = output.lexically_relative(input);
        if (input == output || (!relative.empty() && *relative.begin() != "..") ||
            (!reverse.empty() && *reverse.begin() != ".."))
            return fail(error_code::INVALID_ARGUMENT,
                        "Scene output overlaps reconstruction inputs");
    }
    for (const auto& input : {inputs[0], inputs[1]}) {
        const auto relative = input.lexically_relative(fit);
        const auto reverse = fit.lexically_relative(input);
        if (input == fit || (!relative.empty() && *relative.begin() != "..") ||
            (!reverse.empty() && *reverse.begin() != ".."))
            return fail(error_code::INVALID_ARGUMENT, "Fit output overlaps pose or video input");
    }
    for (const auto& input : {inputs[0], inputs[1], fit, output}) {
        const auto relative = input.lexically_relative(map);
        const auto reverse = map.lexically_relative(input);
        if (input == map || (!relative.empty() && *relative.begin() != "..") ||
            (!reverse.empty() && *reverse.begin() != ".."))
            return fail(error_code::INVALID_ARGUMENT,
                        "Pose map overlaps reconstruction inputs or outputs");
    }
    if (job.inspection && (job.inspection->stage.empty() ||
                           !job_detail::valid_digest(job.inspection->completion_sha256) ||
                           !job.inspection->minimum_free_bytes ||
                           job.inspection->minimum_free_bytes > 32ULL * 1024 * 1024 * 1024))
        return fail(error_code::INVALID_ARGUMENT, "Invalid retained video inspection selection");
    if (job.inspection && job.automatic_inspection)
        return fail(error_code::INVALID_ARGUMENT, "Select one inspection action");
    if (!job.fresh_poses && (job.automatic_decode || job.selected_decode))
        return fail(error_code::INVALID_ARGUMENT,
                    "Decode actions require an explicit fresh pose action");
    if (job.fresh_poses) {
        const auto& config = *job.fresh_poses;
        if (std::filesystem::is_symlink(job.pose_job))
            return fail(error_code::INVALID_ARGUMENT, "Fresh pose job may not be a symlink");
        if ((!job.inspection && !job.automatic_inspection) || !job.decode_stage.empty() ||
            (job.automatic_decode.has_value() == job.selected_decode.has_value()) ||
            config.worker.script.empty() || config.worker.python.empty() ||
            config.pose_python.empty() || config.worker.python.size() > 4096 ||
            config.pose_python.size() > 4096 ||
            config.worker.python.find('\0') != std::string::npos ||
            config.pose_python.find('\0') != std::string::npos || !config.worker.framed_protocol ||
            !config.cpu_threads || config.cpu_threads > 4 ||
            config.journal_bytes.bytes < std::size_t{4} * 1024 * 1024 ||
            config.journal_bytes.bytes > std::size_t{2} * 1024 * 1024 * 1024 ||
            (config.recipe != pose_recipe::SEQUENTIAL &&
             config.recipe != pose_recipe::EXHAUSTIVE_GUIDED))
            return fail(error_code::INVALID_ARGUMENT, "Invalid explicit fresh pose action");
        auto plan = std::filesystem::weakly_canonical(job.pose_job);
        plan += ".plan";
        std::vector<std::filesystem::path> read_only{job.video, job.request_file, job.gsplat_source,
                                                     job.fitting_worker.script,
                                                     config.worker.script};
        if (std::filesystem::path(config.worker.python).has_parent_path())
            read_only.emplace_back(config.worker.python);
        if (std::filesystem::path(config.pose_python).has_parent_path())
            read_only.emplace_back(config.pose_python);
        if (job.inspection)
            read_only.push_back(job.inspection->stage.parent_path().parent_path());
        if (job.automatic_inspection) {
            read_only.push_back(job.automatic_inspection->job_directory);
            read_only.push_back(job.automatic_inspection->worker.script);
            if (std::filesystem::path(job.automatic_inspection->worker.python).has_parent_path())
                read_only.emplace_back(job.automatic_inspection->worker.python);
        }
        if (std::filesystem::path(job.fitting_worker.python).has_parent_path())
            read_only.emplace_back(job.fitting_worker.python);
        if (job.selected_decode) {
            if (!job_detail::valid_digest(job.selected_decode->completion_sha256) ||
                job.selected_decode->stage.empty())
                return fail(error_code::INVALID_ARGUMENT, "Invalid retained decode selection");
            read_only.push_back(job.selected_decode->stage.parent_path().parent_path());
        }
        if (job.automatic_decode) {
            const auto& decode = *job.automatic_decode;
            if (decode.job_directory.empty() || decode.worker.script.empty() ||
                decode.worker.python.empty() || !decode.worker.framed_protocol ||
                decode.worker.python.size() > 4096 ||
                decode.worker.python.find('\0') != std::string::npos ||
                !std::isfinite(decode.sample_fps) || decode.sample_fps < .1 ||
                decode.sample_fps > 4 || decode.max_frames < 3 || decode.max_frames > 120 ||
                !decode.cpu_threads || decode.cpu_threads > 4 || !decode.max_output_bytes ||
                decode.max_output_bytes > 32ULL * 1024 * 1024 * 1024)
                return fail(error_code::INVALID_ARGUMENT, "Invalid automatic decode action");
            read_only.push_back(decode.worker.script);
            if (std::filesystem::path(decode.worker.python).has_parent_path())
                read_only.emplace_back(decode.worker.python);
        }
        std::vector<std::filesystem::path> writable{inputs[1], plan, map, fit, output};
        if (job.automatic_decode)
            writable.push_back(
                std::filesystem::weakly_canonical(job.automatic_decode->job_directory));
        const auto checkpoint = job.checkpoint.empty()
                                    ? std::filesystem::path{}
                                    : std::filesystem::weakly_canonical(job.checkpoint);
        for (std::size_t index = 0; index < writable.size(); ++index) {
            for (std::size_t other = index + 1; other < writable.size(); ++other)
                if (video_detail::overlaps(writable[index], writable[other]))
                    return fail(error_code::INVALID_ARGUMENT,
                                "Fresh reconstruction jobs/outputs overlap");
            for (const auto& input : read_only)
                if (!input.empty() &&
                    video_detail::overlaps(writable[index],
                                           std::filesystem::weakly_canonical(input)))
                    return fail(error_code::INVALID_ARGUMENT,
                                "Fresh reconstruction output overlaps a protected input");
            // Resume reads and later replaces the worker's canonical checkpoint.
            // All other fit files and other writable destinations stay protected.
            if (!checkpoint.empty() && video_detail::overlaps(writable[index], checkpoint) &&
                !(job.action == fit_action::RESUME && writable[index] == fit &&
                  checkpoint == fit / "checkpoint.pt"))
                return fail(error_code::INVALID_ARGUMENT,
                            "Fresh reconstruction output overlaps a protected input");
        }
    }
    if (!job.request_file.empty())
        for (const auto& destination : {fit, map, output})
            if (video_detail::overlaps(destination,
                                       std::filesystem::weakly_canonical(job.request_file)))
                return fail(error_code::INVALID_ARGUMENT,
                            "Reconstruction output overlaps its request file");
    if (job.automatic_inspection) {
        const auto& config = *job.automatic_inspection;
        if (config.job_directory.empty() || config.worker.script.empty() ||
            config.worker.python.empty() || config.worker.python.size() > 4096 ||
            config.worker.python.find('\0') != std::string::npos ||
            !config.worker.framed_protocol || !config.minimum_free_bytes ||
            config.minimum_free_bytes > 32ULL * 1024 * 1024 * 1024)
            return fail(error_code::INVALID_ARGUMENT, "Invalid automatic inspection configuration");
        const auto directory = std::filesystem::weakly_canonical(config.job_directory);
        auto protected_paths = std::vector<std::filesystem::path>{inputs[0],
                                                                  inputs[1],
                                                                  fit,
                                                                  output,
                                                                  map,
                                                                  job.request_file,
                                                                  job.gsplat_source,
                                                                  job.fitting_worker.script,
                                                                  job.checkpoint,
                                                                  job.decode_stage,
                                                                  config.worker.script};
        if (std::filesystem::path(config.worker.python).has_parent_path())
            protected_paths.emplace_back(config.worker.python);
        for (const auto& input : protected_paths)
            if (!input.empty() &&
                video_detail::overlaps(directory, std::filesystem::weakly_canonical(input)))
                return fail(error_code::INVALID_ARGUMENT,
                            "Metadata job overlaps reconstruction inputs/outputs");
    }
    if (job.action == fit_action::REUSE_COMPLETED) {
        if (!job_detail::valid_digest(job.completed_fit_identity) || !job.checkpoint.empty())
            return fail(error_code::INVALID_ARGUMENT,
                        "Completed reuse requires a fit identity and no checkpoint");
    } else if (job.action == fit_action::START || job.action == fit_action::RESUME) {
        if (job.gsplat_source.empty() || job.fitting_worker.script.empty() ||
            job.fitting_worker.python.empty())
            return fail(error_code::INVALID_ARGUMENT,
                        "Fitting requires explicit local backend paths");
        if (std::filesystem::exists(fit / "result.json"))
            return fail(error_code::INVALID_ARGUMENT,
                        "Completed fit exists; select explicit reuse or a fresh directory");
        if (job.action == fit_action::RESUME && !std::filesystem::is_regular_file(job.checkpoint))
            return fail(error_code::NOT_FOUND, "Resume requires a trusted local checkpoint");
        if (job.action == fit_action::START &&
            (!job.checkpoint.empty() || std::filesystem::exists(fit / "checkpoint.pt")))
            return fail(error_code::INVALID_ARGUMENT,
                        "Checkpoint exists; select explicit resume or a fresh directory");
        if (!job.completed_fit_identity.empty())
            return fail(error_code::INVALID_ARGUMENT,
                        "Fit identity is only valid for completed reuse");
    } else
        return fail(error_code::INVALID_ARGUMENT, "Unknown fit action");
    return {};
}
} // namespace

result<reconstruction_job> read_reconstruction_job(const std::filesystem::path& request) {
    try {
        const auto parsed = job_detail::read_json(request, std::size_t{64} * 1024);
        if (!parsed)
            return std::unexpected(parsed.error());
        const auto& value = *parsed;
        const std::set<std::string> fields{"schema_version",
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
                                           "pose_worker"};
        if (!value.is_object() || !value.at("schema_version").is_number_unsigned() ||
            (value.at("schema_version") != 1 && value.at("schema_version") != 2) ||
            value.at("recipe") != "local_gsplat_v1")
            return fail(error_code::INVALID_DATA,
                        "Only explicit local_gsplat_v1 development jobs are supported");
        for (const auto& item : value.items())
            if (!fields.contains(item.key()))
                return fail(error_code::INVALID_DATA,
                            "Unknown reconstruction field: " + item.key());
        const auto base = std::filesystem::canonical(request).parent_path();
        const auto path = [&](const char* name, bool required = true) -> std::filesystem::path {
            if (!required && !value.contains(name))
                return {};
            const auto text = value.at(name).get<std::string>();
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid local path");
            const std::filesystem::path selected(text);
            return selected.is_absolute() ? selected : base / selected;
        };
        reconstruction_job job;
        job.request_file = std::filesystem::canonical(request);
        if (value.at("schema_version") == 2) {
            if (value.at("pose_action") != "adopt_completed" && value.at("pose_action") != "solve")
                return fail(error_code::INVALID_DATA,
                            "Only explicit retained pose adoption is supported");
            const auto& inspection = value.at("inspection");
            if (!inspection.is_object() ||
                !inspection.at("minimum_free_bytes").is_number_unsigned())
                return fail(error_code::INVALID_DATA, "Invalid inspection request");
            const auto nested_path = [&](const char* name) {
                const auto text = inspection.at(name).get<std::string>();
                if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos)
                    throw std::invalid_argument("Invalid inspection path");
                const std::filesystem::path selected(text);
                return selected.is_absolute() ? selected : base / selected;
            };
            if (inspection.at("action") == "adopt_completed" && inspection.size() == 4)
                job.inspection = retained_video_inspection{
                    nested_path("stage"), inspection.at("completion_sha256").get<std::string>(),
                    inspection.at("minimum_free_bytes").get<std::uint64_t>()};
            else if (inspection.at("action") == "inspect" && inspection.size() == 5) {
                auto python = inspection.at("python").get<std::string>();
                if (std::filesystem::path(python).has_parent_path())
                    python = nested_path("python").string();
                job.automatic_inspection = automatic_video_inspection{
                    nested_path("job_directory"),
                    {nested_path("worker_script"), std::move(python)},
                    inspection.at("minimum_free_bytes").get<std::uint64_t>()};
            } else
                return fail(error_code::INVALID_DATA, "Unknown or malformed inspection action");
        } else if (value.contains("pose_action") || value.contains("inspection") ||
                   value.contains("decode") || value.contains("pose_worker"))
            return fail(error_code::INVALID_DATA, "Inspection adoption requires schema 2");
        job.video = path("video");
        job.pose_job = path("pose_job");
        job.fit_directory = path("fit_directory");
        job.scene_output = path("scene_output");
        job.map_directory = path("map_directory", false);
        job.decode_stage = path("decode_stage", false);
        job.gsplat_source = path("gsplat_source", false);
        job.fitting_worker.script = path("fit_worker_script", false);
        if (value.contains("fit_python")) {
            job.fitting_worker.python = value.at("fit_python").get<std::string>();
            if (job.fitting_worker.python.empty() || job.fitting_worker.python.size() > 4096 ||
                job.fitting_worker.python.find('\0') != std::string::npos)
                return fail(error_code::INVALID_DATA, "Invalid fitting executable");
            if (std::filesystem::path(job.fitting_worker.python).has_parent_path())
                job.fitting_worker.python = path("fit_python").string();
        }
        for (const auto* name : {"steps", "max_image_dimension", "max_splats"})
            if (!value.at(name).is_number_unsigned() ||
                value.at(name).get<std::uint64_t>() > UINT32_MAX)
                return fail(error_code::INVALID_DATA, "Invalid integer resource limit");
        job.steps = value.at("steps").get<std::uint32_t>();
        job.max_image_dimension = value.at("max_image_dimension").get<std::uint32_t>();
        job.max_splats = value.at("max_splats").get<std::uint32_t>();
        const auto action = value.at("action").get<std::string>();
        if (action == "start")
            job.action = fit_action::START;
        else if (action == "resume")
            job.action = fit_action::RESUME;
        else if (action == "reuse_completed")
            job.action = fit_action::REUSE_COMPLETED;
        else
            return fail(error_code::INVALID_DATA, "Unknown fit action");
        if (job.action != fit_action::REUSE_COMPLETED && !value.contains("fit_python"))
            return fail(error_code::INVALID_DATA,
                        "Fitting requires an explicit local Python executable");
        job.checkpoint = path("checkpoint", false);
        job.completed_fit_identity = value.value("completed_fit_identity", std::string{});
        const auto local_path = [&](const nlohmann::json& object, const char* name) {
            const auto text = object.at(name).get<std::string>();
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid stage path/executable");
            const std::filesystem::path selected(text);
            return selected.is_absolute() ? selected : base / selected;
        };
        const auto executable = [&](const nlohmann::json& object, const char* name) {
            auto text = object.at(name).get<std::string>();
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid explicit stage executable");
            return std::filesystem::path(text).has_parent_path() ? local_path(object, name).string()
                                                                 : text;
        };
        const auto integer = [&](const nlohmann::json& object, const char* name,
                                 std::uint64_t maximum) {
            const auto& field = object.at(name);
            if (!field.is_number_unsigned() || field.get<std::uint64_t>() > maximum)
                throw std::invalid_argument("Invalid stage integer limit");
            return field.get<std::uint64_t>();
        };
        if (value.at("schema_version") == 2 && value.at("pose_action") == "solve") {
            const auto& pose = value.at("pose_worker");
            if (!pose.is_object() || pose.size() != 6)
                return fail(error_code::INVALID_DATA,
                            "Fresh solving requires explicit local worker/recipe/limits");
            fresh_pose_configuration config;
            config.worker = {local_path(pose, "worker_script"), executable(pose, "worker_python")};
            config.pose_python = executable(pose, "pose_python");
            const auto recipe = pose.at("recipe").get<std::string>();
            if (recipe != "sequential" && recipe != "exhaustive_guided")
                return fail(error_code::INVALID_DATA, "Unknown explicit pose recipe");
            config.recipe =
                recipe == "sequential" ? pose_recipe::SEQUENTIAL : pose_recipe::EXHAUSTIVE_GUIDED;
            config.cpu_threads = static_cast<std::uint32_t>(integer(pose, "cpu_threads", 4));
            config.journal_bytes = {static_cast<std::size_t>(
                integer(pose, "journal_bytes", std::size_t{2} * 1024 * 1024 * 1024))};
            job.fresh_poses = std::move(config);
            const auto& decode = value.at("decode");
            if (!decode.is_object())
                return fail(error_code::INVALID_DATA, "Invalid explicit decode action");
            if (decode.at("action") == "adopt_completed" && decode.size() == 3)
                job.selected_decode = retained_video_decode{
                    local_path(decode, "stage"), decode.at("completion_sha256").get<std::string>()};
            else if (decode.at("action") == "decode" && decode.size() == 8) {
                automatic_video_decode config_decode;
                config_decode.job_directory = local_path(decode, "job_directory");
                config_decode.worker = {local_path(decode, "worker_script"),
                                        executable(decode, "python")};
                if (!decode.at("sample_fps").is_number())
                    return fail(error_code::INVALID_DATA, "Invalid frame selection rate");
                config_decode.sample_fps = decode.at("sample_fps").get<double>();
                config_decode.max_frames =
                    static_cast<std::uint32_t>(integer(decode, "max_frames", 120));
                config_decode.cpu_threads =
                    static_cast<std::uint32_t>(integer(decode, "cpu_threads", 4));
                config_decode.max_output_bytes =
                    integer(decode, "max_output_bytes", 32ULL * 1024 * 1024 * 1024);
                job.automatic_decode = std::move(config_decode);
            } else
                return fail(error_code::INVALID_DATA, "Unknown or malformed decode action");
        } else if (value.contains("decode") || value.contains("pose_worker"))
            return fail(error_code::INVALID_DATA,
                        "Decode/pose worker configuration requires a solve action");
        const auto valid = validate_job(job);
        if (!valid)
            return std::unexpected(valid.error());
        return job;
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Reconstruction request invalid: " + std::string(exception.what()));
    }
}

result<std::filesystem::path> reconstruct(const reconstruction_job& job,
                                          const std::stop_token& stop, const log_callback& log) {
    try {
        const auto valid = validate_job(job);
        if (!valid)
            return std::unexpected(valid.error());
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Reconstruction cancelled");
        if (log)
            log("Recipe: local_gsplat_v1 (development; quality unqualified).\n");
        std::optional<video_preflight_report> preflight;
        auto selected_inspection = job.inspection;
        if (job.automatic_inspection) {
            const auto inspected =
                inspect_video({job.video,
                               *job.automatic_inspection,
                               {pose_map_directory(job), job.fit_directory, job.scene_output}},
                              stop, log);
            if (!inspected)
                return std::unexpected(inspected.error());
            selected_inspection = inspected->inspection;
            preflight = inspected->report;
        } else if (selected_inspection) {
            const auto inspected =
                preflight_video({job.video,
                                 *selected_inspection,
                                 {pose_map_directory(job), job.fit_directory, job.scene_output}},
                                stop, log);
            if (!inspected)
                return std::unexpected(inspected.error());
            preflight = *inspected;
        }
        const auto source_hash = preflight
                                     ? result<std::string>(preflight->source_sha256)
                                     : sha256_file(job.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!source_hash)
            return std::unexpected(source_hash.error());
        std::unique_ptr<file_lock> pose_intent_lock;
        std::optional<decoded_video> selected_decode;
        if (job.fresh_poses) {
            if (!selected_inspection || !preflight)
                return fail(error_code::INVALID_ARGUMENT,
                            "Fresh poses require checked video preflight");
            result<decoded_video> decoded =
                fail(error_code::INVALID_ARGUMENT, "Missing decode action");
            if (job.automatic_decode)
                decoded =
                    decode_video({job.video,
                                  *selected_inspection,
                                  *job.automatic_decode,
                                  {pose_map_directory(job), job.fit_directory, job.scene_output}},
                                 stop, log);
            else if (job.selected_decode)
                decoded = adopt_video_decode(*job.selected_decode, *source_hash, stop);
            if (!decoded)
                return std::unexpected(decoded.error());
            selected_decode = *decoded;
            auto ready = job_detail::prepare_pose_job(job, *decoded, *preflight, stop, log);
            if (!ready)
                return std::unexpected(ready.error());
            pose_intent_lock = std::move(*ready);
        }
        if (log)
            log("Verifying completed poses...\n");
        const auto poses = job_detail::verify_poses(job, *source_hash, stop);
        if (!poses)
            return std::unexpected(poses.error());
        auto prepared = prepare_completed_pose_map(
            {{job.pose_job, job.video, job.decode_stage}, pose_map_directory(job)}, stop, log);
        if (!prepared)
            return std::unexpected(prepared.error());
        if (prepared->provenance.source_sha256 != *source_hash ||
            prepared->provenance.pose_completion_sha256 != poses->completion_hash)
            return fail(error_code::INVALID_DATA, "Pose inputs changed during map preparation");
        if (log)
            log("Durable pose map ready at epoch 0/revision 1.\n");
        if (job.action != fit_action::REUSE_COMPLETED) {
            if (log)
                log("Fitting locally; ETA unknown.\n");
            std::vector<std::string> arguments{"--source",
                                               job.gsplat_source.string(),
                                               "--data",
                                               poses->training.string(),
                                               "--video",
                                               job.video.string(),
                                               "--output",
                                               job.fit_directory.string(),
                                               "--steps",
                                               std::to_string(job.steps),
                                               "--max-image-dimension",
                                               std::to_string(job.max_image_dimension),
                                               "--max-splats",
                                               std::to_string(job.max_splats)};
            if (job.action == fit_action::RESUME) {
                arguments.emplace_back("--resume");
                arguments.push_back(job.checkpoint.string());
            }
            const auto fitting = run_worker(job.fitting_worker, arguments, stop, log);
            if (!fitting)
                return std::unexpected(fitting.error());
            if (*fitting != 0)
                return fail(error_code::BACKEND_FAILURE,
                            "Fit worker failed with exit code " + std::to_string(*fitting));
        } else if (log)
            log("Reusing the explicitly selected completed fit; optimization skipped.\n");
        const auto final_source = sha256_file(job.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!final_source)
            return std::unexpected(final_source.error());
        const auto final_poses = job_detail::verify_poses(job, *source_hash, stop);
        if (!final_poses)
            return std::unexpected(final_poses.error());
        if (*source_hash != *final_source || poses->completion_hash != final_poses->completion_hash)
            return fail(error_code::INVALID_DATA,
                        "Reconstruction input changed; previous scene retained");
        const auto dataset =
            load_completed_poses({job.pose_job, *source_hash, job.decode_stage}, stop);
        if (!dataset)
            return std::unexpected(dataset.error());
        const auto checked_fit = job_detail::verify_fit(job, *poses, *source_hash, **dataset);
        if (!checked_fit)
            return std::unexpected(checked_fit.error());
        const auto canonical = pose_pipeline::canonical(**dataset, stop);
        if (!canonical)
            return std::unexpected(canonical.error());
        const auto bytes = encode_pose_asset(*canonical, {}, stop);
        if (!bytes)
            return std::unexpected(bytes.error());
        if (sha256(*bytes) != prepared->asset_sha256)
            return fail(error_code::INVALID_DATA,
                        "Canonical pose dependency changed; previous scene retained");
        const auto snapshot = prepared->store->acquire_snapshot();
        fit_pose_dependency dependency{prepared->asset_sha256,
                                       prepared->provenance.pose_completion_sha256,
                                       snapshot->epoch,
                                       snapshot->revision,
                                       {}};
        for (const auto& [frame, pose] : snapshot->poses) {
            (void)pose;
            dependency.source_frames.push_back(frame);
        }
        if (selected_inspection) {
            const auto rechecked =
                preflight_video({job.video,
                                 *selected_inspection,
                                 {pose_map_directory(job), job.fit_directory, job.scene_output}},
                                stop);
            if (!rechecked)
                return std::unexpected(rechecked.error());
            if (rechecked->source_sha256 != *source_hash)
                return fail(error_code::INVALID_DATA,
                            "Video changed after preflight; previous scene retained");
        }
        if (selected_decode) {
            const auto rechecked =
                adopt_video_decode(selected_decode->selected, *source_hash, stop);
            if (!rechecked)
                return std::unexpected(rechecked.error());
        }
        if (log)
            log("Compiling checked fit and publishing scene...\n");
        const auto published = compile_fit_scene(
            {.input = job.fit_directory / "result.json",
             .destination = job.scene_output,
             .stop = stop},
            {*source_hash, *checked_fit, std::move(dependency),
             preflight ? preflight->inspection_completion_sha256 : std::string{}});
        if (!published)
            return std::unexpected(published.error());
        if (log)
            log("Scene published. Scale arbitrary; geometry and walking unavailable.\n");
        return job.scene_output;
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Reconstruction failed: " + std::string(exception.what()));
    }
}
} // namespace world_explorer
