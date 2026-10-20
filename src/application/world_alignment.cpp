#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

namespace world_explorer {

Result<void> align_world_scene(const WorldAlignmentRequest& request, const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "World alignment cancelled");
        }
        if (request.m_destination.empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(request.m_destination))) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new alignment output directory");
        }
        const auto K_ROOT = std::filesystem::canonical(request.m_source);
        const auto K_OUTPUT =
            std::filesystem::weakly_canonical(std::filesystem::absolute(request.m_destination));
        const auto K_RELATIVE = K_OUTPUT.lexically_relative(K_ROOT);
        if (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") {
            return fail(ErrorCode::INVALID_ARGUMENT, "Alignment output overlaps immutable source");
        }
        const auto K_SCENE = load_bundle(request.m_source);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        const auto K_BYTES =
            asset_files::read_bytes(request.m_document, K_WORLD_FRAME_DOCUMENT_BYTES, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const std::string_view K_TEXT(reinterpret_cast<const char*>(K_BYTES->data()),
                                      K_BYTES->size());
        const auto K_FRAME = admit_world_frame(**K_SCENE, K_TEXT, request.m_document_sha256);
        if (!K_FRAME) {
            return std::unexpected(K_FRAME.error());
        }
        auto aligned = std::make_shared<SceneState>(**K_SCENE);
        aligned->m_world_frame = *K_FRAME;
        aligned->m_scale =
            K_FRAME->m_synthetic ? ScaleStatus::ARBITRARY : ScaleStatus::METRIC_MEASURED;
        ++aligned->m_epoch.m_value;
        ++aligned->m_revision.m_value;
        const auto K_VALID = validate_scene(*aligned);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "World alignment cancelled before publication");
        }
        if (!std::filesystem::create_directory(K_OUTPUT)) {
            return fail(ErrorCode::IO_ERROR, "Alignment output was not reserved");
        }
        const auto K_WRITTEN = write_bundle(K_OUTPUT, aligned);
        if (!K_WRITTEN) {
            return K_WRITTEN;
        }
        const auto K_RELOADED = load_bundle(K_OUTPUT);
        if (!K_RELOADED || scene_content_hash(**K_RELOADED) != scene_content_hash(*aligned)) {
            return fail(ErrorCode::INVALID_DATA,
                        "Published alignment failed independent admission");
        }
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "World alignment allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

}  // namespace world_explorer
