#pragma once

#include "evaluation_corpus_fixture.hpp"
#include "world_explorer/application/geometry_product.hpp"

namespace world_explorer::geometry_product_test {

using namespace asset_corpus_test;

inline Json native_pin(const GeometryNativeSource& pin) {
    return { { "scene_id", pin.m_scene.m_value },
            { "world_epoch", pin.m_epoch.m_value },
            { "scene_revision", pin.m_revision.m_value },
            { "scene_sha256", pin.m_scene_sha256 },
            { "chunk_id", pin.m_chunk.m_value },
            { "generation", pin.m_generation },
            { "chunk_sha256", pin.m_chunk_sha256 } };
}

struct GeometryFixture {
    SelectionFixture m_views;
    RecipeFixture m_output;
    std::shared_ptr<SceneState> m_scene;
    std::shared_ptr<SceneChunk> m_chunk;
    GeometryProductSelection m_selected;
    Json m_record, m_binding;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    using Mutation = std::function<void(Json&, GeometryProductSelection&)>;
    using SourceMutation = std::function<void(SceneState&, SceneChunk&)>;
    using GraphMutation =
        std::function<void(AssetDependencyGraph&, const GeometryProductSelection&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_record, m_source;
    };

    explicit GeometryFixture(const Mutation& change = {},
                             const SourceMutation& source = {},
                             const GraphMutation& graph = {},
                             const ByteMutation& bytes = {},
                             bool manual = false)
        : m_receipt(stage(change, source, graph, bytes, manual)),
          m_capture(checked(m_views.inspect(m_receipt, false))) { }

