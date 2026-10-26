#pragma once

#include "material_product_fixture.hpp"
#include "world_explorer/application/native_assets.hpp"

namespace world_explorer::native_assets_test {

using namespace material_product_test;

inline const AdmittedGeometryProduct& native_geometry(const NativeAssetSelection& asset) {
    if (!asset.m_geometry) {
        throw std::logic_error("Missing native geometry fixture");
    }
    return *asset.m_geometry;
}

struct NativeFixture {
    MaterialFixture m_base;
    RecipeFixture m_output;
    std::vector<MaterialProductSelection> m_materials;
    std::vector<AssetProvenanceSelection> m_provenance;
    AssetDefinition m_definition;
    AdmittedAssetReceipt m_receipt;
    using Mutation = std::function<void(Json&, bool)>;

    explicit NativeFixture(bool manual = false,
                           const Mutation& change = {},
                           AssetLightingMode lighting = AssetLightingMode::ESTIMATED_PBR)
        : m_base({ .m_lighting = manual ? AssetLightingMode::GENERATED_PBR : lighting,
                  .m_manual = manual }),
          m_receipt(stage(manual, change)) { }

    AdmittedAssetReceipt stage(bool manual, const Mutation& change) {
        auto& region = m_base.m_base.m_views.m_object.m_region;
        const auto K_GEOMETRY = m_base.m_base.m_selected.m_product;
        const auto K_CAPTURE = m_base.m_capture.record().m_selection.m_product;
        const auto K_NATIVE = m_base.m_base.m_selected.m_source;
        m_materials = { m_base.m_selected };
        auto second_doc = m_base.m_record;
        second_doc["variant_id"] = 102;
        auto second = region.node(12201, second_doc.dump());
        const auto K_FIRST = std::ranges::find(region.m_graph.m_products,
                                               m_base.m_selected.m_product,
                                               &AssetDependencyNode::m_product);
        REQUIRE(K_FIRST != region.m_graph.m_products.end());
        second.m_recipe_sha256 = K_FIRST->m_recipe_sha256;
        second.m_dependencies = K_FIRST->m_dependencies;
        second.m_manual_ancestor = manual;
        if (manual) {
            K_FIRST->m_manual_action_sha256 = hash("synthetic declared material edit");
            second.m_manual_action_sha256 = K_FIRST->m_manual_action_sha256;
        }
        region.m_graph.m_products.push_back(second);
        auto second_pin = m_base.m_selected;
        second_pin.m_product = second.m_product;
        second_pin.m_variant = { 102 };
        m_materials.push_back(second_pin);

        const auto& channel = m_base.m_selected.m_channels.begin()->second;
        auto prototype = Json::parse(region.m_data.at(channel.m_provenance.m_content_sha256));
        const auto K_ADD = [&](std::uint64_t id,
                               const AssetRevisionRef& target,
                               Json record,
                               const AssetRegionSelection& selected,
                               bool geometry) {
            const auto K_TARGET = std::ranges::find(
                region.m_graph.m_products, target, &AssetDependencyNode::m_product);
            REQUIRE(K_TARGET != region.m_graph.m_products.end());
            record["target"] = region_ref(target);
            record["recipe_sha256"] = K_TARGET->m_recipe_sha256;
            if (change) {
                change(record, geometry);
            }
            auto node = region.node(id, record.dump());
            node.m_recipe_sha256 = K_TARGET->m_recipe_sha256;
            node.m_dependencies = { target };
            node.m_manual_ancestor = K_TARGET->m_manual_ancestor;
            region.m_graph.m_products.push_back(node);
            auto selected_domain = selected;
            selected_domain.m_domain.m_elements = record["domain"]["elements"].get<std::uint64_t>();
            selected_domain.m_domain.m_width = record["domain"]["width"].get<std::uint64_t>();
            selected_domain.m_domain.m_height = record["domain"]["height"].get<std::uint64_t>();
            m_provenance.push_back({ node.m_product, selected_domain });
        };
        for (std::size_t index { 0uz }; index < m_materials.size(); ++index) {
            const auto& material = m_materials[index];
            auto selected = channel.m_region;
            selected.m_target = material.m_product;
            K_ADD(16000uz + index, material.m_product, prototype, selected, false);
        }
        auto faces = prototype;
        faces["domain"] = {
            { "kind", "GEOMETRY_FACES" }, { "elements", 2 }, { "width", 0 }, { "height", 0 } };
        faces["manual_ancestor"] = false;
        faces["spans"] = Json::array();
        for (std::size_t index { 0uz }; index < 2uz; ++index) {
            const auto& support = m_base.m_base.m_record["support"][index];
            faces["spans"].push_back({ { "first", index },
                                      { "count", 1 },
                                      { "class", support["class"] },
                                      { "confidence", nullptr },
                                      { "parents", Json::array({ region_ref(K_NATIVE) }) },
                                      { "support", support["views"] },
                                      { "manual_ancestor", false } });
        }
        auto selected = channel.m_region;
        selected.m_target = K_GEOMETRY;
        selected.m_domain = { AssetRegionKind::GEOMETRY_FACES, 2, 0, 0 };
        K_ADD(16002, K_GEOMETRY, faces, selected, true);
        auto rights = region.node(16003, "synthetic native adoption rights; unreviewed");
        region.m_graph.m_products.push_back(rights);
        m_definition = { 1,
                        { 42 },
                        1,
                        region.m_graph.m_context,
                        AssetKind::PROP,
                        { K_CAPTURE, K_NATIVE },
                        K_GEOMETRY,
                        {},
                        {},
                        {},
                        {},
                        { { -1, -2, -3 }, { 1, 1, -2.5 } },
                        { AssetPivotPolicy::ATTACHMENT_ORIGIN, { {}, { -1, -2, -3 }, 1 }, true, {} },
                        {},
                        rights.m_product,
                        manual };
        for (const auto& material : m_materials) {
            m_definition.m_materials.push_back(
                { material.m_variant, material.m_product, material.m_lighting });
        }
        for (const auto& pin : m_provenance) {
            m_definition.m_provenance.push_back(pin.m_product);
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        return m_base.m_base.m_views.freeze(false);
    }

    NativeAssetsRequest request() const {
        auto capture = checked(m_base.m_base.m_views.inspect(m_receipt, false));
        auto geometry_request = m_base.m_base.request();
        geometry_request.m_receipt = m_receipt;
        geometry_request.m_capture = capture;
        auto geometry = checked(inspect_geometry_product(geometry_request));
        NativeAssetSelection asset { m_definition, m_provenance, geometry, {}, {}, { 1 } };
        for (const auto& material : m_materials) {
            asset.m_materials.push_back(checked(inspect_material_product(
                { material, geometry, capture, m_receipt, m_receipt.graph().m_context })));
        }
        const auto K_DEFINITION = checked(admit_asset_definition(
            m_definition, m_receipt, m_provenance, m_receipt.graph().m_context));
        AssetInstanceSet instances {
            1, { 90 }, { 1 }, m_receipt.graph().m_context, {}, {}, {}, m_definition.m_manual_ancestor };
        // Original native world = 2 * geometry + (10,-4,3). Compensate
        // asset = geometry + (-1,-2,-3) to preserve every world position.
        instances.m_instances = { { { 1 },
                                  K_DEFINITION.reference(),
                                  { {}, { 12, 0, 9 }, 2 },
                                  { 101 },
                                  true,
                                  {},
                                  {},
                                  {},
                                  m_definition.m_manual_ancestor },
                                 { { 2 },
                                  K_DEFINITION.reference(),
                                  { {}, { 30, 5, 9 }, 2 },
                                  { 102 },
                                  true,
                                  {},
                                  {},
                                  {},
                                  m_definition.m_manual_ancestor } };
        return { m_receipt, { asset }, instances, m_receipt.graph().m_context };
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "native-assets";
    }
};

inline void rebind(NativeAssetsRequest& request) {
    const auto K_DEFINITION = checked(admit_asset_definition(request.m_assets[0uz].m_definition,
                                                             request.m_receipt.value(),
                                                             request.m_assets[0uz].m_provenance,
                                                             request.m_expected));
    for (auto& instance : request.m_instances.m_instances) {
        instance.m_definition = K_DEFINITION.reference();
    }
}

}  // namespace world_explorer::native_assets_test
