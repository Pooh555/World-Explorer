#ifndef WORLD_EXPLORER_TESTS_GPU_REBASING_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_GPU_REBASING_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"
#include <string_view>

namespace world_explorer {
inline result<vec3d> rebasing_offset(std::string_view name) {
    if (name == "zero")
        return vec3d{};
    if (name == "positive")
        return vec3d{1073741824., -536870912., 268435456.};
    if (name == "negative")
        return vec3d{-1073741824., 536870912., -268435456.};
    return fail(error_code::INVALID_ARGUMENT, "unknown synthetic rebasing case");
}

// This fixed binary-grid diagnostic requires lossless double translations.
// It does not impose a coordinate limit on production scenes.
inline result<vec3d> translated_diagnostic_position(vec3d point, vec3d offset) {
    if (!finite(point) || !finite(offset))
        return fail(error_code::INVALID_ARGUMENT, "nonfinite diagnostic translation");
    const auto translated = point + offset;
    if (!finite(translated) || translated - offset != point)
        return fail(error_code::INVALID_ARGUMENT, "unrepresentable diagnostic translation");
    return translated;
}

inline result<scene_snapshot> rebasing_fixture(vec3d offset) {
    auto scene = std::make_shared<scene_state>();
    scene->id = {76};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "floating_origin_translation_v1"}};
    for (std::size_t group = 0; group < 2; ++group) {
        auto chunk = std::make_shared<scene_chunk>();
        chunk->id = {group + 1};
        chunk->bounds = {{-2, -2, -2}, {2, 2, 2}};
        const auto base_translation = group == 0 ? vec3d{} : vec3d{0.5, -0.25, 1};
        const auto translation = translated_diagnostic_position(base_translation, offset);
        if (!translation)
            return std::unexpected(translation.error());
        chunk->world_from_chunk.translation = *translation;
        if (group != 0) {
            chunk->world_from_chunk.rotation = {0, 1, 0, 0};
            chunk->world_from_chunk.scale = 0.5;
        }
        const std::size_t count = group == 0 ? 12 : 4;
        for (std::size_t index = 0; index < count; ++index) {
            const auto row = index / 4;
            gaussian splat;
            splat.mean = {(static_cast<float>(index % 4) - 1.5F) * 0.5F,
                          (static_cast<float>(row) - 1.F) * 0.5F,
                          -static_cast<float>(index % 3) * 0.25F};
            splat.scale = {0.25F, 0.125F, 0.0625F};
            splat.opacity = 0.625F;
            splat.sh_degree = 3;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto color = 0.125F + static_cast<float>((index + channel) % 4) * 0.1875F;
                splat.sh[channel] = (color - 0.5F) / 0.2820947918F;
                for (std::size_t coefficient = 1; coefficient < 16; ++coefficient)
                    splat.sh[coefficient * 3 + channel] =
                        static_cast<float>(static_cast<int>((index + coefficient + channel) % 3) -
                                           1) *
                        0.015625F;
            }
            auto base_transform = chunk->world_from_chunk;
            base_transform.translation = base_translation;
            const auto local =
                transform_point(base_transform, {splat.mean[0], splat.mean[1], splat.mean[2]});
            const auto roundtrip = translated_diagnostic_position(local, offset);
            if (!roundtrip)
                return std::unexpected(roundtrip.error());
            chunk->splats.push_back(splat);
        }
        scene->chunks.emplace(chunk->id, chunk);
    }
    // Include a fractional camera displacement in the representability contract.
    const auto camera = translated_diagnostic_position({-0.25, 0.125, 6.5}, offset);
    if (!camera)
        return std::unexpected(camera.error());
    return scene_snapshot{std::move(scene)};
}
} // namespace world_explorer

#endif // WORLD_EXPLORER_TESTS_GPU_REBASING_FIXTURE_HPP
