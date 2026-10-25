#pragma once

#include "asset_schema_fixture.hpp"
#include "world_explorer/application/asset_definition_edit.hpp"

#include <span>
#include <stdexcept>

namespace world_explorer::asset_edit_test {

using namespace asset_schema_test;

struct AssetEditFixture {
    SchemaFixture m_before, m_after;
    RecipeFixture m_workspace;
    Json m_geometry, m_material, m_next_geometry, m_next_material;
    EditAssetDefinitionCommand m_command;
    DraftMetadataAttempt m_attempt { { 1 }, 0, m_workspace.m_root / "attempts" };

  private:
    std::optional<AdmittedAssetReceipt> m_input, m_output;
    std::optional<AdmittedManualActionJournal> m_journal;

  public:
    const AdmittedAssetReceipt& input() const {
        if (!m_input) {
            throw std::logic_error("Missing asset edit input");
        }
        return *m_input;
    }

    const AdmittedAssetReceipt& output() const {
        if (!m_output) {
            throw std::logic_error("Missing asset edit output");
        }
        return *m_output;
    }

    const AdmittedManualActionJournal& journal() const {
        if (!m_journal) {
            throw std::logic_error("Missing asset edit journal");
        }
        return *m_journal;
    }

    explicit AssetEditFixture(ManualActionKind kind = ManualActionKind::EDIT_GEOMETRY) {
        m_geometry = { { "schema_version", 1 },
                      { "kind", "GENERATED_STATIC_MESH" },
                      { "product_id", 1000 },
                      { "revision", 1 },
                      { "world_epoch", 3 },
                      { "pose_revision", 5 },
                      { "frame", "GEOMETRY_LOCAL_RIGHT_HANDED_Y_UP" },
                      { "surface", "OPEN_SURFACE_ALLOWED" },
                      { "variant_id", 7 },
                      { "positions", { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } } },
                      { "normals", { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } } },
                      { "uv", { { 0, 0 }, { 1, 0 }, { 0, 1 } } },
                      { "triangles", { { 0, 1, 2 } } } };
        m_material = { { "schema_version", 1 },
                      { "kind", "GENERATED_CONSTANT_PBR" },
                      { "product_id", 2000 },
                      { "revision", 1 },
                      { "world_epoch", 3 },
                      { "pose_revision", 5 },
                      { "geometry", nullptr },
                      { "brdf", "METALLIC_ROUGHNESS_GGX" },
                      { "color_space", "LINEAR_SRGB" },
                      { "normal", { 0, 0, 1 } },
                      { "base_color", { 0.25, 0.5, 0.75 } },
                      { "metallic", 0.1 },
                      { "roughness", 0.5 },
                      { "emission", { 0, 0, 0 } },
                      { "occlusion", 1 } };
        m_before.m_region.m_selection.m_sources.clear();
        m_before.m_region.m_record["sources"] = Json::array();
        m_before.m_material_record["sources"] = Json::array();
        m_input = stage(m_before, m_geometry, m_material, m_command.m_before);
        m_command.m_draft = { { { 1 }, kind, { 1 }, 0, {} }, m_workspace.m_root / "draft" };
        configure(kind, 1);
    }

    void configure(ManualActionKind kind, std::uint64_t action) {
        const auto K_GEOMETRY = kind == ManualActionKind::EDIT_GEOMETRY;
        const auto K_INDEX = K_GEOMETRY ? 2uz : 4uz;
        m_command.m_draft.m_mutation.m_targets = { input().graph().m_products[K_INDEX].m_product };
        const ManualActionRequest K_REQUEST { { action },
                                             kind,
                                             "synthetic-asset-editor",
                                             1000,
                                             500,
                                             200,
                                             m_command.m_draft.m_mutation.m_targets,
                                             "synthetic generated asset edit" };
        const auto K_COMMIT = append_manual_action(K_REQUEST,
                                                   input(),
                                                   m_workspace.m_root / "journal",
                                                   { action },
                                                   action - 1,
                                                   input().graph().m_context);
        REQUIRE(K_COMMIT);
        const auto K_JOURNAL =
            read_manual_action_journal(m_workspace.m_root / "journal", *K_COMMIT);
        REQUIRE(K_JOURNAL);
        m_journal = *K_JOURNAL;
        m_after.m_region.m_graph = input().graph();
        m_after.m_region.m_data = m_before.m_region.m_data;
        m_after.m_region.m_record = m_before.m_region.m_record;
        m_after.m_region.m_selection = m_before.m_region.m_selection;
        m_after.m_material_record = m_before.m_material_record;
        m_after.m_definition = m_before.m_definition;
        ++m_after.m_definition.m_revision;
        m_after.m_definition.m_manual_ancestor = true;
        const auto K_STORE = read_immutable_records(journal().source_directory());
        REQUIRE(K_STORE);
        const auto& bytes = K_STORE->m_records.at("manual-" + std::to_string(action));
        const std::string K_TEXT(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        m_after.m_region.m_data[hash(K_TEXT)] = K_TEXT;
        m_after.m_region.m_graph.m_products[K_INDEX].m_manual_action_sha256 = hash(K_TEXT);
        for (auto index : { 2uz, 3uz, 4uz, 5uz }) {
            if (!K_GEOMETRY && index < 4uz) {
                continue;
            }
            auto& node = m_after.m_region.m_graph.m_products[index];
            ++node.m_product.m_revision;
            node.m_manual_ancestor = true;
        }
        m_next_geometry = m_geometry;
        m_next_material = m_material;
        if (K_GEOMETRY) {
            m_next_geometry["positions"][1uz][0uz] =
                m_geometry["positions"][1uz][0uz].get<double>() + 1;
        } else {
            m_next_material["roughness"] = 0.75;
        }
        save_after();
    }

    AdmittedAssetReceipt stage(SchemaFixture& side,
                               Json& geometry,
                               Json& material,
                               AssetDefinitionEditSelection& selected) const {
        auto& region = side.m_region;
        auto& nodes = region.m_graph.m_products;
        geometry["revision"] = nodes[2uz].m_product.m_revision;
        material["revision"] = nodes[4uz].m_product.m_revision;
        const auto K_GEOMETRY = geometry.dump();
        region.m_data[hash(K_GEOMETRY)] = K_GEOMETRY;
        nodes[2uz].m_product.m_content_sha256 = hash(K_GEOMETRY);
        side.m_definition.m_geometry = nodes[2uz].m_product;
        material["geometry"] = region_ref(nodes[2uz].m_product);
        const auto K_MATERIAL = material.dump();
        region.m_data[hash(K_MATERIAL)] = K_MATERIAL;
        nodes[4uz].m_product.m_content_sha256 = hash(K_MATERIAL);
        nodes[4uz].m_dependencies = { nodes[2uz].m_product };
        nodes[3uz].m_dependencies = { nodes[2uz].m_product };
        nodes[5uz].m_dependencies = { nodes[4uz].m_product };
        side.m_definition.m_materials = {
            { { 7 }, nodes[4uz].m_product, AssetLightingMode::GENERATED_PBR } };
        region.m_selection = { nodes[2uz].m_product,
                              { AssetRegionKind::GEOMETRY_FACES, geometry["triangles"].size(), 0, 0 },
                              {} };
        region.m_record["target"] = region_ref(nodes[2uz].m_product);
        region.m_record["domain"] = { { "kind", "GEOMETRY_FACES" },
                                     { "elements", geometry["triangles"].size() },
                                     { "width", 0 },
                                     { "height", 0 } };
        region.m_record["manual_ancestor"] = nodes[2uz].m_manual_ancestor;
        region.m_record["sources"] = Json::array();
        const auto K_SPAN =
            [](const AssetDependencyNode& node, std::uint64_t count, Json parents) -> Json {
            return { { "first", 0 },
                    { "count", count },
                    { "class",
                     node.m_manual_action_sha256.has_value() ? "MANUALLY_EDITED" : "GENERATED" },
                    { "confidence", 1.0 },
                    { "parents", parents },
                    { "support", Json::array() },
                    { "manual_ancestor", node.m_manual_ancestor } };
        };
        region.m_record["spans"] =
            Json::array({ K_SPAN(nodes[2uz], geometry["triangles"].size(), Json::array()) });
        side.m_material_record["target"] = region_ref(nodes[4uz].m_product);
        side.m_material_record["domain"] = {
            { "kind", "MATERIAL_TEXELS" }, { "elements", 1 }, { "width", 1 }, { "height", 1 } };
        side.m_material_record["sources"] = Json::array();
        side.m_material_record["manual_ancestor"] = nodes[4uz].m_manual_ancestor;
        side.m_material_record["spans"] =
            Json::array({ K_SPAN(nodes[4uz], 1, Json::array({ region_ref(nodes[2uz].m_product) })) });
        Bounds3d bounds { { 0, 0, 0 }, { geometry["positions"][1uz][0uz].get<double>(), 1, 0 } };
        side.m_definition.m_bounds = bounds;
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        const auto K_MATERIAL_RECORD = side.m_material_record.dump();
        region.m_data[hash(K_MATERIAL_RECORD)] = K_MATERIAL_RECORD;
        nodes[5uz].m_product.m_content_sha256 = hash(K_MATERIAL_RECORD);
        const auto K_RECEIPT = region.freeze();
        side.m_definition.m_provenance = { nodes[3uz].m_product, nodes[5uz].m_product };
        side.m_provenance = {
            { nodes[3uz].m_product, region.m_selection },
            { nodes[5uz].m_product,
             { nodes[4uz].m_product, { AssetRegionKind::MATERIAL_TEXELS, 1, 1, 1 }, {} } } };
        const auto K_ADMITTED = side.admit(K_RECEIPT);
        INFO((K_ADMITTED ? "metadata admitted" : K_ADMITTED.error().m_message));
        REQUIRE(K_ADMITTED);
        const auto K_VIEW = std::as_bytes(std::span(K_ADMITTED->canonical_json()));
        selected = { K_ADMITTED->reference(), { K_VIEW.begin(), K_VIEW.end() }, side.m_provenance };
        return K_RECEIPT;
    }

    void save_after() {
        m_output = stage(m_after, m_next_geometry, m_next_material, m_command.m_after);
    }

    void advance() {
        m_before.m_region.m_graph = output().graph();
        m_before.m_region.m_data = m_after.m_region.m_data;
        m_before.m_region.m_record = m_after.m_region.m_record;
        m_before.m_material_record = m_after.m_material_record;
        m_before.m_definition = m_after.m_definition;
        m_geometry = m_next_geometry;
        m_material = m_next_material;
        m_input = stage(m_before, m_geometry, m_material, m_command.m_before);
        m_command.m_draft.m_mutation.m_action = { 2 };
        m_command.m_draft.m_mutation.m_operation = { 2 };
        m_command.m_draft.m_mutation.m_base_generation = 2;
        m_attempt = { { 2 }, 2, m_attempt.m_directory };
        configure(ManualActionKind::EDIT_GEOMETRY, 2);
    }

    Result<DraftAttemptSnapshot> run(const std::stop_token& stop = {},
                                     const DraftMutationObserver& observed = {}) const {
        return attempt_asset_definition_edit(m_command,
                                             m_attempt,
                                             input(),
                                             output(),
                                             journal(),
                                             input().graph().m_context,
                                             stop,
                                             observed);
    }

    Result<AdmittedAssetEdit> read(const RecordCommit& pin) const {
        return read_asset_definition_edit(
            m_command, input(), output(), journal(), pin, input().graph().m_context);
    }
};

}  // namespace world_explorer::asset_edit_test