    AdmittedAssetReceipt stage(const Mutation& change,
                               const SourceMutation& source,
                               const GraphMutation& graph,
                               const ByteMutation& bytes,
                               bool manual) {
        const auto K_INITIAL = m_views.freeze(false);
        (void)K_INITIAL;
        const auto K_VIEW = m_views.m_selection.m_frames[0uz].m_view;
        m_scene = std::make_shared<SceneState>();
        m_scene->m_id = { 31 };
        m_scene->m_epoch = { 3 };
        m_scene->m_revision = { 13 };
        m_scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                                 { "fixture", "asymmetric geometry product" } };
        m_chunk = std::make_shared<SceneChunk>();
        m_chunk->m_id = { 41 };
        m_chunk->m_pose_revision = { 5 };
        m_chunk->m_geometry_valid = true;
        m_chunk->m_source_frames = { K_VIEW.m_frame };
        m_chunk->m_bounds = { { 0, 0, 0 }, { 2, 3, .5 } };
        m_chunk->m_world_from_chunk.m_translation = { 10, -4, 3 };
        m_chunk->m_world_from_chunk.m_scale = 2;
        const ObservedMeshSource K_OBSERVED { { hash("{}"),
                                              hash("{}"),
                                              hash("synthetic geometry source"),
                                              K_VIEW.m_pose_asset.m_content_sha256,
                                              { 3 },
                                              { 5 } },
                                             hash("synthetic source mesh bytes"),
                                             "{}",
                                             "{}" };
        m_chunk->m_mesh = SceneMesh { { { { 0, 0, 0 }, { 0, 0, 1 } },
                                      { { 2, 0, 0 }, { 0, 0, 1 } },
                                      { { 0, 3, 0 }, { 0, 0, 1 } },
                                      { { 2, 3, .5 }, { 0, 0, 1 } } },
                                     { { 0, 1, 2 }, { 1, 3, 2 } },
                                     K_OBSERVED };
        m_scene->m_chunks.emplace(m_chunk->m_id, m_chunk);
        if (source) {
            source(*m_scene, *m_chunk);
        }
        m_selected.m_native = { m_scene->m_id,
                               m_scene->m_epoch,
                               m_scene->m_revision,
                               scene_content_hash(*m_scene),
                               m_chunk->m_id,
                               m_chunk->m_generation,
                               scene_chunk_content_hash(*m_chunk) };
        m_selected.m_views = { K_VIEW };
        m_selected.m_max_deviation_units = .25;
        auto& region = m_views.m_object.m_region;
        const std::string K_RECIPE { "synthetic geometry product import; inference-only" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        m_binding = { { "schema_version", 1 },
                     { "kind", "NATIVE_GEOMETRY_SOURCE" },
                     { "world_epoch", 3 },
                     { "pose_revision", 5 },
                     { "recipe_sha256", hash(K_RECIPE) },
                     { "native", native_pin(m_selected.m_native) },
                     { "manual_ancestor", false } };
        auto binding = m_binding.dump();
        if (bytes.m_source) {
            bytes.m_source(binding);
        }
        auto source_node = region.node(11000, binding);
        source_node.m_recipe_sha256 = hash(K_RECIPE);
        source_node.m_dependencies = { K_VIEW.m_image, K_VIEW.m_pose_asset };
        m_selected.m_source = source_node.m_product;
        auto support = Json::array();
        for (std::uint32_t triangle { 0 }; triangle < 2; ++triangle) {
            auto anchors = Json::array();
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                std::array<double, 3> weights {};
                weights[corner] = 1;
                anchors.push_back({ { "triangle", triangle }, { "weights", weights } });
            }
            support.push_back({ { "class", triangle == 0 ? "OBSERVED" : "INFERRED" },
                               { "anchors", anchors },
                               { "views",
                                Json::array({ { { "capture_id", K_VIEW.m_capture.m_value },
                                              { "frame_id", K_VIEW.m_frame.m_value } } }) },
                               { "uncertainty_units", triangle == 0 ? Json(nullptr) : Json(.1) } });
        }
        m_record = { { "schema_version", 1 },
                    { "kind", "GEOMETRY_PRODUCT" },
                    { "world_epoch", 3 },
                    { "pose_revision", 5 },
                    { "recipe_sha256", hash(K_RECIPE) },
                    { "capture", region_ref(m_views.m_selection.m_product) },
                    { "source", region_ref(m_selected.m_source) },
                    { "views", Json::array({ object_view(K_VIEW) }) },
                    { "frame", "SOURCE_CHUNK_LOCAL_RIGHT_HANDED_Y_UP" },
                    { "surface", "OPEN_SURFACE_ALLOWED" },
                    { "max_deviation_units", m_selected.m_max_deviation_units },
                    { "mesh",
                     { { "positions", { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 3, 0 }, { 2, 3, .5 } } },
                      { "normals", { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } } },
                      { "uv", { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } } },
                      { "tangents", { { 1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0, 0, 1 } } },
                      { "triangles", { { 0, 1, 2 }, { 1, 3, 2 } } },
                      { "groups", { 101, 102 } } } },
                    { "support", support },
                    { "manual_ancestor", manual } };
        if (change) {
            change(m_record, m_selected);
        }
        auto record = m_record.dump();
        if (bytes.m_record) {
            bytes.m_record(record);
        }
        auto node = region.node(11001, record);
        node.m_recipe_sha256 = hash(K_RECIPE);
        node.m_dependencies = { K_VIEW.m_image, K_VIEW.m_pose_asset, source_node.m_product };
        if (manual) {
            const std::string K_ACTION { "synthetic declared geometry assistance" };
            region.m_data[hash(K_ACTION)] = K_ACTION;
            node.m_manual_action_sha256 = hash(K_ACTION);
            node.m_manual_ancestor = true;
        }
        m_selected.m_product = node.m_product;
        region.m_graph.m_products.insert(region.m_graph.m_products.end(), { source_node, node });
        if (graph) {
            m_views.m_change_graph = [&](auto& value) {
                graph(value, m_selected);
            };
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto receipt = m_views.freeze(false);
        m_views.m_change_graph = {};
        return receipt;
    }

    GeometryProductRequest request() const {
        return { m_selected, m_capture, m_receipt, m_scene, m_receipt.graph().m_context };
    }

    SceneMesh& source_mesh() {
        if (!m_chunk || !m_chunk->m_mesh) {
            throw std::logic_error("Missing geometry fixture mesh");
        }
        return *m_chunk->m_mesh;
    }

    Result<AdmittedGeometryProduct> inspect(const std::stop_token& stop = {}) const {
        return inspect_geometry_product(request(), stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "geometry";
    }
};

}  // namespace world_explorer::geometry_product_test
