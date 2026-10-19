#include "world_explorer/application/geometry_job.hpp"

#include "job_validation.hpp"
#include "owned_job.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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
    if (!job_detail::valid_digest(pin))
        return fail(ErrorCode::INVALID_DATA, "Geometry document requires a lowercase SHA-256");
    const auto K_BYTES = asset_files::read_bytes(path, K_DOCUMENT_BYTES, stop);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    if (sha256(*K_BYTES) != pin)
        return fail(ErrorCode::INVALID_DATA, "Geometry document checksum differs");
    const std::string K_TEXT { reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size() };
    if (K_TEXT.contains('\0'))
        return fail(ErrorCode::INVALID_DATA, "Geometry document contains raw NUL");
    std::vector<std::set<std::string>> keys;
    auto document = nlohmann::json::parse(
        K_TEXT, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            if (depth > 32) throw std::runtime_error("Geometry document nesting exceeds limit");
            if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate geometry document key");
            if (event == nlohmann::json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
    if (!document.is_object()) return fail(ErrorCode::INVALID_DATA, "Expected geometry object");
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
    if (K_NAME.empty() || K_NAME.contains('\0'))
        return fail(ErrorCode::INVALID_DATA, "Empty or invalid geometry input path");
    const auto K_PATH = std::filesystem::absolute(base / K_NAME).lexically_normal();
    if (!executable && std::filesystem::canonical(K_PATH) != K_PATH)
        return fail(ErrorCode::INVALID_DATA, "Geometry input path may not use symlinks");
    return std::filesystem::path { K_PATH };
}

Result<std::string>
file_hash(const std::filesystem::path& path, std::size_t maximum, const std::stop_token& stop) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        return fail(ErrorCode::INVALID_DATA, "Geometry asset is missing, linked or nonregular");
    return sha256_file(path, maximum, stop);
}

}  // namespace

