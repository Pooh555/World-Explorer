#include "world_explorer/application/geometry_job.hpp"

#include "depth_job_outputs.hpp"
#include "job_validation.hpp"
#include "mesh_job.hpp"
#include "owned_job.hpp"
#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_visibility.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace world_explorer {
namespace {

constexpr std::size_t K_DOCUMENT_BYTES { 1024uz * 1024uz };
constexpr std::size_t K_ASSET_BYTES { 256uz * 1024uz * 1024uz };
constexpr std::string_view K_SCOPE { "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK" };

std::string text_hash(const std::string& text) {
    return sha256(std::as_bytes(std::span(text)));
}

// Read the same bytes that are pinned; reject parser ambiguities at this boundary.
Result<nlohmann::json> read_document(const std::filesystem::path& path,
                                     const std::string& pin,
                                     const std::stop_token& stop) {
    if (!job_detail::valid_digest(pin)) {
        return fail(ErrorCode::INVALID_DATA, "Geometry document requires a lowercase SHA-256");
    }
    const auto K_BYTES = asset_files::read_bytes(path, K_DOCUMENT_BYTES, stop);
    if (!K_BYTES) {
        return std::unexpected(K_BYTES.error());
    }
    if (sha256(*K_BYTES) != pin) {
        return fail(ErrorCode::INVALID_DATA, "Geometry document checksum differs");
    }
    const std::string K_TEXT { reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size() };
    if (K_TEXT.contains('\0')) {
        return fail(ErrorCode::INVALID_DATA, "Geometry document contains raw NUL");
    }
    std::vector<std::set<std::string>> keys;
    auto document = nlohmann::json::parse(
        K_TEXT, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            if (depth > 32) {
                throw std::runtime_error("Geometry document nesting exceeds limit");
            }
            if (event == nlohmann::json::parse_event_t::object_start) {
                keys.emplace_back();
            }
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.back().insert(value.get<std::string>()).second) {
                throw std::runtime_error("Duplicate geometry document key");
            }
            if (event == nlohmann::json::parse_event_t::object_end) {
                keys.pop_back();
            }
            return true;
        });
    if (!document.is_object()) {
        return fail(ErrorCode::INVALID_DATA, "Expected geometry object");
    }
    return document;
}

bool overlaps(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto K_WITHIN = [](const auto& child, const auto& parent) {
        const auto K_RELATIVE = child.lexically_relative(parent);
        return !K_RELATIVE.empty() && *K_RELATIVE.begin() != "..";
    };
    return K_WITHIN(left, right) || K_WITHIN(right, left);
}

Result<std::filesystem::path> local_path(const std::filesystem::path& base,
                                         const nlohmann::json& value,
                                         bool executable = false) {
    const auto K_NAME = value.get<std::string>();
    if (K_NAME.empty() || K_NAME.contains('\0')) {
        return fail(ErrorCode::INVALID_DATA, "Empty or invalid geometry input path");
    }
    const auto K_PATH = std::filesystem::absolute(base / K_NAME).lexically_normal();
    if (!executable && std::filesystem::canonical(K_PATH) != K_PATH) {
        return fail(ErrorCode::INVALID_DATA, "Geometry input path may not use symlinks");
    }
    return std::filesystem::path { K_PATH };
}

Result<std::string>
file_hash(const std::filesystem::path& path, std::size_t maximum, const std::stop_token& stop) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
        return fail(ErrorCode::INVALID_DATA, "Geometry asset is missing, linked or nonregular");
    }
    return sha256_file(path, maximum, stop);
}

}  // namespace

