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

template <class Fixture>
std::pair<NativeColliderSelection, Json>
attach_collider(Fixture& fixture,
                const ColliderMutation& change = {},
                bool reorder = true,
                std::uint64_t id = 20000,
                const std::function<void(Json&)>& provenance_change = {}) {
    GeometryFixture& geometry { fixture.m_base.m_base };
    auto& region = geometry.m_views.m_object.m_region;
    auto staged = append_collider(geometry, change, reorder, id);
    auto selection = staged.first;
    auto root = staged.second;
    const auto K_BASE_REGION = std::ranges::find_if(fixture.m_provenance, [&](const auto& item) {
        return item.m_region.m_target == geometry.m_selected.m_product;
    });
    REQUIRE(K_BASE_REGION != fixture.m_provenance.end());
    auto provenance = Json::parse(region.m_data.at(K_BASE_REGION->m_product.m_content_sha256));
    const auto K_ORIGINAL = provenance["spans"];
    provenance["target"] = region_ref(selection.m_product);
    provenance["recipe_sha256"] = root["recipe_sha256"];
    provenance["spans"] = Json::array();
    for (std::size_t face { 0uz }; face < root["faces"].size(); ++face) {
        const auto K_SOURCE = root["faces"][face]["source_face"].get<std::size_t>();
        // Invalid descriptor cases still need a well-formed independent mask.
        const auto K_SPAN = std::ranges::find_if(K_ORIGINAL, [&](const auto& item) {
            const auto K_FIRST = item.at("first").template get<std::size_t>();
            return K_SOURCE >= K_FIRST &&
                   K_SOURCE - K_FIRST < item.at("count").template get<std::size_t>();
        });
        auto span = K_SPAN == K_ORIGINAL.end() ? K_ORIGINAL[0uz] : *K_SPAN;
        span["first"] = face;
        span["count"] = 1;
        span["parents"] = Json::array({ region_ref(geometry.m_selected.m_product) });
        provenance["spans"].push_back(span);
    }
    provenance["domain"]["elements"] = root["faces"].size();
    if (provenance_change) {
        provenance_change(provenance);
    }
    auto node = region.node(20001, provenance.dump());
    node.m_recipe_sha256 = hash("synthetic exact static structural collider; no walking authority");
    node.m_dependencies = { selection.m_product };
    region.m_graph.m_products.push_back(node);
    auto selected = *K_BASE_REGION;
    selected.m_product = node.m_product;
    selected.m_region.m_target = selection.m_product;
    selected.m_region.m_domain.m_elements = root["faces"].size();
    fixture.m_provenance.push_back(selected);
    fixture.m_definition.m_provenance.push_back(node.m_product);
    fixture.m_definition.m_collider = { selection.m_product, AssetColliderMode::SYNTHETIC_GAMEPLAY };
    fixture.m_definition.m_kind = AssetKind::STRUCTURAL;
    fixture.m_definition.m_pivot.m_policy = AssetPivotPolicy::STRUCTURAL_FRAME;
    fixture.m_receipt = freeze_collider(geometry);
    return { selection, root };
}

struct ColliderFixture {
    NativeFixture m_base;
    NativeColliderSelection m_selected;
    Json m_root;

    explicit ColliderFixture(const ColliderMutation& change = {},
                             bool reorder = true,
                             std::uint64_t id = 20000,
                             const std::function<void(Json&)>& provenance_change = {}) {
        auto staged = attach_collider(m_base, change, reorder, id, provenance_change);
        m_selected = staged.first;
        m_root = staged.second;
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
