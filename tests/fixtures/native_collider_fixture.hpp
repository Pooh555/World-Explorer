#pragma once

#include "native_assets_fixture.hpp"
#include "structure_graph_fixture.hpp"

namespace world_explorer::native_collider_test {

using namespace native_assets_test;
using ColliderMutation = std::function<void(Json&, NativeColliderSelection&)>;

inline std::pair<NativeColliderSelection, Json>
append_collider(GeometryFixture& geometry,
                const ColliderMutation& change = {},
                bool reorder = true,
                std::uint64_t id = 20000,
                const std::function<void(std::string&)>& bytes_change = {},
                bool wrong_parent = false) {
    auto& region = geometry.m_views.m_object.m_region;
    const std::string K_RECIPE { "synthetic exact static structural collider; no walking authority" };
    region.m_data[hash(K_RECIPE)] = K_RECIPE;
    const auto& mesh = geometry.m_record["mesh"];
    const auto K_VERTICES = mesh["positions"].size();
    const auto K_FACES = mesh["triangles"].size();
    auto vertices = Json::array(), faces = Json::array();
    for (std::size_t vertex { 0uz }; vertex < K_VERTICES; ++vertex) {
        vertices.push_back(reorder ? K_VERTICES - vertex - 1uz : vertex);
    }
    for (std::size_t face { 0uz }; face < K_FACES; ++face) {
        const auto K_SOURCE = reorder ? K_FACES - face - 1uz : face;
        auto indices = Json::array();
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            const auto K_VERTEX =
                mesh["triangles"][K_SOURCE][(corner + 1uz) % 3uz].get<std::size_t>();
            indices.push_back(reorder ? K_VERTICES - K_VERTEX - 1uz : K_VERTEX);
        }
        faces.push_back({ { "source_face", K_SOURCE }, { "vertices", indices } });
    }
    Json root { { "schema_version", 1 },
               { "kind", "NATIVE_STATIC_TRIANGLE_COLLIDER" },
               { "world_epoch", 3 },
               { "pose_revision", 5 },
               { "recipe_sha256", hash(K_RECIPE) },
               { "base", region_ref(geometry.m_selected.m_product) },
               { "purpose", "SYNTHETIC_GAMEPLAY" },
               { "body", "STATIC_STRUCTURAL" },
               { "units", "ARBITRARY" },
               { "frame", "BASE_GEOMETRY_LOCAL" },
               { "cook",
                { { "format", "WE_NATIVE_COLLIDER_TRIANGLES_V1" },
                 { "weld_vertices", false },
                 { "double_sided", false },
                 { "max_vertices", 8192 },
                 { "max_triangles", 8192 } } },
               { "vertex_sources", vertices },
               { "faces", faces },
               { "manual_ancestor", geometry.m_record["manual_ancestor"] } };
    NativeColliderSelection selection {};
    if (change) {
        change(root, selection);
    }
    auto text = root.dump();
    if (bytes_change) {
        bytes_change(text);
    }
    auto node = region.node(id, text);
    node.m_recipe_sha256 = hash(K_RECIPE);
    node.m_dependencies = { wrong_parent ? geometry.m_selected.m_source
                                        : geometry.m_selected.m_product };
    node.m_manual_ancestor = geometry.m_record["manual_ancestor"].get<bool>();
    region.m_graph.m_products.push_back(node);
    selection.m_product = node.m_product;
    return { selection, root };
}

inline AdmittedAssetReceipt freeze_collider(GeometryFixture& geometry) {
    auto& region = geometry.m_views.m_object.m_region;
    std::filesystem::remove_all(region.m_directory.m_root / "blobs");
    std::filesystem::create_directory(region.m_directory.m_root / "blobs");
    return geometry.m_views.freeze(false);
}

inline NativeColliderRequest collider_request(const GeometryFixture& geometry,
                                              const AdmittedAssetReceipt& receipt,
                                              const NativeColliderSelection& selection) {
    auto input = geometry.request();
    input.m_receipt = receipt;
    input.m_capture = checked(geometry.m_views.inspect(receipt, false));
    return { selection, checked(inspect_geometry_product(input)), receipt, input.m_expected };
}

struct ColliderFixture {
    NativeFixture m_base;
    NativeColliderSelection m_selected;
    Json m_root;

    explicit ColliderFixture(const ColliderMutation& change = {},
                             bool reorder = true,
                             std::uint64_t id = 20000,
                             const std::function<void(Json&)>& provenance_change = {}) {
        auto& geometry = m_base.m_base.m_base;
        auto& region = geometry.m_views.m_object.m_region;
        auto staged = append_collider(geometry, change, reorder, id);
        m_selected = staged.first;
        m_root = staged.second;
        auto provenance =
            Json::parse(region.m_data.at(m_base.m_provenance.back().m_product.m_content_sha256));
        const auto K_ORIGINAL = provenance["spans"];
        provenance["target"] = region_ref(m_selected.m_product);
        provenance["recipe_sha256"] = m_root["recipe_sha256"];
        provenance["spans"] = Json::array();
        for (std::size_t face { 0uz }; face < m_root["faces"].size(); ++face) {
            const auto K_SOURCE = m_root["faces"][face]["source_face"].get<std::size_t>();
            // Invalid descriptor cases still need a well-formed independent mask.
            auto span = K_ORIGINAL[K_SOURCE < K_ORIGINAL.size() ? K_SOURCE : 0uz];
            span["first"] = face;
            span["parents"] = Json::array({ region_ref(geometry.m_selected.m_product) });
            provenance["spans"].push_back(span);
        }
        provenance["domain"]["elements"] = m_root["faces"].size();
        if (provenance_change) {
            provenance_change(provenance);
        }
        auto node = region.node(20001, provenance.dump());
        node.m_recipe_sha256 =
            hash("synthetic exact static structural collider; no walking authority");
        node.m_dependencies = { m_selected.m_product };
        region.m_graph.m_products.push_back(node);
        auto selected = m_base.m_provenance.back();
        selected.m_product = node.m_product;
        selected.m_region.m_target = m_selected.m_product;
        selected.m_region.m_domain.m_elements = m_root["faces"].size();
        m_base.m_provenance.push_back(selected);
        m_base.m_definition.m_provenance.push_back(node.m_product);
        m_base.m_definition.m_collider = { m_selected.m_product,
                                          AssetColliderMode::SYNTHETIC_GAMEPLAY };
        m_base.m_definition.m_kind = AssetKind::STRUCTURAL;
        m_base.m_definition.m_pivot.m_policy = AssetPivotPolicy::STRUCTURAL_FRAME;
        m_base.m_receipt = freeze_collider(geometry);
    }

    NativeColliderRequest collider() const {
        return collider_request(m_base.m_base.m_base, m_base.m_receipt, m_selected);
    }

    NativeAssetsRequest request() const {
        auto input = m_base.request();
        input.m_assets[0uz].m_collider = checked(inspect_native_collider(collider()));
        return input;
    }
};

}  // namespace world_explorer::native_collider_test
