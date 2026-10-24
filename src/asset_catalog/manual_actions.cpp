#include "world_explorer/asset_catalog/manual_actions.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::size_t K_RECORD_BYTES { 16uz * 1024uz };
constexpr std::uint64_t K_MAX_ELAPSED_MS { 604800000 };
constexpr std::uint64_t K_MAX_UNIX_MS { 253402300799999 };
constexpr std::array<const char*, 11> K_KINDS { "SELECT_INPUT",
                                               "SELECT_RECIPE",
                                               "CAPTURE_MEASUREMENT",
                                               "ANNOTATE_EVALUATION",
                                               "SELECT_OBJECT",
                                               "EDIT_MASK",
                                               "SELECT_CANDIDATE",
                                               "ALIGN_OUTPUT",
                                               "EDIT_GEOMETRY",
                                               "EDIT_MATERIAL",
                                               "APPROVE_OUTPUT" };

ManualActionScope scope(ManualActionKind kind) {
    const auto K_INDEX = static_cast<std::size_t>(kind);
    require(
        K_INDEX < K_KINDS.size(), "Unknown manual-action kind", ErrorCode::UNSUPPORTED_OPERATION);
    if (K_INDEX < 3uz) {
        return ManualActionScope::SETUP;
    }
    return kind == ManualActionKind::ANNOTATE_EVALUATION ? ManualActionScope::EVALUATION_ONLY
                                                         : ManualActionScope::OUTPUT_ASSISTANCE;
}

const char* scope_name(ManualActionScope value) {
    switch (value) {
    case ManualActionScope::SETUP:
        return "SETUP";
    case ManualActionScope::EVALUATION_ONLY:
        return "EVALUATION_ONLY";
    case ManualActionScope::OUTPUT_ASSISTANCE:
        return "OUTPUT_ASSISTANCE";
    }
    throw RecordFailure(ErrorCode::INVALID_DATA, "Unknown action scope");
}

std::string record_key(ManualActionId action) {
    return "manual-" + std::to_string(action.m_value);
}

