#include "world_explorer/application/asset_definition_edit.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <limits>
#include <span>

namespace world_explorer {
namespace {

using namespace json_record_detail;

template <class Value> Value take(Result<Value> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

std::string metadata_key(const AssetDefinitionRef& ref) {
    return "metadata-asset-" + std::to_string(ref.m_asset.m_value) + "-" +
           std::to_string(ref.m_revision);
}

struct Validated {
    AdmittedAssetDefinition m_before, m_after;
    StaticAssetPayloads m_payloads;
    ReplaceDraftMetadataCommand m_draft;
};

Validated validate(const EditAssetDefinitionCommand& command,
                   const AdmittedAssetReceipt& before,
                   const AdmittedAssetReceipt& after,
                   const AssetDependencyContext& expected,
                   const std::stop_token& stop) {
    check_stop(stop);
    const auto& mutation = command.m_draft.m_mutation;
    require(!mutation.m_admission_sha256.has_value() && mutation.m_metadata.empty(),
            "Asset edit binding/metadata must be derived",
            ErrorCode::INVALID_ARGUMENT);
    require(mutation.m_kind == ManualActionKind::EDIT_GEOMETRY ||
                mutation.m_kind == ManualActionKind::EDIT_MATERIAL,
            "Unsupported asset edit kind",
            ErrorCode::UNSUPPORTED_OPERATION);
    auto old = take(decode_asset_definition(command.m_before.m_bytes,
                                            command.m_before.m_definition,
                                            before,
                                            command.m_before.m_provenance,
                                            expected,
                                            stop));
    auto next = take(decode_asset_definition(command.m_after.m_bytes,
                                             command.m_after.m_definition,
                                             after,
                                             command.m_after.m_provenance,
                                             expected,
                                             stop));
    const auto& a = old.definition();
    const auto& b = next.definition();
    require(a.m_asset == b.m_asset && a.m_revision < std::numeric_limits<std::uint64_t>::max() &&
                b.m_revision == a.m_revision + 1 && b.m_manual_ancestor,
            "Asset edit identity/revision/ancestry differs",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_OLD_PAYLOADS = take(
        inspect_static_asset_payloads(old, before, command.m_before.m_provenance, expected, stop));
    auto new_payloads = take(
        inspect_static_asset_payloads(next, after, command.m_after.m_provenance, expected, stop));
    auto old_doc = parse(command.m_before.m_bytes, 64uz * 1024uz, stop);
    auto new_doc = parse(command.m_after.m_bytes, 64uz * 1024uz, stop);
    for (const auto* name : { "receipt_sha256",
                             "revision",
                             "manual_ancestor",
                             "geometry",
                             "materials",
                             "provenance",
                             "bounds" }) {
        old_doc.erase(name);
        new_doc.erase(name);
    }
    require(old_doc == new_doc && a.m_geometry.m_product == b.m_geometry.m_product &&
                a.m_materials.front().m_product.m_product ==
                    b.m_materials.front().m_product.m_product &&
                a.m_materials.front().m_variant == b.m_materials.front().m_variant,
            "Asset edit changes stable definition fields",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_GEOMETRY_EDIT = mutation.m_kind == ManualActionKind::EDIT_GEOMETRY;
    const auto K_TARGET = K_GEOMETRY_EDIT ? a.m_geometry : a.m_materials.front().m_product;
    require(mutation.m_targets == std::vector { K_TARGET },
            "Asset edit targets differ",
            ErrorCode::INVALID_ARGUMENT);
    require(K_GEOMETRY_EDIT ? (K_OLD_PAYLOADS.m_mesh != new_payloads.m_mesh &&
                               K_OLD_PAYLOADS.m_material == new_payloads.m_material)
                            : (K_OLD_PAYLOADS.m_mesh == new_payloads.m_mesh &&
                               K_OLD_PAYLOADS.m_material != new_payloads.m_material &&
                               a.m_geometry == b.m_geometry),
            "Asset edit changes wrong payload or has no semantic change");
    auto old_ids = a.m_provenance;
    auto new_ids = b.m_provenance;
    std::ranges::sort(old_ids);
    std::ranges::sort(new_ids);
    require(old_ids.size() == new_ids.size(), "Asset provenance membership changed");
    for (std::size_t index { 0uz }; index < old_ids.size(); ++index) {
        require(old_ids[index].m_product == new_ids[index].m_product,
                "Asset provenance role changed",
                ErrorCode::RESYNC_REQUIRED);
    }
    const auto K_NEW_TARGET = K_GEOMETRY_EDIT ? b.m_geometry : b.m_materials.front().m_product;
    for (const auto& selected : command.m_after.m_provenance) {
        if (selected.m_region.m_target == K_NEW_TARGET) {
            const auto K_REGION = take(inspect_asset_region_provenance(
                after, selected.m_product.m_product, selected.m_region, expected, stop));
            require(K_REGION.m_coverage[4uz] == selected.m_region.m_domain.m_elements,
                    "Edited payload must label all elements manually edited");
        }
    }
    auto bound = command.m_draft;
    const auto K_REFERENCE = [](const AssetDefinitionRef& ref) -> Json {
        return { { "asset_id", ref.m_asset.m_value },
                { "revision", ref.m_revision },
                { "sha256", ref.m_sha256 } };
    };
    const Json K_BINDING { { "schema_version", 1 },
                          { "adapter", "GENERATED_STATIC_ASSET_EDIT" },
                          { "before", K_REFERENCE(old.reference()) },
                          { "after", K_REFERENCE(next.reference()) } };
    const auto K_TEXT = K_BINDING.dump();
    bound.m_mutation.m_admission_sha256 = sha256(std::as_bytes(std::span(K_TEXT)));
    bound.m_mutation.m_metadata = { { metadata_key(old.reference()), command.m_before.m_bytes },
                                   { metadata_key(next.reference()), command.m_after.m_bytes } };
    return { old, next, std::move(new_payloads), std::move(bound) };
}

void selected_head(const Validated& admitted, const std::stop_token& stop) {
    const auto& directory = admitted.m_draft.m_directory;
    if (!std::filesystem::exists(directory / "owner.json")) {
        return;
    }
    const auto K_STORE = take(read_immutable_records(directory, stop));
    if (K_STORE.m_records.contains("mutation-" +
                                   std::to_string(admitted.m_draft.m_mutation.m_action.m_value))) {
        return;  // Exact retry/collision is checked by the generic publisher.
    }
    const auto& before = admitted.m_before;
    const auto K_PREFIX =
        "metadata-asset-" + std::to_string(before.reference().m_asset.m_value) + "-";
    std::uint64_t latest {};
    std::string latest_sha;
    for (const auto& [K_KEY, K_BYTES] : K_STORE.m_records) {
        if (!K_KEY.starts_with(K_PREFIX)) {
            continue;
        }
        const auto K_DOC = parse(K_BYTES, 64uz * 1024uz, stop);
        const AssetDefinitionRef K_REF {
            { integer(K_DOC.at("asset_id")) }, integer(K_DOC.at("revision")), sha256(K_BYTES) };
        require(K_KEY == metadata_key(K_REF) && K_REF.m_asset == before.reference().m_asset &&
                    K_REF.m_revision,
                "Foreign asset metadata history");
        if (K_REF.m_revision > latest) {
            latest = K_REF.m_revision;
            latest_sha = K_REF.m_sha256;
        }
    }
    require(!latest || (latest == before.reference().m_revision &&
                        latest_sha == before.reference().m_sha256),
            "Asset definition branches from a stale committed head",
            ErrorCode::RESYNC_REQUIRED);
}

}  // namespace

struct AdmittedAssetEdit::Implementation {
    AdmittedAssetDefinition m_definition;
    StaticAssetPayloads m_payloads;
    RecordCommit m_commit;
    std::string m_result_sha256;
};

AdmittedAssetEdit::AdmittedAssetEdit(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const AdmittedAssetDefinition& AdmittedAssetEdit::definition() const {
    return m_state->m_definition;
}

const StaticAssetPayloads& AdmittedAssetEdit::payloads() const {
    return m_state->m_payloads;
}

const RecordCommit& AdmittedAssetEdit::commit() const {
    return m_state->m_commit;
}

const std::string& AdmittedAssetEdit::result_sha256() const {
    return m_state->m_result_sha256;
}

Result<DraftAttemptSnapshot>
attempt_asset_definition_edit(const EditAssetDefinitionCommand& command,
                              const DraftMetadataAttempt& attempt,
                              const AdmittedAssetReceipt& before,
                              const AdmittedAssetReceipt& after,
                              const AdmittedManualActionJournal& journal,
                              const AssetDependencyContext& expected,
                              const std::stop_token& stop,
                              const DraftMutationObserver& mutation_observed,
                              const DraftAttemptObserver& observed) {
    try {
        const auto K_ADMITTED = validate(command, before, after, expected, stop);
        selected_head(K_ADMITTED, stop);
        return attempt_draft_metadata(K_ADMITTED.m_draft,
                                      attempt,
                                      before,
                                      after,
                                      journal,
                                      expected,
                                      stop,
                                      mutation_observed,
                                      observed);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset edit allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<AdmittedAssetEdit> read_asset_definition_edit(const EditAssetDefinitionCommand& command,
                                                     const AdmittedAssetReceipt& before,
                                                     const AdmittedAssetReceipt& after,
                                                     const AdmittedManualActionJournal& journal,
                                                     const RecordCommit& selected,
                                                     const AssetDependencyContext& expected,
                                                     const std::stop_token& stop) {
    try {
        auto admitted = validate(command, before, after, expected, stop);
        const auto K_DRAFT = take(read_draft_metadata_mutation(admitted.m_draft.m_mutation,
                                                               before,
                                                               after,
                                                               journal,
                                                               command.m_draft.m_directory,
                                                               selected,
                                                               expected,
                                                               stop));
        require(K_DRAFT.m_phase == DraftMutationPhase::COMMITTED,
                "Asset edit has no committed result",
                ErrorCode::WOULD_BLOCK);
        require(K_DRAFT.m_metadata_bytes.at(metadata_key(admitted.m_after.reference())) ==
                    command.m_after.m_bytes,
                "Committed asset definition differs");
        check_stop(stop);
        return AdmittedAssetEdit(std::make_shared<const AdmittedAssetEdit::Implementation>(
            AdmittedAssetEdit::Implementation { admitted.m_after,
                                               std::move(admitted.m_payloads),
                                               K_DRAFT.m_commit,
                                               K_DRAFT.m_result_sha256 }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset edit read allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
