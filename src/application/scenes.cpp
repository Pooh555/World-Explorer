#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace world_explorer {
scene_snapshot make_diagnostic_scene() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {1};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "asymmetric_gaussians_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-1.5, -1, -1}, {1.5, 1, 1}};
    // An asymmetric arrangement exposes handedness and depth-order mistakes.
    for (int index = 0; index < 45; ++index) {
        gaussian splat;
        splat.mean = {static_cast<float>(index % 9 - 4) * 0.28F,
                      static_cast<float>(index / 9 - 2) * 0.28F,
                      static_cast<float>(index % 3) * -0.2F};
        splat.scale = {0.13F, 0.08F, 0.06F};
        splat.opacity = 0.9F;
        splat.sh[0] = (static_cast<float>(index % 9) / 8.F - 0.5F) / 0.2820947918F;
        splat.sh[1] = (static_cast<float>(index / 9) / 4.F - 0.5F) / 0.2820947918F;
        splat.sh[2] = 0.25F / 0.2820947918F;
        chunk->splats.push_back(splat);
    }
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
result<void> compile_splat_scene(const std::filesystem::path& ply,
                                 const std::filesystem::path& destination) {
    const auto source_hash = sha256_file(ply, 8ULL * 1024 * 1024 * 1024);
    if (!source_hash)
        return std::unexpected(source_hash.error());
    std::uint64_t identity{};
    const auto converted =
        std::from_chars(source_hash->data(), source_hash->data() + 16, identity, 16);
    if (converted.ec != std::errc{})
        return fail(error_code::INVALID_DATA, "Invalid source digest");
    auto imported = load_gsplat_ply(ply, scene_id{identity ? identity : 1});
    if (!imported)
        return std::unexpected(imported.error());
    auto scene = std::make_shared<scene_state>(**imported);
    scene->provenance = {{"source_kind", "gsplat_ply_import"},
                         {"input_sha256", *source_hash},
                         {"exporter", "gsplat-1.5.3"},
                         {"quality_gate", "UNVERIFIED"}};
    return write_bundle(destination, scene);
}
bounds3d world_bounds(const scene_state& scene) {
    const auto infinity = std::numeric_limits<double>::infinity();
    bounds3d result{{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
    for (const auto& [id, chunk] : scene.chunks) {
        (void)id;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const auto& bounds = chunk->bounds;
            const auto point = transform_point(chunk->world_from_chunk,
                                               {corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                                                corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                                                corner & 4 ? bounds.maximum.z : bounds.minimum.z});
            result.minimum.x = std::min(result.minimum.x, point.x);
            result.minimum.y = std::min(result.minimum.y, point.y);
            result.minimum.z = std::min(result.minimum.z, point.z);
            result.maximum.x = std::max(result.maximum.x, point.x);
            result.maximum.y = std::max(result.maximum.y, point.y);
            result.maximum.z = std::max(result.maximum.z, point.z);
        }
    }
    return result;
}
} // namespace world_explorer
