#include "mesh_job.hpp"

#include "color_inputs.hpp"
#include "job_validation.hpp"
#include "owned_job.hpp"
#include "world_explorer/application/fusion_cleanup.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/application/gltf.hpp"
#include "world_explorer/application/mesh_texture.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <charconv>
#include <functional>
#include <set>

namespace world_explorer::job_detail {
namespace {

using Json = nlohmann::json;
constexpr std::size_t K_DOCUMENT_BYTES { 1024uz * 1024uz };
constexpr std::size_t K_STAGE_BYTES { 3uz * 1024uz * 1024uz * 1024uz };

class JobFailure final : public std::exception {
  public:
    explicit JobFailure(Error error) : m_error(std::move(error)) {}

    const char* what() const noexcept override {
        return m_error.m_message.c_str();
    }

    Error m_error;
};

template <class Value> Value take(Result<Value> result) {
    if (!result) {
        throw JobFailure { std::move(result.error()) };
    }
    return std::move(*result);
}

void check(Result<void> result) {
    if (!result) {
        throw JobFailure { std::move(result.error()) };
    }
}

void require(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void cancelled(const std::stop_token& stop) {
    if (stop.stop_requested()) {
        throw JobFailure { { ErrorCode::CANCELLED, "Mesh job cancelled; stages retained" } };
    }
}

std::string text_hash(const Json& document) {
    const auto K_TEXT = document.dump(2) + '\n';
    return sha256(std::as_bytes(std::span(K_TEXT)));
}

bool present(const std::filesystem::path& path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}

std::filesystem::path local_path(const std::filesystem::path& base, const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    require(!K_TEXT.empty() && K_TEXT.find('\0') == K_TEXT.npos, "Invalid mesh job input path");
    const auto K_PATH = std::filesystem::absolute(base / K_TEXT).lexically_normal();
    require(std::filesystem::canonical(K_PATH) == K_PATH, "Linked mesh job input");
    return K_PATH;
}

Json document(const std::filesystem::path& path,
              const std::string& pin,
              const std::stop_token& stop) {
    require(pin.empty() || valid_digest(pin), "Invalid mesh job digest");
    require(std::filesystem::canonical(path) == path && std::filesystem::is_regular_file(path),
            "Linked or nonregular mesh job document");
    const auto K_BYTES = take(asset_files::read_bytes(path, K_DOCUMENT_BYTES, stop));
    require(pin.empty() || sha256(K_BYTES) == pin, "Mesh job document checksum differs");
    const std::string K_TEXT { reinterpret_cast<const char*>(K_BYTES.data()), K_BYTES.size() };
    require(K_TEXT.find('\0') == K_TEXT.npos, "Raw NUL in mesh job document");
    std::vector<std::set<std::string>> keys;
    auto result = Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 8, "Mesh job JSON nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    "Duplicate mesh job JSON key");
        }
        return true;
    });
    require(result.is_object(), "Expected mesh job object");
    return result;
}

struct Input {
    std::filesystem::path m_path;
    std::string m_sha256;
};

Input input(const std::filesystem::path& base, const Json& value) {
    if (value.is_null()) {
        return {};
    }
    require(value.is_object() && value.size() == 2uz, "Invalid mesh job input reference");
    auto pin = value.at("sha256").get<std::string>();
    require(valid_digest(pin), "Invalid mesh job input pin");
    return { local_path(base, value.at("path")), std::move(pin) };
}

SceneSnapshot bundle(const Input& reference, const std::stop_token& stop) {
    static_cast<void>(document(reference.m_path / "manifest.json", reference.m_sha256, stop));
    return take(load_bundle(reference.m_path));
}

void same_geometry(const SceneState& expected, const SceneState& actual) {
    require(expected.m_id == actual.m_id && expected.m_epoch == actual.m_epoch &&
                expected.m_revision == actual.m_revision && expected.m_scale == actual.m_scale &&
                expected.m_chunks.size() == actual.m_chunks.size(),
            "Mesh job scene identity differs");
    for (const auto& [K_ID, K_CHUNK] : expected.m_chunks) {
        const auto K_FOUND = actual.m_chunks.find(K_ID);
        require(K_FOUND != actual.m_chunks.end() &&
                    scene_untextured_chunk_hash(*K_CHUNK) ==
                        scene_untextured_chunk_hash(*K_FOUND->second),
                "Mesh job geometry or pose dependencies differ");
    }
}