namespace job_detail {

Result<nlohmann::json> depth_job_outputs(const std::filesystem::path& job,
                                         const std::string& request_sha256,
                                         const std::filesystem::path& request,
                                         std::span<const PreparedDenseFrame> frames,
                                         const std::stop_token& stop) {
    using Json = nlohmann::json;
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Depth terminal admission cancelled");
        }
        if (frames.empty() || frames.size() > 120uz || std::filesystem::canonical(job) != job ||
            std::filesystem::canonical(job / "stages") != job / "stages") {
            return fail(ErrorCode::INVALID_DATA, "Invalid depth job inventory or path");
        }
        const auto K_REQUEST = read_document(request, request_sha256, stop);
        if (!K_REQUEST) {
            return std::unexpected(K_REQUEST.error());
        }
        const auto K_RECIPE_PATH = local_path(request.parent_path(), K_REQUEST->at("recipe"));
        if (!K_RECIPE_PATH) {
            return std::unexpected(K_RECIPE_PATH.error());
        }
        const auto K_RECIPE =
            read_document(*K_RECIPE_PATH, K_REQUEST->at("recipe_sha256").get<std::string>(), stop);
        if (!K_RECIPE) {
            return std::unexpected(K_RECIPE.error());
        }
        const bool K_SEQUENCE { K_RECIPE->at("scope") == "BOUNDED_TEMPORAL_DEPTH_SEQUENCE_V1" };
        if (!valid_digest(K_RECIPE->at("worker_sha256").get<std::string>())) {
            return fail(ErrorCode::INVALID_DATA, "Invalid depth worker identity");
        }
        const auto K_INDICES = K_RECIPE->at("selection_indices");
        if (!K_INDICES.is_array() || K_INDICES.empty() ||
            K_INDICES.size() > (K_SEQUENCE ? 120uz : 18uz)) {
            return fail(ErrorCode::INVALID_DATA, "Invalid selected depth inventory");
        }
        std::vector<std::size_t> selection;
        std::set<std::size_t> seen;
        for (const auto& index : K_INDICES) {
            if (!index.is_number_unsigned() || index >= frames.size() ||
                !seen.insert(index.get<std::size_t>()).second) {
                return fail(ErrorCode::INVALID_DATA, "Invalid selected depth index");
            }
            selection.push_back(index.get<std::size_t>());
        }
        if (K_SEQUENCE && (frames.size() < 19uz || selection.size() != frames.size() ||
                           !std::ranges::is_sorted(selection) ||
                           !K_RECIPE->at("neighbor_radius").is_number_unsigned() ||
                           K_RECIPE->at("neighbor_radius") != 3U)) {
            return fail(ErrorCode::INVALID_DATA, "Invalid complete depth sequence policy");
        }
        const auto K_TERMINAL_SHA = file_hash(job / "manifest.json", K_DOCUMENT_BYTES, stop);
        if (!K_TERMINAL_SHA) {
            return std::unexpected(K_TERMINAL_SHA.error());
        }
        const auto K_TERMINAL = read_document(job / "manifest.json", *K_TERMINAL_SHA, stop);
        if (!K_TERMINAL) {
            return std::unexpected(K_TERMINAL.error());
        }
        const auto K_SCHEMA_ONE = [](const Json& value) {
            return value.at("schema_version").is_number_unsigned() &&
                   value.at("schema_version") == 1U;
        };
        if (K_TERMINAL->size() != 6uz || !K_SCHEMA_ONE(*K_TERMINAL) ||
            K_TERMINAL->at("kind") != (K_SEQUENCE ? "OWNED_RESIZED_DEPTH_SEQUENCE_TERMINAL_V1"
                                                  : "OWNED_RESIZED_DEPTH_TERMINAL_V1") ||
            K_TERMINAL->at("scope") != "PARTIAL_DEPTH_NO_Q5_N2_OR_WALK") {
            return fail(ErrorCode::INVALID_DATA, "Invalid owned depth terminal");
        }
        const auto K_OWNER_SHA = K_TERMINAL->at("owner_sha256").get<std::string>();
        const auto K_OWNER = read_document(job / "owner.json", K_OWNER_SHA, stop);
        if (!K_OWNER) {
            return std::unexpected(K_OWNER.error());
        }
        if (!K_SCHEMA_ONE(*K_OWNER) || K_OWNER->at("kind") != "OWNED_RESIZED_DEPTH_JOB_V1" ||
            K_OWNER->at("request_sha256") != request_sha256 ||
            K_OWNER->at("request") != request.string() ||
            K_OWNER->at("worker_sha256") != K_RECIPE->at("worker_sha256")) {
            return fail(ErrorCode::INVALID_DATA, "Depth terminal owner differs");
        }
        Json batches = Json::array();
        if (K_SEQUENCE) {
            if (!K_TERMINAL->at("frame_count").is_number_unsigned() ||
                K_TERMINAL->at("frame_count") != frames.size() ||
                !K_TERMINAL->at("batches").is_array() ||
                K_TERMINAL->at("batches").size() != (frames.size() + 8uz) / 9uz) {
                return fail(ErrorCode::INVALID_DATA, "Incomplete depth sequence batches");
            }
            batches = K_TERMINAL->at("batches");
        } else {
            batches.push_back({ { "stage", K_TERMINAL->at("stage") },
                               { "result_sha256", K_TERMINAL->at("result_sha256") } });
        }
        Json outputs = Json::array();
        for (std::size_t batch_index { 0uz }; batch_index < batches.size(); ++batch_index) {
            const auto& batch = batches[batch_index];
            if (batch.size() != (K_SEQUENCE ? 3uz : 2uz)) {
                return fail(ErrorCode::INVALID_DATA, "Invalid depth batch record");
            }
            const auto K_NAME = batch.at("stage").get<std::string>();
            const auto K_PREFIX = "depth-" + K_OWNER_SHA +
                                  (K_SEQUENCE ? "-batch-" + std::to_string(batch_index) : "") +
                                  "-attempt-";
            bool selected {};
            for (std::size_t attempt { 0uz }; attempt < 128uz; ++attempt) {
                selected = selected || K_NAME == K_PREFIX + std::to_string(attempt);
            }
            const auto K_STAGE = job / "stages" / K_NAME;
            if (!selected || std::filesystem::canonical(K_STAGE) != K_STAGE) {
                return fail(ErrorCode::INVALID_DATA, "Invalid selected depth attempt");
            }
            const auto K_RESULT = read_document(
                K_STAGE / "result.json", batch.at("result_sha256").get<std::string>(), stop);
            if (!K_RESULT) {
                return std::unexpected(K_RESULT.error());
            }
            const auto K_PREPARED =
                read_document(K_STAGE / "prepared.json",
                              K_RESULT->at("prepared_sha256").get<std::string>(),
                              stop);
            if (!K_PREPARED) {
                return std::unexpected(K_PREPARED.error());
            }
            for (const auto& document : { *K_RESULT, *K_PREPARED }) {
                if (!K_SCHEMA_ONE(document) ||
                    document.at("manifest_sha256") != K_REQUEST->at("manifest_sha256") ||
                    document.at("recipe_sha256") != K_REQUEST->at("recipe_sha256") ||
                    document.at("worker_sha256") != K_RECIPE->at("worker_sha256")) {
                    return fail(ErrorCode::INVALID_DATA, "Depth stage dependencies differ");
                }
            }
            const auto K_FIRST = K_SEQUENCE ? batch_index * 9uz : 0uz;
            const auto K_END =
                K_SEQUENCE ? std::min(K_FIRST + 9uz, frames.size()) : selection.size();
            const auto K_BEGIN = K_SEQUENCE && K_FIRST >= 3uz ? K_FIRST - 3uz : 0uz;
            const auto K_SOLVED_END =
                K_SEQUENCE ? std::min(K_END + 3uz, frames.size()) : selection.size();
            Json references = Json::array(), solved = Json::array();
            for (auto index = K_FIRST; index < K_END; ++index) {
                references.push_back(index);
            }
            for (auto index = K_BEGIN; index < K_SOLVED_END; ++index) {
                solved.push_back(selection[index]);
            }
            if (K_RESULT->at("status") != "PARTIAL_NEIGHBOR_PILOT_COMPLETE" ||
                K_PREPARED->at("status") != "PARTIAL_NEIGHBOR_INPUTS_PREPARED" ||
                !K_RESULT->at("frames").is_array() ||
                K_RESULT->at("frames").size() != solved.size()) {
                return fail(ErrorCode::INVALID_DATA, "Depth result inventory differs");
            }
            if (K_SEQUENCE && (!std::ranges::all_of(batch.at("reference_indices"),
                                                    [](const Json& value) {
                                                        return value.is_number_unsigned();
                                                    }) ||
                               !std::ranges::all_of(K_RESULT->at("reference_indices"),
                                                    [](const Json& value) {
                                                        return value.is_number_unsigned();
                                                    }) ||
                               !std::ranges::all_of(K_PREPARED->at("reference_indices"),
                                                    [](const Json& value) {
                                                        return value.is_number_unsigned();
                                                    }) ||
                               !std::ranges::all_of(K_PREPARED->at("selection_indices"),
                                                    [](const Json& value) {
                                                        return value.is_number_unsigned();
                                                    }) ||
                               batch.at("reference_indices") != references ||
                               K_RESULT->at("reference_indices") != references ||
                               K_PREPARED->at("reference_indices") != references ||
                               K_PREPARED->at("selection_indices") != solved ||
                               !K_PREPARED->at("batch_index").is_number_unsigned() ||
                               K_PREPARED->at("batch_index") != batch_index)) {
                return fail(ErrorCode::INVALID_DATA,
                            "Depth batch reference/support selection differs");
            }
            for (std::size_t index { 0uz }; index < solved.size(); ++index) {
                const auto K_FRAME_INDEX = solved[index].get<std::size_t>();
                const auto& expected = frames[K_FRAME_INDEX];
                const auto& record = K_RESULT->at("frames")[index];
                const auto K_ASSET = "assets/" + expected.m_image_name + ".wedepth";
                if (expected.m_image_name.empty() ||
                    expected.m_image_name.find_first_of("/\\") != std::string::npos ||
                    !record.at("frame").is_number_unsigned() ||
                    record.at("frame") != expected.m_expected_depth.m_dependency.m_frame.m_value ||
                    record.at("asset") != K_ASSET) {
                    return fail(ErrorCode::INVALID_DATA, "Depth result frame order/path differs");
                }
                const auto K_PATH = K_STAGE / K_ASSET;
                if (std::filesystem::canonical(K_PATH) != K_PATH) {
                    return fail(ErrorCode::INVALID_DATA, "Linked depth result asset");
                }
                const auto K_HASH = file_hash(K_PATH, K_ASSET_BYTES, stop);
                if (!K_HASH) {
                    return std::unexpected(K_HASH.error());
                }
                if (*K_HASH != record.at("sha256").get<std::string>()) {
                    return fail(ErrorCode::INVALID_DATA, "Depth result asset checksum differs");
                }
                if (!K_SEQUENCE || (K_FRAME_INDEX >= K_FIRST && K_FRAME_INDEX < K_END)) {
                    outputs.push_back({ { "frame", record.at("frame") },
                                       { "path", K_PATH.string() },
                                       { "sha256", *K_HASH } });
                }
            }
        }
        return Json { { "schema_version", K_SEQUENCE ? 2 : 1 }, { "outputs", outputs } };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth terminal admission allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Depth terminal admission failed: ") + error.what());
    }
}

}  // namespace job_detail

