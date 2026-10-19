#include "world_explorer/application/mesh_texture.hpp"

#include "color_inputs.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"

namespace world_explorer {

Result<void> bake_mesh_texture(const std::filesystem::path& request,
                               const std::string& expected_sha256,
                               const std::filesystem::path& destination,
                               const std::stop_token& stop) {
    try {
        const auto K_INPUT = color_inputs::load(request, expected_sha256, destination, stop);
        if (!K_INPUT) return std::unexpected(K_INPUT.error());
        auto scene = std::make_shared<SceneState>(*K_INPUT->m_scene);
        auto chunk = std::make_shared<SceneChunk>(*scene->m_chunks.begin()->second);
        auto baker = MeshTextureBaker::create(*chunk);
        if (!baker) return std::unexpected(baker.error());
        const auto& frames = K_INPUT->m_receipt.at("frames");
        for (std::size_t i { 0uz }; i < frames.size(); ++i) {
            auto view = color_inputs::view(*K_INPUT, i, stop);
            if (!view) return std::unexpected(view.error());
            const auto K_ADDED =
                (*baker)->add_view(*view, { frames[i].at("frame").get<std::uint64_t>() }, stop);
            if (!K_ADDED) return std::unexpected(K_ADDED.error());
        }
        auto baked = (*baker)->finish(stop);
        if (!baked) return std::unexpected(baked.error());
        if (!chunk->m_mesh) return fail(ErrorCode::INVALID_DATA, "Missing baked geometry");
        chunk->m_mesh->m_baked = std::move(*baked);
        scene->m_chunks[chunk->m_id] = std::move(chunk);
        scene->m_provenance["appearance"] =
            "baked_appearance; captured unlit RGB; unknown texels masked";
        const auto K_VALID = validate_scene(*scene);
        if (!K_VALID) return std::unexpected(K_VALID.error());
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Texture publication cancelled");
        return write_bundle(K_INPUT->m_output, scene);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture compilation allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer
