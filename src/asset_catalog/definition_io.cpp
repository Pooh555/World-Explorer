#include "world_explorer/asset_catalog/definition_io.hpp"

#include "definition_json.hpp"
#include "world_explorer/core/hash.hpp"

namespace world_explorer {

using namespace asset_metadata_detail;

Result<AdmittedAssetDefinition>
decode_asset_definition(std::span<const std::byte> bytes,
                        const AssetDefinitionRef& selected,
                        const AdmittedAssetReceipt& receipt,
                        const std::vector<AssetProvenanceSelection>& provenance,
                        const AssetDependencyContext& expected,
                        const std::stop_token& stop) {
    try {
        digest_text(selected.m_sha256);
        const auto K_DOC = parse(bytes, 64uz * 1024uz, stop);
        require(sha256(bytes) == selected.m_sha256,
                "Definition hash differs from selected bytes",
                ErrorCode::RESYNC_REQUIRED);
        keys(K_DOC,
             { "schema_version",
              "asset_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "receipt_sha256",
              "kind",
              "sources",
              "geometry",
              "materials",
              "lods",
              "collider",
              "units",
              "bounds",
              "pivot",
              "provenance",
              "rights",
              "manual_ancestor" });
        const auto K_SCHEMA = integer(K_DOC.at("schema_version"));
        require(K_SCHEMA == 1 || K_SCHEMA == 2,
                "Unsupported definition schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(digest(K_DOC.at("receipt_sha256")) == receipt.receipt_sha256(),
                "Definition input receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        AssetDefinition value;
        value.m_schema_version = static_cast<std::uint32_t>(K_SCHEMA);
        value.m_asset = { integer(K_DOC.at("asset_id")) };
        value.m_revision = integer(K_DOC.at("revision"));
        require(value.m_asset == selected.m_asset && value.m_revision == selected.m_revision,
                "Definition identity differs",
                ErrorCode::RESYNC_REQUIRED);
        value.m_context = { { integer(K_DOC.at("world_epoch")) },
                           { integer(K_DOC.at("pose_revision")) } };
        value.m_kind = named<AssetKind>(
            K_DOC.at("kind"), { { "PROP", AssetKind::PROP }, { "STRUCTURAL", AssetKind::STRUCTURAL } });
        const auto K_REFS = [&](const Json& rows, std::size_t maximum) {
            array(rows, maximum);
            std::vector<AssetRevisionRef> result;
            for (const auto& row : rows) {
                check_stop(stop);
                result.push_back(reference(row));
            }
            return result;
        };
        value.m_sources = K_REFS(K_DOC.at("sources"), 64uz);
        value.m_geometry = reference(K_DOC.at("geometry"));
        array(K_DOC.at("materials"), 16uz);
        for (const auto& row : K_DOC.at("materials")) {
            if (K_SCHEMA == 1) {
                keys(row, { "variant_id", "product", "lighting" });
            } else {
                keys(row, { "variant_id", "product", "lighting", "channels" });
            }
            value.m_materials.push_back(
                { { integer(row.at("variant_id")) },
                 reference(row.at("product")),
                 named<AssetLightingMode>(
                     row.at("lighting"),
                     { { "CAPTURED_APPEARANCE", AssetLightingMode::CAPTURED_APPEARANCE },
                      { "ESTIMATED_PBR", AssetLightingMode::ESTIMATED_PBR },
                      { "GENERATED_PBR", AssetLightingMode::GENERATED_PBR } }) });
            if (K_SCHEMA == 2) {
                array(row.at("channels"), 6uz);
                for (const auto& channel : row.at("channels")) {
                    keys(channel, { "role", "product", "provenance" });
                    value.m_materials.back().m_channels.push_back(
                        { named<MaterialChannelRole>(
                             channel.at("role"),
                             { { "CAPTURED_RGB", MaterialChannelRole::CAPTURED_RGB },
                              { "BASE_COLOR", MaterialChannelRole::BASE_COLOR },
                              { "METALLIC", MaterialChannelRole::METALLIC },
                              { "ROUGHNESS", MaterialChannelRole::ROUGHNESS },
                              { "NORMAL", MaterialChannelRole::NORMAL },
                              { "OCCLUSION", MaterialChannelRole::OCCLUSION },
                              { "EMISSION", MaterialChannelRole::EMISSION } }),
                         reference(channel.at("product")),
                         reference(channel.at("provenance")) });
                }
            }
        }
        value.m_lods = K_REFS(K_DOC.at("lods"), 8uz);
        const auto& collider = K_DOC.at("collider");
        if (!collider.is_null()) {
            keys(collider, { "product", "mode" });
            value.m_collider = AssetColliderBinding {
                reference(collider.at("product")),
                named<AssetColliderMode>(
                    collider.at("mode"),
                    { { "UNVERIFIED", AssetColliderMode::UNVERIFIED },
                     { "SYNTHETIC_GAMEPLAY", AssetColliderMode::SYNTHETIC_GAMEPLAY } }) };
        }
        value.m_units = units(K_DOC.at("units"));
        const auto& bounds = K_DOC.at("bounds");
        keys(bounds, { "minimum", "maximum" });
        value.m_bounds = { vector3(bounds.at("minimum")), vector3(bounds.at("maximum")) };
        const auto& pivot = K_DOC.at("pivot");
        keys(pivot, { "policy", "asset_from_geometry", "orientation_ambiguous", "override_action" });
        value.m_pivot = {
            named<AssetPivotPolicy>(pivot.at("policy"),
                                    { { "SUPPORT_BASE_CENTER", AssetPivotPolicy::SUPPORT_BASE_CENTER },
                                     { "ATTACHMENT_ORIGIN", AssetPivotPolicy::ATTACHMENT_ORIGIN },
                                     { "STRUCTURAL_FRAME", AssetPivotPolicy::STRUCTURAL_FRAME },
                                     { "OVERRIDE", AssetPivotPolicy::OVERRIDE } }),
            transform(pivot.at("asset_from_geometry")),
            pivot.at("orientation_ambiguous").get<bool>(),
            optional_reference(pivot.at("override_action")) };
        value.m_provenance = K_REFS(K_DOC.at("provenance"), K_SCHEMA == 1 ? 26uz : 106uz);
        value.m_rights = reference(K_DOC.at("rights"));
        value.m_manual_ancestor = K_DOC.at("manual_ancestor").get<bool>();
        auto admitted = admit_asset_definition(value, receipt, provenance, expected, stop);
        if (!admitted) {
            return std::unexpected(admitted.error());
        }
        require(same_bytes(bytes, admitted->canonical_json()),
                "Definition bytes are not canonical");
        check_stop(stop);
        return admitted;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Definition decode allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_asset_definition(const AdmittedAssetDefinition& definition,
                                              const AdmittedAssetReceipt& receipt,
                                              const std::filesystem::path& directory,
                                              OperationId operation,
                                              std::uint64_t base_generation,
                                              const AssetDependencyContext& expected,
                                              const std::stop_token& stop,
                                              const std::function<void(std::size_t)>& observed) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        return std::unexpected(K_GRAPH.error());
    }
    const auto& context = definition.definition().m_context;
    if (definition.receipt_sha256() != receipt.receipt_sha256() ||
        context.m_world_epoch != expected.m_world_epoch ||
        context.m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Definition source/context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { definition_key(definition.reference()), encoded(definition.canonical_json()) } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Definition publication allocation failed");
    }
}

}  // namespace world_explorer