namespace {

#if defined(__linux__)
Result<std::filesystem::path> run_fresh_geometry(const std::filesystem::path& request,
                                                 const std::string& request_sha256,
                                                 const nlohmann::json& configuration,
                                                 const std::filesystem::path& destination,
                                                 const std::stop_token& stop,
                                                 const LogCallback& log) {
    const std::set<std::string> K_FIELDS { "schema_version",
                                          "kind",
                                          "dense_request",
                                          "dense_request_sha256",
                                          "dense_python",
                                          "dense_coordinator",
                                          "dense_coordinator_sha256",
                                          "visibility_template",
                                          "visibility_template_sha256",
                                          "retained_template",
                                          "retained_template_sha256",
                                          "fusion_recipe",
                                          "fusion_recipe_sha256" };
    if (!configuration.at("schema_version").is_number_unsigned() ||
        configuration.at("schema_version") != 1 || configuration.size() != K_FIELDS.size() ||
        !std::ranges::all_of(configuration.items(), [&](const auto& item) {
            return K_FIELDS.contains(item.key());
        })) {
        return fail(ErrorCode::INVALID_DATA, "Unsupported fresh geometry request");
    }
    const auto K_ROOT = std::filesystem::absolute(destination).lexically_normal();
    if (std::filesystem::weakly_canonical(K_ROOT) != K_ROOT || overlaps(K_ROOT, request)) {
        return fail(ErrorCode::INVALID_DATA, "Fresh geometry output is linked or overlaps input");
    }
    const auto K_PIN = [&](const char* key) {
        return configuration.at(key).get<std::string>();
    };
    std::map<std::string, std::filesystem::path> paths;
    for (const auto& key : { "dense_request",
                            "dense_python",
                            "dense_coordinator",
                            "visibility_template",
                            "retained_template",
                            "fusion_recipe" }) {
        const auto K_PATH = local_path(
            request.parent_path(), configuration.at(key), std::string_view(key) == "dense_python");
        if (!K_PATH) {
            return std::unexpected(K_PATH.error());
        }
        if (overlaps(K_ROOT, std::filesystem::canonical(*K_PATH))) {
            return fail(ErrorCode::INVALID_DATA, "Fresh output overlaps a dependency");
        }
        paths.emplace(key, *K_PATH);
    }
    const auto K_DENSE =
        read_document(paths.at("dense_request"), K_PIN("dense_request_sha256"), stop);
    auto visibility =
        read_document(paths.at("visibility_template"), K_PIN("visibility_template_sha256"), stop);
    auto retained =
        read_document(paths.at("retained_template"), K_PIN("retained_template_sha256"), stop);
    const auto K_RECIPE =
        read_document(paths.at("fusion_recipe"), K_PIN("fusion_recipe_sha256"), stop);
    if (!K_DENSE) {
        return std::unexpected(K_DENSE.error());
    }
    if (!visibility) {
        return std::unexpected(visibility.error());
    }
    if (!retained) {
        return std::unexpected(retained.error());
    }
    if (!K_RECIPE) {
        return std::unexpected(K_RECIPE.error());
    }
    if (visibility->size() != 13uz || !visibility->at("outputs").is_array() ||
        !visibility->at("outputs").empty() || retained->size() != 9uz ||
        retained->at("kind") != "RETAINED_GEOMETRY_PIPELINE_V1") {
        return fail(ErrorCode::INVALID_DATA, "Invalid fresh geometry templates");
    }
    // Resolve template paths before moving their exact selected values into owned files.
    for (const auto& key :
         { "pose_job", "poses", "native_recipe", "recipe", "manifest", "decode_stage" }) {
        if (std::string_view(key) == "decode_stage" && visibility->at(key) == "") {
            continue;
        }
        const auto K_PATH =
            local_path(paths.at("visibility_template").parent_path(), visibility->at(key));
        if (!K_PATH) {
            return std::unexpected(K_PATH.error());
        }
        const auto K_INPUT = std::string_view(key) == "manifest" ? K_PATH->parent_path() : *K_PATH;
        if (overlaps(K_ROOT, K_INPUT)) {
            return fail(ErrorCode::INVALID_DATA, "Fresh output overlaps source data");
        }
        (*visibility)[key] = K_PATH->string();
    }
    for (const auto& key : { "poses", "python", "coordinator", "converter" }) {
        const auto K_PATH = local_path(paths.at("retained_template").parent_path(),
                                       retained->at(key),
                                       std::string_view(key) == "python");
        if (!K_PATH) {
            return std::unexpected(K_PATH.error());
        }
        if (overlaps(K_ROOT, std::filesystem::canonical(*K_PATH))) {
            return fail(ErrorCode::INVALID_DATA, "Fresh output overlaps execution input");
        }
        (*retained)[key] = K_PATH->string();
    }
    for (const auto& key : { "manifest", "recipe", "poses" }) {
        const auto K_PATH = local_path(paths.at("dense_request").parent_path(), K_DENSE->at(key));
        if (!K_PATH) {
            return std::unexpected(K_PATH.error());
        }
        const auto K_PIN_KEY =
            std::string(key) == "poses" ? "pose_sha256" : std::string(key) + "_sha256";
        if (K_PATH->string() != visibility->at(key).get<std::string>() ||
            K_DENSE->at(K_PIN_KEY) != visibility->at(K_PIN_KEY)) {
            return fail(ErrorCode::INVALID_DATA, "Dense and native admission inputs differ");
        }
    }
    if (retained->at("poses") != visibility->at("poses") ||
        retained->at("pose_sha256") != visibility->at("pose_sha256")) {
        return fail(ErrorCode::INVALID_DATA, "Fresh geometry pose selections differ");
    }
    const auto K_VIS_PATH = [&](const char* key) {
        return std::filesystem::path { visibility->at(key).get<std::string>() };
    };
    const auto K_DEPTH_RECIPE = read_document(
        K_VIS_PATH("recipe"), visibility->at("recipe_sha256").get<std::string>(), stop);
    if (!K_DEPTH_RECIPE) {
        return std::unexpected(K_DEPTH_RECIPE.error());
    }
    const bool K_SEQUENCE { K_DEPTH_RECIPE->at("scope") == "BOUNDED_TEMPORAL_DEPTH_SEQUENCE_V1" };
    if (!visibility->at("schema_version").is_number_unsigned() ||
        visibility->at("schema_version") != (K_SEQUENCE ? 2U : 1U) ||
        K_RECIPE->at("kind") !=
            (K_SEQUENCE ? "OBSERVED_TSDF_CPU_SEQUENCE_V1" : "OBSERVED_TSDF_CPU_PILOT_V1")) {
        return fail(ErrorCode::INVALID_DATA, "Depth, visibility and fusion policies differ");
    }
    const DenseInputRequest K_NATIVE { { K_VIS_PATH("pose_job"),
                                       visibility->at("source_sha256").get<std::string>(),
                                       K_VIS_PATH("decode_stage") },
                                      K_VIS_PATH("poses"),
                                      visibility->at("pose_sha256").get<std::string>(),
                                      K_VIS_PATH("native_recipe"),
                                      visibility->at("native_recipe_sha256").get<std::string>() };
    const auto K_INPUTS = load_resized_dense_inputs(
        { K_NATIVE, K_VIS_PATH("recipe"), visibility->at("recipe_sha256").get<std::string>() },
        K_VIS_PATH("manifest"),
        visibility->at("manifest_sha256").get<std::string>(),
        stop);
    if (!K_INPUTS) {
        return std::unexpected(K_INPUTS.error());
    }
    const auto K_COORDINATOR = file_hash(paths.at("dense_coordinator"), K_DOCUMENT_BYTES, stop);
    const auto K_PYTHON = sha256_file(paths.at("dense_python"), K_ASSET_BYTES, stop);
    if (!K_COORDINATOR) {
        return std::unexpected(K_COORDINATOR.error());
    }
    if (!K_PYTHON) {
        return std::unexpected(K_PYTHON.error());
    }
    if (*K_COORDINATOR != K_PIN("dense_coordinator_sha256")) {
        return fail(ErrorCode::INVALID_DATA, "Dense coordinator checksum differs");
    }
    const nlohmann::json K_OWNER {
        { "schema_version", 1 },
        { "kind", "FRESH_GEOMETRY_PIPELINE_V1" },
        { "request", request.string() },
        { "request_sha256", request_sha256 },
        { "python_sha256", *K_PYTHON },
        { "python_resolved", std::filesystem::canonical(paths.at("dense_python")).string() } };
    auto lock = job_detail::open_owned_job(
        K_ROOT,
        { "owner.json", "writer.lock" },
        K_OWNER,
        { "stages", "visibility.json", "cache.json", "fusion.json", "retained.json" },
        stop);
    if (!lock) {
        return std::unexpected(lock.error());
    }
    const auto K_STAGES = K_ROOT / "stages";
    std::filesystem::create_directories(K_STAGES);
    std::set<std::string> allowed { "depth", "geometry" };
    for (std::size_t index { 0uz }; index < 128uz; ++index) {
        allowed.insert("cache-" + std::to_string(index));
    }
    for (const auto& entry : std::filesystem::directory_iterator(K_STAGES)) {
        if (entry.is_symlink() || !entry.is_directory() ||
            !allowed.contains(entry.path().filename().string())) {
            return fail(ErrorCode::INVALID_DATA, "Invalid fresh geometry stage inventory");
        }
    }
    if (log) {
        log("Running or reusing pinned CUDA depth. Partial geometry; walking unavailable.\n");
    }
    ProcessControl control;
    control.m_termination_grace = std::chrono::seconds(5);
    const auto K_DEPTH_ROOT = K_STAGES / "depth";
    const auto K_PROCESS = execute_process({ paths.at("dense_python").string(),
                                            paths.at("dense_coordinator").string(),
                                            paths.at("dense_request").string(),
                                            K_PIN("dense_request_sha256"),
                                            K_DEPTH_ROOT.string(),
                                            "--parent-supervised" },
                                           stop,
                                           log,
                                           log,
                                           control);
    if (!K_PROCESS) {
        return std::unexpected(K_PROCESS.error());
    }
    if (*K_PROCESS != 0) {
        return fail(ErrorCode::BACKEND_FAILURE, "Depth job failed; attempts retained");
    }
    // Recheck top-level bytes and executable identities before accepting child results.
    const auto K_RECHECK = read_document(request, request_sha256, stop);
    const auto K_FINAL_COORDINATOR =
        file_hash(paths.at("dense_coordinator"), K_DOCUMENT_BYTES, stop);
    const auto K_FINAL_PYTHON = sha256_file(paths.at("dense_python"), K_ASSET_BYTES, stop);
    if (!K_RECHECK) {
        return std::unexpected(K_RECHECK.error());
    }
    if (!K_FINAL_COORDINATOR) {
        return std::unexpected(K_FINAL_COORDINATOR.error());
    }
    if (!K_FINAL_PYTHON) {
        return std::unexpected(K_FINAL_PYTHON.error());
    }
    if (*K_FINAL_COORDINATOR != *K_COORDINATOR || *K_FINAL_PYTHON != *K_PYTHON ||
        std::filesystem::canonical(paths.at("dense_python")).string() !=
            K_OWNER.at("python_resolved").get<std::string>()) {
        return fail(ErrorCode::INVALID_DATA, "Depth execution identity changed");
    }
    const auto K_OUTPUTS = job_detail::depth_job_outputs(K_DEPTH_ROOT,
                                                         K_PIN("dense_request_sha256"),
                                                         paths.at("dense_request"),
                                                         K_INPUTS->m_frames,
                                                         stop);
    if (!K_OUTPUTS) {
        return std::unexpected(K_OUTPUTS.error());
    }
    if (K_OUTPUTS->at("schema_version") != visibility->at("schema_version")) {
        return fail(ErrorCode::INVALID_DATA, "Depth terminal and visibility schemas differ");
    }
    (*visibility)["outputs"] = K_OUTPUTS->at("outputs");
    const auto K_VISIBILITY_PATH = K_ROOT / "visibility.json";
    const auto K_VISIBILITY_SHA = text_hash(visibility->dump(2) + '\n');
    auto published = job_detail::publish_owned_json(K_VISIBILITY_PATH, *visibility, stop);
    if (!published) {
        return std::unexpected(published.error());
    }
    const auto K_CACHE_RECORD = K_ROOT / "cache.json";
    std::filesystem::path cache;
    std::string receipt_sha;
    if (std::filesystem::exists(K_CACHE_RECORD)) {
        const auto K_HASH = file_hash(K_CACHE_RECORD, K_DOCUMENT_BYTES, stop);
        if (!K_HASH) {
            return std::unexpected(K_HASH.error());
        }
        const auto K_SAVED = read_document(K_CACHE_RECORD, *K_HASH, stop);
        if (!K_SAVED) {
            return std::unexpected(K_SAVED.error());
        }
        if (K_SAVED->size() != 3uz || K_SAVED->at("visibility_sha256") != K_VISIBILITY_SHA ||
            !K_SAVED->at("attempt").is_number_unsigned() || K_SAVED->at("attempt") >= 128U) {
            return fail(ErrorCode::INVALID_DATA, "Invalid cache publication receipt");
        }
        cache = K_STAGES / ("cache-" + std::to_string(K_SAVED->at("attempt").get<std::uint32_t>()));
        receipt_sha = K_SAVED->at("receipt_sha256").get<std::string>();
    } else {
        for (std::size_t index { 0uz }; index < 128uz; ++index) {
            const auto K_CACHE = K_STAGES / ("cache-" + std::to_string(index));
            if (std::filesystem::exists(K_CACHE)) {
                continue;
            }
            if (log) {
                log("Admitting fresh depth and publishing a native visibility cache.\n");
            }
            const auto K_EXPORTED =
                export_depth_visibility_cache(K_VISIBILITY_PATH, K_VISIBILITY_SHA, K_CACHE, stop);
            if (!K_EXPORTED) {
                return std::unexpected(K_EXPORTED.error());
            }
            published = job_detail::publish_owned_json(K_CACHE_RECORD,
                                                       { { "attempt", index },
                                                        { "receipt_sha256", *K_EXPORTED },
                                                        { "visibility_sha256", K_VISIBILITY_SHA } },
                                                       stop);
            if (!published) {
                return std::unexpected(published.error());
            }
            cache = K_CACHE;
            receipt_sha = *K_EXPORTED;
            break;
        }
        if (cache.empty()) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fresh geometry exhausted cache attempts");
        }
    }
    if (std::filesystem::canonical(cache) != cache) {
        return fail(ErrorCode::INVALID_DATA, "Linked fresh depth cache");
    }
    const auto K_RECEIPT = read_document(cache / "manifest.json", receipt_sha, stop);
    if (!K_RECEIPT) {
        return std::unexpected(K_RECEIPT.error());
    }
    if (K_RECEIPT->at("request_sha256") != K_VISIBILITY_SHA ||
        K_RECEIPT->at("pose_asset_sha256") != retained->at("pose_sha256")) {
        return fail(ErrorCode::INVALID_DATA, "Fresh depth cache dependencies differ");
    }
    const nlohmann::json K_FUSION { { "schema_version", 1 },
                                   { "cache", cache.string() },
                                   { "receipt_sha256", receipt_sha },
                                   { "recipe", paths.at("fusion_recipe").string() },
                                   { "recipe_sha256", K_PIN("fusion_recipe_sha256") } };
    const auto K_FUSION_PATH = K_ROOT / "fusion.json";
    published = job_detail::publish_owned_json(K_FUSION_PATH, K_FUSION, stop);
    if (!published) {
        return std::unexpected(published.error());
    }
    (*retained)["fusion_request"] = K_FUSION_PATH.string();
    (*retained)["fusion_request_sha256"] = text_hash(K_FUSION.dump(2) + '\n');
    const auto K_RETAINED_PATH = K_ROOT / "retained.json";
    published = job_detail::publish_owned_json(K_RETAINED_PATH, *retained, stop);
    if (!published) {
        return std::unexpected(published.error());
    }
    if (log) {
        log("Fresh depth admitted; continuing the owned retained-cache geometry path.\n");
    }
    return run_geometry_job(
        K_RETAINED_PATH, text_hash(retained->dump(2) + '\n'), K_STAGES / "geometry", stop, log);
}