Result<std::filesystem::path> adopt_geometry_job(const std::filesystem::path& request,
                                                 const std::string& expected_sha256,
                                                 const std::filesystem::path& destination,
                                                 const std::stop_token& stop,
                                                 const LogCallback& log) {
    try {
        const auto K_REQUEST = std::filesystem::absolute(request).lexically_normal();
        const auto K_CONFIG = read_document(K_REQUEST, expected_sha256, stop);
        if (!K_CONFIG) return std::unexpected(K_CONFIG.error());
        const std::set<std::string> K_FIELDS {"schema_version",
                                              "job",
                                              "job_manifest_sha256",
                                              "fusion_request",
                                              "fusion_request_sha256",
                                              "poses",
                                              "pose_sha256",
                                              "python",
                                              "converter",
                                              "converter_sha256"};
        if (!K_CONFIG->at("schema_version").is_number_unsigned() ||
            K_CONFIG->at("schema_version") != 1 || K_CONFIG->size() != K_FIELDS.size() ||
            !std::ranges::all_of(K_CONFIG->items(),
                                 [&](const auto& item) { return K_FIELDS.contains(item.key()); }))
            return fail(ErrorCode::INVALID_DATA, "Unsupported native geometry job request");
        const auto K_ROOT = std::filesystem::absolute(destination).lexically_normal();
        if (std::filesystem::weakly_canonical(K_ROOT) != K_ROOT)
            return fail(ErrorCode::INVALID_DATA, "Geometry output may not use symlinks");
        std::map<std::string, std::filesystem::path> paths;
        for (const auto& key : { "job", "fusion_request", "poses", "python", "converter" }) {
            const auto K_PATH = local_path(
                K_REQUEST.parent_path(), K_CONFIG->at(key), std::string_view(key) == "python");
            if (!K_PATH) return std::unexpected(K_PATH.error());
            if (overlaps(K_ROOT, *K_PATH))
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps an input");
            if (overlaps(K_ROOT, std::filesystem::canonical(*K_PATH)))
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps a resolved input");
            paths.emplace(key, *K_PATH);
        }
        if (overlaps(K_ROOT, K_REQUEST))
            return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps its request");
        const auto K_PIN = [&](const char* key) { return K_CONFIG->at(key).get<std::string>(); };
        const auto K_TERMINAL =
            read_document(paths.at("job") / "manifest.json", K_PIN("job_manifest_sha256"), stop);
        if (!K_TERMINAL) return std::unexpected(K_TERMINAL.error());
        if (K_TERMINAL->size() != 6 || K_TERMINAL->at("schema_version") != 1 ||
            K_TERMINAL->at("kind") != "OWNED_PARTIAL_OBSERVED_GEOMETRY_V1" ||
            K_TERMINAL->at("scope").get<std::string>() != K_SCOPE)
            return fail(ErrorCode::INVALID_DATA, "Unsupported terminal geometry job");
        const auto K_OWNER_SHA = K_TERMINAL->at("owner_sha256").get<std::string>();
        const auto K_OWNER = read_document(paths.at("job") / "owner.json", K_OWNER_SHA, stop);
        const auto K_FUSION =
            read_document(paths.at("fusion_request"), K_PIN("fusion_request_sha256"), stop);
        if (!K_OWNER) return std::unexpected(K_OWNER.error());
        if (!K_FUSION) return std::unexpected(K_FUSION.error());
        if (K_OWNER->at("kind") != "RETAINED_OBSERVED_GEOMETRY_JOB_V1" ||
            K_OWNER->at("schema_version") != 1 ||
            K_OWNER->at("request_sha256") != K_PIN("fusion_request_sha256") ||
            K_OWNER->at("pose_sha256") != K_PIN("pose_sha256") ||
            K_OWNER->at("request") != paths.at("fusion_request").string() ||
            K_OWNER->at("pose") != paths.at("poses").string() ||
            K_OWNER->at("receipt_sha256") != K_FUSION->at("receipt_sha256") ||
            K_OWNER->at("recipe_sha256") != K_FUSION->at("recipe_sha256"))
            return fail(ErrorCode::INVALID_DATA, "Terminal geometry ownership differs");
        const auto K_STAGE_NAME = K_TERMINAL->at("stage").get<std::string>();
        const auto K_PREFIX = "fusion-" + K_OWNER_SHA + "-attempt-";
        if (!K_STAGE_NAME.starts_with(K_PREFIX) || K_STAGE_NAME.size() <= K_PREFIX.size() ||
            !std::ranges::all_of(K_STAGE_NAME.substr(K_PREFIX.size()),
                                 [](char c) { return c >= '0' && c <= '9'; }))
            return fail(ErrorCode::INVALID_DATA, "Unsafe geometry terminal stage");
        const auto K_STAGE = paths.at("job") / "stages" / K_STAGE_NAME;
        if (std::filesystem::canonical(K_STAGE) != K_STAGE)
            return fail(ErrorCode::INVALID_DATA, "Linked geometry terminal stage");
        const auto K_STAGE_SHA = K_TERMINAL->at("stage_manifest_sha256").get<std::string>();
        const auto K_MANIFEST = read_document(K_STAGE / "manifest.json", K_STAGE_SHA, stop);
        if (!K_MANIFEST) return std::unexpected(K_MANIFEST.error());
        if (K_MANIFEST->at("request_sha256") != K_PIN("fusion_request_sha256") ||
            K_MANIFEST->at("pose_asset_sha256") != K_PIN("pose_sha256") ||
            K_MANIFEST->at("receipt_sha256") != K_FUSION->at("receipt_sha256") ||
            K_MANIFEST->at("recipe_sha256") != K_FUSION->at("recipe_sha256"))
            return fail(ErrorCode::INVALID_DATA, "Terminal geometry dependencies differ");
        const auto& files = K_MANIFEST->at("files");
        if (!files.is_object() || files.size() != 2 || !files.contains("observed-voxels.npz") ||
            !files.contains("mesh.npz"))
            return fail(ErrorCode::INVALID_DATA, "Geometry source inventory differs");
        std::set<std::string> names {
            "manifest.json", "result.pending.json", "observed-voxels.npz", "mesh.npz"};
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGE))
            if (entry.is_symlink() || !entry.is_regular_file() ||
                names.erase(entry.path().filename().string()) != 1)
                return fail(ErrorCode::INVALID_DATA, "Unexpected geometry source entry");
        if (!names.empty()) return fail(ErrorCode::INVALID_DATA, "Incomplete geometry source");
        for (const auto& [name, record] : files.items()) {
            if (record.size() != 2 || !record.at("bytes").is_number_unsigned() ||
                record.at("bytes").get<std::uint64_t>() > K_ASSET_BYTES ||
                std::filesystem::file_size(K_STAGE / name) != record.at("bytes"))
                return fail(ErrorCode::INVALID_DATA, "Geometry source size differs");
            const auto K_HASH = file_hash(K_STAGE / name, K_ASSET_BYTES, stop);
            if (!K_HASH) return std::unexpected(K_HASH.error());
            if (record.at("sha256") != *K_HASH)
                return fail(ErrorCode::INVALID_DATA, "Geometry source checksum differs");
        }
        const auto K_POSES = read_pose_asset(paths.at("poses"), K_PIN("pose_sha256"), {}, stop);
        if (!K_POSES) return std::unexpected(K_POSES.error());
        if (K_MANIFEST->at("world_epoch") != K_POSES->m_snapshot->m_epoch.m_value ||
            K_MANIFEST->at("pose_revision") != K_POSES->m_snapshot->m_revision.m_value)
            return fail(ErrorCode::RESYNC_REQUIRED, "Terminal geometry poses are stale");
        const auto K_CONVERTER_SHA = file_hash(paths.at("converter"), K_DOCUMENT_BYTES, stop);
        if (!K_CONVERTER_SHA) return std::unexpected(K_CONVERTER_SHA.error());
        if (*K_CONVERTER_SHA != K_PIN("converter_sha256"))
            return fail(ErrorCode::INVALID_DATA, "Geometry converter checksum differs");
        // The output cannot contain even indirectly referenced source files.
        for (const auto& key : { "cache", "recipe" }) {
            const auto K_PATH =
                local_path(paths.at("fusion_request").parent_path(), K_FUSION->at(key));
            if (!K_PATH) return std::unexpected(K_PATH.error());
            if (overlaps(K_ROOT, *K_PATH))
                return fail(ErrorCode::INVALID_DATA, "Geometry output overlaps a dependency");
        }
        const auto K_PYTHON_SHA = sha256_file(paths.at("python"), K_ASSET_BYTES, stop);
        if (!K_PYTHON_SHA) return std::unexpected(K_PYTHON_SHA.error());
        const nlohmann::json K_NATIVE_OWNER {
            { "schema_version", 1 },
            { "kind", "NATIVE_GEOMETRY_ADOPTION_V1" },
            { "request", K_REQUEST.string() },
            { "request_sha256", expected_sha256 },
            { "source_owner_sha256", K_OWNER_SHA },
            { "source_stage_sha256", K_STAGE_SHA },
            { "python_sha256", *K_PYTHON_SHA },
            { "python_resolved", std::filesystem::canonical(paths.at("python")).string() }};
        auto lock = job_detail::open_owned_job(K_ROOT,
                                               { "owner.json", "writer.lock" },
                                               K_NATIVE_OWNER,
                                               { "stages", "manifest.json" },
                                               stop);
        if (!lock) return std::unexpected(lock.error());
        const auto K_NATIVE_OWNER_SHA = text_hash(K_NATIVE_OWNER.dump(2) + '\n');
        const auto K_STAGES = K_ROOT / "stages";
        std::filesystem::create_directories(K_STAGES);
        if (std::filesystem::canonical(K_STAGES) != K_STAGES)
            return fail(ErrorCode::INVALID_DATA, "Linked native geometry stages");
        // A terminal receipt pins a complete bundle; failed replay never repairs it.
        const auto K_TERMINAL_PATH = K_ROOT / "manifest.json";
        if (std::filesystem::exists(K_TERMINAL_PATH)) {
            const auto K_HASH = file_hash(K_TERMINAL_PATH, K_DOCUMENT_BYTES, stop);
            if (!K_HASH) return std::unexpected(K_HASH.error());
            const auto K_SAVED = read_document(K_TERMINAL_PATH, *K_HASH, stop);
            if (!K_SAVED) return std::unexpected(K_SAVED.error());
            if (K_SAVED->size() != 6 || K_SAVED->at("schema_version") != 1 ||
                K_SAVED->at("owner_sha256") != K_NATIVE_OWNER_SHA ||
                K_SAVED->at("scope").get<std::string>() != K_SCOPE)
                return fail(ErrorCode::INVALID_DATA, "Native geometry terminal differs");
            if (!K_SAVED->at("attempt").is_number_unsigned() || K_SAVED->at("attempt") >= 128U)
                return fail(ErrorCode::INVALID_DATA, "Native geometry attempt exceeds limit");
            const auto K_ATTEMPT = K_SAVED->at("attempt").get<std::uint32_t>();
            const auto K_DIRECTORY = K_STAGES / ("attempt-" + std::to_string(K_ATTEMPT));
            if (std::filesystem::canonical(K_DIRECTORY) != K_DIRECTORY)
                return fail(ErrorCode::INVALID_DATA, "Linked native geometry attempt");
            const auto K_ASSET = K_DIRECTORY / "observed.wefusion";
            FusionAdmissionRequest admission {K_ASSET,
                                              paths.at("poses"),
                                              K_SAVED->at("asset_sha256").get<std::string>(),
                                              K_PIN("pose_sha256"),
                                              K_STAGE_SHA,
                                              K_FUSION->at("receipt_sha256").get<std::string>(),
                                              K_FUSION->at("recipe_sha256").get<std::string>()};
            const auto K_ADMITTED = load_fusion_scene(admission, stop);
            if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
            auto bundle = K_DIRECTORY / "scene.wexplore";
            if (std::filesystem::canonical(bundle) != bundle)
                return fail(ErrorCode::INVALID_DATA, "Linked native geometry bundle");
            const auto K_BUNDLE_SHA = file_hash(bundle / "manifest.json", K_DOCUMENT_BYTES, stop);
            if (!K_BUNDLE_SHA) return std::unexpected(K_BUNDLE_SHA.error());
            if (K_SAVED->at("bundle_manifest_sha256") != *K_BUNDLE_SHA)
                return fail(ErrorCode::INVALID_DATA, "Committed geometry bundle checksum differs");
            const auto K_LOADED = load_bundle(bundle);
            if (!K_LOADED) return std::unexpected(K_LOADED.error());
            if (scene_content_hash(**K_LOADED) != scene_content_hash(**K_ADMITTED))
                return fail(ErrorCode::INVALID_DATA,
                            "Geometry bundle differs from admitted source");
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Geometry replay cancelled");
            if (log) log("Reopened pose-admitted partial geometry; conversion skipped.\n");
            return bundle;
        }
        // Reserve a fresh attempt. Existing partial data is never overwritten or adopted.
        std::size_t entries {};
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGES)) {
            if (++entries > 128uz || entry.is_symlink() || !entry.is_directory())
                return fail(ErrorCode::INVALID_DATA, "Invalid native geometry attempt inventory");
            const auto K_NAME = entry.path().filename().string();
            bool recognized {};
            for (std::size_t index { 0uz }; index < 128uz; ++index)
                if (K_NAME == "attempt-" + std::to_string(index)) recognized = true;
            if (!recognized)
                return fail(ErrorCode::INVALID_DATA, "Unknown native geometry attempt");
        }
        for (std::size_t index { 0uz }; index < 128uz; ++index) {
            const auto K_DIRECTORY = K_STAGES / ("attempt-" + std::to_string(index));
            if (!std::filesystem::create_directory(K_DIRECTORY)) continue;
            const auto K_ASSET = K_DIRECTORY / "observed.wefusion";
            if (log) log("Converting the pinned terminal partial geometry.\n");
            const auto K_PROCESS = execute_process({paths.at("python").string(),
                                                    paths.at("converter").string(),
                                                    K_STAGE.string(),
                                                    K_STAGE_SHA,
                                                    paths.at("fusion_request").string(),
                                                    K_PIN("fusion_request_sha256"),
                                                    K_ASSET.string()},
                                                   stop,
                                                   log,
                                                   log);
            if (!K_PROCESS) return std::unexpected(K_PROCESS.error());
            if (*K_PROCESS != 0)
                return fail(ErrorCode::BACKEND_FAILURE,
                            "Geometry conversion failed; attempt retained");
            if (log)
                log("Converted partial geometry; checking current poses before publication.\n");
            const auto K_ASSET_SHA = file_hash(K_ASSET, K_ASSET_BYTES, stop);
            if (!K_ASSET_SHA) return std::unexpected(K_ASSET_SHA.error());
            FusionAdmissionRequest admission {K_ASSET,
                                              paths.at("poses"),
                                              *K_ASSET_SHA,
                                              K_PIN("pose_sha256"),
                                              K_STAGE_SHA,
                                              K_FUSION->at("receipt_sha256").get<std::string>(),
                                              K_FUSION->at("recipe_sha256").get<std::string>()};
            const auto K_SCENE = load_fusion_scene(admission, stop);
            if (!K_SCENE) return std::unexpected(K_SCENE.error());
            const auto K_FINAL_CONVERTER = file_hash(paths.at("converter"), K_DOCUMENT_BYTES, stop);
            if (!K_FINAL_CONVERTER) return std::unexpected(K_FINAL_CONVERTER.error());
            if (*K_FINAL_CONVERTER != *K_CONVERTER_SHA)
                return fail(ErrorCode::INVALID_DATA, "Geometry converter changed during execution");
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED,
                            "Geometry publication cancelled; attempt retained");
            auto bundle = K_DIRECTORY / "scene.wexplore";
            const auto K_WRITTEN = write_bundle(bundle, *K_SCENE);
            if (!K_WRITTEN) return std::unexpected(K_WRITTEN.error());
            const auto K_BUNDLE_SHA = file_hash(bundle / "manifest.json", K_DOCUMENT_BYTES, {});
            if (!K_BUNDLE_SHA) return std::unexpected(K_BUNDLE_SHA.error());
            const nlohmann::json K_RESULT {{ "schema_version", 1 },
                                           { "owner_sha256", K_NATIVE_OWNER_SHA },
                                           { "attempt", index },
                                           { "asset_sha256", *K_ASSET_SHA },
                                           { "bundle_manifest_sha256", *K_BUNDLE_SHA },
                                           { "scope", K_SCOPE }};
            // Once bundle publication starts, finish its terminal receipt as one commit boundary.
            const auto K_TEXT = K_RESULT.dump(2) + '\n';
            const auto K_PUBLISHED =
                asset_files::publish_asset(K_TERMINAL_PATH, std::as_bytes(std::span(K_TEXT)), {});
            if (!K_PUBLISHED) return std::unexpected(K_PUBLISHED.error());
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
