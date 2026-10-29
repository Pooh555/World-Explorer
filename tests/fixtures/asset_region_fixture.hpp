#pragma once

#include "asset_component_fixture.hpp"
#include "world_explorer/asset_catalog/region_provenance.hpp"

#include <set>

namespace world_explorer::asset_region_test {

using namespace asset_recipe_test;

inline Json region_ref(const AssetRevisionRef& value) {
    return { { "product_id", value.m_product.m_value },
            { "revision", value.m_revision },
            { "content_sha256", value.m_content_sha256 } };
}

inline Json source_view_id(const AssetSourceViewId& value) {
    return { { "capture_id", value.m_capture.m_value }, { "frame_id", value.m_frame.m_value } };
}

inline Json region_source(const AssetSourceViewRef& value) {
    return { { "view", source_view_id(value.m_view) },
            { "calibration_id", value.m_calibration.m_value },
            { "image", region_ref(value.m_image) },
            { "calibration_sha256", value.m_calibration_sha256 },
            { "pose_sha256", value.m_pose_sha256 },
            { "image_grid_sha256", value.m_image_grid_sha256 } };
}

struct RegionFixture {
    RecipeFixture m_directory;
    AssetDependencyGraph m_graph { 1, { { 3 }, { 5 } }, {} };
    AssetRegionSelection m_selection;
    Json m_record;
    AssetReceiptProfile m_receipt_profile { AssetReceiptProfile::SMALL_V1 };
    std::map<std::string, std::string> m_data;

    AssetDependencyNode node(std::uint64_t id, const std::string& bytes) {
        m_data[hash(bytes)] = bytes;
        const std::string K_RECIPE { "synthetic opaque recipe; not an executable recipe" };
        m_data[hash(K_RECIPE)] = K_RECIPE;
        return { { { id }, 1, hash(bytes) }, {}, hash(K_RECIPE), {}, false };
    }

    RegionFixture() {
        auto image_a = node(1, "synthetic pixels A");
        auto image_b = node(2, "synthetic pixels B");
        auto target = node(1000, "synthetic opaque target; not actual mesh/texture bytes");
        target.m_dependencies = { image_a.m_product, image_b.m_product };
        auto sidecar = node(1001, "replaced by sidecar JSON");
        sidecar.m_dependencies = { target.m_product };
        m_graph.m_products = { image_a, image_b, target, sidecar };
        m_selection = { target.m_product,
                       { AssetRegionKind::GEOMETRY_FACES, 8, 0, 0 },
                       { { { { 7 }, { 10 } },
                         { 8 },
                         image_a.m_product,
                         hash("synthetic calibration"),
                         hash("synthetic pose A"),
                         hash("identity grid") },
                        { { { 7 }, { 20 } },
                         { 8 },
                         image_b.m_product,
                         hash("synthetic calibration"),
                         hash("synthetic pose B"),
                         hash("identity grid") } } };
        auto sources = Json::array();
        for (const auto& source : m_selection.m_sources) {
            sources.push_back(region_source(source));
        }
        auto spans = Json::array();
        const std::array<std::string, 4> K_CLASSES { "UNKNOWN", "OBSERVED", "INFERRED", "GENERATED" };
        for (std::size_t index { 0uz }; index < K_CLASSES.size(); ++index) {
            spans.push_back(
                { { "first", index * 2uz },
                 { "count", 2 },
                 { "class", K_CLASSES[index] },
                 { "confidence", index ? Json(1.0) : Json(nullptr) },
                 { "parents", index ? Json::array({ region_ref(image_a.m_product) }) : Json::array() },
                 { "support",
                  index == 1uz ? Json::array({ source_view_id(m_selection.m_sources[0uz].m_view),
                                              source_view_id(m_selection.m_sources[1uz].m_view) })
                               : Json::array() },
                 { "manual_ancestor", false } });
        }
        spans[3uz]["parents"] = Json::array({ region_ref(image_b.m_product) });
        m_record = {
            { "schema_version", 1 },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "target", region_ref(target.m_product) },
            { "recipe_sha256", target.m_recipe_sha256 },
            { "domain", { { "kind", "GEOMETRY_FACES" }, { "elements", 8 }, { "width", 0 }, { "height", 0 } } },
            { "sources", sources },
            { "spans", spans },
            { "manual_ancestor", false } };
    }

