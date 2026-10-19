#include "world_explorer/application/fusion_scene.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/fusion_cleanup.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace world_explorer {
namespace {

Result<SceneSnapshot> load_scene(const FusionAdmissionRequest& request,
                                 const std::filesystem::path& cleanup,
                                 const std::string& cleanup_sha256,
                                 const std::stop_token& stop) {
    try {
        auto asset = load_pose_bound_fusion(request, {}, stop);
        if (!asset) return std::unexpected(asset.error());
        std::string cleanup_json;
        if (!cleanup.empty()) {
            const auto K_BYTES = asset_files::read_bytes(cleanup, 4uz * 1024uz * 1024uz, stop);
            if (!K_BYTES) return std::unexpected(K_BYTES.error());
            auto cleaned = decode_fusion_cleanup(*K_BYTES, cleanup_sha256, *asset, stop);
            if (!cleaned) return std::unexpected(cleaned.error());
            asset->m_vertices = std::move(cleaned->m_vertices);
            asset->m_triangles = std::move(cleaned->m_triangles);
            cleanup_json = std::move(cleaned->m_document);
        }
        if (asset->m_vertices.empty() || asset->m_triangles.empty())
            return fail(ErrorCode::INVALID_DATA, "Fusion has no drawable observed surfaces");
        auto scene = std::make_shared<SceneState>();
        scene->m_id = { 1 };
        scene->m_epoch = asset->m_context.m_epoch;
        scene->m_revision = { 1 };
        scene->m_provenance = {{ "source_kind", "partial_observed_geometry" },
                               { "fusion_asset_sha256", asset->m_asset_sha256 },
                               { "geometry_scope", "partial observed surfaces; unknown elsewhere" }};
        if (!cleanup.empty())
            scene->m_provenance["geometry_processing"] =
                "error-bounded observed proxy; raw source retained";
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { 1 };
        chunk->m_pose_revision = { asset->m_context.m_pose_revision.m_value };
        chunk->m_geometry_valid = true;
        const auto K_RECEIPT = nlohmann::json::parse(asset->m_receipt_json);
        for (const auto& frame : K_RECEIPT.at("frames"))
            chunk->m_source_frames.push_back({ frame.at("frame").get<std::uint64_t>() });
        const auto K_INFINITY = std::numeric_limits<double>::infinity();
        chunk->m_bounds = {{ K_INFINITY, K_INFINITY, K_INFINITY },
                           { -K_INFINITY, -K_INFINITY, -K_INFINITY }};
        for (const auto& vertex : asset->m_vertices) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Mesh scene load cancelled");
            const auto& point = vertex.m_position;
            auto& bounds = chunk->m_bounds;
            bounds.m_minimum.m_x = std::min(bounds.m_minimum.m_x, static_cast<double>(point[0]));
            bounds.m_minimum.m_y = std::min(bounds.m_minimum.m_y, static_cast<double>(point[1]));
            bounds.m_minimum.m_z = std::min(bounds.m_minimum.m_z, static_cast<double>(point[2]));
            bounds.m_maximum.m_x = std::max(bounds.m_maximum.m_x, static_cast<double>(point[0]));
            bounds.m_maximum.m_y = std::max(bounds.m_maximum.m_y, static_cast<double>(point[1]));
            bounds.m_maximum.m_z = std::max(bounds.m_maximum.m_z, static_cast<double>(point[2]));
        }
        chunk->m_mesh = SceneMesh {std::move(asset->m_vertices),
                                   std::move(asset->m_triangles),
                                   ObservedMeshSource {std::move(asset->m_context),
                                                       std::move(asset->m_asset_sha256),
                                                       std::move(asset->m_manifest_json),
                                                       std::move(asset->m_receipt_json),
                                                       cleanup_sha256,
                                                       std::move(cleanup_json)}};
        scene->m_chunks.emplace(chunk->m_id, chunk);
        const auto K_VALID = validate_scene(*scene);
        if (!K_VALID) return std::unexpected(K_VALID.error());
        return SceneSnapshot { std::move(scene) };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion scene allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fusion scene failed: ") + exception.what());
    }
}

}  // namespace

Result<SceneSnapshot> load_fusion_scene(const FusionAdmissionRequest& request,
                                        const std::stop_token& stop) {
    return load_scene(request, {}, {}, stop);
}

Result<SceneSnapshot> load_fusion_scene_request(const std::filesystem::path& request,
                                                const std::string& expected_sha256,
                                                const std::stop_token& stop) {
    try {
        const auto K_BYTES = asset_files::read_bytes(request, 1024uz * 1024uz, stop);
        if (!K_BYTES) return std::unexpected(K_BYTES.error());
        if (sha256(*K_BYTES) != expected_sha256)
            return fail(ErrorCode::INVALID_DATA, "Fusion scene request checksum differs");
        const std::string K_TEXT { reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size() };
        if (K_TEXT.find('\0') != K_TEXT.npos)
            return fail(ErrorCode::INVALID_DATA, "Fusion scene request contains raw NUL");
        std::set<std::string> keys;
        const auto K_CONFIG = nlohmann::json::parse(
            K_TEXT, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
                if (depth > 1) throw std::runtime_error("Nested fusion scene request");
                if (event == nlohmann::json::parse_event_t::key &&
                    !keys.insert(value.get<std::string>()).second)
                    throw std::runtime_error("Duplicate fusion scene request key");
                return true;
            });
        if (!K_CONFIG.is_object() || (K_CONFIG.size() != 8uz && K_CONFIG.size() != 10uz) ||
            !K_CONFIG.at("schema_version").is_number_unsigned() ||
            K_CONFIG.at("schema_version") != 1)
            return fail(ErrorCode::INVALID_DATA, "Invalid fusion scene request schema");
        const auto K_BASE = std::filesystem::absolute(request).parent_path();
        const auto K_PATH = [&](const char* key) {
            const auto K_VALUE = K_CONFIG.at(key).get<std::string>();
            if (K_VALUE.empty() || K_VALUE.find('\0') != K_VALUE.npos)
                throw std::runtime_error("Invalid fusion scene input path");
            return K_BASE / K_VALUE;
        };
        const FusionAdmissionRequest K_ADMISSION {K_PATH("asset"),
                                                  K_PATH("poses"),
                                                  K_CONFIG.at("asset_sha256").get<std::string>(),
                                                  K_CONFIG.at("pose_sha256").get<std::string>(),
                                                  K_CONFIG.at("manifest_sha256").get<std::string>(),
                                                  K_CONFIG.at("receipt_sha256").get<std::string>(),
                                                  K_CONFIG.at("recipe_sha256").get<std::string>()};
        if (K_CONFIG.size() == 10uz)
            return load_scene(K_ADMISSION,
                              K_PATH("cleanup"),
                              K_CONFIG.at("cleanup_sha256").get<std::string>(),
                              stop);
        return load_fusion_scene(K_ADMISSION, stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion request allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fusion request failed: ") + exception.what());
    }
}

Result<void> compile_fusion_scene(const std::filesystem::path& request,
                                  const std::string& expected_sha256,
                                  const std::filesystem::path& destination,
                                  const std::stop_token& stop) {
    try {
        if (std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new mesh bundle directory");
        const auto K_SCENE = load_fusion_scene_request(request, expected_sha256, stop);
        if (!K_SCENE) return std::unexpected(K_SCENE.error());
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Mesh compilation cancelled");
        return write_bundle(destination, *K_SCENE);
    } catch (const std::exception& exception) {
        return fail(ErrorCode::IO_ERROR, exception.what());
    }
}

}  // namespace world_explorer
