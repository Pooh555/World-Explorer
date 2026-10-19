#include "world_explorer/application/fusion_cleanup.hpp"

#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/fusion_cleanup.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <new>

namespace world_explorer {

Result<std::string> export_fusion_cleanup(const FusionAdmissionRequest& request,
                                          const std::filesystem::path& destination,
                                          const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Cleanup export cancelled");
        }
        const auto K_OUTPUT = std::filesystem::canonical(destination.parent_path().empty()
                                                             ? std::filesystem::path { "." }
                                                             : destination.parent_path()) /
                              destination.filename();
        if (destination.filename().empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(K_OUTPUT))) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Cleanup output directory must be new");
        }
        auto asset = load_pose_bound_fusion(request, {}, stop);
        if (!asset) {
            return std::unexpected(asset.error());
        }
        auto cleaned = simplify_fusion_mesh(*asset, stop);
        if (!cleaned) {
            return std::unexpected(cleaned.error());
        }
        const auto K_CERTIFICATE = cleaned->m_document;
        const auto K_CERTIFICATE_BYTES = std::as_bytes(std::span(K_CERTIFICATE));
        const auto K_CERTIFICATE_SHA = sha256(K_CERTIFICATE_BYTES);
        const auto K_REPLAY =
            decode_fusion_cleanup(K_CERTIFICATE_BYTES, K_CERTIFICATE_SHA, *asset, stop);
        if (!K_REPLAY) {
            return std::unexpected(K_REPLAY.error());
        }
        const auto K_BEFORE = inspect_fusion_components(*asset, stop);
        if (!K_BEFORE) {
            return std::unexpected(K_BEFORE.error());
        }
        asset->m_vertices = std::move(cleaned->m_vertices);
        asset->m_triangles = std::move(cleaned->m_triangles);
        const auto K_AFTER = inspect_fusion_components(*asset, stop);
        if (!K_AFTER) {
            return std::unexpected(K_AFTER.error());
        }
        const nlohmann::json K_REPORT {
            { "kind", "PARTIAL_OBSERVED_AFFINE_CLEANUP_REPORT_V1" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "walking_enabled", false },
            { "cleanup_sha256", K_CERTIFICATE_SHA },
            { "mesh_sha256", cleaned->m_geometry_sha256 },
            { "maximum_allowed_error", asset->m_voxel_size * .25 },
            { "original_to_cleaned_surface_distance_upper_bound", cleaned->m_distance_upper_bound },
            { "cleaned_to_original_surface_distance_upper_bound", cleaned->m_distance_upper_bound },
            { "distance_scope",
             "WHOLE_SURFACE_AFFINE_CORRESPONDENCE_BOUND; NOT_MEASURED_GEOMETRY_ERROR" },
            { "decision_order",
             { "ineligible_degree",
              "locked",
              "topology",
              "support",
              "shape",
              "error_bound",
              "accepted" } },
            { "source_vertex_decisions", cleaned->m_decisions },
            { "before", nlohmann::json::parse(*K_BEFORE) },
            { "after", nlohmann::json::parse(*K_AFTER) } };
        const auto K_REPORT_TEXT = K_REPORT.dump(2) + '\n';
        if (K_REPORT_TEXT.size() > 64uz * 1024uz * 1024uz) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup report byte budget exceeded");
        }
        const nlohmann::json K_SCENE_REQUEST {
            { "schema_version", 1U },
            { "asset", std::filesystem::canonical(request.m_asset).generic_string() },
            { "asset_sha256", request.m_asset_sha256 },
            { "poses", std::filesystem::canonical(request.m_poses).generic_string() },
            { "pose_sha256", request.m_pose_sha256 },
            { "manifest_sha256", request.m_manifest_sha256 },
            { "receipt_sha256", request.m_receipt_sha256 },
            { "recipe_sha256", request.m_recipe_sha256 },
            { "cleanup", "mesh.wecleanup" },
            { "cleanup_sha256", K_CERTIFICATE_SHA } };
        const auto K_REQUEST_TEXT = K_SCENE_REQUEST.dump(2) + '\n';
        if (!std::filesystem::create_directory(K_OUTPUT)) {
            return fail(ErrorCode::IO_ERROR, "Cannot reserve cleanup output directory");
        }
        const auto K_FLUSH = asset_files::flush_directory_entry(K_OUTPUT.parent_path());
        if (!K_FLUSH) {
            return std::unexpected(K_FLUSH.error());
        }
        auto files = nlohmann::json::object();
        for (const auto& [K_NAME, K_TEXT] : std::array<std::pair<std::string, std::string>, 3> {
                 { { "mesh.wecleanup", K_CERTIFICATE },
                  { "report.json", K_REPORT_TEXT },
                  { "scene-request.json", K_REQUEST_TEXT } } }) {
            const auto K_HASH = asset_files::publish_asset(
                K_OUTPUT / K_NAME, std::as_bytes(std::span(K_TEXT)), stop);
            if (!K_HASH) {
                return std::unexpected(K_HASH.error());
            }
            files[K_NAME] = { { "sha256", *K_HASH }, { "bytes", K_TEXT.size() } };
        }
        const auto K_SCENE =
            load_fusion_scene_request(K_OUTPUT / "scene-request.json",
                                      sha256(std::as_bytes(std::span(K_REQUEST_TEXT))),
                                      stop);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        const nlohmann::json K_MANIFEST { { "schema_version", 1U },
                                         { "kind", "PARTIAL_OBSERVED_AFFINE_CLEANUP_V1" },
                                         { "parent_asset_sha256", request.m_asset_sha256 },
                                         { "files", std::move(files) },
                                         { "scene_sha256", scene_content_hash(**K_SCENE) },
                                         { "walking_enabled", false },
                                         { "units", "ARBITRARY_POSE_CAMERA_UNITS" } };
        const auto K_MANIFEST_TEXT = K_MANIFEST.dump(2) + '\n';
        return asset_files::publish_asset(
            K_OUTPUT / "manifest.json", std::as_bytes(std::span(K_MANIFEST_TEXT)), stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup export allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        return fail(ErrorCode::IO_ERROR, std::string("Cleanup export failed: ") + exception.what());
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Cleanup export failed: ") + exception.what());
    }
}

}  // namespace world_explorer