#endif

}  // namespace

namespace {

#if defined(__linux__)
Result<std::filesystem::path> run_solver_geometry_job(const std::filesystem::path& request,
                                                      const std::string& expected_sha256,
                                                      const std::filesystem::path& destination,
                                                      const std::stop_token& stop,
                                                      const LogCallback& log) {
    try {
        const auto K_REQUEST = std::filesystem::absolute(request).lexically_normal();
        const auto K_CONFIG = read_document(K_REQUEST, expected_sha256, stop);
        if (!K_CONFIG) {
            return std::unexpected(K_CONFIG.error());
        }
        if (K_CONFIG->contains("kind") && K_CONFIG->at("kind") == "FRESH_GEOMETRY_PIPELINE_V1") {
            return run_fresh_geometry(
                K_REQUEST, expected_sha256, *K_CONFIG, destination, stop, log);
        }
        const std::set<std::string> K_FIELDS { "schema_version",
                                              "kind",
                                              "fusion_request",
                                              "fusion_request_sha256",
                                              "poses",
                                              "pose_sha256",
                                              "python",
                                              "coordinator",
                                              "coordinator_sha256",
                                              "converter",
                                              "converter_sha256" };
        if (!K_CONFIG->at("schema_version").is_number_unsigned() ||
            K_CONFIG->at("schema_version") != 1 ||
            K_CONFIG->at("kind") != "RETAINED_GEOMETRY_PIPELINE_V1" ||
            K_CONFIG->size() != K_FIELDS.size() ||
            !std::ranges::all_of(K_CONFIG->items(), [&](const auto& item) {
                return K_FIELDS.contains(item.key());
            })) {
            return fail(ErrorCode::INVALID_DATA, "Unsupported retained geometry pipeline request");
        }
        const auto K_ROOT = std::filesystem::absolute(destination).lexically_normal();
        if (std::filesystem::weakly_canonical(K_ROOT) != K_ROOT || overlaps(K_ROOT, K_REQUEST)) {
            return fail(ErrorCode::INVALID_DATA,
                        "Geometry output is linked or overlaps its request");
        }
        std::map<std::string, std::filesystem::path> paths;
        for (const auto& key : { "fusion_request", "poses", "python", "coordinator", "converter" }) {
            const auto K_PATH = local_path(
                K_REQUEST.parent_path(), K_CONFIG->at(key), std::string_view(key) == "python");
            if (!K_PATH) {
                return std::unexpected(K_PATH.error());
            }
            if (overlaps(K_ROOT, *K_PATH) ||
                overlaps(K_ROOT, std::filesystem::canonical(*K_PATH))) {
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps an input");
            }
            paths.emplace(key, *K_PATH);
        }
        const auto K_PIN = [&](const char* key) {
            return K_CONFIG->at(key).get<std::string>();
        };
        const auto K_FUSION =
            read_document(paths.at("fusion_request"), K_PIN("fusion_request_sha256"), stop);
        if (!K_FUSION) {
            return std::unexpected(K_FUSION.error());
        }
        for (const auto& key : { "cache", "recipe" }) {
            const auto K_PATH =
                local_path(paths.at("fusion_request").parent_path(), K_FUSION->at(key));
            if (!K_PATH) {
                return std::unexpected(K_PATH.error());
            }
            if (overlaps(K_ROOT, *K_PATH)) {
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps a dependency");
            }
        }
        const auto K_POSES = read_pose_asset(paths.at("poses"), K_PIN("pose_sha256"), {}, stop);
        if (!K_POSES) {
            return std::unexpected(K_POSES.error());
        }
        for (const auto& key : { "coordinator", "converter" }) {
            const auto K_HASH = file_hash(paths.at(key), K_DOCUMENT_BYTES, stop);
            if (!K_HASH) {
                return std::unexpected(K_HASH.error());
            }
            if (*K_HASH != K_PIN((std::string(key) + "_sha256").c_str())) {
                return fail(ErrorCode::INVALID_DATA, "Geometry script checksum differs");
            }
        }
        const auto K_PYTHON_SHA = sha256_file(paths.at("python"), K_ASSET_BYTES, stop);
        if (!K_PYTHON_SHA) {
            return std::unexpected(K_PYTHON_SHA.error());
        }
        const nlohmann::json K_OWNER {
            { "schema_version", 1 },
            { "kind", "RETAINED_GEOMETRY_PIPELINE_V1" },
            { "request", K_REQUEST.string() },
            { "request_sha256", expected_sha256 },
            { "python_sha256", *K_PYTHON_SHA },
            { "python_resolved", std::filesystem::canonical(paths.at("python")).string() } };
        auto lock = job_detail::open_owned_job(
            K_ROOT, { "owner.json", "writer.lock" }, K_OWNER, { "stages", "adoption.json" }, stop);
        if (!lock) {
            return std::unexpected(lock.error());
        }
        const auto K_STAGES = K_ROOT / "stages";
        std::filesystem::create_directories(K_STAGES);
        if (std::filesystem::canonical(K_STAGES) != K_STAGES) {
            return fail(ErrorCode::INVALID_DATA, "Linked geometry pipeline stages");
        }
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGES)) {
            if (entry.is_symlink() || !entry.is_directory() ||
                (entry.path().filename() != "solver" && entry.path().filename() != "native")) {
                return fail(ErrorCode::INVALID_DATA, "Unknown geometry pipeline stage");
            }
        }
        if (log) {
            log("Running or reusing retained-depth geometry. Partial surfaces; walking "
                "unavailable.\n");
        }
        const auto K_SOLVER = K_STAGES / "solver";
        ProcessControl control;
        control.m_termination_grace = std::chrono::seconds(5);
        const auto K_PROCESS = execute_process({ paths.at("python").string(),
                                                paths.at("coordinator").string(),
                                                paths.at("fusion_request").string(),
                                                K_PIN("fusion_request_sha256"),
                                                paths.at("poses").string(),
                                                K_PIN("pose_sha256"),
                                                K_SOLVER.string(),
                                                "--parent-supervised" },
                                               stop,
                                               log,
                                               log,
                                               control);
        if (!K_PROCESS) {
            return std::unexpected(K_PROCESS.error());
        }
        if (*K_PROCESS != 0) {
            return fail(ErrorCode::BACKEND_FAILURE, "Geometry solver failed; attempts retained");
        }
        // Recheck launch inputs after the child boundary, including on completed reuse.
        const auto K_RECHECK = read_document(K_REQUEST, expected_sha256, stop);
        if (!K_RECHECK) {
            return std::unexpected(K_RECHECK.error());
        }
        const auto K_COORDINATOR = file_hash(paths.at("coordinator"), K_DOCUMENT_BYTES, stop);
        const auto K_PYTHON = sha256_file(paths.at("python"), K_ASSET_BYTES, stop);
        if (!K_COORDINATOR || !K_PYTHON) {
            return !K_COORDINATOR ? std::unexpected(K_COORDINATOR.error())
                                  : std::unexpected(K_PYTHON.error());
        }
        if (*K_COORDINATOR != K_PIN("coordinator_sha256") || *K_PYTHON != *K_PYTHON_SHA ||
            std::filesystem::canonical(paths.at("python")).string() !=
                K_OWNER.at("python_resolved").get<std::string>()) {
            return fail(ErrorCode::INVALID_DATA, "Geometry execution inputs changed");
        }
        const auto K_TERMINAL_SHA = file_hash(K_SOLVER / "manifest.json", K_DOCUMENT_BYTES, stop);
        if (!K_TERMINAL_SHA) {
            return std::unexpected(K_TERMINAL_SHA.error());
        }
        const nlohmann::json K_ADOPTION { { "schema_version", 1 },
                                         { "job", K_SOLVER.string() },
                                         { "job_manifest_sha256", *K_TERMINAL_SHA },
                                         { "fusion_request", paths.at("fusion_request").string() },
                                         { "fusion_request_sha256", K_PIN("fusion_request_sha256") },
                                         { "poses", paths.at("poses").string() },
                                         { "pose_sha256", K_PIN("pose_sha256") },
                                         { "python", paths.at("python").string() },
                                         { "converter", paths.at("converter").string() },
                                         { "converter_sha256", K_PIN("converter_sha256") } };
        const auto K_ADOPTION_PATH = K_ROOT / "adoption.json";
        const auto K_SAVED = job_detail::publish_owned_json(K_ADOPTION_PATH, K_ADOPTION, stop);
        if (!K_SAVED) {
            return std::unexpected(K_SAVED.error());
        }
        if (log) {
            log("Retained-depth solver complete; adopting against current poses.\n");
        }
        return adopt_geometry_job(
            K_ADOPTION_PATH, text_hash(K_ADOPTION.dump(2) + '\n'), K_STAGES / "native", stop, log);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry pipeline allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Geometry pipeline failed: ") + exception.what());
    }
}

