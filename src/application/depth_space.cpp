#include "world_explorer/application/depth_space.hpp"

#include "color_inputs.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/depth_space.hpp"

#include <algorithm>
#include <limits>

namespace world_explorer {
namespace {

using Json = nlohmann::json;

struct Grid {
    Vec3d m_origin;
    double m_size {};
    std::array<std::size_t, 3uz> m_dimensions {};
    std::vector<Bounds3d> m_cells;
};

Grid grid_for(const SceneState& scene) {
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
    Grid grid;
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
                         const Grid& grid,
                         std::span<const DepthCellEvidence> votes) {
    auto scene = std::make_shared<SceneState>(*source);
    scene->m_provenance["source_kind"] = "observed_depth_space_diagnostic";
    scene->m_provenance["depth_space"] =
        "Observed-depth diagnostic: green front-only, orange surface/behind, blue unknown, red "
        "conflict. Arbitrary scale; sigma may be unavailable; walking unavailable.";
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

Result<void> inspect_depth_space(const std::filesystem::path& request,
                                 const std::string& expected_sha256,
                                 const std::filesystem::path& destination,
                                 const std::stop_token& stop) {
    try {
        const auto K_INPUTS = color_inputs::load(request, expected_sha256, destination, stop);
        if (!K_INPUTS) {
            return std::unexpected(K_INPUTS.error());
        }
        const auto K_GRID = grid_for(*K_INPUTS->m_scene);
        const auto& frames = K_INPUTS->m_receipt.at("frames");
        std::vector<DepthCellEvidence> votes(K_GRID.m_cells.size());
        std::vector<std::byte> ledger;
        ledger.reserve(frames.size() * votes.size());
        Json view_counts = Json::array();
        for (std::size_t view { 0uz }; view < frames.size(); ++view) {
            const auto K_SOURCE = color_inputs::view(*K_INPUTS, view, stop);
            if (!K_SOURCE) {
                return std::unexpected(K_SOURCE.error());
            }
            const auto K_VIEW = DepthSpaceView::create(
                K_SOURCE->depth(), { K_SOURCE->world_from_camera(), K_INPUTS->m_voxel_size }, stop);
            if (!K_VIEW) {
                return std::unexpected(K_VIEW.error());
            }
            std::array<std::size_t, 5uz> counts {};
            for (std::size_t cell { 0uz }; cell < votes.size(); ++cell) {
                if (cell % 256uz == 0uz && stop.stop_requested()) {
                    return fail(ErrorCode::CANCELLED, "Depth-space classification cancelled");
                }
                const auto K_RELATION = K_VIEW->classify(K_GRID.m_cells[cell]);
                if (!K_RELATION) {
                    return std::unexpected(K_RELATION.error());
                }
                const auto K_CODE = static_cast<std::uint8_t>(*K_RELATION);
                ledger.push_back(static_cast<std::byte>(K_CODE));
                ++counts.at(K_CODE);
                ++votes[cell].m_counts.at(K_CODE);
            }
            view_counts.push_back({ { "frame", frames.at(view).at("frame") },
                                   { "uncertainty_available", K_VIEW->uncertainty_available() },
                                   { "counts", counts } });
        }
        std::array<std::size_t, 4uz> categories {};
        for (const auto& vote : votes) {
            ++categories.at(static_cast<std::size_t>(vote.state()));
        }
        const auto K_SCENE = diagnostic(K_INPUTS->m_scene, K_GRID, votes);
        const auto K_VALID = validate_scene(*K_SCENE);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Depth-space publication cancelled");
        }
        if (!std::filesystem::create_directory(K_INPUTS->m_output)) {
            return fail(ErrorCode::IO_ERROR, "Depth-space output reservation failed");
        }
        const auto K_LEDGER =
            asset_files::publish_asset(K_INPUTS->m_output / "relations.u8", ledger, stop);
        if (!K_LEDGER) {
            return std::unexpected(K_LEDGER.error());
        }
        const auto K_WRITTEN = write_bundle(K_INPUTS->m_output / "diagnostic.wexplore", K_SCENE);
        if (!K_WRITTEN) {
            return K_WRITTEN;
        }
        const auto K_RELOADED = load_bundle(K_INPUTS->m_output / "diagnostic.wexplore");
        if (!K_RELOADED || scene_content_hash(**K_RELOADED) != scene_content_hash(*K_SCENE)) {
            return fail(ErrorCode::INVALID_DATA, "Depth-space diagnostic reload differs");
        }
        const auto K_BUNDLE_HASH = sha256_file(
            K_INPUTS->m_output / "diagnostic.wexplore/manifest.json", 1024uz * 1024uz, stop);
        if (!K_BUNDLE_HASH) {
            return std::unexpected(K_BUNDLE_HASH.error());
        }
        const Json K_REPORT {
            { "kind", "OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V1" },
            { "request_sha256", expected_sha256 },
            { "source_scene_sha256", scene_content_hash(*K_INPUTS->m_scene) },
            { "source_receipt", K_INPUTS->m_receipt },
            { "grid",
             { { "origin", { K_GRID.m_origin.m_x, K_GRID.m_origin.m_y, K_GRID.m_origin.m_z } },
              { "cell_size", K_GRID.m_size },
              { "dimensions", K_GRID.m_dimensions },
              { "order", "view-major; cells x then y then z" },
              { "domain", "full source chunk bounds, rounded out to cells" } } },
            { "recipe",
             { { "minimum_support", 2 },
              { "confidence", "positive recipe weight, not probability" },
              { "sigma_multiplier", 2 },
              { "world_guard", K_INPUTS->m_voxel_size },
              { "missing_sigma", "UNAVAILABLE; guard is not a confidence interval" },
              { "footprint",
               "outward floor/ceil with 1e-7 pixel guard; enclosing aligned power-of-two mip "
               "blocks" } } },
            { "view_counts", view_counts },
            { "categories_unknown_front_blocked_conflict", categories },
            { "relation_codes",
             { "OUTSIDE_VIEW",
              "UNKNOWN_DEPTH",
              "FRONT_DIAGNOSTIC",
              "SURFACE_BAND",
              "BEHIND_SURFACE" } },
            { "relations_bytes", ledger.size() },
            { "relations_sha256", *K_LEDGER },
            { "diagnostic_manifest_sha256", *K_BUNDLE_HASH },
            { "scale", "ARBITRARY" },
            { "measured_collision", false },
            { "walking_enabled", false } };
        const auto K_TEXT = K_REPORT.dump(2);
        const auto K_PUBLISHED = asset_files::publish_asset(
            K_INPUTS->m_output / "manifest.json", std::as_bytes(std::span(K_TEXT)), stop);
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
