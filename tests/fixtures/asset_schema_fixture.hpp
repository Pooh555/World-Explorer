#pragma once

#include "asset_region_fixture.hpp"
#include "world_explorer/asset_catalog/asset_definition.hpp"
#include "world_explorer/scene_store/asset_instances.hpp"

namespace world_explorer::asset_schema_test {

using namespace asset_region_test;

struct SchemaFixture {
    RegionFixture m_region;
    Json m_material_record;
    std::vector<AssetProvenanceSelection> m_provenance;
    AssetDefinition m_definition;
    AssetRevisionRef m_measurement, m_action;

    SchemaFixture() {
        auto material = m_region.node(2000, "synthetic material declaration; not PBR payload");
        material.m_dependencies = m_region.m_graph.m_products[2uz].m_dependencies;
        auto material_region = m_region.node(2001, "replaced material provenance");
        material_region.m_dependencies = { material.m_product };
        auto rights = m_region.node(3000, "synthetic rights declaration; no permission grant");
        auto measurement =
            m_region.node(3001, "synthetic metric declaration; not independent measurement");
        auto action = m_region.node(3002, "synthetic declared manual action; no authentication");
        action.m_manual_action_sha256 = action.m_product.m_content_sha256;
        action.m_manual_ancestor = true;
        m_measurement = measurement.m_product;
        m_action = action.m_product;
        m_region.m_graph.m_products.insert(
            m_region.m_graph.m_products.end(),
            { material, material_region, rights, measurement, action });
        m_material_record = m_region.m_record;
        m_material_record["target"] = region_ref(material.m_product);
        m_material_record["domain"] = {
            { "kind", "MATERIAL_TEXELS" }, { "elements", 8 }, { "width", 4 }, { "height", 2 } };
        m_definition = { 1,
                        { 42 },
                        1,
                        m_region.m_graph.m_context,
                        AssetKind::PROP,
                        { m_region.m_graph.m_products[0uz].m_product,
                         m_region.m_graph.m_products[1uz].m_product },
                        m_region.m_selection.m_target,
                        { { { 7 }, material.m_product, AssetLightingMode::GENERATED_PBR } },
                        {},
                        {},
                        {},
                        { { -1, 0, -1 }, { 1, 2, 1 } },
                        { AssetPivotPolicy::SUPPORT_BASE_CENTER, {}, false, {} },
                        {},
                        rights.m_product,
                        false };
    }

    AdmittedAssetReceipt freeze() {
        const auto K_RAW = m_material_record.dump();
        m_region.m_data[hash(K_RAW)] = K_RAW;
        m_region.m_graph.m_products[5uz].m_product.m_content_sha256 = hash(K_RAW);
        const auto K_RECEIPT = m_region.freeze();
        auto material_selection = m_region.m_selection;
        material_selection.m_target = m_region.m_graph.m_products[4uz].m_product;
        material_selection.m_domain = { AssetRegionKind::MATERIAL_TEXELS, 8, 4, 2 };
        m_definition.m_provenance = { m_region.m_graph.m_products[3uz].m_product,
                                     m_region.m_graph.m_products[5uz].m_product };
        m_provenance = { { m_definition.m_provenance[0uz], m_region.m_selection },
                        { m_definition.m_provenance[1uz], material_selection } };
        return K_RECEIPT;
    }

    Result<AdmittedAssetDefinition> admit(const AdmittedAssetReceipt& receipt,
                                          const std::stop_token& stop = {}) const {
        return admit_asset_definition(
            m_definition, receipt, m_provenance, m_region.m_graph.m_context, stop);
    }

    AssetInstanceSet instances(const AdmittedAssetDefinition& definition) const {
        AssetInstanceSet result { 1,
                                 { 90 },
                                 { 1 },
                                 m_region.m_graph.m_context,
                                 definition.definition().m_units,
                                 {},
                                 {},
                                 definition.definition().m_manual_ancestor };
        for (std::size_t index { 0uz }; index < 2uz; ++index) {
            result.m_instances.push_back({ { index + 1uz },
                                          definition.reference(),
                                          { {}, { static_cast<double>(index) * 5, 0, 0 }, 1 },
                                          { 7 },
                                          true,
                                          {},
                                          {},
                                          {},
                                          definition.definition().m_manual_ancestor });
        }
        return result;
    }
};

}  // namespace world_explorer::asset_schema_test
