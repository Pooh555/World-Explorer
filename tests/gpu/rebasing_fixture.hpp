#pragma once

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <string_view>

namespace world_explorer {

inline Result<Vec3d> rebasing_offset(std::string_view name) {
    if (name == "zero") return Vec3d {};
    if (name == "positive") return Vec3d { 1073741824., -536870912., 268435456. };
    if (name == "negative") return Vec3d { -1073741824., 536870912., -268435456. };
    return fail(ErrorCode::INVALID_ARGUMENT, "unknown synthetic rebasing case");
}

// This fixed binary-grid diagnostic requires lossless double translations.
// It does not impose a coordinate limit on production scenes.
inline Result<Vec3d> translated_diagnostic_position(Vec3d point, Vec3d offset) {
    if (!finite(point) || !finite(offset))
        return fail(ErrorCode::INVALID_ARGUMENT, "nonfinite diagnostic translation");
    const auto translated = point + offset;
    if (!finite(translated) || translated - offset != point)
        return fail(ErrorCode::INVALID_ARGUMENT, "unrepresentable diagnostic translation");
    return translated;
}

inline Result<SceneSnapshot> rebasing_fixture(Vec3d offset) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 76 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "floating_origin_translation_v1" } };
    for (std::size_t group { 0uz }; group < 2uz; ++group) {
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { group + 1 };
        chunk->m_bounds = { { -2, -2, -2 }, { 2, 2, 2 } };
        const auto base_translation = group == 0 ? Vec3d {} : Vec3d { 0.5, -0.25, 1 };
        const auto translation = translated_diagnostic_position(base_translation, offset);
        if (!translation) return std::unexpected(translation.error());
        chunk->m_world_from_chunk.m_translation = *translation;
        if (group != 0) {
            chunk->m_world_from_chunk.m_rotation = { 0, 1, 0, 0 };
            chunk->m_world_from_chunk.m_scale = 0.5;
        }
        const std::size_t count = group == 0 ? 12 : 4;
        for (std::size_t index { 0uz }; index < count; ++index) {
            const auto row = index / 4;
            Gaussian splat;
            splat.m_mean = { (static_cast<float>(index % 4) - 1.5F) * 0.5F,
                            (static_cast<float>(row) - 1.F) * 0.5F,
                            -static_cast<float>(index % 3) * 0.25F };
            splat.m_scale = { 0.25F, 0.125F, 0.0625F };
            splat.m_opacity = 0.625F;
            splat.m_sh_degree = 3;
            for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                const auto color = 0.125F + static_cast<float>((index + channel) % 4) * 0.1875F;
                splat.m_sh[channel] = (color - 0.5F) / 0.2820947918F;
                for (std::size_t coefficient { 1uz }; coefficient < 16uz; ++coefficient)
                    splat.m_sh[coefficient * 3 + channel] =
                        static_cast<float>(static_cast<int>((index + coefficient + channel) % 3) -
                                           1) *
                        0.015625F;
            }
            auto base_transform = chunk->m_world_from_chunk;
            base_transform.m_translation = base_translation;
            const auto local = transform_point(base_transform,
                                               {splat.m_mean[0], splat.m_mean[1], splat.m_mean[2]});
            const auto roundtrip = translated_diagnostic_position(local, offset);
            if (!roundtrip) return std::unexpected(roundtrip.error());
            chunk->m_splats.push_back(splat);
        }
        scene->m_chunks.emplace(chunk->m_id, chunk);
    }
    // Include a fractional camera displacement in the representability contract.
    const auto camera = translated_diagnostic_position({-0.25, 0.125, 6.5}, offset);
    if (!camera) return std::unexpected(camera.error());
    return SceneSnapshot { std::move(scene) };
}

}  // namespace world_explorer