bool baked(const SceneState& scene) {
    bool complete { true };
    bool any {};
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        static_cast<void>(K_ID);
        if (!K_CHUNK->m_geometry_valid || !K_CHUNK->m_splats.empty() || !K_CHUNK->m_mesh) {
            throw std::runtime_error("Mesh job requires current mesh-only chunks");
        }
        if (K_CHUNK->m_mesh->m_baked) {
            any = true;
            require(K_CHUNK->m_mesh->m_baked->m_portable_parent_sha256.empty(),
                    "Mesh job requires original bakes, not lossy portable previews");
        } else {
            complete = false;
        }
    }
    require(!scene.m_world_frame && !scene.m_chunks.empty() &&
                scene.m_scale == ScaleStatus::ARBITRARY,
            "Mesh job requires nonempty arbitrary-scale geometry");
    require(!any || complete, "Mesh job cannot implicitly replace partially baked scenes");
    return complete;
}

bool within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto K_RELATIVE = child.lexically_relative(parent);
    return !K_RELATIVE.empty() && *K_RELATIVE.begin() != "..";
}

void separate(const std::filesystem::path& root, const std::filesystem::path& source) {
    require(!within(root, source) && !within(source, root), "Mesh job overlaps an immutable input");
}

// Hash every regular file and reject links, extra directories and unbounded inventories.
Json inventory(const std::filesystem::path& root, const std::stop_token& stop) {
    Json result = Json::object();
    std::size_t bytes {}, entries {};
    for (auto iterator = std::filesystem::recursive_directory_iterator(root);
         iterator != std::filesystem::recursive_directory_iterator();
         ++iterator) {
        cancelled(stop);
        require(++entries <= 40uz && iterator.depth() <= 2 && !iterator->is_symlink(),
                "Unsafe mesh stage inventory");
        if (iterator->is_directory()) {
            require(iterator->path() == root / "output", "Unknown mesh stage directory");
            continue;
        }
        require(iterator->is_regular_file(), "Nonregular mesh stage asset");
        const auto K_SIZE = std::filesystem::file_size(iterator->path());
        require(K_SIZE <= K_STAGE_BYTES - bytes, "Mesh stage payload exceeds budget");
        bytes += static_cast<std::size_t>(K_SIZE);
        result[iterator->path().lexically_relative(root).generic_string()] = {
            { "bytes", K_SIZE },
            { "sha256", take(sha256_file(iterator->path(), K_STAGE_BYTES, stop)) } };
    }
    return result;
}

std::size_t attempt_index(std::string_view text) {
    std::size_t value {};
    const auto K_PARSED = std::from_chars(text.data(), text.data() + text.size(), value);
    require(K_PARSED.ec == std::errc {} && K_PARSED.ptr == text.data() + text.size() &&
                value < 128uz && std::to_string(value) == text,
            "Invalid mesh stage attempt");
    return value;
}

class Stages {
  public:
    std::filesystem::path m_root;
    std::string m_dependency;
    std::stop_token m_stop;
    LogCallback m_log;
    Json m_receipts = Json::object();

    std::filesystem::path run(const std::string& name,
                              const std::function<void(const std::filesystem::path&)>& produce) {
        cancelled(m_stop);
        const auto K_RECEIPT = m_root / (name + ".json");
        std::filesystem::path attempt;
        Json receipt;
        if (present(K_RECEIPT)) {
            receipt = document(K_RECEIPT, {}, m_stop);
            const auto& index = receipt.at("attempt");
            require(index.is_number_unsigned() && index.get<std::uint64_t>() < 128,
                    "Invalid mesh receipt attempt");
            attempt = m_root / "stages" / (name + "-" + std::to_string(index.get<std::uint64_t>()));
            require(std::filesystem::canonical(attempt) == attempt, "Linked completed mesh stage");
            const Json K_EXPECTED { { "schema_version", 1U },
                                   { "stage", name },
                                   { "dependency_sha256", m_dependency },
                                   { "attempt", index },
                                   { "files", inventory(attempt, m_stop) } };
            require(receipt.dump() == K_EXPECTED.dump(),
                    "Completed mesh stage differs; refusing repair");
            if (m_log) {
                m_log("Reusing completed mesh stage: " + name + "\n");
            }
        } else {
            for (std::size_t index { 0uz }; index < 128uz; ++index) {
                attempt = m_root / "stages" / (name + "-" + std::to_string(index));
                if (present(attempt)) {
                    continue;
                }
                require(std::filesystem::create_directory(attempt), "Cannot reserve mesh attempt");
                if (m_log) {
                    m_log("Starting mesh stage: " + name + "\n");
                }
                cancelled(m_stop);
                produce(attempt);
                // Complete an output and its receipt as one commit boundary, even after late
                // cancel.
                receipt = { { "schema_version", 1U },
                           { "stage", name },
                           { "dependency_sha256", m_dependency },
                           { "attempt", index },
                           { "files", inventory(attempt, {}) } };
                check(publish_owned_json(K_RECEIPT, receipt, {}));
                if (m_log) {
                    m_log("Completed mesh stage: " + name + "\n");
                }
                break;
            }
            require(!receipt.empty(), "Mesh job exhausted 128 retained attempts");
        }
        m_dependency = text_hash(receipt);
        m_receipts[name] = m_dependency;
        return attempt / "output";
    }
};

}  // namespace