void validate_request(const ManualActionRequest& value) {
    scope(value.m_kind);
    require(value.m_action.m_value && !value.m_actor.empty() && value.m_actor.size() <= 64uz &&
                value.m_actor.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") ==
                    value.m_actor.npos,
            "Invalid manual-action identity/actor");
    require(!value.m_targets.empty() && value.m_targets.size() <= 16uz && !value.m_detail.empty() &&
                value.m_detail.size() <= 4096uz,
            "Manual-action target/detail budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    require(value.m_detail.find('\0') == value.m_detail.npos, "NUL in manual-action detail");
    require(value.m_elapsed_ms <= K_MAX_ELAPSED_MS && value.m_active_ms <= value.m_elapsed_ms &&
                value.m_started_unix_ms <= K_MAX_UNIX_MS - value.m_elapsed_ms,
            "Invalid manual-action declared time");
    std::set<AssetProductId> ids;
    for (const auto& target : value.m_targets) {
        digest_text(target.m_content_sha256);
        require(target.m_product.m_value && target.m_revision &&
                    ids.insert(target.m_product).second,
                "Invalid/duplicate manual-action target");
    }
}

Json encode(const ManualActionRecord& value) {
    const auto& request = value.m_request;
    auto targets = request.m_targets;
    std::ranges::sort(targets);
    auto rows = Json::array();
    for (const auto& target : targets) {
        rows.push_back({ { "product_id", target.m_product.m_value },
                        { "revision", target.m_revision },
                        { "content_sha256", target.m_content_sha256 } });
    }
    return { { "schema_version", 1 },
            { "action_id", request.m_action.m_value },
            { "kind", K_KINDS.at(static_cast<std::size_t>(request.m_kind)) },
            { "scope", scope_name(value.m_scope) },
            { "actor", request.m_actor },
            { "started_unix_ms", request.m_started_unix_ms },
            { "elapsed_ms", request.m_elapsed_ms },
            { "active_ms", request.m_active_ms },
            { "detail", request.m_detail },
            { "targets", rows },
            { "operation_id", value.m_operation.m_value },
            { "base_generation", value.m_base_generation },
            { "world_epoch", value.m_context.m_world_epoch.m_value },
            { "pose_revision", value.m_context.m_pose_revision.m_value },
            { "receipt_sha256", value.m_receipt_sha256 },
            { "graph_sha256", value.m_graph_sha256 } };
}

ManualActionRecord decode(const std::vector<std::byte>& bytes, const std::stop_token& stop) {
    const auto K_DOC = parse(bytes, K_RECORD_BYTES, stop);
    keys(K_DOC,
         { "schema_version",
          "action_id",
          "kind",
          "scope",
          "actor",
          "started_unix_ms",
          "elapsed_ms",
          "active_ms",
          "detail",
          "targets",
          "operation_id",
          "base_generation",
          "world_epoch",
          "pose_revision",
          "receipt_sha256",
          "graph_sha256" });
    require(integer(K_DOC.at("schema_version")) == 1,
            "Unsupported manual-action schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_KIND = K_DOC.at("kind").get<std::string>();
    const auto K_FOUND = std::ranges::find(K_KINDS, K_KIND);
    require(
        K_FOUND != K_KINDS.end(), "Unknown manual-action kind", ErrorCode::UNSUPPORTED_OPERATION);
    ManualActionRecord result;
    auto& request = result.m_request;
    request.m_action = { integer(K_DOC.at("action_id")) };
    request.m_kind = static_cast<ManualActionKind>(K_FOUND - K_KINDS.begin());
    request.m_actor = K_DOC.at("actor").get<std::string>();
    request.m_started_unix_ms = integer(K_DOC.at("started_unix_ms"));
    request.m_elapsed_ms = integer(K_DOC.at("elapsed_ms"));
    request.m_active_ms = integer(K_DOC.at("active_ms"));
    request.m_detail = K_DOC.at("detail").get<std::string>();
    array(K_DOC.at("targets"), 16uz);
    for (const auto& row : K_DOC.at("targets")) {
        keys(row, { "product_id", "revision", "content_sha256" });
        request.m_targets.push_back({ { integer(row.at("product_id")) },
                                     integer(row.at("revision")),
                                     digest(row.at("content_sha256")) });
    }
    validate_request(request);
    result.m_scope = scope(request.m_kind);
    const auto K_SCOPE = K_DOC.at("scope").get<std::string>();
    require(K_SCOPE == scope_name(result.m_scope), "Manual-action scope differs from kind");
    result.m_operation = { integer(K_DOC.at("operation_id")) };
    result.m_base_generation = integer(K_DOC.at("base_generation"));
    result.m_context = { { integer(K_DOC.at("world_epoch")) }, { integer(K_DOC.at("pose_revision")) } };
    require(result.m_operation.m_value && result.m_base_generation < 1024 &&
                result.m_context.m_pose_revision.m_value,
            "Invalid manual-action operation/context");
    result.m_receipt_sha256 = digest(K_DOC.at("receipt_sha256"));
    result.m_graph_sha256 = digest(K_DOC.at("graph_sha256"));
    require(same_bytes(bytes, encode(result).dump()), "Noncanonical manual-action bytes");
    result.m_sha256 = sha256(bytes);
    return result;
}

std::vector<ManualActionRecord> records(const RecordStoreSnapshot& snapshot,
                                        const std::stop_token& stop) {
    require(snapshot.m_records.size() <= 1024uz &&
                snapshot.m_records.size() == snapshot.m_commit.m_generation,
            "Manual-action journal history differs");
    std::vector<ManualActionRecord> result;
    std::set<std::uint64_t> bases;
    std::set<OperationId> operations;
    for (const auto& [K_KEY, K_BYTES] : snapshot.m_records) {
        auto value = decode(K_BYTES, stop);
        require(K_KEY == record_key(value.m_request.m_action) &&
                    value.m_base_generation < snapshot.m_commit.m_generation &&
                    bases.insert(value.m_base_generation).second &&
                    operations.insert(value.m_operation).second,
                "Foreign/duplicate manual-action history");
        result.push_back(std::move(value));
    }
    std::ranges::sort(result, {}, &ManualActionRecord::m_base_generation);
    return result;
}

std::string bind_targets(const ManualActionRequest& request,
                         const AdmittedAssetReceipt& receipt,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        throw RecordFailure(K_GRAPH.error().m_code, K_GRAPH.error().m_message);
    }
    for (const auto& target : request.m_targets) {
        check_stop(stop);
        require(std::ranges::any_of(receipt.graph().m_products,
                                    [&](const auto& node) {
                                        return node.m_product == target;
                                    }),
                "Stale/foreign manual-action target",
                ErrorCode::RESYNC_REQUIRED);
    }
    return K_GRAPH->m_graph_sha256;
}

}  // namespace

struct AdmittedManualActionJournal::Implementation {
    RecordCommit m_commit;
    std::filesystem::path m_source;
    std::vector<ManualActionRecord> m_records;
    ManualActionAccounting m_accounting;
};

