#include "world_explorer/scene_store/depth_space.hpp"

#include <algorithm>
#include <limits>

namespace world_explorer {
namespace {

DepthSpaceGrid grid_for(const SceneState& scene) {
    Bounds3d bounds { { std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity() },
                     { -std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity() } };
    if (scene.m_world_frame) {
        throw std::runtime_error("Depth-space V1 requires original pose coordinates");
    }
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        static_cast<void>(K_ID);
        for (std::size_t corner { 0uz }; corner < 8uz; ++corner) {
            const auto& box = K_CHUNK->m_bounds;
            const auto K_POINT =
                transform_point(K_CHUNK->m_world_from_chunk,
                                { (corner & 1uz) ? box.m_maximum.m_x : box.m_minimum.m_x,
                                 (corner & 2uz) ? box.m_maximum.m_y : box.m_minimum.m_y,
                                 (corner & 4uz) ? box.m_maximum.m_z : box.m_minimum.m_z });
            bounds.m_minimum.m_x = std::min(bounds.m_minimum.m_x, K_POINT.m_x);
            bounds.m_minimum.m_y = std::min(bounds.m_minimum.m_y, K_POINT.m_y);
            bounds.m_minimum.m_z = std::min(bounds.m_minimum.m_z, K_POINT.m_z);
            bounds.m_maximum.m_x = std::max(bounds.m_maximum.m_x, K_POINT.m_x);
            bounds.m_maximum.m_y = std::max(bounds.m_maximum.m_y, K_POINT.m_y);
            bounds.m_maximum.m_z = std::max(bounds.m_maximum.m_z, K_POINT.m_z);
        }
    }
    const auto K_EXTENT = bounds.m_maximum - bounds.m_minimum;
    DepthSpaceGrid grid;
    grid.m_origin = bounds.m_minimum;
    grid.m_size = std::max({ K_EXTENT.m_x, K_EXTENT.m_y, K_EXTENT.m_z }) / 32.;
    if (!valid(bounds) || !std::isfinite(grid.m_size) || grid.m_size < 1e-6 ||
        grid.m_size > 1000.) {
        throw std::runtime_error("Depth-space source extent exceeds diagnostic grid limits");
    }
    const std::array K_EXTENTS { K_EXTENT.m_x, K_EXTENT.m_y, K_EXTENT.m_z };
    for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
        grid.m_dimensions[axis] = std::clamp(
            static_cast<std::size_t>(std::ceil(K_EXTENTS[axis] / grid.m_size)), 1uz, 32uz);
    }
    for (std::size_t z { 0uz }; z < grid.m_dimensions[2uz]; ++z) {
        for (std::size_t y { 0uz }; y < grid.m_dimensions[1uz]; ++y) {
            for (std::size_t x { 0uz }; x < grid.m_dimensions[0uz]; ++x) {
                const auto K_MIN =
                    grid.m_origin +
                    Vec3d { static_cast<double>(x), static_cast<double>(y), static_cast<double>(z) } *
                        grid.m_size;
                grid.m_cells.push_back(
                    { K_MIN, K_MIN + Vec3d { grid.m_size, grid.m_size, grid.m_size } });
            }
        }
    }
    return grid;
}

SceneSnapshot diagnostic(const SceneSnapshot& source,
                         const DepthSpaceGrid& grid,
                         std::span<const DepthCellEvidence> votes) {
    auto scene = std::make_shared<SceneState>(*source);
    const auto K_KIND = source->m_provenance.find("source_kind");
    const bool K_SYNTHETIC { K_KIND != source->m_provenance.end() &&
                            K_KIND->second == "synthetic_diagnostic" };
    scene->m_provenance["source_kind"] =
        K_SYNTHETIC ? "synthetic_diagnostic" : "observed_depth_space_diagnostic";
    scene->m_provenance["depth_space"] =
        "Observed-depth diagnostic: green front-only, orange surface/behind, blue unknown, red "
        "conflict. Arbitrary scale; sigma may be unavailable; walking unavailable.";
    if (K_SYNTHETIC) {
        scene->m_provenance["depth_space"] = "Synthetic " + scene->m_provenance["depth_space"];
    }
    scene->m_provenance["source_scene_sha256"] = scene_content_hash(*source);
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    while (scene->m_chunks.contains(chunk->m_id)) {
        ++chunk->m_id.m_value;
    }
    chunk->m_bounds = { grid.m_cells.front().m_minimum, grid.m_cells.back().m_maximum };
    const std::array K_COLORS { std::array { .1F, .3F, 1.F },
                               std::array { .1F, .9F, .15F },
                               std::array { 1.F, .5F, .05F },
                               std::array { 1.F, .05F, .1F } };
    for (std::size_t index { 0uz }; index < votes.size(); ++index) {
        const auto K_CENTER = (grid.m_cells[index].m_minimum + grid.m_cells[index].m_maximum) * .5;
        Gaussian splat;
        splat.m_mean = { static_cast<float>(K_CENTER.m_x),
                        static_cast<float>(K_CENTER.m_y),
                        static_cast<float>(K_CENTER.m_z) };
        const float K_RADIUS { static_cast<float>(grid.m_size * .15) };
        splat.m_scale = { K_RADIUS, K_RADIUS, K_RADIUS };
        const auto K_CATEGORY = static_cast<std::size_t>(votes[index].state());
        splat.m_opacity = K_CATEGORY == 0uz ? .12F : .6F;
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            splat.m_sh[channel] = (K_COLORS[K_CATEGORY][channel] - .5F) / .2820947918F;
        }
        chunk->m_splats.push_back(splat);
    }
    scene->m_chunks.emplace(chunk->m_id, std::move(chunk));
    return scene;
}

}  // namespace

Result<DepthSpaceGrid> make_depth_space_grid(const SceneState& source,
                                             const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Depth-space grid cancelled");
    }
    const auto K_VALID = validate_scene(source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    try {
        return grid_for(source);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space grid allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<SceneSnapshot> make_depth_space_diagnostic(const SceneSnapshot& source,
                                                  std::span<const DepthCellEvidence> cells,
                                                  const std::stop_token& stop) {
    if (!source) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Missing depth-space source");
    }
    const auto K_GRID = make_depth_space_grid(*source, stop);
    if (!K_GRID) {
        return std::unexpected(K_GRID.error());
    }
    if (cells.size() != K_GRID->m_cells.size()) {
        return fail(ErrorCode::INVALID_DATA, "Depth-space evidence does not cover full grid");
    }
    std::size_t views {};
    for (const auto& cell : cells) {
        std::size_t total {};
        for (auto count : cell.m_counts) {
            if (count > 120uz - total) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space view budget exceeded");
            }
            total += count;
        }
        if (total == 0uz || (views != 0uz && views != total)) {
            return fail(ErrorCode::INVALID_DATA, "Depth-space cell view totals differ");
        }
        views = total;
    }
    try {
        auto result = diagnostic(source, *K_GRID, cells);
        const auto K_VALID = validate_scene(*result);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space diagnostic allocation failed");
    }
}

}  // namespace world_explorer
