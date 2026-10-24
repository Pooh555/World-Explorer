#include "world_explorer/scene_store/asset_instances_io.hpp"

#include "../asset_catalog/definition_json.hpp"
#include "world_explorer/core/hash.hpp"

namespace world_explorer {

using namespace asset_metadata_detail;

Result<AdmittedAssetInstances>
decode_asset_instances(std::span<const std::byte> bytes,
                       SceneId scene,
                       SceneRevisionId revision,
                       const std::string& selected_sha256,
                       const std::vector<AdmittedAssetDefinition>& definitions,
                       const AdmittedAssetReceipt& receipt,
                       const AssetDependencyContext& expected,
                       const std::stop_token& stop) {
    try {
        digest_text(selected_sha256);
        require(definitions.size() <= 64uz,
                "Selected definition budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_DOC = parse(bytes, 4uz * 1024uz * 1024uz, stop);
        require(sha256(bytes) == selected_sha256,
                "Instance hash differs from selection",
                ErrorCode::RESYNC_REQUIRED);
        keys(K_DOC,
             { "schema_version",
              "scene_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "receipt_sha256",
              "definitions",
              "instances",
              "units",
              "manual_action",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1,
                "Unsupported instance schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(digest(K_DOC.at("receipt_sha256")) == receipt.receipt_sha256(),
                "Instance input receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        AssetInstanceSet value;
        value.m_scene = { integer(K_DOC.at("scene_id")) };
        value.m_revision = { integer(K_DOC.at("revision")) };
        require(value.m_scene == scene && value.m_revision == revision,
                "Instance identity differs",
                ErrorCode::RESYNC_REQUIRED);
        value.m_context = { { integer(K_DOC.at("world_epoch")) },
                           { integer(K_DOC.at("pose_revision")) } };
        value.m_units = units(K_DOC.at("units"));
        value.m_manual_action = optional_reference(K_DOC.at("manual_action"));
        value.m_manual_ancestor = K_DOC.at("manual_ancestor").get<bool>();
        const auto& refs = K_DOC.at("definitions");
        array(refs, 64uz);
        require(refs.size() == definitions.size(),
                "Selected definitions differ",
                ErrorCode::RESYNC_REQUIRED);
        std::map<AssetId, AssetDefinitionRef> selected;
        for (const auto& definition : definitions) {
            require(selected.emplace(definition.reference().m_asset, definition.reference()).second,
                    "Duplicate selected definition");
        }
        for (const auto& row : refs) {
            const auto K_REF = definition_ref(row);
            const auto K_FOUND = selected.find(K_REF.m_asset);
            require(K_FOUND != selected.end() && K_FOUND->second == K_REF,
                    "Foreign definition selection",
                    ErrorCode::RESYNC_REQUIRED);
            selected.erase(K_REF.m_asset);
        }
        const auto& rows = K_DOC.at("instances");
        array(rows, 4096uz);
        std::size_t lineage_count {};
        for (const auto& row : rows) {
            check_stop(stop);
            keys(row,
                 { "instance_id",
                  "definition",
                  "world_from_asset",
                  "material_variant_id",
                  "visible",
                  "room",
                  "support",
                  "lineage",
                  "manual_ancestor" });
            AssetInstance item;
            item.m_instance = { integer(row.at("instance_id")) };
            item.m_definition = definition_ref(row.at("definition"));
            item.m_world_from_asset = transform(row.at("world_from_asset"));
            item.m_material_variant = { integer(row.at("material_variant_id")) };
            item.m_visible = row.at("visible").get<bool>();
            item.m_room = optional_reference(row.at("room"));
            item.m_support = optional_reference(row.at("support"));
            item.m_manual_ancestor = row.at("manual_ancestor").get<bool>();
            const auto& lineage = row.at("lineage");
            array(lineage, 16uz);
            require(lineage.size() <= 16384uz - lineage_count,
                    "Lineage budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            lineage_count += lineage.size();
            for (const auto& parent : lineage) {
                keys(parent, { "instance_id", "product" });
                item.m_lineage.push_back(
                    { { integer(parent.at("instance_id")) }, reference(parent.at("product")) });
            }
            value.m_instances.push_back(std::move(item));
        }
        auto admitted = admit_asset_instances(value, definitions, receipt, expected, stop);
        if (!admitted) {
            return std::unexpected(admitted.error());
        }
        require(same_bytes(bytes, admitted->canonical_json()), "Instance bytes are not canonical");
        check_stop(stop);
        return admitted;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Instance decode allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_asset_instances(const AdmittedAssetInstances& instances,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop,
                                             const std::function<void(std::size_t)>& observed) {
    try {
        const auto& value = instances.instances();
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        if (value.m_context.m_world_epoch != expected.m_world_epoch ||
            value.m_context.m_pose_revision != expected.m_pose_revision) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Instance context differs");
        }
        // Re-admission also binds an empty instance set to the selected receipt.
        const auto K_CHECKED =
            admit_asset_instances(value, instances.definitions(), receipt, expected, stop);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        if (K_CHECKED->canonical_json() != instances.canonical_json()) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Instance source differs");
        }
        std::vector<ImmutableRecord> records;
        for (const auto& definition : instances.definitions()) {
            records.push_back(
                { definition_key(definition.reference()), encoded(definition.canonical_json()) });
        }
        records.push_back({ "scene-" + std::to_string(value.m_scene.m_value) + "-" +
                               std::to_string(value.m_revision.m_value),
                           encoded(instances.canonical_json()) });
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         records,
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Instance publication allocation failed");
    }
}

}  // namespace world_explorer
