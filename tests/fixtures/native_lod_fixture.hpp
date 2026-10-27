#pragma once

#include "native_assets_fixture.hpp"

namespace world_explorer::native_lod_test {

using namespace native_assets_test;

inline void subdivide(Json& doc, GeometryProductSelection&) {
    auto& mesh = doc["mesh"];
    mesh["positions"].push_back({ .5, 1.5, 0 });
    mesh["normals"].push_back({ 0, 0, 1 });
    mesh["uv"].push_back({ .25, .5 });
    mesh["tangents"].push_back({ 1, 0, 0, 1 });
    mesh["triangles"] = { { 0, 1, 4 }, { 1, 2, 4 }, { 2, 0, 4 }, { 1, 3, 2 } };
    mesh["groups"] = { 101, 101, 101, 102 };
    const auto K_ORIGINAL = doc["support"];
    auto support = Json::array();
    const Json K_CENTER { { "triangle", 0 }, { "weights", { .25, .25, .5 } } };
    for (std::size_t edge { 0uz }; edge < 3uz; ++edge) {
        auto face = K_ORIGINAL[0uz];
        face["anchors"] = Json::array({ K_ORIGINAL[0uz]["anchors"][edge],
                                       K_ORIGINAL[0uz]["anchors"][(edge + 1uz) % 3uz],
                                       K_CENTER });
        support.push_back(face);
    }
    support.push_back(K_ORIGINAL[1uz]);
    doc["support"] = support;
}

struct LodFixture {
    NativeFixture m_base;
    GeometryProductSelection m_candidate;
    NativeLodSelection m_selected;
    Json m_doc;
    using Mutation = std::function<void(Json&, Json&)>;

    explicit LodFixture(const Mutation& change = {},
                        std::uint64_t binding_id = 19001,
                        const GeometryFixture::Mutation& base_change = {})
        : m_base(false,
                 {},
                 AssetLightingMode::ESTIMATED_PBR,
                 {},
                 [&](Json& doc, GeometryProductSelection& selected) {
                     subdivide(doc, selected);
                     if (base_change) {
                         base_change(doc, selected);
                     }
                 }) {
        auto& geometry = m_base.m_base.m_base;
        auto& region = geometry.m_views.m_object.m_region;
        auto candidate = geometry.m_record;
        for (auto name : { "positions", "normals", "uv", "tangents" }) {
            candidate["mesh"][name].erase(4uz);
        }
        candidate["mesh"]["triangles"] = { { 0, 1, 2 }, { 1, 3, 2 } };
        candidate["mesh"]["groups"] = { 101, 102 };
        auto support = candidate["support"][0uz];
        support["anchors"] = Json::array();
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            std::array<double, 3> weights {};
            weights[corner] = 1;
            support["anchors"].push_back({ { "triangle", 0 }, { "weights", weights } });
        }
        candidate["support"] = Json::array({ support, candidate["support"][3uz] });
        m_doc = {
            { "schema_version", 1 },
            { "kind", "NATIVE_EXACT_FAN_LOD" },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "recipe_sha256", geometry.m_record["recipe_sha256"] },
            { "base", region_ref(geometry.m_selected.m_product) },
            { "candidate", nullptr },
            { "max_deviation_units", 0 },
            { "vertex_sources", { 0, 1, 2, 3 } },
            { "faces",
             Json::array({ { { "source_faces", { 0, 1, 2 } }, { "center", 4 }, { "weights", { 64, 64, 128 } } },
                          { { "source_faces", { 3 } }, { "center", nullptr }, { "weights", nullptr } } }) },
            { "manual_ancestor", false } };
        if (change) {
            change(m_doc, candidate);
        }
        const auto K_BASE = std::ranges::find(region.m_graph.m_products,
                                              geometry.m_selected.m_product,
                                              &AssetDependencyNode::m_product);
        REQUIRE(K_BASE != region.m_graph.m_products.end());
        auto node = region.node(19000, candidate.dump());
        node.m_recipe_sha256 = K_BASE->m_recipe_sha256;
        node.m_dependencies = K_BASE->m_dependencies;
        region.m_graph.m_products.push_back(node);
        m_candidate = geometry.m_selected;
        m_candidate.m_product = node.m_product;
        m_doc["candidate"] = region_ref(node.m_product);
        auto binding = region.node(binding_id, m_doc.dump());
        binding.m_recipe_sha256 = node.m_recipe_sha256;
        binding.m_dependencies = { geometry.m_selected.m_product, node.m_product };
        region.m_graph.m_products.push_back(binding);
        m_selected.m_product = binding.m_product;

        const auto K_BASE_REGION = m_base.m_provenance.back();
        auto provenance = Json::parse(region.m_data.at(K_BASE_REGION.m_product.m_content_sha256));
        provenance["target"] = region_ref(node.m_product);
        provenance["domain"]["elements"] = 2;
        auto second = provenance["spans"][3uz];
        second["first"] = 1;
        provenance["spans"] = Json::array({ provenance["spans"][0uz], second });
        auto sidecar = region.node(19002, provenance.dump());
        sidecar.m_recipe_sha256 = node.m_recipe_sha256;
        sidecar.m_dependencies = { node.m_product };
        region.m_graph.m_products.push_back(sidecar);
        auto selected = K_BASE_REGION;
        selected.m_product = sidecar.m_product;
        selected.m_region.m_target = node.m_product;
        selected.m_region.m_domain.m_elements = 2;
        m_base.m_provenance.push_back(selected);
        m_base.m_definition.m_lods = { node.m_product };
        m_base.m_definition.m_sources.push_back(binding.m_product);
        m_base.m_definition.m_provenance.push_back(sidecar.m_product);
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        m_base.m_receipt = geometry.m_views.freeze(false);
    }

    NativeLodRequest lod_request() const {
        auto base = m_base.m_base.m_base.request();
        base.m_receipt = m_base.m_receipt;
        base.m_capture = checked(m_base.m_base.m_base.m_views.inspect(m_base.m_receipt, false));
        auto candidate = base;
        candidate.m_selection = m_candidate;
        return { m_selected,
                checked(inspect_geometry_product(base)),
                checked(inspect_geometry_product(candidate)),
                m_base.m_receipt,
                base.m_expected };
    }

    NativeAssetsRequest request() const {
        auto result = m_base.request();
        result.m_assets[0uz].m_lods = { checked(inspect_native_lod(lod_request())) };
        return result;
    }
};

}  // namespace world_explorer::native_lod_test
