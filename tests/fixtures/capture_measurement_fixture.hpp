#pragma once

#include "evaluation_corpus_fixture.hpp"

namespace world_explorer::capture_measurement_test {

using namespace asset_corpus_test;

struct MeasurementFixture {
    SelectionFixture m_views;
    RecipeFixture m_output;
    SceneState m_scene;
    std::shared_ptr<SceneChunk> m_chunk;
    CaptureMeasurementSelection m_selected;
    Json m_record, m_frame;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;

    using Mutation = std::function<void(Json&, Json&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_record, m_alignment;
    };

    explicit MeasurementFixture(bool synthetic = true,
                                const Mutation& change = {},
                                const ByteMutation& bytes = {})
        : m_receipt(stage(synthetic, change, bytes)),
          m_capture(checked(m_views.inspect(m_receipt, false))) { }

    AdmittedAssetReceipt stage(bool synthetic, const Mutation& change, const ByteMutation& bytes) {
        const auto K_INITIAL = m_views.freeze(false);
        (void)K_INITIAL;
        const auto K_VIEW = m_views.m_selection.m_frames[0uz].m_view;
        m_scene.m_id = { 31 };
        m_scene.m_epoch = { 3 };
        m_scene.m_revision = { 13 };
        m_scene.m_provenance = {
            { "source_kind", synthetic ? "synthetic_diagnostic" : "partial_observed_geometry" } };
        m_chunk = std::make_shared<SceneChunk>();
        m_chunk->m_id = { 41 };
        m_chunk->m_pose_revision = { 5 };
        m_chunk->m_geometry_valid = true;
        m_chunk->m_source_frames = { K_VIEW.m_frame };
        m_chunk->m_bounds = { { 0, 0, 0 }, { 2, 3, 0 } };
        m_chunk->m_world_from_chunk.m_translation = { 10, -4, 3 };
        m_chunk->m_world_from_chunk.m_scale = 2;
        m_chunk->m_mesh =
            SceneMesh { { { { 0, 0, 0 }, { 0, 0, 1 } }, { { 2, 0, 0 }, { 0, 0, 1 } }, { { 0, 3, 0 }, { 0, 0, 1 } } },
                       { { 0, 1, 2 } },
                       {} };
        if (!synthetic) {
            ObservedMeshSource source;
            source.m_context = { hash("{}"),
                                hash("{}"),
                                hash("synthetic geometry recipe"),
                                K_VIEW.m_pose_asset.m_content_sha256,
                                { 3 },
                                { 5 } };
            source.m_asset_sha256 = hash("synthetic geometry source declaration");
            source.m_manifest_json = source.m_receipt_json = "{}";
            m_chunk->m_mesh->m_observed = source;
        }
        m_scene.m_chunks.emplace(m_chunk->m_id, m_chunk);
        const auto K_ANCHOR = [](std::array<double, 3uz> weights) {
            return Json { { "chunk_id", 41 }, { "triangle", 0 }, { "barycentric", weights } };
        };
        m_frame = {
            { "schema", "WORLD_FRAME_ALIGNMENT_V1" },
            { "source_sha256", scene_content_hash(m_scene) },
            { "source_epoch", 3 },
            { "source_revision", 13 },
            { "evidence_kind", synthetic ? "SYNTHETIC_DIAGNOSTIC" : "INDEPENDENT_MEASUREMENT" },
            { "distance",
             { { "a", K_ANCHOR({ 1, 0, 0 }) },
              { "b", K_ANCHOR({ 0, 1, 0 }) },
              { "metres", 8 },
              { "uncertainty_metres", .02 },
              { "method", synthetic ? "SYNTHETIC" : "MEASURED_DISTANCE" },
              { "record", "Synthetic contract fixture; no physical measurement" } } },
            { "up",
             { { "direction", { 0, 0, 1 } },
              { "uncertainty_degrees", .5 },
              { "method", synthetic ? "SYNTHETIC" : "IMU" },
              { "record", "Synthetic up reference" } } },
            { "origin", K_ANCHOR({ 1, 0, 0 }) } };
        auto& region = m_views.m_object.m_region;
        const auto K_RECIPE = hash("synthetic independent measurement import; not labels");
        region.m_data[K_RECIPE] = "synthetic independent measurement import; not labels";
        m_record = { { "schema_version", 1 },
                    { "world_epoch", 3 },
                    { "pose_revision", 5 },
                    { "recipe_sha256", K_RECIPE },
                    { "source_sha256", hash("synthetic inference video") },
                    { "view", object_view(K_VIEW) },
                    { "scene",
                     { { "scene_id", 31 },
                      { "world_epoch", 3 },
                      { "revision", 13 },
                      { "sha256", scene_content_hash(m_scene) } } },
                    { "alignment", nullptr },
                    { "manual_ancestor", false } };
        if (change) {
            change(m_record, m_frame);
        }
        auto frame_bytes = m_frame.dump();
        if (bytes.m_alignment) {
            bytes.m_alignment(frame_bytes);
        }
        auto alignment = region.node(8000, frame_bytes);
        alignment.m_recipe_sha256 = K_RECIPE;
        m_record["alignment"] = region_ref(alignment.m_product);
        auto record_bytes = m_record.dump();
        if (bytes.m_record) {
            bytes.m_record(record_bytes);
        }
        auto measurement = region.node(8001, record_bytes);
        measurement.m_recipe_sha256 = K_RECIPE;
        measurement.m_dependencies = { K_VIEW.m_image, K_VIEW.m_pose_asset, alignment.m_product };
        region.m_graph.m_products.insert(region.m_graph.m_products.end(), { alignment, measurement });
        m_selected = {
            measurement.m_product,
            K_VIEW,
            { m_scene.m_id, m_scene.m_epoch, m_scene.m_revision, scene_content_hash(m_scene) },
            alignment.m_product };
        m_views.m_selection.m_measurement = measurement.m_product;
        // Re-stage this fixture's own tree; production admission never repairs sources.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        return m_views.freeze(false);
    }

    Result<AdmittedCaptureMeasurement> inspect(const std::stop_token& stop = {}) const {
        return inspect_capture_measurement(
            m_selected, m_capture, m_receipt, m_scene, m_receipt.graph().m_context, stop);
    }

    SceneMesh& mesh() {
        if (!m_chunk || !m_chunk->m_mesh) {
            throw std::runtime_error("Expected measurement fixture mesh");
        }
        return *m_chunk->m_mesh;
    }

    ObservedMeshSource& observed() {
        auto& geometry = mesh();
        if (!geometry.m_observed) {
            throw std::runtime_error("Expected measurement fixture observed metadata");
        }
        return *geometry.m_observed;
    }
};

}  // namespace world_explorer::capture_measurement_test
