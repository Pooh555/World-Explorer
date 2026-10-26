#pragma once

#include "completion_product_fixture.hpp"
#include "native_assets_fixture.hpp"

namespace world_explorer::native_assets_test {

struct CompletedNativeFixture {
    completion_product_test::CompletionFixture m_base;
    RecipeFixture m_output;
    MaterialProductSelection m_material;
    AssetDefinition m_definition;
    std::vector<AssetProvenanceSelection> m_provenance;
    AdmittedAssetReceipt m_receipt;

    explicit CompletedNativeFixture(CompletionSubject subject)
        : m_base({ .m_subject = subject }), m_receipt(stage()) { }

    AdmittedAssetReceipt stage() {
        auto& region = m_base.m_base.m_base.m_views.m_object.m_region;
        const auto& geometry = m_base.m_candidates[0uz];
        const auto K_VIEW = geometry.selection().m_views[0uz];
        const AssetSourceViewRef K_SOURCE { { K_VIEW.m_capture, K_VIEW.m_frame },
                                           K_VIEW.m_calibration,
                                           K_VIEW.m_image,
                                           hash("synthetic calibration declaration"),
                                           K_VIEW.m_pose_asset.m_content_sha256,
                                           hash("synthetic source pixel grid") };
        const std::string K_RECIPE { "synthetic completed native asset material import" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        const auto K_NODE =
            [&](std::uint64_t id, const std::string& bytes, std::vector<AssetRevisionRef> parents) {
                auto node = region.node(id, bytes);
                node.m_recipe_sha256 = hash(K_RECIPE);
                node.m_dependencies = std::move(parents);
                region.m_graph.m_products.push_back(node);
                return node.m_product;
            };
        const auto K_SIDECAR = [&](std::uint64_t id,
                                   const AssetRevisionRef& target,
                                   AssetRegionDomain domain,
                                   const Json& spans) {
            const auto K_TARGET = std::ranges::find(
                region.m_graph.m_products, target, &AssetDependencyNode::m_product);
            REQUIRE(K_TARGET != region.m_graph.m_products.end());
            Json doc { { "schema_version", 1 },
                      { "world_epoch", 3 },
                      { "pose_revision", 5 },
                      { "target", region_ref(target) },
                      { "recipe_sha256", K_TARGET->m_recipe_sha256 },
                      { "domain",
                       { { "kind",
                         domain.m_kind == AssetRegionKind::GEOMETRY_FACES ? "GEOMETRY_FACES"
                                                                          : "MATERIAL_TEXELS" },
                        { "elements", domain.m_elements },
                        { "width", domain.m_width },
                        { "height", domain.m_height } } },
                      { "sources", Json::array({ region_source(K_SOURCE) }) },
                      { "spans", spans },
                      { "manual_ancestor", false } };
            auto node = region.node(id, doc.dump());
            node.m_recipe_sha256 = K_TARGET->m_recipe_sha256;
            node.m_dependencies = { target };
            region.m_graph.m_products.push_back(node);
            return AssetProvenanceSelection { node.m_product, { target, domain, { K_SOURCE } } };
        };
        m_material = { {},
                      { 101 },
                      AssetLightingMode::GENERATED_PBR,
                      2,
                      2,
                      MaterialFilter::LINEAR,
                      MaterialWrap::CLAMP_TO_EDGE,
                      {},
                      {} };
        const auto K_MATERIAL_SPANS =
            Json::array({ { { "first", 0 },
                          { "count", 4 },
                          { "class", "GENERATED" },
                          { "confidence", .5 },
                          { "parents", Json::array({ region_ref(geometry.selection().m_product) }) },
                          { "support", Json::array({ source_view_id(K_SOURCE.m_view) }) },
                          { "manual_ancestor", false } } });
        std::vector<AssetRevisionRef> parents { geometry.selection().m_product };
        auto channels = Json::object();
        for (std::size_t index { 1uz }; index < 7uz; ++index) {
            const auto K_ROLE = static_cast<MaterialChannelRole>(index);
            const auto K_ID = 17000uz + 2uz * index;
            const auto K_CHANNEL =
                K_NODE(K_ID, channel_bytes(K_ROLE, K_ID, 2, 2), { geometry.selection().m_product });
            const auto K_REGION = K_SIDECAR(K_ID + 1uz,
                                            K_CHANNEL,
                                            { AssetRegionKind::MATERIAL_TEXELS, 4, 2, 2 },
                                            K_MATERIAL_SPANS);
            m_material.m_channels.emplace(
                K_ROLE,
                MaterialChannelSelection { K_CHANNEL, K_REGION.m_product, K_REGION.m_region });
            parents.push_back(K_CHANNEL);
            parents.push_back(K_REGION.m_product);
            channels[K_CHANNELS[index]] = { { "product", region_ref(K_CHANNEL) },
                                           { "provenance", region_ref(K_REGION.m_product) },
                                           { "color_space",
                                            K_ROLE == MaterialChannelRole::BASE_COLOR ||
                                                    K_ROLE == MaterialChannelRole::EMISSION
                                                ? "LINEAR_SRGB"
                                                : "LINEAR_DATA" } };
        }
        Json material {
            { "schema_version", 1 },
            { "kind", "MATERIAL_PRODUCT" },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "recipe_sha256", hash(K_RECIPE) },
            { "geometry", region_ref(geometry.selection().m_product) },
            { "capture", region_ref(geometry.capture_product()) },
            { "variant_id", 101 },
            { "lighting", "GENERATED_PBR" },
            { "brdf", "METALLIC_ROUGHNESS_GGX" },
            { "grid", { { "width", 2 }, { "height", 2 } } },
            { "uv_set", 0 },
            { "uv_origin", "TOP_LEFT" },
            { "sampler", { { "filter", "LINEAR" }, { "wrap", "CLAMP_TO_EDGE" }, { "mip", "NONE" } } },
            { "normal", { { "basis", "GEOMETRY_TANGENT_W_SIGN" }, { "y", "POSITIVE_BITANGENT" } } },
            { "relighting_scope", "UNQUALIFIED" },
            { "channels", channels },
            { "fit_inputs", Json::array() },
            { "manual_ancestor", false } };
        m_material.m_product = K_NODE(17100, material.dump(), parents);
        m_provenance.push_back(K_SIDECAR(17101,
                                         m_material.m_product,
                                         { AssetRegionKind::MATERIAL_TEXELS, 4, 2, 2 },
                                         K_MATERIAL_SPANS));
        auto spans = Json::array();
        const std::array K_CLASSES {
            "UNKNOWN", "OBSERVED", "INFERRED", "GENERATED", "MANUALLY_EDITED" };
        for (std::size_t index { 0uz }; index < geometry.support().size(); ++index) {
            const auto& support = geometry.support()[index];
            auto views = Json::array();
            for (const auto& view : support.m_views) {
                views.push_back(source_view_id(view));
            }
            spans.push_back({ { "first", index },
                             { "count", 1 },
                             { "class", K_CLASSES.at(static_cast<std::size_t>(support.m_class)) },
                             { "confidence", nullptr },
                             { "parents", Json::array({ region_ref(geometry.selection().m_source) }) },
                             { "support", views },
                             { "manual_ancestor", false } });
        }
        m_provenance.push_back(
            K_SIDECAR(17102,
                      geometry.selection().m_product,
                      { AssetRegionKind::GEOMETRY_FACES, geometry.support().size(), 0, 0 },
                      spans));
        const auto K_RIGHTS = K_NODE(17103, "synthetic completed asset rights; unreviewed", {});
        const bool K_SHELL = m_base.m_selected.m_subject == CompletionSubject::SHELL_GEOMETRY;
        m_definition = {
            1,
            { 42 },
            1,
            region.m_graph.m_context,
            K_SHELL ? AssetKind::STRUCTURAL : AssetKind::PROP,
            { geometry.capture_product(),
             geometry.selection().m_source,
             m_base.m_selected.m_product },
            geometry.selection().m_product,
            { { { 101 }, m_material.m_product, AssetLightingMode::GENERATED_PBR } },
            {},
            {},
            {},
            { { 0, 0, 0 }, { 4, 4, 0 } },
            { K_SHELL ? AssetPivotPolicy::STRUCTURAL_FRAME : AssetPivotPolicy::ATTACHMENT_ORIGIN,
             {},
             true,
             {} },
            {},
            K_RIGHTS,
            false };
        for (const auto& pin : m_provenance) {
            m_definition.m_provenance.push_back(pin.m_product);
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        return m_base.m_base.m_base.m_views.freeze(false);
    }

    NativeAssetsRequest request() const {
        const auto K_CONTEXT = m_receipt.graph().m_context;
        const auto K_CAPTURE = checked(m_base.m_base.m_base.m_views.inspect(m_receipt, false));
        auto source_request = m_base.m_base.m_base.request();
        source_request.m_capture = K_CAPTURE;
        source_request.m_receipt = m_receipt;
        const auto K_SOURCE = checked(inspect_geometry_product(source_request));
        const auto K_STRUCTURE = checked(inspect_structure_graph(
            { m_base.m_base.m_selected, K_SOURCE, K_CAPTURE, m_receipt, K_CONTEXT }));
        std::vector<AdmittedGeometryProduct> candidates;
        for (const auto& pin : m_base.m_geometry_pins) {
            source_request.m_selection = pin;
            candidates.push_back(checked(inspect_geometry_product(source_request)));
        }
        const auto K_COMPLETION = checked(inspect_completion_product({ m_base.m_selected,
                                                                      K_SOURCE,
                                                                      K_STRUCTURE,
                                                                      K_CAPTURE,
                                                                      m_receipt,
                                                                      candidates,
                                                                      K_CONTEXT }));
        const auto K_GEOMETRY = K_COMPLETION.selected_geometry();
        const auto K_MATERIAL = checked(
            inspect_material_product({ m_material, K_GEOMETRY, K_CAPTURE, m_receipt, K_CONTEXT }));
        const auto K_DEFINITION =
            checked(admit_asset_definition(m_definition, m_receipt, m_provenance, K_CONTEXT));
        AssetInstanceSet instances { 1, { 90 }, { 1 }, K_CONTEXT, {}, {}, {}, false };
        instances.m_instances = { { { 1 },
                                  K_DEFINITION.reference(),
                                  K_GEOMETRY.world_from_geometry(),
                                  { 101 },
                                  true,
                                  {},
                                  {},
                                  {},
                                  false } };
        return { m_receipt,
                { { m_definition, m_provenance, K_GEOMETRY, { K_MATERIAL }, K_COMPLETION, { 1 } } },
                instances,
                K_CONTEXT };
    }
};

}  // namespace world_explorer::native_assets_test
