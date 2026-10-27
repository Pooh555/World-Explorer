#pragma once

#include "native_assets_fixture.hpp"
#include "structure_graph_fixture.hpp"
#include "world_explorer/application/native_structure.hpp"

namespace world_explorer::native_structure_test {

using namespace native_assets_test;
using namespace structure_graph_test;

struct NativeStructureFixtureOptions {
    bool m_relations {}, m_manual {};
};

struct NativeStructureFixture {
    StructureFixture m_base;
    RecipeFixture m_output;
    MaterialProductSelection m_material;
    AssetDefinition m_definition;
    std::vector<AssetProvenanceSelection> m_provenance;
    AdmittedAssetReceipt m_receipt;

    explicit NativeStructureFixture(const NativeStructureFixtureOptions& options = {})
        : m_base({ .m_record =
                      [relations = options.m_relations](Json& doc, StructureGraphSelection&) {
                          if (relations) {
                              split_patch(doc);
                          }
                      },
                  .m_bytes = {},
                  .m_graph = {} },
                 options.m_manual),
          m_receipt(stage()) { }

    AdmittedAssetReceipt stage() {
        auto& region = m_base.m_base.m_views.m_object.m_region;
        const auto& geometry = m_base.m_geometry;
        const auto K_VIEW = geometry.selection().m_views[0uz];
        const AssetSourceViewRef K_SOURCE { { K_VIEW.m_capture, K_VIEW.m_frame },
                                           K_VIEW.m_calibration,
                                           K_VIEW.m_image,
                                           hash("synthetic calibration declaration"),
                                           K_VIEW.m_pose_asset.m_content_sha256,
                                           hash("synthetic source pixel grid") };
        const std::string K_RECIPE { "synthetic structural material import; unqualified" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        const auto K_NODE =
            [&](std::uint64_t id, const std::string& data, std::vector<AssetRevisionRef> parents) {
                auto node = region.node(id, data);
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
        const auto K_SPANS =
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
            const auto K_ID = 18000uz + 2uz * index;
            const auto K_CHANNEL =
                K_NODE(K_ID, channel_bytes(K_ROLE, K_ID, 2, 2), { geometry.selection().m_product });
            const auto K_REGION = K_SIDECAR(
                K_ID + 1uz, K_CHANNEL, { AssetRegionKind::MATERIAL_TEXELS, 4, 2, 2 }, K_SPANS);
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
        const Json K_MATERIAL {
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
        m_material.m_product = K_NODE(18100, K_MATERIAL.dump(), parents);
        m_provenance.push_back(K_SIDECAR(
            18101, m_material.m_product, { AssetRegionKind::MATERIAL_TEXELS, 4, 2, 2 }, K_SPANS));
        auto faces = Json::array();
        for (std::size_t face { 0uz }; face < geometry.mesh().m_triangles.size(); ++face) {
            faces.push_back({ { "first", face },
                             { "count", 1 },
                             { "class", "OBSERVED" },
                             { "confidence", nullptr },
                             { "parents", Json::array({ region_ref(geometry.selection().m_source) }) },
                             { "support", Json::array({ source_view_id(K_SOURCE.m_view) }) },
                             { "manual_ancestor", false } });
        }
        m_provenance.push_back(
            K_SIDECAR(18102,
                      geometry.selection().m_product,
                      { AssetRegionKind::GEOMETRY_FACES, geometry.mesh().m_triangles.size(), 0, 0 },
                      faces));
        const auto K_RIGHTS = K_NODE(18103, "synthetic structure rights; unreviewed", {});
        m_definition = { 1,
                        { 42 },
                        1,
                        region.m_graph.m_context,
                        AssetKind::STRUCTURAL,
                        { geometry.capture_product(), geometry.selection().m_source },
                        geometry.selection().m_product,
                        { { { 101 }, m_material.m_product, AssetLightingMode::GENERATED_PBR } },
                        {},
                        {},
                        {},
                        { { -1, -2, -3 }, { 3, 2, -3 } },
                        { AssetPivotPolicy::STRUCTURAL_FRAME, { {}, { -1, -2, -3 }, 1 }, true, {} },
                        {},
                        K_RIGHTS,
                        false };
        for (const auto& pin : m_provenance) {
            m_definition.m_provenance.push_back(pin.m_product);
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        return m_base.m_base.m_views.freeze(false);
    }

    NativeAssetsRequest assets() const {
        const auto K_CAPTURE = checked(m_base.m_base.m_views.inspect(m_receipt, false));
        auto input = m_base.m_base.request();
        input.m_receipt = m_receipt;
        input.m_capture = K_CAPTURE;
        const auto K_GEOMETRY = checked(inspect_geometry_product(input));
        const auto K_MATERIAL = checked(inspect_material_product(
            { m_material, K_GEOMETRY, K_CAPTURE, m_receipt, m_receipt.graph().m_context }));
        const auto K_DEFINITION = checked(admit_asset_definition(
            m_definition, m_receipt, m_provenance, m_receipt.graph().m_context));
        AssetInstanceSet instances { 1, { 90 }, { 1 }, m_receipt.graph().m_context, {}, {}, {}, false };
        instances.m_instances = {
            { { 1 }, K_DEFINITION.reference(), { {}, { 12, 0, 9 }, 2 }, { 101 }, true, {}, {}, {}, false },
            { { 2 }, K_DEFINITION.reference(), { {}, { 30, 5, 9 }, 2 }, { 101 }, true, {}, {}, {}, false } };
        return { m_receipt,
                { { m_definition, m_provenance, K_GEOMETRY, { K_MATERIAL }, {}, { 1 } } },
                instances,
                m_receipt.graph().m_context };
    }

    NativeStructureRequest request() const {
        const auto K_ASSETS = assets();
        const auto K_CAPTURE = checked(m_base.m_base.m_views.inspect(m_receipt, false));
        const auto& source = K_ASSETS.m_assets[0uz].m_geometry;
        if (!source) {
            throw std::logic_error("Structural fixture geometry missing");
        }
        const auto K_GRAPH = checked(inspect_structure_graph(
            { m_base.m_selected, *source, K_CAPTURE, m_receipt, K_ASSETS.m_expected }));
        NativeStructureSelection binding { K_GRAPH,
                                          K_ASSETS.m_instances.m_instances[0uz].m_definition,
                                          {},
                                          {},
                                          {},
                                          {},
                                          { { 1 }, { 2 } } };
        for (const auto& surface : K_GRAPH.surfaces()) {
            binding.m_surfaces.push_back(surface.m_id);
        }
        for (const auto& opening : K_GRAPH.openings()) {
            binding.m_openings.push_back(opening.m_id);
        }
        for (const auto& room : K_GRAPH.rooms()) {
            binding.m_rooms.push_back(room.m_id);
        }
        for (const auto& relation : K_GRAPH.relations()) {
            binding.m_relations.push_back({ relation.m_kind, relation.m_source, relation.m_target });
        }
        return {
            checked(inspect_native_assets(K_ASSETS)), m_receipt, { binding }, K_ASSETS.m_expected };
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "structured";
    }
};

}  // namespace world_explorer::native_structure_test
