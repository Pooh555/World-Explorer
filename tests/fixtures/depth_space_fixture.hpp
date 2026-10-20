#pragma once

#include "world_explorer/application/depth_space.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace world_explorer::testing {

inline std::string depth_space_digest(std::string_view value) {
    return sha256(std::as_bytes(std::span(value.data(), value.size())));
}

inline void depth_space_write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!file) {
        throw std::runtime_error("Could not write synthetic depth-space fixture");
    }
}

struct DepthSpaceFixture {
    std::filesystem::path m_path;
    SceneSnapshot m_source, m_diagnostic;
    nlohmann::json m_report;
    std::string m_relations, m_pin;
    static constexpr std::string_view K_REQUEST {
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" };

    explicit DepthSpaceFixture(std::filesystem::path path) : m_path(std::move(path)) {
        using Json = nlohmann::json;
        std::filesystem::create_directories(m_path);
        m_path = std::filesystem::canonical(m_path);
        auto source = std::make_shared<SceneState>();
        source->m_id = { 39 };
        source->m_epoch = { 7 };
        source->m_revision = { 13 };
        source->m_provenance = {
            { "source_kind", "synthetic_diagnostic" },
            { "fixture", "analytic depth-space admission only; no reconstruction" } };
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { 9007199254740993ULL };
        chunk->m_pose_revision = { 4 };
        chunk->m_geometry_valid = true;
        chunk->m_source_frames = { { 9007199254740995ULL }, { 18446744073709551614ULL } };
        chunk->m_bounds = { {}, { 1, 1, 0 } };
        chunk->m_mesh =
            SceneMesh { { { { 0, 0, 0 }, { 0, 0, 1 } }, { { 1, 0, 0 }, { 0, 0, 1 } }, { { 0, 1, 0 }, { 0, 0, 1 } } },
                       { { 0, 1, 2 } },
                       {} };
        const Json K_RECEIPT {
            { "kind", "POSE_ADMITTED_DEPTH_SEQUENCE_V1" },
            { "schema_version", 1 },
            { "world_epoch", 7 },
            { "pose_revision", 4 },
            { "pose_asset_sha256", std::string(64uz, 'a') },
            { "depth_recipe_sha256", std::string(64uz, 'b') },
            { "frames",
             Json::array({ { { "frame", chunk->m_source_frames[0uz].m_value },
                           { "uncertainty", "UNAVAILABLE" },
                           { "depth_sha256", std::string(64uz, 'c') },
                           { "calibrated_image_sha256", std::string(64uz, 'd') } },
                          { { "frame", chunk->m_source_frames[1uz].m_value },
                           { "uncertainty", "CAMERA_UNIT_STANDARD_DEVIATION" },
                           { "depth_sha256", std::string(64uz, 'd') },
                           { "calibrated_image_sha256", std::string(64uz, 'c') } } }) } };
        const std::string K_MANIFEST { "{\"voxel_size\":0.01}" };
        const auto K_RECEIPT_TEXT = K_RECEIPT.dump();
        chunk->m_mesh->m_observed = ObservedMeshSource { { depth_space_digest(K_MANIFEST),
                                                         depth_space_digest(K_RECEIPT_TEXT),
                                                         std::string(64uz, 'b'),
                                                         std::string(64uz, 'a'),
                                                         { 7 },
                                                         { 4 } },
                                                        std::string(64uz, 'f'),
                                                        K_MANIFEST,
                                                        K_RECEIPT_TEXT };
        source->m_chunks.emplace(chunk->m_id, chunk);
        m_source = source;
        const auto K_GRID = make_depth_space_grid(*m_source);
        if (!K_GRID) {
            throw std::runtime_error(K_GRID.error().m_message);
        }
        std::vector<DepthCellEvidence> cells(K_GRID->m_cells.size());
        const std::array<std::array<std::uint8_t, 2uz>, 5uz> K_STATES {
            { { 0, 1 }, { 2, 1 }, { 4, 3 }, { 2, 4 }, { 3, 2 } } };
        Json view_counts = Json::array();
        for (std::size_t view { 0uz }; view < 2uz; ++view) {
            std::array<std::size_t, 5uz> counts {};
            for (std::size_t cell { 0uz }; cell < cells.size(); ++cell) {
                const auto K_CODE = K_STATES[cell % 5uz][view];
                m_relations += static_cast<char>(K_CODE);
                ++counts[K_CODE];
                ++cells[cell].m_counts[K_CODE];
            }
            view_counts.push_back({ { "frame", chunk->m_source_frames[view].m_value },
                                   { "uncertainty_available", view == 1uz },
                                   { "counts", counts } });
        }
        const auto K_DIAGNOSTIC = make_depth_space_diagnostic(m_source, cells);
        if (!K_DIAGNOSTIC) {
            throw std::runtime_error(K_DIAGNOSTIC.error().m_message);
        }
        m_diagnostic = *K_DIAGNOSTIC;
        const auto K_WRITTEN = write_bundle(m_path / "diagnostic.wexplore", m_diagnostic);
        if (!K_WRITTEN) {
            throw std::runtime_error(K_WRITTEN.error().m_message);
        }
        std::array<std::size_t, 4uz> categories {};
        for (const auto& cell : cells) {
            ++categories[static_cast<std::size_t>(cell.state())];
        }
        const auto K_HASH =
            sha256_file(m_path / "diagnostic.wexplore/manifest.json", 1024uz * 1024uz);
        if (!K_HASH) {
            throw std::runtime_error(K_HASH.error().m_message);
        }
        m_report = { { "kind", "OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V1" },
                    { "request_sha256", K_REQUEST },
                    { "source_scene_sha256", scene_content_hash(*m_source) },
                    { "source_receipt", K_RECEIPT },
                    { "grid",
                     { { "origin", { 0, 0, 0 } },
                      { "cell_size", K_GRID->m_size },
                      { "dimensions", K_GRID->m_dimensions },
                      { "order", "view-major; cells x then y then z" },
                      { "domain", "full source chunk bounds, rounded out to cells" } } },
                    { "recipe",
                     { { "minimum_support", 2 },
                      { "confidence", "positive recipe weight, not probability" },
                      { "sigma_multiplier", 2 },
                      { "world_guard", .01 },
                      { "missing_sigma", "UNAVAILABLE; guard is not a confidence interval" },
                      { "footprint",
                       "outward floor/ceil with 1e-7 pixel guard; enclosing aligned power-of-two "
                       "mip blocks" } } },
                    { "view_counts", view_counts },
                    { "categories_unknown_front_blocked_conflict", categories },
                    { "relation_codes",
                     { "OUTSIDE_VIEW",
                      "UNKNOWN_DEPTH",
                      "FRONT_DIAGNOSTIC",
                      "SURFACE_BAND",
                      "BEHIND_SURFACE" } },
                    { "relations_bytes", m_relations.size() },
                    { "relations_sha256", depth_space_digest(m_relations) },
                    { "diagnostic_manifest_sha256", *K_HASH },
                    { "scale", "ARBITRARY" },
                    { "measured_collision", false },
                    { "walking_enabled", false } };
        publish();
    }

    void publish() {
        m_report["relations_sha256"] = depth_space_digest(m_relations);
        const auto K_TEXT = m_report.dump(2);
        m_pin = depth_space_digest(K_TEXT);
        depth_space_write(m_path / "relations.u8", m_relations);
        depth_space_write(m_path / "manifest.json", K_TEXT);
    }

    Result<std::shared_ptr<const DepthSpaceEvidence>> load(const std::stop_token& stop = {}) const {
        return load_depth_space_evidence({ m_source, m_path, m_pin, std::string(K_REQUEST) }, stop);
    }
};

}  // namespace world_explorer::testing