Result<std::filesystem::path> run_mesh_job(const std::filesystem::path& request,
                                           const std::string& expected_sha256,
                                           const std::filesystem::path& destination,
                                           const std::stop_token& stop,
                                           const LogCallback& log) {
    try {
        cancelled(stop);
        require(valid_digest(expected_sha256), "Mesh job request requires SHA-256");
        const auto K_REQUEST = std::filesystem::absolute(request).lexically_normal();
        const auto K_CONFIG = document(K_REQUEST, expected_sha256, stop);
        const std::set<std::string> K_FIELDS { "schema_version",
                                              "kind",
                                              "source_bundle",
                                              "source_manifest_sha256",
                                              "cleanup_request",
                                              "bake_request",
                                              "reuse_baked_bundle",
                                              "export_glb" };
        require(K_CONFIG.size() == K_FIELDS.size() &&
                    K_CONFIG.at("schema_version").is_number_unsigned() &&
                    K_CONFIG.at("schema_version") == 1 &&
                    K_CONFIG.at("kind") == "MESH_POSTPROCESS_PIPELINE_V1",
                "Invalid mesh job schema");
        for (const auto& key : K_FIELDS) {
            require(K_CONFIG.contains(key), "Missing mesh job field");
        }
        require(K_CONFIG.at("export_glb").is_boolean(), "Invalid mesh export selection");
        const auto K_BASE = K_REQUEST.parent_path();
        const Input K_SOURCE { local_path(K_BASE, K_CONFIG.at("source_bundle")),
                              K_CONFIG.at("source_manifest_sha256").get<std::string>() };
        require(valid_digest(K_SOURCE.m_sha256), "Invalid source bundle pin");
        const auto K_CLEANUP = input(K_BASE, K_CONFIG.at("cleanup_request"));
        const auto K_BAKE = input(K_BASE, K_CONFIG.at("bake_request"));
        const auto K_REUSE = input(K_BASE, K_CONFIG.at("reuse_baked_bundle"));
        require(!destination.empty(), "Missing mesh job destination");
        const auto K_ROOT = std::filesystem::absolute(destination).lexically_normal();
        require(std::filesystem::weakly_canonical(K_ROOT) == K_ROOT, "Linked mesh job destination");
        for (const auto& path :
             { K_REQUEST, K_SOURCE.m_path, K_CLEANUP.m_path, K_BAKE.m_path, K_REUSE.m_path }) {
            if (!path.empty()) {
                separate(K_ROOT, path);
            }
        }
        auto source = bundle(K_SOURCE, stop);
        const bool K_ALREADY_BAKED { baked(*source) };
        require(!K_ALREADY_BAKED ||
                    (K_CLEANUP.m_path.empty() && K_BAKE.m_path.empty() && K_REUSE.m_path.empty()),
                "Cannot replace an original source bake implicitly");
        require(!K_CONFIG.at("export_glb").get<bool>() || K_ALREADY_BAKED ||
                    !K_BAKE.m_path.empty() || !K_REUSE.m_path.empty(),
                "GLB export requires a bake");
        Json cleanup, color;
        const auto K_ADMIT = [&] {
            same_geometry(*source, *bundle(K_SOURCE, stop));
            if (!K_CLEANUP.m_path.empty()) {
                cleanup = document(K_CLEANUP.m_path, K_CLEANUP.m_sha256, stop);
                require(cleanup.size() == 8uz,
                        "Cleanup stage requires an original raw fusion request");
                for (const auto* key : { "asset", "poses" }) {
                    separate(K_ROOT, local_path(K_CLEANUP.m_path.parent_path(), cleanup.at(key)));
                }
                same_geometry(
                    *source,
                    *take(load_fusion_scene_request(K_CLEANUP.m_path, K_CLEANUP.m_sha256, stop)));
            }
            if (!K_BAKE.m_path.empty()) {
                color = document(K_BAKE.m_path, K_BAKE.m_sha256, stop);
                const auto K_INPUT = take(color_inputs::load(
                    K_BAKE.m_path, K_BAKE.m_sha256, K_ROOT / "validation-probe", stop));
                same_geometry(*source, *K_INPUT.m_scene);
                const auto K_FUSION_PATH =
                    local_path(K_BAKE.m_path.parent_path(), color.at("fusion_request"));
                const auto K_FUSION = document(
                    K_FUSION_PATH, color.at("fusion_request_sha256").get<std::string>(), stop);
                for (const auto* key : { "asset", "poses" }) {
                    separate(K_ROOT, local_path(K_FUSION_PATH.parent_path(), K_FUSION.at(key)));
                }
                separate(K_ROOT, K_FUSION_PATH);
                separate(K_ROOT, K_INPUT.m_cache_root);
            }
            if (!K_REUSE.m_path.empty()) {
                const auto K_REUSED = bundle(K_REUSE, stop);
                require(baked(*K_REUSED), "Selected reusable bundle is not completely baked");
                if (K_CLEANUP.m_path.empty()) {
                    same_geometry(*source, *K_REUSED);
                }
            }
        };
        K_ADMIT();
        const Json K_OWNER { { "schema_version", 1U },
                            { "kind", "MESH_POSTPROCESS_PIPELINE_V1" },
                            { "request", K_REQUEST.generic_string() },
                            { "request_sha256", expected_sha256 },
                            { "source_scene_sha256", scene_content_hash(*source) } };
        auto lock = take(open_owned_job(
            K_ROOT,
            { "owner.json", "writer.lock" },
            K_OWNER,
            { "stages", "cleanup.json", "native.json", "bake.json", "export.json", "manifest.json" },
            stop));
        std::set<std::string> selected { "native" };
        if (!K_CLEANUP.m_path.empty()) {
            selected.insert("cleanup");
        }
        if (!K_BAKE.m_path.empty() || !K_REUSE.m_path.empty()) {
            selected.insert("bake");
        }
        if (K_CONFIG.at("export_glb").get<bool>()) {
            selected.insert("export");
        }
        const bool K_TERMINAL { present(K_ROOT / "manifest.json") };
        for (const auto& name : { "cleanup", "native", "bake", "export" }) {
            const bool K_EXISTS { present(K_ROOT / (std::string(name) + ".json")) };
            require((!K_EXISTS || selected.contains(name)) &&
                        (!K_TERMINAL || !selected.contains(name) || K_EXISTS),
                    "Invalid mesh stage receipt selection; refusing terminal repair");
        }
        const auto K_STAGES = K_ROOT / "stages";
        std::filesystem::create_directories(K_STAGES);
        require(std::filesystem::canonical(K_STAGES) == K_STAGES, "Linked mesh stages");
        std::size_t attempts {};
        for (const auto& entry : std::filesystem::directory_iterator(K_STAGES)) {
            const auto K_NAME = entry.path().filename().string();
            const auto K_DASH = K_NAME.find('-');
            require(++attempts <= 512uz && !entry.is_symlink() && entry.is_directory() &&
                        K_DASH != K_NAME.npos && selected.contains(K_NAME.substr(0uz, K_DASH)),
                    "Unknown mesh stage attempt");
            static_cast<void>(attempt_index(std::string_view(K_NAME).substr(K_DASH + 1uz)));
            static_cast<void>(inventory(entry.path(), stop));
        }
        Stages stages { K_ROOT, text_hash(K_OWNER), stop, log };
        std::filesystem::path derived_request;
        if (selected.contains("cleanup")) {
            const auto K_OUTPUT = stages.run("cleanup", [&](const auto& attempt) {
                const auto K_BASE_PATH = K_CLEANUP.m_path.parent_path();
                const FusionAdmissionRequest K_ADMISSION {
                    local_path(K_BASE_PATH, cleanup.at("asset")),
                    local_path(K_BASE_PATH, cleanup.at("poses")),
                    cleanup.at("asset_sha256").template get<std::string>(),
                    cleanup.at("pose_sha256").template get<std::string>(),
                    cleanup.at("manifest_sha256").template get<std::string>(),
                    cleanup.at("receipt_sha256").template get<std::string>(),
                    cleanup.at("recipe_sha256").template get<std::string>() };
                static_cast<void>(
                    take(export_fusion_cleanup(K_ADMISSION, attempt / "output", stop)));
            });
            derived_request = K_OUTPUT / "scene-request.json";
            source = take(load_fusion_scene_request(
                derived_request, take(sha256_file(derived_request, K_DOCUMENT_BYTES, stop)), stop));
        }
        auto result = stages.run("native", [&](const auto& attempt) {
            cancelled(stop);
            check(write_bundle(attempt / "output", source));
        });
        auto expected = take(load_bundle(result));
        same_geometry(*source, *expected);
        require(scene_content_hash(*source) == scene_content_hash(*expected),
                "Native stage differs from selected source");
        if (selected.contains("bake")) {
            result = stages.run("bake", [&](const auto& attempt) {
                if (!K_REUSE.m_path.empty()) {
                    const auto K_REUSED = bundle(K_REUSE, stop);
                    same_geometry(*expected, *K_REUSED);
                    cancelled(stop);
                    check(write_bundle(attempt / "output", K_REUSED));
                } else {
                    auto request_json = color;
                    request_json["cache"] =
                        local_path(K_BAKE.m_path.parent_path(), color.at("cache")).generic_string();
                    const auto K_FUSION =
                        derived_request.empty()
                            ? local_path(K_BAKE.m_path.parent_path(), color.at("fusion_request"))
                            : derived_request;
                    request_json["fusion_request"] = K_FUSION.generic_string();
                    request_json["fusion_request_sha256"] =
                        take(sha256_file(K_FUSION, K_DOCUMENT_BYTES, stop));
                    check(publish_owned_json(attempt / "color.json", request_json, stop));
                    check(bake_mesh_texture(
                        attempt / "color.json", text_hash(request_json), attempt / "output", stop));
                }
            });
            const auto K_BAKED = take(load_bundle(result));
            same_geometry(*expected, *K_BAKED);
            require(baked(*K_BAKED), "Completed bake stage lacks original captured appearance");
            expected = K_BAKED;
        }
        if (selected.contains("export")) {
            const auto K_OUTPUT = stages.run("export", [&](const auto& attempt) {
                check(export_baked_glb_bundle({ result, attempt / "output" }, stop));
            });
            const auto K_MANIFEST = document(K_OUTPUT / "manifest.json", {}, stop);
            require(
                K_MANIFEST.at("source_scene_sha256") == scene_content_hash(*expected) &&
                    K_MANIFEST.at("glb").at("sha256") ==
                        take(sha256_file(K_OUTPUT / "scene.glb", 256uz * 1024uz * 1024uz, stop)),
                "Completed GLB stage differs from final scene");
        }
        // Dependencies are checked again before committing a terminal handoff.
        source = bundle(K_SOURCE, stop);
        K_ADMIT();
        cancelled(stop);
        const Json K_TERMINAL_DOCUMENT {
            { "schema_version", 1U },
            { "kind", "OWNED_MESH_POSTPROCESS_TERMINAL_V1" },
            { "owner_sha256", text_hash(K_OWNER) },
            { "stages", stages.m_receipts },
            { "bundle", result.lexically_relative(K_ROOT).generic_string() },
            { "bundle_manifest_sha256",
             take(sha256_file(result / "manifest.json", K_DOCUMENT_BYTES, stop)) },
            { "scene_sha256", scene_content_hash(*expected) },
            { "walking_enabled", false } };
        check(publish_owned_json(K_ROOT / "manifest.json", K_TERMINAL_DOCUMENT, {}));
        return result;
    } catch (const JobFailure& failure) {
        return std::unexpected(failure.m_error);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mesh job allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Mesh postprocessing failed: ") + exception.what());
    }
}

}  // namespace world_explorer::job_detail
