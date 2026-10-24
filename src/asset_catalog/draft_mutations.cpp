#include "world_explorer/asset_catalog/draft_mutations.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::size_t K_PRODUCT_BYTES { 64uz * 1024uz };
constexpr std::size_t K_INTENT_BYTES { 32uz * 1024uz };

template <class Value> Value take(Result<Value> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

std::vector<std::byte> encoded(const Json& value) {
    const auto K_TEXT = value.dump();
    const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
    return { K_BYTES.begin(), K_BYTES.end() };
}

const char* kind_name(ManualActionKind kind) {
    switch (kind) {
    case ManualActionKind::EDIT_MASK:
        return "EDIT_MASK";
    case ManualActionKind::EDIT_GEOMETRY:
        return "EDIT_GEOMETRY";
    case ManualActionKind::EDIT_MATERIAL:
        return "EDIT_MATERIAL";
    default:
        throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported draft replacement kind");
    }
}

Json reference(const AssetRevisionRef& ref) {
    return { { "product_id", ref.m_product.m_value },
            { "revision", ref.m_revision },
            { "content_sha256", ref.m_content_sha256 } };
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    AssetRevisionRef result { { integer(value.at("product_id")) },
                             integer(value.at("revision")),
                             digest(value.at("content_sha256")) };
    require(result.m_product.m_value && result.m_revision, "Invalid draft product reference");
    return result;
}

std::string product_key(const AssetRevisionRef& ref) {
    return "product-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

std::string intent_key(ManualActionId id) {
    return "mutation-" + std::to_string(id.m_value);
}

std::string result_key(OperationId id) {
    return "result-" + std::to_string(id.m_value);
}

std::string action_key(ManualActionId id) {
    return "action-" + std::to_string(id.m_value);
}

struct Intent {
    ManualActionId m_action;
    OperationId m_operation;
    std::uint64_t m_base {};
    std::string m_action_sha;
    std::vector<DraftMetadataChange> m_changes;
    std::vector<std::byte> m_bytes;
};

void validate_identity(ManualActionId action, OperationId operation, std::uint64_t base) {
    require(action.m_value && operation.m_value &&
                operation.m_value <= std::numeric_limits<std::uint64_t>::max() / 2 && base < 1023 &&
                base % 2 == 0,
            "Invalid draft action/operation/base",
            ErrorCode::INVALID_ARGUMENT);
}

Intent parse_intent(const std::vector<std::byte>& bytes, const std::stop_token& stop) {
    const auto K_DOC = parse(bytes, K_INTENT_BYTES, stop);
    keys(K_DOC,
         { "schema_version",
          "action_id",
          "kind",
          "operation_id",
          "base_generation",
          "world_epoch",
          "pose_revision",
          "before_receipt_sha256",
          "after_receipt_sha256",
          "before_graph_sha256",
          "after_graph_sha256",
          "journal_generation",
          "journal_manifest_sha256",
          "action_sha256",
          "targets",
          "changes" });
    require(integer(K_DOC.at("schema_version")) == 1,
            "Unsupported draft intent schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_KIND = K_DOC.at("kind").get<std::string>();
    require(K_KIND == "EDIT_MASK" || K_KIND == "EDIT_GEOMETRY" || K_KIND == "EDIT_MATERIAL",
            "Unsupported draft intent kind",
            ErrorCode::UNSUPPORTED_OPERATION);
    Intent result { { integer(K_DOC.at("action_id")) },
                   { integer(K_DOC.at("operation_id")) },
                   integer(K_DOC.at("base_generation")),
                   digest(K_DOC.at("action_sha256")),
                   {},
                   bytes };
    validate_identity(result.m_action, result.m_operation, result.m_base);
    integer(K_DOC.at("world_epoch"));
    require(integer(K_DOC.at("pose_revision")) && integer(K_DOC.at("journal_generation")),
            "Missing draft context/journal generation");
    for (const auto* key : { "before_receipt_sha256",
                            "after_receipt_sha256",
                            "before_graph_sha256",
                            "after_graph_sha256",
                            "journal_manifest_sha256" }) {
        digest(K_DOC.at(key));
    }
    array(K_DOC.at("targets"), 16uz);
    array(K_DOC.at("changes"), 16uz);
    require(!K_DOC.at("targets").empty() && !K_DOC.at("changes").empty(), "Empty draft mutation");
    std::map<AssetProductId, AssetRevisionRef> changed;
    AssetProductId previous {};
    for (const auto& row : K_DOC.at("changes")) {
        keys(row, { "before", "after" });
        DraftMetadataChange change { reference(row.at("before")), reference(row.at("after")) };
        require(change.m_before.m_product > previous &&
                    change.m_before.m_product == change.m_after.m_product &&
                    change.m_before.m_revision < std::numeric_limits<std::uint64_t>::max() &&
                    change.m_after.m_revision == change.m_before.m_revision + 1 &&
                    change.m_after.m_content_sha256 != change.m_before.m_content_sha256,
                "Invalid draft revision transition");
        previous = change.m_before.m_product;
        changed.emplace(previous, change.m_before);
        result.m_changes.push_back(std::move(change));
    }
    previous = {};
    for (const auto& row : K_DOC.at("targets")) {
        const auto K_REF = reference(row);
        require(K_REF.m_product > previous && changed.contains(K_REF.m_product) &&
                    changed.at(K_REF.m_product) == K_REF,
                "Invalid draft root selection");
        previous = K_REF.m_product;
    }
    require(encoded(K_DOC) == bytes, "Noncanonical draft intent");
    return result;
}

std::vector<std::byte> result_bytes(const Intent& intent) {
    auto outputs = Json::array();
    for (const auto& change : intent.m_changes) {
        outputs.push_back(reference(change.m_after));
    }
    return encoded({ { "schema_version", 1 },
                    { "status", "COMMITTED" },
                    { "assisted", true },
                    { "action_id", intent.m_action.m_value },
                    { "operation_id", intent.m_operation.m_value },
                    { "intent_sha256", sha256(intent.m_bytes) },
                    { "outputs", outputs } });
}

struct Plan {
    Intent m_intent;
    std::vector<ImmutableRecord> m_result_records;
};

Plan plan(const DraftMetadataMutation& mutation,
          const AdmittedAssetReceipt& before,
          const AdmittedAssetReceipt& after,
          const AdmittedManualActionJournal& journal,
          const AssetDependencyContext& expected,
          const std::stop_token& stop) {
    check_stop(stop);
    validate_identity(mutation.m_action, mutation.m_operation, mutation.m_base_generation);
    const auto* kind = kind_name(mutation.m_kind);
    require(!mutation.m_targets.empty() && mutation.m_targets.size() <= 16uz,
            "Draft target budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_BEFORE = take(validate_asset_dependencies(before.graph(), expected, stop));
    const auto K_AFTER = take(validate_asset_dependencies(after.graph(), expected, stop));
    const auto K_ACTION =
        std::ranges::find(journal.records(), mutation.m_action, [](const auto& value) {
            return value.m_request.m_action;
        });
    require(
        K_ACTION != journal.records().end(), "Draft action not journaled", ErrorCode::NOT_FOUND);
    require(K_ACTION->m_request.m_kind == mutation.m_kind, "Draft action kind differs");
    auto targets = mutation.m_targets;
    auto declared = K_ACTION->m_request.m_targets;
    std::ranges::sort(targets);
    std::ranges::sort(declared);
    require(targets == declared,
            "Draft target selection differs from action",
            ErrorCode::RESYNC_REQUIRED);
    std::set<AssetProductId> roots;
    std::string action_sha;
    auto target_rows = Json::array();
    for (const auto& target : targets) {
        require(roots.insert(target.m_product).second, "Duplicate draft target");
        action_sha = take(
            inference_manual_action(journal, mutation.m_action, before, target, expected, stop));
        target_rows.push_back(reference(target));
    }
    std::map<AssetProductId, AssetDependencyNode> old_nodes, new_nodes;
    for (const auto& node : before.graph().m_products) {
        old_nodes.emplace(node.m_product.m_product, node);
    }
    for (const auto& node : after.graph().m_products) {
        new_nodes.emplace(node.m_product.m_product, node);
    }
    require(old_nodes.size() == new_nodes.size(),
            "Draft graph membership changed",
            ErrorCode::RESYNC_REQUIRED);
    auto affected = roots;
    for (const auto K_ID : K_BEFORE.m_topological_order) {
        const auto& node = old_nodes.at(K_ID);
        if (std::ranges::any_of(node.m_dependencies, [&](const auto& ref) {
                return affected.contains(ref.m_product);
            })) {
            affected.insert(K_ID);
        }
    }
    require(
        affected.size() <= 16uz, "Draft descendant budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
    Plan result;
    result.m_intent = {
        mutation.m_action, mutation.m_operation, mutation.m_base_generation, action_sha, {}, {} };
    auto changes = Json::array();
    for (const auto& [K_ID, K_OLD] : old_nodes) {
        check_stop(stop);
        require(new_nodes.contains(K_ID), "Draft product removed", ErrorCode::RESYNC_REQUIRED);
        const auto& next = new_nodes.at(K_ID);
        if (!affected.contains(K_ID)) {
            auto old = K_OLD;
            auto current = next;
            std::ranges::sort(old.m_dependencies);
            std::ranges::sort(current.m_dependencies);
            require(old == current, "Unrelated draft product changed", ErrorCode::RESYNC_REQUIRED);
            continue;
        }
        require(K_OLD.m_product.m_revision < std::numeric_limits<std::uint64_t>::max() &&
                    next.m_product.m_revision == K_OLD.m_product.m_revision + 1 &&
                    next.m_product.m_content_sha256 != K_OLD.m_product.m_content_sha256 &&
                    next.m_recipe_sha256 == K_OLD.m_recipe_sha256 && next.m_manual_ancestor,
                "Draft descendant must have new bytes/revision and manual ancestry",
                ErrorCode::RESYNC_REQUIRED);
        auto expected_parents = K_OLD.m_dependencies;
        for (auto& ref : expected_parents) {
            ref = new_nodes.at(ref.m_product).m_product;
        }
        auto actual_parents = next.m_dependencies;
        std::ranges::sort(expected_parents);
        std::ranges::sort(actual_parents);
        require(expected_parents == actual_parents &&
                    next.m_manual_action_sha256 == (roots.contains(K_ID)
                                                        ? std::optional { action_sha }
                                                        : K_OLD.m_manual_action_sha256),
                "Draft dependency/action lineage differs");
        result.m_intent.m_changes.push_back({ K_OLD.m_product, next.m_product });
        changes.push_back(
            { { "before", reference(K_OLD.m_product) }, { "after", reference(next.m_product) } });
        result.m_result_records.push_back(
            { product_key(next.m_product),
             take(after.copy_blob(next.m_product.m_content_sha256, K_PRODUCT_BYTES, stop)) });
        result.m_result_records.push_back(
            { product_key(K_OLD.m_product),
             take(before.copy_blob(K_OLD.m_product.m_content_sha256, K_PRODUCT_BYTES, stop)) });
    }
    result.m_intent.m_bytes =
        encoded({ { "schema_version", 1 },
                 { "action_id", mutation.m_action.m_value },
                 { "kind", kind },
                 { "operation_id", mutation.m_operation.m_value },
                 { "base_generation", mutation.m_base_generation },
                 { "world_epoch", expected.m_world_epoch.m_value },
                 { "pose_revision", expected.m_pose_revision.m_value },
                 { "before_receipt_sha256", before.receipt_sha256() },
                 { "after_receipt_sha256", after.receipt_sha256() },
                 { "before_graph_sha256", K_BEFORE.m_graph_sha256 },
                 { "after_graph_sha256", K_AFTER.m_graph_sha256 },
                 { "journal_generation", journal.commit().m_generation },
                 { "journal_manifest_sha256", journal.commit().m_manifest_sha256 },
                 { "action_sha256", action_sha },
                 { "targets", target_rows },
                 { "changes", changes } });
    require(result.m_intent.m_bytes.size() <= K_INTENT_BYTES,
            "Draft intent byte budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    result.m_result_records.push_back(
        { action_key(mutation.m_action), take(after.copy_blob(action_sha, 16uz * 1024uz, stop)) });
    result.m_result_records.push_back(
        { result_key(mutation.m_operation), result_bytes(result.m_intent) });
    return result;
}

struct History {
    std::map<AssetProductId, AssetRevisionRef> m_heads;
};

History validate_history(const RecordStoreSnapshot& snapshot, const std::stop_token& stop) {
    std::vector<Intent> intents;
    for (const auto& [K_KEY, K_BYTES] : snapshot.m_records) {
        if (K_KEY.starts_with("mutation-")) {
            auto intent = parse_intent(K_BYTES, stop);
            require(K_KEY == intent_key(intent.m_action), "Draft intent key differs");
            intents.push_back(std::move(intent));
        }
    }
    require(
        intents.size() <= 512uz, "Draft history budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
    std::ranges::sort(intents, {}, &Intent::m_base);
    std::set<std::string> expected_keys;
    std::set<OperationId> operations;
    std::uint64_t generation {};
    History result;
    for (const auto& intent : intents) {
        check_stop(stop);
        require(intent.m_base == generation && generation % 2 == 0 &&
                    operations.insert(intent.m_operation).second,
                "Draft history order/operation differs");
        expected_keys.insert(intent_key(intent.m_action));
        ++generation;
        const auto K_RESULT = snapshot.m_records.find(result_key(intent.m_operation));
        for (const auto& change : intent.m_changes) {
            const auto K_HEAD = result.m_heads.find(change.m_before.m_product);
            require(K_HEAD == result.m_heads.end() || K_HEAD->second == change.m_before,
                    "Draft history branches from a stale revision",
                    ErrorCode::RESYNC_REQUIRED);
        }
        if (K_RESULT == snapshot.m_records.end()) {
            continue;
        }
        require(K_RESULT->second == result_bytes(intent), "Draft result differs from intent");
        expected_keys.insert(K_RESULT->first);
        const auto K_ACTION_KEY = action_key(intent.m_action);
        const auto K_ACTION = snapshot.m_records.find(K_ACTION_KEY);
        require(K_ACTION != snapshot.m_records.end() && K_ACTION->second.size() <= 16uz * 1024uz &&
                    sha256(K_ACTION->second) == intent.m_action_sha,
                "Draft action bytes differ");
        expected_keys.insert(K_ACTION_KEY);
        for (const auto& change : intent.m_changes) {
            for (const auto& ref : { change.m_before, change.m_after }) {
                const auto K_KEY = product_key(ref);
                const auto K_RECORD = snapshot.m_records.find(K_KEY);
                require(K_RECORD != snapshot.m_records.end() &&
                            K_RECORD->second.size() <= K_PRODUCT_BYTES &&
                            sha256(K_RECORD->second) == ref.m_content_sha256,
                        "Draft product bytes differ");
                expected_keys.insert(K_KEY);
            }
            result.m_heads[change.m_after.m_product] = change.m_after;
        }
        ++generation;
    }
    require(generation == snapshot.m_commit.m_generation &&
                expected_keys.size() == snapshot.m_records.size(),
            "Foreign/incomplete draft history");
    return result;
}

void notify(const DraftMutationObserver& observed,
            DraftMutationProgress progress,
            std::size_t count) {
    if (!observed) {
        return;
    }
    try {
        observed(progress, count);
    } catch (const std::exception& error) {
        throw RecordFailure(ErrorCode::BACKEND_FAILURE, error.what());
    } catch (...) {
        throw RecordFailure(ErrorCode::BACKEND_FAILURE, "Draft observer failed");
    }
}

}  // namespace

Result<RecordCommit> publish_draft_metadata_mutation(const DraftMetadataMutation& mutation,
                                                     const AdmittedAssetReceipt& before,
                                                     const AdmittedAssetReceipt& after,
                                                     const AdmittedManualActionJournal& journal,
                                                     const std::filesystem::path& directory,
                                                     const AssetDependencyContext& expected,
                                                     const std::stop_token& stop,
                                                     const DraftMutationObserver& observed) {
    try {
        require(!directory.empty() &&
                    directory.native().find(std::filesystem::path::value_type {}) ==
                        std::filesystem::path::string_type::npos,
                "Invalid draft output path",
                ErrorCode::INVALID_ARGUMENT);
        const auto K_PLAN = plan(mutation, before, after, journal, expected, stop);
        RecordStoreSnapshot current;
        if (std::filesystem::exists(directory / "owner.json")) {
            current = take(read_immutable_records(directory, stop));
        }
        const auto K_HISTORY = validate_history(current, stop);
        const auto K_PRIOR = current.m_records.find(intent_key(mutation.m_action));
        if (K_PRIOR != current.m_records.end()) {
            require(K_PRIOR->second == K_PLAN.m_intent.m_bytes,
                    "Draft action/operation intent collision",
                    ErrorCode::DUPLICATE_OPERATION);
            if (current.m_records.contains(result_key(mutation.m_operation))) {
                return current.m_commit;
            }
        } else {
            require(current.m_commit.m_generation == mutation.m_base_generation,
                    "Draft base differs; reopen",
                    ErrorCode::RESYNC_REQUIRED);
            for (const auto& node : before.graph().m_products) {
                const auto K_HEAD = K_HISTORY.m_heads.find(node.m_product.m_product);
                require(K_HEAD == K_HISTORY.m_heads.end() || K_HEAD->second == node.m_product,
                        "Draft input receipt contains stale committed products",
                        ErrorCode::RESYNC_REQUIRED);
            }
        }
        const std::vector<std::filesystem::path> K_PROTECTED {
            before.source_directory(), after.source_directory(), journal.source_directory() };
        take(publish_immutable_records(directory,
                                       { mutation.m_operation.m_value * 2 - 1 },
                                       mutation.m_base_generation,
                                       { { intent_key(mutation.m_action), K_PLAN.m_intent.m_bytes } },
                                       K_PROTECTED,
                                       stop));
        notify(observed, DraftMutationProgress::INTENT_DURABLE, 0uz);
        return publish_immutable_records(
            directory,
            { mutation.m_operation.m_value * 2 },
            mutation.m_base_generation + 1,
            K_PLAN.m_result_records,
            K_PROTECTED,
            stop,
            [&](std::size_t count) {
                notify(observed, DraftMutationProgress::RESULT_RECORD_WRITTEN, count);
            });
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Draft mutation allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<DraftMutationSnapshot>
read_draft_metadata_mutation(const DraftMetadataMutation& mutation,
                             const AdmittedAssetReceipt& before,
                             const AdmittedAssetReceipt& after,
                             const AdmittedManualActionJournal& journal,
                             const std::filesystem::path& directory,
                             const RecordCommit& selected,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop) {
    try {
        const auto K_PLAN = plan(mutation, before, after, journal, expected, stop);
        digest_text(selected.m_manifest_sha256);
        const auto K_STORE = take(read_immutable_records(directory, stop));
        require(K_STORE.m_commit.m_generation == selected.m_generation &&
                    K_STORE.m_commit.m_manifest_sha256 == selected.m_manifest_sha256,
                "Draft commit selection differs",
                ErrorCode::RESYNC_REQUIRED);
        validate_history(K_STORE, stop);
        const auto K_INTENT = K_STORE.m_records.find(intent_key(mutation.m_action));
        require(K_INTENT != K_STORE.m_records.end(),
                "Draft intent not committed",
                ErrorCode::NOT_FOUND);
        require(K_INTENT->second == K_PLAN.m_intent.m_bytes,
                "Draft selected intent differs",
                ErrorCode::RESYNC_REQUIRED);
        DraftMutationSnapshot result { DraftMutationPhase::PREPARED,
                                      K_STORE.m_commit,
                                      sha256(K_INTENT->second),
                                      {},
                                      K_PLAN.m_intent.m_changes,
                                      {} };
        const auto K_RESULT = K_STORE.m_records.find(result_key(mutation.m_operation));
        if (K_RESULT != K_STORE.m_records.end()) {
            for (const auto& record : K_PLAN.m_result_records) {
                const auto K_FOUND = K_STORE.m_records.find(record.m_key);
                require(K_FOUND != K_STORE.m_records.end() && K_FOUND->second == record.m_bytes,
                        "Draft replacement records differ");
            }
            result.m_phase = DraftMutationPhase::COMMITTED;
            result.m_result_sha256 = sha256(K_RESULT->second);
            for (const auto& change : result.m_changes) {
                result.m_output_bytes.emplace(change.m_after.m_product,
                                              K_STORE.m_records.at(product_key(change.m_after)));
            }
        }
        check_stop(stop);
        return result;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Draft snapshot allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