AdmittedManualActionJournal::AdmittedManualActionJournal(
    std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const RecordCommit& AdmittedManualActionJournal::commit() const {
    return m_state->m_commit;
}

const std::filesystem::path& AdmittedManualActionJournal::source_directory() const {
    return m_state->m_source;
}

const std::vector<ManualActionRecord>& AdmittedManualActionJournal::records() const {
    return m_state->m_records;
}

const ManualActionAccounting& AdmittedManualActionJournal::accounting() const {
    return m_state->m_accounting;
}

Result<RecordCommit> append_manual_action(const ManualActionRequest& request,
                                          const AdmittedAssetReceipt& receipt,
                                          const std::filesystem::path& directory,
                                          OperationId operation,
                                          std::uint64_t base_generation,
                                          const AssetDependencyContext& expected,
                                          const std::stop_token& stop,
                                          const std::function<void(std::size_t)>& observed) {
    try {
        check_stop(stop);
        require(!directory.empty() &&
                    directory.native().find(std::filesystem::path::value_type {}) ==
                        std::filesystem::path::string_type::npos &&
                    operation.m_value && base_generation < 1024,
                "Invalid manual-action journal path/operation/base",
                ErrorCode::INVALID_ARGUMENT);
        validate_request(request);
        const auto K_GRAPH_SHA = bind_targets(request, receipt, expected, stop);
        if (std::filesystem::exists(directory / "owner.json")) {
            const auto K_EXISTING = read_immutable_records(directory, stop);
            if (!K_EXISTING) {
                return std::unexpected(K_EXISTING.error());
            }
            records(*K_EXISTING, stop);
        }
        const ManualActionRecord K_VALUE { request,
                                          scope(request.m_kind),
                                          operation,
                                          base_generation,
                                          expected,
                                          receipt.receipt_sha256(),
                                          K_GRAPH_SHA,
                                          {} };
        const auto K_TEXT = encode(K_VALUE).dump();
        require(K_TEXT.size() <= K_RECORD_BYTES,
                "Manual-action byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { record_key(request.m_action), { K_BYTES.begin(), K_BYTES.end() } } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Manual-action allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<AdmittedManualActionJournal>
read_manual_action_journal(const std::filesystem::path& directory,
                           const RecordCommit& selected,
                           const std::stop_token& stop) {
    try {
        if (selected.m_generation) {
            digest_text(selected.m_manifest_sha256);
        } else {
            require(selected.m_manifest_sha256.empty(),
                    "Invalid empty journal selection",
                    ErrorCode::INVALID_ARGUMENT);
        }
        const auto K_SNAPSHOT = read_immutable_records(directory, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        require(selected.m_generation == K_SNAPSHOT->m_commit.m_generation &&
                    selected.m_manifest_sha256 == K_SNAPSHOT->m_commit.m_manifest_sha256,
                "Manual-action journal selection differs",
                ErrorCode::RESYNC_REQUIRED);
        auto state = std::make_shared<AdmittedManualActionJournal::Implementation>();
        state->m_commit = K_SNAPSHOT->m_commit;
        state->m_source = std::filesystem::canonical(directory);
        state->m_records = records(*K_SNAPSHOT, stop);
        for (const auto& value : state->m_records) {
            auto& accounting = state->m_accounting;
            switch (value.m_scope) {
            case ManualActionScope::SETUP:
                ++accounting.m_setup_count;
                accounting.m_setup_active_ms += value.m_request.m_active_ms;
                break;
            case ManualActionScope::EVALUATION_ONLY:
                ++accounting.m_evaluation_count;
                accounting.m_evaluation_active_ms += value.m_request.m_active_ms;
                break;
            case ManualActionScope::OUTPUT_ASSISTANCE:
                ++accounting.m_assistance_count;
                accounting.m_assistance_active_ms += value.m_request.m_active_ms;
                break;
            }
        }
        check_stop(stop);
        return AdmittedManualActionJournal(std::move(state));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Manual-action snapshot allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<std::string> inference_manual_action(const AdmittedManualActionJournal& journal,
                                            ManualActionId action,
                                            const AdmittedAssetReceipt& receipt,
                                            const AssetRevisionRef& target,
                                            const AssetDependencyContext& expected,
                                            const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_FOUND = std::ranges::find(journal.records(), action, [](const auto& value) {
            return value.m_request.m_action;
        });
        require(K_FOUND != journal.records().end(),
                "Manual action not journaled",
                ErrorCode::NOT_FOUND);
        require(K_FOUND->m_scope == ManualActionScope::OUTPUT_ASSISTANCE,
                "Setup/evaluation actions cannot enter inference lineage");
        require(K_FOUND->m_receipt_sha256 == receipt.receipt_sha256() &&
                    K_FOUND->m_context.m_world_epoch == expected.m_world_epoch &&
                    K_FOUND->m_context.m_pose_revision == expected.m_pose_revision &&
                    std::ranges::find(K_FOUND->m_request.m_targets, target) !=
                        K_FOUND->m_request.m_targets.end(),
                "Manual-action target/source/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(bind_targets(K_FOUND->m_request, receipt, expected, stop) ==
                    K_FOUND->m_graph_sha256,
                "Manual-action graph differs",
                ErrorCode::RESYNC_REQUIRED);
        return K_FOUND->m_sha256;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Manual-action binding allocation failed");
    }
}

}  // namespace world_explorer