    void manual(bool ancestor) {
        const std::string K_ACTION {
            "synthetic declared manual action; not an authenticated journal" };
        m_data[hash(K_ACTION)] = K_ACTION;
        auto& target = m_graph.m_products[2uz];
        if (ancestor) {
            m_graph.m_products[0uz].m_manual_action_sha256 = hash(K_ACTION);
            m_graph.m_products[0uz].m_manual_ancestor = true;
            m_record["spans"][1uz]["manual_ancestor"] = true;
            m_record["spans"][2uz]["manual_ancestor"] = true;
        } else {
            target.m_manual_action_sha256 = hash(K_ACTION);
            m_record["spans"][3uz]["class"] = "MANUALLY_EDITED";
            m_record["spans"][3uz]["manual_ancestor"] = true;
        }
        target.m_manual_ancestor = true;
        m_graph.m_products[3uz].m_manual_ancestor = true;
        m_record["manual_ancestor"] = true;
    }

    AdmittedAssetReceipt freeze(const std::optional<std::string>& raw = {}) {
        const auto K_TEXT = raw.value_or(m_record.dump());
        m_data[hash(K_TEXT)] = K_TEXT;
        m_graph.m_products[3uz].m_product.m_content_sha256 = hash(K_TEXT);
        const auto K_GRAPH = validate_asset_dependencies(m_graph, m_graph.m_context);
        REQUIRE(K_GRAPH);
        auto products = Json::array();
        std::set<std::string> needed;
        for (const auto& node : m_graph.m_products) {
            auto dependencies = Json::array();
            for (const auto& parent : node.m_dependencies) {
                dependencies.push_back(region_ref(parent));
            }
            products.push_back({ { "product", region_ref(node.m_product) },
                                { "dependencies", dependencies },
                                { "recipe_sha256", node.m_recipe_sha256 },
                                { "manual_action_sha256",
                                 node.m_manual_action_sha256.has_value()
                                     ? Json(node.m_manual_action_sha256.value())
                                     : Json(nullptr) },
                                { "manual_ancestor", node.m_manual_ancestor } });
            needed.insert(node.m_product.m_content_sha256);
            needed.insert(node.m_recipe_sha256);
            if (node.m_manual_action_sha256.has_value()) {
                needed.insert(node.m_manual_action_sha256.value());
            }
        }
        auto files = Json::array();
        for (const auto& sha : needed) {
            const auto& bytes = m_data.at(sha);
            write(m_directory.m_root / "blobs" / (sha + ".bin"), bytes);
            files.push_back(
                { { "file", "blobs/" + sha + ".bin" }, { "sha256", sha }, { "bytes", bytes.size() } });
        }
        const Json K_DOCUMENT { { "schema_version", static_cast<std::uint32_t>(m_receipt_profile) },
                               { "operation_id", 1 },
                               { "graph_sha256", K_GRAPH->m_graph_sha256 },
                               { "graph",
                                { { "schema_version", 1 },
                                 { "world_epoch", m_graph.m_context.m_world_epoch.m_value },
                                 { "pose_revision", m_graph.m_context.m_pose_revision.m_value },
                                 { "products", products } } },
                               { "files", files } };
        const auto K_RAW = K_DOCUMENT.dump();
        write(m_directory.m_root / "receipt.json", K_RAW);
        const auto K_RECEIPT = read_asset_receipt(
            { m_directory.m_root, hash(K_RAW), m_receipt_profile }, m_graph.m_context);
        REQUIRE(K_RECEIPT);
        return *K_RECEIPT;
    }

    Result<AssetRegionInspection> inspect(const AdmittedAssetReceipt& receipt,
                                          const std::stop_token& stop = {}) const {
        return inspect_asset_region_provenance(
            receipt, { 1001 }, m_selection, m_graph.m_context, stop);
    }
};

}  // namespace world_explorer::asset_region_test
