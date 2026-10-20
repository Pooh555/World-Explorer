#include "world_explorer/application/gltf.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/gltf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace world_explorer {
namespace {

bool available(const std::filesystem::path& path) {
    return !path.empty() && !std::filesystem::exists(std::filesystem::symlink_status(path));
}

}  // namespace

Result<void> export_baked_glb_bundle(const BakedGlbExport& request, const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "GLB export cancelled");
        }
        if (!available(request.m_destination)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new GLB export directory");
        }
        const auto K_ROOT = std::filesystem::canonical(request.m_source);
        const auto K_OUTPUT =
            std::filesystem::weakly_canonical(std::filesystem::absolute(request.m_destination));
        const auto K_RELATIVE = K_OUTPUT.lexically_relative(K_ROOT);
        if (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") {
            return fail(ErrorCode::INVALID_ARGUMENT, "GLB output overlaps immutable source bundle");
        }
        const auto K_SCENE = load_bundle(request.m_source);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        auto bytes = encode_baked_glb(*K_SCENE, stop);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "GLB export cancelled before publication");
        }
        if (!std::filesystem::create_directory(K_OUTPUT)) {
            return fail(ErrorCode::IO_ERROR, "GLB export directory was not created");
        }
        const auto K_FLUSH = asset_files::flush_directory_entry(K_OUTPUT.parent_path());
        if (!K_FLUSH) {
            return K_FLUSH;
        }
        const auto K_HASH = asset_files::publish_asset(K_OUTPUT / "scene.glb", *bytes, stop);
        if (!K_HASH) {
            return std::unexpected(K_HASH.error());
        }
        const bool K_SEQUENCE = std::ranges::any_of((*K_SCENE)->m_chunks, [](const auto& entry) {
            return entry.second->m_mesh && entry.second->m_mesh->m_baked &&
                   entry.second->m_mesh->m_baked->m_profile == MeshTextureProfile::DEPTH_SEQUENCE;
        });
        const nlohmann::json K_MANIFEST {
            { "schema_version", K_SEQUENCE ? 2 : 1 },
            { "kind", K_SEQUENCE ? "CAPTURED_UNLIT_GLB_EXPORT_V2" : "CAPTURED_UNLIT_GLB_EXPORT_V1" },
            { "source_scene_sha256", scene_content_hash(**K_SCENE) },
            { "glb", { { "path", "scene.glb" }, { "bytes", bytes->size() }, { "sha256", *K_HASH } } },
            { "lighting", "captured_unlit" },
            { "units", "arbitrary; nominal glTF metre mapping, not measured" },
            { "source_images_added", false },
            { "location_fields_added", false },
            { "authoring_documents_included", true },
            { "authoring_metadata_review", "UNINSPECTED_RETAINED_DOCUMENTS" },
            { "walking_enabled", false } };
        const auto K_TEXT = K_MANIFEST.dump(2);
        const auto K_PUBLISHED = asset_files::publish_asset(
            K_OUTPUT / "manifest.json", std::as_bytes(std::span(K_TEXT)), stop);
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "GLB publication allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

Result<void> import_baked_glb(const std::filesystem::path& file,
                              const std::string& expected_sha256,
                              const std::filesystem::path& destination,
                              const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "GLB import cancelled");
        }
        if (!available(destination)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new native import directory");
        }
        const auto K_BYTES = asset_files::read_bytes(file, 256uz * 1024uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        if (sha256(*K_BYTES) != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "GLB checksum differs");
        }
        const auto K_SCENE = decode_baked_glb(*K_BYTES, stop);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "GLB import cancelled before publication");
        }
        return write_bundle(destination, *K_SCENE);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "GLB import allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

}  // namespace world_explorer
