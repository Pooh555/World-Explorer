#pragma once

#include "geometry_product_fixture.hpp"
#include "world_explorer/application/structure_graph.hpp"

namespace world_explorer::structure_graph_test {

using namespace geometry_product_test;

inline std::vector<Vec3d> ring_positions() {
    return { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 0, 4, 0 }, { 1, 1, 0 }, { 3, 1, 0 }, { 3, 3, 0 }, { 1, 3, 0 } };
}

inline std::vector<std::array<std::uint32_t, 3>> ring_faces() {
    return { { 0, 1, 5 }, { 0, 5, 4 }, { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
}

inline void ring_source(SceneState&, SceneChunk& chunk) {
    if (!chunk.m_mesh) {
        throw std::logic_error("Missing synthetic native mesh");
    }
    auto& mesh = chunk.m_mesh.value();
    mesh.m_vertices.clear();
    for (auto point : ring_positions()) {
        mesh.m_vertices.push_back({ { static_cast<float>(point.m_x),
                                    static_cast<float>(point.m_y),
                                    static_cast<float>(point.m_z) },
                                   { 0, 0, 1 } });
    }
    mesh.m_triangles = ring_faces();
    chunk.m_bounds = { { 0, 0, 0 }, { 4, 4, 1 } };
}

inline void ring_geometry(Json& doc, GeometryProductSelection&) {
    Json positions = Json::array(), uv = Json::array();
    for (auto point : ring_positions()) {
        positions.push_back({ point.m_x, point.m_y, point.m_z });
        uv.push_back({ point.m_x / 4, point.m_y / 4 });
    }
    doc["mesh"] = { { "positions", positions },
                   { "normals", std::vector<Json>(8uz, Json { 0, 0, 1 }) },
                   { "uv", uv },
                   { "tangents", std::vector<Json>(8uz, Json { 1, 0, 0, 1 }) },
                   { "triangles", ring_faces() },
                   { "groups", std::vector<int>(8uz, 101) } };
    const auto K_VIEW = doc["support"][0uz]["views"];
    doc["support"] = Json::array();
    for (std::size_t face { 0uz }; face < 8uz; ++face) {
        auto anchors = Json::array();
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            std::array<double, 3> weights {};
            weights[corner] = 1;
            anchors.push_back({ { "triangle", face }, { "weights", weights } });
        }
        doc["support"].push_back({ { "class", "OBSERVED" },
                                  { "anchors", anchors },
                                  { "views", K_VIEW },
                                  { "uncertainty_units", nullptr } });
    }
}

struct StructureFixtureMutations {
    std::function<void(Json&, StructureGraphSelection&)> m_record;
    std::function<void(std::string&)> m_bytes;
    std::function<void(AssetDependencyGraph&, const StructureGraphSelection&)> m_graph;
};

struct StructureFixture {
    GeometryFixture m_base;
    RecipeFixture m_output;
    StructureGraphSelection m_selected;
    Json m_record;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    AdmittedGeometryProduct m_geometry;

    explicit StructureFixture(const StructureFixtureMutations& changes = {},
                              bool manual = false,
                              const GeometryFixture::Mutation& geometry_change = {},
                              const GeometryFixture::SourceMutation& source_change = {})
        : m_base(
              [&](auto& doc, auto& selection) {
                  ring_geometry(doc, selection);
                  if (geometry_change) {
                      geometry_change(doc, selection);
                  }
              },
              [&](auto& scene, auto& chunk) {
                  ring_source(scene, chunk);
                  if (source_change) {
                      source_change(scene, chunk);
                  }
              }),
          m_receipt(stage(changes, manual)),
          m_capture(checked(m_base.m_views.inspect(m_receipt, false))), m_geometry(geometry()) { }

    AdmittedGeometryProduct geometry() const {
        auto request = m_base.request();
        request.m_receipt = m_receipt;
        request.m_capture = m_capture;
        return checked(inspect_geometry_product(request));
    }

    AdmittedAssetReceipt stage(const StructureFixtureMutations& changes, bool manual) {
        auto& region = m_base.m_views.m_object.m_region;
        const std::string K_RECIPE { "synthetic partial structure import; inference-only" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        m_selected = { {}, .1, .1 };
        m_record = {
            { "schema_version", 1 },
            { "kind", "STRUCTURE_GRAPH" },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "recipe_sha256", hash(K_RECIPE) },
            { "geometry", region_ref(m_base.m_selected.m_product) },
            { "capture", region_ref(m_base.m_capture.record().m_selection.m_product) },
            { "frame", "GEOMETRY_LOCAL_ARBITRARY_UNITS" },
            { "scope", "PARTIAL_PATCHES" },
            { "surfaces",
             Json::array(
                 { { { "id", 11 },
                   { "kind", "WALL" },
                   { "patch", "PLANE" },
                   { "faces", { 0, 1, 2, 3, 4, 5, 6, 7 } },
                   { "loops", { { 0, 1, 2, 3 }, { 4, 7, 6, 5 } } },
                   { "plane",
                    { { "normal", { 0, 0, 1 } }, { "offset_units", 0 }, { "residual_bound_units", 0 } } },
                   { "thickness", { { "source", "UNKNOWN" }, { "value_units", nullptr } } },
                   { "coverage", { 0, 8, 0, 0, 0 } } } }) },
            { "openings",
             Json::array({ { { "id", 31 },
                           { "surface_id", 11 },
                           { "loop", 1 },
                           { "rooms", { 21, nullptr } },
                           { "class", "INFERRED" },
                           { "confidence", .5 } } }) },
            { "rooms",
             Json::array({ { { "id", 21 },
                           { "boundaries", { 11 } },
                           { "contains",
                            Json::array({ { { "surface_id", 11 }, { "residual_bound_units", 0 } } }) } } }) },
            { "relations", Json::array() },
            { "manual_ancestor", manual } };
        if (changes.m_record) {
            changes.m_record(m_record, m_selected);
        }
        auto bytes = m_record.dump();
        if (changes.m_bytes) {
            changes.m_bytes(bytes);
        }
        auto root = region.node(13000, bytes);
        root.m_recipe_sha256 = hash(K_RECIPE);
        root.m_dependencies = { m_base.m_selected.m_product };
        root.m_manual_ancestor = manual;
        if (manual) {
            const std::string K_ACTION { "synthetic declared structure edit" };
            region.m_data[hash(K_ACTION)] = K_ACTION;
            root.m_manual_action_sha256 = hash(K_ACTION);
        }
        m_selected.m_product = root.m_product;
        region.m_graph.m_products.push_back(root);
        if (changes.m_graph) {
            m_base.m_views.m_change_graph = [&](auto& graph) {
                changes.m_graph(graph, m_selected);
            };
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto receipt = m_base.m_views.freeze(false);
        m_base.m_views.m_change_graph = {};
        return receipt;
    }

    StructureGraphRequest request() const {
        return { m_selected, m_geometry, m_capture, m_receipt, m_receipt.graph().m_context };
    }

    Result<AdmittedStructureGraph> inspect(const std::stop_token& stop = {}) const {
        return inspect_structure_graph(request(), stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "structure";
    }
};

inline void split_patch(Json& doc) {
    auto first = doc["surfaces"][0uz], second = first;
    first["id"] = 11;
    first["faces"] = { 0, 1 };
    first["loops"] = { { 0, 1, 5, 4 } };
    first["coverage"] = { 0, 2, 0, 0, 0 };
    second["id"] = 12;
    second["faces"] = { 2, 3, 4, 5, 6, 7 };
    second["loops"] = { { 1, 2, 3, 0, 4, 7, 6, 5 } };
    second["coverage"] = { 0, 6, 0, 0, 0 };
    doc["surfaces"] = Json::array({ first, second });
    doc["openings"] = Json::array();
    doc["rooms"][0uz]["boundaries"] = { 11, 12 };
    doc["relations"] = Json::array({ { { "kind", "SUPPORT" },
                                     { "source", 11 },
                                     { "target", 12 },
                                     { "source_vertex", 1 },
                                     { "target_vertex", 1 },
                                     { "residual_bound_units", 0 } } });
}

}  // namespace world_explorer::structure_graph_test