#else
Result<std::filesystem::path> run_solver_geometry_job(const std::filesystem::path&,
                                                      const std::string&,
                                                      const std::filesystem::path&,
                                                      const std::stop_token&,
                                                      const LogCallback&) {
    return fail(ErrorCode::BACKEND_FAILURE, "Retained geometry solving requires Linux");
}
#endif

}  // namespace

Result<std::filesystem::path> run_geometry_job(const std::filesystem::path& request,
                                               const std::string& expected_sha256,
                                               const std::filesystem::path& destination,
                                               const std::stop_token& stop,
                                               const LogCallback& log) {
    try {
#if !defined(__linux__)
        // Preserve the solver's missing-input refusal while admitting the CPU-only profile.
        if (!std::filesystem::is_regular_file(request)) {
            return run_solver_geometry_job(request, expected_sha256, destination, stop, log);
        }
#endif
        const auto K_CONFIG = read_document(request, expected_sha256, stop);
        if (!K_CONFIG) {
            return std::unexpected(K_CONFIG.error());
        }
        if (K_CONFIG->contains("kind") && K_CONFIG->at("kind") == "MESH_POSTPROCESS_PIPELINE_V1") {
            return job_detail::run_mesh_job(request, expected_sha256, destination, stop, log);
        }
        return run_solver_geometry_job(request, expected_sha256, destination, stop, log);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry dispatch allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<std::filesystem::path> adopt_geometry_job(const std::filesystem::path& request,
                                                 const std::string& expected_sha256,
                                                 const std::filesystem::path& destination,
                                                 const std::stop_token& stop,
                                                 const LogCallback& log) {
    try {
        const auto K_REQUEST = std::filesystem::absolute(request).lexically_normal();
        const auto K_CONFIG = read_document(K_REQUEST, expected_sha256, stop);
        if (!K_CONFIG) {
            return std::unexpected(K_CONFIG.error());
        }
        const std::set<std::string> K_FIELDS { "schema_version",
                                              "job",
                                              "job_manifest_sha256",
                                              "fusion_request",
                                              "fusion_request_sha256",
                                              "poses",
                                              "pose_sha256",
                                              "python",
                                              "converter",
                                              "converter_sha256" };
        if (!K_CONFIG->at("schema_version").is_number_unsigned() ||
            K_CONFIG->at("schema_version") != 1 || K_CONFIG->size() != K_FIELDS.size() ||
            !std::ranges::all_of(K_CONFIG->items(), [&](const auto& item) {
                return K_FIELDS.contains(item.key());
            })) {
            return fail(ErrorCode::INVALID_DATA, "Unsupported native geometry job request");
        }
        const auto K_ROOT = std::filesystem::absolute(destination).lexically_normal();
        if (std::filesystem::weakly_canonical(K_ROOT) != K_ROOT) {
            return fail(ErrorCode::INVALID_DATA, "Geometry output may not use symlinks");
        }
        std::map<std::string, std::filesystem::path> paths;
        for (const auto& key : { "job", "fusion_request", "poses", "python", "converter" }) {
            const auto K_PATH = local_path(
                K_REQUEST.parent_path(), K_CONFIG->at(key), std::string_view(key) == "python");
            if (!K_PATH) {
                return std::unexpected(K_PATH.error());
            }
            if (overlaps(K_ROOT, *K_PATH)) {
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps an input");
            }
            if (overlaps(K_ROOT, std::filesystem::canonical(*K_PATH))) {
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps a resolved input");
            }
            paths.emplace(key, *K_PATH);
        }
        if (overlaps(K_ROOT, K_REQUEST)) {
            return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps its request");
        }
        const auto K_PIN = [&](const char* key) {
            return K_CONFIG->at(key).get<std::string>();
        };
        const auto K_TERMINAL =
            read_document(paths.at("job") / "manifest.json", K_PIN("job_manifest_sha256"), stop);
        if (!K_TERMINAL) {
            return std::unexpected(K_TERMINAL.error());
        }
        if (K_TERMINAL->size() != 6 || K_TERMINAL->at("schema_version") != 1 ||
            K_TERMINAL->at("kind") != "OWNED_PARTIAL_OBSERVED_GEOMETRY_V1" ||
            K_TERMINAL->at("scope").get<std::string>() != K_SCOPE) {
            return fail(ErrorCode::INVALID_DATA, "Unsupported terminal geometry job");
        }
        const auto K_OWNER_SHA = K_TERMINAL->at("owner_sha256").get<std::string>();
        const auto K_OWNER = read_document(paths.at("job") / "owner.json", K_OWNER_SHA, stop);
        const auto K_FUSION =
            read_document(paths.at("fusion_request"), K_PIN("fusion_request_sha256"), stop);
        if (!K_OWNER) {
            return std::unexpected(K_OWNER.error());
        }
        if (!K_FUSION) {
            return std::unexpected(K_FUSION.error());
        }
        if (K_OWNER->at("kind") != "RETAINED_OBSERVED_GEOMETRY_JOB_V1" ||
            K_OWNER->at("schema_version") != 1 ||
            K_OWNER->at("request_sha256") != K_PIN("fusion_request_sha256") ||
            K_OWNER->at("pose_sha256") != K_PIN("pose_sha256") ||
            K_OWNER->at("request") != paths.at("fusion_request").string() ||
            K_OWNER->at("pose") != paths.at("poses").string() ||
            K_OWNER->at("receipt_sha256") != K_FUSION->at("receipt_sha256") ||
            K_OWNER->at("recipe_sha256") != K_FUSION->at("recipe_sha256")) {
            return fail(ErrorCode::INVALID_DATA, "Terminal geometry ownership differs");
        }
        const auto K_STAGE_NAME = K_TERMINAL->at("stage").get<std::string>();
        const auto K_PREFIX = "fusion-" + K_OWNER_SHA + "-attempt-";
        if (!K_STAGE_NAME.starts_with(K_PREFIX) || K_STAGE_NAME.size() <= K_PREFIX.size() ||
            !std::ranges::all_of(K_STAGE_NAME.substr(K_PREFIX.size()), [](char c) {
                return c >= '0' && c <= '9';
            })) {
            return fail(ErrorCode::INVALID_DATA, "Unsafe geometry terminal stage");
        }
        const auto K_STAGE = paths.at("job") / "stages" / K_STAGE_NAME;
        if (std::filesystem::canonical(K_STAGE) != K_STAGE) {
            return fail(ErrorCode::INVALID_DATA, "Linked geometry terminal stage");
        }
        const auto K_STAGE_SHA = K_TERMINAL->at("stage_manifest_sha256").get<std::string>();
        const auto K_MANIFEST = read_document(K_STAGE / "manifest.json", K_STAGE_SHA, stop);
        if (!K_MANIFEST) {
            return std::unexpected(K_MANIFEST.error());
        }
        if (K_MANIFEST->at("request_sha256") != K_PIN("fusion_request_sha256") ||
            K_MANIFEST->at("pose_asset_sha256") != K_PIN("pose_sha256") ||
            K_MANIFEST->at("receipt_sha256") != K_FUSION->at("receipt_sha256") ||
            K_MANIFEST->at("recipe_sha256") != K_FUSION->at("recipe_sha256")) {
            return fail(ErrorCode::INVALID_DATA, "Terminal geometry dependencies differ");
        }
        const auto& files = K_MANIFEST->at("files");
        if (!files.is_object() || files.size() != 2 || !files.contains("observed-voxels.npz") ||
            !files.contains("mesh.npz")) {
            return fail(ErrorCode::INVALID_DATA, "Geometry source inventory differs");
        }
        std::set<std::string> names {
            "manifest.json", "result.pending.json", "observed-voxels.npz", "mesh.npz" };
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGE)) {
            if (entry.is_symlink() || !entry.is_regular_file() ||
                names.erase(entry.path().filename().string()) != 1) {
                return fail(ErrorCode::INVALID_DATA, "Unexpected geometry source entry");
            }
        }
        if (!names.empty()) {
            return fail(ErrorCode::INVALID_DATA, "Incomplete geometry source");
        }
        for (const auto& [name, record] : files.items()) {
            if (record.size() != 2 || !record.at("bytes").is_number_unsigned() ||
                record.at("bytes").get<std::uint64_t>() > K_ASSET_BYTES ||
                std::filesystem::file_size(K_STAGE / name) != record.at("bytes")) {
                return fail(ErrorCode::INVALID_DATA, "Geometry source size differs");
            }
            const auto K_HASH = file_hash(K_STAGE / name, K_ASSET_BYTES, stop);
            if (!K_HASH) {
                return std::unexpected(K_HASH.error());
            }
            if (record.at("sha256") != *K_HASH) {
                return fail(ErrorCode::INVALID_DATA, "Geometry source checksum differs");
            }
        }
        const auto K_POSES = read_pose_asset(paths.at("poses"), K_PIN("pose_sha256"), {}, stop);
        if (!K_POSES) {
            return std::unexpected(K_POSES.error());
        }
        if (K_MANIFEST->at("world_epoch") != K_POSES->m_snapshot->m_epoch.m_value ||
            K_MANIFEST->at("pose_revision") != K_POSES->m_snapshot->m_revision.m_value) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Terminal geometry poses are stale");
        }
        const auto K_CONVERTER_SHA = file_hash(paths.at("converter"), K_DOCUMENT_BYTES, stop);
        if (!K_CONVERTER_SHA) {
            return std::unexpected(K_CONVERTER_SHA.error());
        }
        if (*K_CONVERTER_SHA != K_PIN("converter_sha256")) {
            return fail(ErrorCode::INVALID_DATA, "Geometry converter checksum differs");
        }
        // The output cannot contain even indirectly referenced source files.
        for (const auto& key : { "cache", "recipe" }) {
            const auto K_PATH =
                local_path(paths.at("fusion_request").parent_path(), K_FUSION->at(key));
            if (!K_PATH) {
                return std::unexpected(K_PATH.error());
            }
            if (overlaps(K_ROOT, *K_PATH)) {
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps a dependency");
            }
        }
        const auto K_PYTHON_SHA = sha256_file(paths.at("python"), K_ASSET_BYTES, stop);
        if (!K_PYTHON_SHA) {
            return std::unexpected(K_PYTHON_SHA.error());
        }
        const nlohmann::json K_NATIVE_OWNER {
            { "schema_version", 1 },
            { "kind", "NATIVE_GEOMETRY_ADOPTION_V1" },
            { "request", K_REQUEST.string() },
            { "request_sha256", expected_sha256 },
            { "source_owner_sha256", K_OWNER_SHA },
            { "source_stage_sha256", K_STAGE_SHA },
            { "python_sha256", *K_PYTHON_SHA },
            { "python_resolved", std::filesystem::canonical(paths.at("python")).string() } };
        auto lock = job_detail::open_owned_job(K_ROOT,
                                               { "owner.json", "writer.lock" },
                                               K_NATIVE_OWNER,
                                               { "stages", "manifest.json" },
                                               stop);
        if (!lock) {
            return std::unexpected(lock.error());
        }
        const auto K_NATIVE_OWNER_SHA = text_hash(K_NATIVE_OWNER.dump(2) + '\n');
        const auto K_STAGES = K_ROOT / "stages";
        std::filesystem::create_directories(K_STAGES);
        if (std::filesystem::canonical(K_STAGES) != K_STAGES) {
            return fail(ErrorCode::INVALID_DATA, "Linked native geometry stages");
        }
        // A terminal receipt pins a complete bundle; failed replay never repairs it.
        const auto K_TERMINAL_PATH = K_ROOT / "manifest.json";
        if (std::filesystem::exists(K_TERMINAL_PATH)) {
            const auto K_HASH = file_hash(K_TERMINAL_PATH, K_DOCUMENT_BYTES, stop);
            if (!K_HASH) {
                return std::unexpected(K_HASH.error());
            }
            const auto K_SAVED = read_document(K_TERMINAL_PATH, *K_HASH, stop);
            if (!K_SAVED) {
                return std::unexpected(K_SAVED.error());
            }
            if (K_SAVED->size() != 6 || K_SAVED->at("schema_version") != 1 ||
                K_SAVED->at("owner_sha256") != K_NATIVE_OWNER_SHA ||
                K_SAVED->at("scope").get<std::string>() != K_SCOPE) {
                return fail(ErrorCode::INVALID_DATA, "Native geometry terminal differs");
            }
            if (!K_SAVED->at("attempt").is_number_unsigned() || K_SAVED->at("attempt") >= 128U) {
                return fail(ErrorCode::INVALID_DATA, "Native geometry attempt exceeds limit");
            }
            const auto K_ATTEMPT = K_SAVED->at("attempt").get<std::uint32_t>();
            const auto K_DIRECTORY = K_STAGES / ("attempt-" + std::to_string(K_ATTEMPT));
            if (std::filesystem::canonical(K_DIRECTORY) != K_DIRECTORY) {
                return fail(ErrorCode::INVALID_DATA, "Linked native geometry attempt");
            }
            const auto K_ASSET = K_DIRECTORY / "observed.wefusion";
            FusionAdmissionRequest admission { K_ASSET,
                                              paths.at("poses"),
                                              K_SAVED->at("asset_sha256").get<std::string>(),
                                              K_PIN("pose_sha256"),
                                              K_STAGE_SHA,
                                              K_FUSION->at("receipt_sha256").get<std::string>(),
                                              K_FUSION->at("recipe_sha256").get<std::string>() };
            const auto K_ADMITTED = load_fusion_scene(admission, stop);
            if (!K_ADMITTED) {
                return std::unexpected(K_ADMITTED.error());
            }
            auto bundle = K_DIRECTORY / "scene.wexplore";
            if (std::filesystem::canonical(bundle) != bundle) {
                return fail(ErrorCode::INVALID_DATA, "Linked native geometry bundle");
            }
            const auto K_BUNDLE_SHA = file_hash(bundle / "manifest.json", K_DOCUMENT_BYTES, stop);
            if (!K_BUNDLE_SHA) {
                return std::unexpected(K_BUNDLE_SHA.error());
            }
            if (K_SAVED->at("bundle_manifest_sha256") != *K_BUNDLE_SHA) {
                return fail(ErrorCode::INVALID_DATA, "Committed geometry bundle checksum differs");
            }
            const auto K_LOADED = load_bundle(bundle);
            if (!K_LOADED) {
                return std::unexpected(K_LOADED.error());
            }
            if (scene_content_hash(**K_LOADED) != scene_content_hash(**K_ADMITTED)) {
                return fail(ErrorCode::INVALID_DATA,
                            "Geometry bundle differs from admitted source");
            }
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Geometry replay cancelled");
            }
            if (log) {
                log("Reopened pose-admitted partial geometry; conversion skipped.\n");
            }
            return bundle;
        }
        // Reserve a fresh attempt. Existing partial data is never overwritten or adopted.
        std::size_t entries {};
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGES)) {
            if (++entries > 128uz || entry.is_symlink() || !entry.is_directory()) {
                return fail(ErrorCode::INVALID_DATA, "Invalid native geometry attempt inventory");
            }
            const auto K_NAME = entry.path().filename().string();
            bool recognized {};
            for (std::size_t index { 0uz }; index < 128uz; ++index) {
                if (K_NAME == "attempt-" + std::to_string(index)) {
                    recognized = true;
                }
            }
            if (!recognized) {
                return fail(ErrorCode::INVALID_DATA, "Unknown native geometry attempt");
            }
        }
        for (std::size_t index { 0uz }; index < 128uz; ++index) {
            const auto K_DIRECTORY = K_STAGES / ("attempt-" + std::to_string(index));
            if (!std::filesystem::create_directory(K_DIRECTORY)) {
                continue;
            }
            const auto K_ASSET = K_DIRECTORY / "observed.wefusion";
            if (log) {
                log("Converting the pinned terminal partial geometry.\n");
            }
            const auto K_PROCESS = execute_process({ paths.at("python").string(),
                                                    paths.at("converter").string(),
                                                    K_STAGE.string(),
                                                    K_STAGE_SHA,
                                                    paths.at("fusion_request").string(),
                                                    K_PIN("fusion_request_sha256"),
                                                    K_ASSET.string() },
                                                   stop,
                                                   log,
                                                   log);
            if (!K_PROCESS) {
                return std::unexpected(K_PROCESS.error());
            }
            if (*K_PROCESS != 0) {
                return fail(ErrorCode::BACKEND_FAILURE,
                            "Geometry conversion failed; attempt retained");
            }
            if (log) {
                log("Converted partial geometry; checking current poses before publication.\n");
            }
            const auto K_ASSET_SHA = file_hash(K_ASSET, K_ASSET_BYTES, stop);
            if (!K_ASSET_SHA) {
                return std::unexpected(K_ASSET_SHA.error());
            }
            FusionAdmissionRequest admission { K_ASSET,
                                              paths.at("poses"),
                                              *K_ASSET_SHA,
                                              K_PIN("pose_sha256"),
                                              K_STAGE_SHA,
                                              K_FUSION->at("receipt_sha256").get<std::string>(),
                                              K_FUSION->at("recipe_sha256").get<std::string>() };
            const auto K_SCENE = load_fusion_scene(admission, stop);
            if (!K_SCENE) {
                return std::unexpected(K_SCENE.error());
            }
            const auto K_FINAL_CONVERTER = file_hash(paths.at("converter"), K_DOCUMENT_BYTES, stop);
            if (!K_FINAL_CONVERTER) {
                return std::unexpected(K_FINAL_CONVERTER.error());
            }
            if (*K_FINAL_CONVERTER != *K_CONVERTER_SHA) {
                return fail(ErrorCode::INVALID_DATA, "Geometry converter changed during execution");
            }
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED,
                            "Geometry publication cancelled; attempt retained");
            }
            auto bundle = K_DIRECTORY / "scene.wexplore";
            const auto K_WRITTEN = write_bundle(bundle, *K_SCENE);
            if (!K_WRITTEN) {
                return std::unexpected(K_WRITTEN.error());
            }
            const auto K_BUNDLE_SHA = file_hash(bundle / "manifest.json", K_DOCUMENT_BYTES, {});
            if (!K_BUNDLE_SHA) {
                return std::unexpected(K_BUNDLE_SHA.error());
            }
            const nlohmann::json K_RESULT { { "schema_version", 1 },
                                           { "owner_sha256", K_NATIVE_OWNER_SHA },
                                           { "attempt", index },
                                           { "asset_sha256", *K_ASSET_SHA },
                                           { "bundle_manifest_sha256", *K_BUNDLE_SHA },
                                           { "scope", K_SCOPE } };
            // Once bundle publication starts, finish its terminal receipt as one commit boundary.
            const auto K_TEXT = K_RESULT.dump(2) + '\n';
            const auto K_PUBLISHED =
                asset_files::publish_asset(K_TERMINAL_PATH, std::as_bytes(std::span(K_TEXT)), {});
            if (!K_PUBLISHED) {
                return std::unexpected(K_PUBLISHED.error());
            }
            return bundle;
        }
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry job exhausted 128 retained attempts");
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry job allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Geometry adoption failed: ") + exception.what());
    }
}

}  // namespace world_explorer
