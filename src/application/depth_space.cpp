#include "world_explorer/application/depth_space.hpp"

#include "color_inputs.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/depth_space.hpp"

namespace world_explorer {
namespace {

using Json = nlohmann::json;

}  // namespace

Result<std::shared_ptr<const DepthSpaceEvidence>>
open_depth_space(const DepthSpaceOpenRequest& request, const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Depth-space open cancelled");
    }
    if (request.m_source_manifest_sha256.size() != 64uz) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Depth-space source requires a manifest pin");
    }
    const auto K_SOURCE =
        load_bundle(request.m_source_bundle, {}, request.m_source_manifest_sha256);
    if (!K_SOURCE) {
        return std::unexpected(K_SOURCE.error());
    }
    return load_depth_space_evidence(
        { *K_SOURCE, request.m_directory, request.m_report_sha256, request.m_request_sha256 }, stop);
}

Result<void> inspect_depth_space(const std::filesystem::path& request,
                                 const std::string& expected_sha256,
                                 const std::filesystem::path& destination,
                                 const std::stop_token& stop) {
    try {
        const auto K_INPUTS = color_inputs::load(request, expected_sha256, destination, stop);
        if (!K_INPUTS) {
            return std::unexpected(K_INPUTS.error());
        }
        const auto K_GRID_RESULT = make_depth_space_grid(*K_INPUTS->m_scene, stop);
        if (!K_GRID_RESULT) {
            return std::unexpected(K_GRID_RESULT.error());
        }
        const auto& grid = *K_GRID_RESULT;
        const auto& frames = K_INPUTS->m_receipt.at("frames");
        std::vector<DepthCellEvidence> votes(grid.m_cells.size());
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
                const auto K_RELATION = K_VIEW->classify(grid.m_cells[cell]);
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
        const auto K_DIAGNOSTIC = make_depth_space_diagnostic(K_INPUTS->m_scene, votes, stop);
        if (!K_DIAGNOSTIC) {
            return std::unexpected(K_DIAGNOSTIC.error());
        }
        const auto& scene = *K_DIAGNOSTIC;
        const auto K_VALID = validate_scene(*scene);
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
        const auto K_WRITTEN = write_bundle(K_INPUTS->m_output / "diagnostic.wexplore", scene);
        if (!K_WRITTEN) {
            return K_WRITTEN;
        }
        const auto K_RELOADED = load_bundle(K_INPUTS->m_output / "diagnostic.wexplore");
        if (!K_RELOADED || scene_content_hash(**K_RELOADED) != scene_content_hash(*scene)) {
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
             { { "origin", { grid.m_origin.m_x, grid.m_origin.m_y, grid.m_origin.m_z } },
              { "cell_size", grid.m_size },
              { "dimensions", grid.m_dimensions },
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
