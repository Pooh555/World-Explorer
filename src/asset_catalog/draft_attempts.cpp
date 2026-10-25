#include "world_explorer/asset_catalog/draft_attempts.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <array>
#include <limits>
#include <span>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::array<std::pair<ErrorCode, const char*>, 14> K_ERRORS {
    { { ErrorCode::INVALID_ARGUMENT, "INVALID_ARGUMENT" },
     { ErrorCode::INVALID_DATA, "INVALID_DATA" },
     { ErrorCode::UNSUPPORTED_OPERATION, "UNSUPPORTED_OPERATION" },
     { ErrorCode::WOULD_BLOCK, "WOULD_BLOCK" },
     { ErrorCode::CANCELLED, "CANCELLED" },
     { ErrorCode::IO_ERROR, "IO_ERROR" },
     { ErrorCode::RESOURCE_EXHAUSTED, "RESOURCE_EXHAUSTED" },
     { ErrorCode::WRONG_BASE, "WRONG_BASE" },
     { ErrorCode::WRONG_EPOCH, "WRONG_EPOCH" },
     { ErrorCode::RESYNC_REQUIRED, "RESYNC_REQUIRED" },
     { ErrorCode::DUPLICATE_OPERATION, "DUPLICATE_OPERATION" },
     { ErrorCode::NOT_FOUND, "NOT_FOUND" },
     { ErrorCode::BACKEND_FAILURE, "BACKEND_FAILURE" },
     { ErrorCode::DEVICE_LOST, "DEVICE_LOST" } } };

template <class Value> Value take(Result<Value> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

std::vector<std::byte> encode(const Json& doc) {
    const auto K_TEXT = doc.dump();
    const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
    return { K_BYTES.begin(), K_BYTES.end() };
}

std::string key(const char* prefix, std::uint64_t id) {
    return prefix + std::to_string(id);
}

void identity(std::uint64_t id, std::uint64_t base) {
    require(id && id <= std::numeric_limits<std::uint64_t>::max() / 2 && base <= 1022 &&
                base % 2 == 0,
            "Invalid draft attempt identity/base",
            ErrorCode::INVALID_ARGUMENT);
}

Json intent(const DraftMetadataAttempt& attempt,
            const DraftMetadataMutation& mutation,
            const DraftMutationIdentity& selected,
            const std::filesystem::path& draft_directory) {
    identity(attempt.m_attempt.m_value, attempt.m_base_generation);
    require(!draft_directory.empty() &&
                draft_directory.native().find(std::filesystem::path::value_type {}) ==
                    std::filesystem::path::string_type::npos,
            "Invalid selected draft directory",
            ErrorCode::INVALID_ARGUMENT);
    const auto K_PATH =
        std::filesystem::weakly_canonical(std::filesystem::absolute(draft_directory))
            .generic_string();
    require(K_PATH.size() <= 4096uz,
            "Draft attempt path budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    return { { "schema_version", 1 },
            { "attempt_id", attempt.m_attempt.m_value },
            { "base_generation", attempt.m_base_generation },
            { "action_id", mutation.m_action.m_value },
            { "mutation_operation_id", mutation.m_operation.m_value },
            { "draft_directory", K_PATH },
            { "mutation_intent_sha256", selected.m_intent_sha256 },
            { "mutation_result_sha256", selected.m_result_sha256 } };
}

Json parse_intent(const std::vector<std::byte>& bytes, const std::stop_token& stop) {
    const auto K_DOC = parse(bytes, 8uz * 1024uz, stop);
    keys(K_DOC,
         { "schema_version",
          "attempt_id",
          "base_generation",
          "action_id",
          "mutation_operation_id",
          "draft_directory",
          "mutation_intent_sha256",
          "mutation_result_sha256" });
    require(integer(K_DOC.at("schema_version")) == 1,
            "Unsupported draft attempt schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    identity(integer(K_DOC.at("attempt_id")), integer(K_DOC.at("base_generation")));
    require(integer(K_DOC.at("action_id")) && integer(K_DOC.at("mutation_operation_id")),
            "Missing draft attempt action/operation");
    const auto K_PATH = K_DOC.at("draft_directory").get<std::string>();
    require(!K_PATH.empty() && K_PATH.size() <= 4096uz && K_PATH.find('\0') == K_PATH.npos,
            "Invalid draft attempt path");
    digest(K_DOC.at("mutation_intent_sha256"));
    digest(K_DOC.at("mutation_result_sha256"));
    require(encode(K_DOC) == bytes, "Noncanonical draft attempt intent");
    return K_DOC;
}

Json outcome(const Json& start, std::optional<ErrorCode> error) {
    Json code = nullptr;
    if (error.has_value()) {
        bool found {};
        for (const auto& [K_CODE, K_NAME] : K_ERRORS) {
            if (K_CODE == *error) {
                code = K_NAME;
                found = true;
            }
        }
        require(found, "Unsupported draft attempt error");
    }
    return { { "schema_version", 1 },
            { "attempt_id", integer(start.at("attempt_id")) },
            { "attempt_intent_sha256", sha256(encode(start)) },
            { "status",
             !error.has_value()               ? "COMMITTED"
             : *error == ErrorCode::CANCELLED ? "CANCELLED"
                                              : "FAILED" },
            { "error_code", code },
            { "mutation_result_sha256",
             error.has_value() ? Json(nullptr) : start.at("mutation_result_sha256") } };
}

std::optional<ErrorCode>
parse_outcome(const Json& start, const std::vector<std::byte>& bytes, const std::stop_token& stop) {
    const auto K_DOC = parse(bytes, 2048uz, stop);
    keys(K_DOC,
         { "schema_version",
          "attempt_id",
          "attempt_intent_sha256",
          "status",
          "error_code",
          "mutation_result_sha256" });
    std::optional<ErrorCode> error;
    if (!K_DOC.at("error_code").is_null()) {
        const auto K_NAME = K_DOC.at("error_code").get<std::string>();
        for (const auto& [K_CODE, K_TOKEN] : K_ERRORS) {
            if (K_NAME == K_TOKEN) {
                error = K_CODE;
            }
        }
        require(error.has_value(), "Unsupported draft attempt error token");
    }
    require(bytes == encode(outcome(start, error)), "Draft attempt outcome differs");
    return error;
}

void history(const RecordStoreSnapshot& store, const std::stop_token& stop) {
    std::map<std::uint64_t, Json> starts;
    for (const auto& [K_KEY, K_BYTES] : store.m_records) {
        if (K_KEY.starts_with("attempt-")) {
            auto doc = parse_intent(K_BYTES, stop);
            require(K_KEY == key("attempt-", integer(doc.at("attempt_id"))) &&
                        starts.emplace(integer(doc.at("base_generation")), doc).second,
                    "Draft attempt key/order differs");
        }
    }
    require(starts.size() <= 512uz,
            "Draft attempt history budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::uint64_t generation {};
    std::size_t count {};
    for (const auto& [K_BASE, K_DOC] : starts) {
        check_stop(stop);
        require(K_BASE == generation, "Draft attempt history is not serial");
        ++generation;
        ++count;
        const auto K_FOUND = store.m_records.find(key("outcome-", integer(K_DOC.at("attempt_id"))));
        if (K_FOUND != store.m_records.end()) {
            parse_outcome(K_DOC, K_FOUND->second, stop);
            ++generation;
            ++count;
        }
    }
    require(generation == store.m_commit.m_generation && count == store.m_records.size(),
            "Foreign/incomplete draft attempt history");
}

DraftAttemptSnapshot
snapshot(const Json& start, const RecordStoreSnapshot& store, const std::stop_token& stop) {
    const auto K_BYTES = encode(start);
    const auto K_START = store.m_records.find(key("attempt-", integer(start.at("attempt_id"))));
    require(K_START != store.m_records.end() && K_START->second == K_BYTES,
            "Draft attempt selection differs",
            ErrorCode::RESYNC_REQUIRED);
    DraftAttemptSnapshot result { DraftAttemptPhase::PENDING,
                                 store.m_commit,
                                 sha256(K_BYTES),
                                 digest(start.at("mutation_intent_sha256")),
                                 {},
                                 {} };
    const auto K_FOUND = store.m_records.find(key("outcome-", integer(start.at("attempt_id"))));
    if (K_FOUND != store.m_records.end()) {
        result.m_error = parse_outcome(start, K_FOUND->second, stop);
        result.m_phase = !result.m_error.has_value()               ? DraftAttemptPhase::COMMITTED
                         : *result.m_error == ErrorCode::CANCELLED ? DraftAttemptPhase::CANCELLED
                                                                   : DraftAttemptPhase::FAILED;
        if (result.m_phase == DraftAttemptPhase::COMMITTED) {
            result.m_mutation_result_sha256 = digest(start.at("mutation_result_sha256"));
        }
    }
    return result;
}

// The finite canonical outcome vocabulary reconstructs an exact durable decision
// even if interruption happened after the core intent but before its blob write.
std::optional<std::vector<std::byte>> pending_outcome(const DraftMetadataAttempt& attempt,
                                                      const Json& start) {
    const auto K_OPERATION = take(
        inspect_immutable_record_operation(attempt.m_directory, { attempt.m_attempt.m_value * 2 }));
    if (!K_OPERATION.has_value()) {
        return std::nullopt;
    }
    require(K_OPERATION->m_base_generation == attempt.m_base_generation + 1 &&
                K_OPERATION->m_records.size() == 1uz,
            "Foreign draft attempt outcome operation");
    const auto& binding = K_OPERATION->m_records.front();
    require(binding.m_key == key("outcome-", attempt.m_attempt.m_value),
            "Foreign draft attempt outcome key");
    std::vector<std::optional<ErrorCode>> codes { std::nullopt };
    for (const auto& [K_CODE, K_NAME] : K_ERRORS) {
        codes.push_back(K_CODE);
    }
    for (const auto K_CODE : codes) {
        auto bytes = encode(outcome(start, K_CODE));
        if (binding.m_sha256 == sha256(bytes) && binding.m_bytes == bytes.size()) {
            return bytes;
        }
    }
    throw RecordFailure(ErrorCode::INVALID_DATA, "Unrecognized durable draft attempt outcome");
}

}  // namespace

Result<DraftAttemptSnapshot>
run_draft_metadata_attempt(const DraftMetadataAttempt& attempt,
                           const DraftMetadataMutation& mutation,
                           const AdmittedAssetReceipt& before,
                           const AdmittedAssetReceipt& after,
                           const AdmittedManualActionJournal& journal,
                           const std::filesystem::path& draft_directory,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop,
                           const DraftMutationObserver& mutation_observed,
                           const DraftAttemptObserver& observed) {
    try {
        check_stop(stop);
        const auto K_IDENTITY =
            take(inspect_draft_metadata_mutation(mutation, before, after, journal, expected, stop));
        const auto K_START = intent(attempt, mutation, K_IDENTITY, draft_directory);
        RecordStoreSnapshot current;
        if (std::filesystem::exists(attempt.m_directory / "owner.json")) {
            current = take(read_immutable_records(attempt.m_directory, stop));
        }
        history(current, stop);
        const auto K_PRIOR = current.m_records.find(key("attempt-", attempt.m_attempt.m_value));
        if (K_PRIOR != current.m_records.end()) {
            auto prior_snapshot = snapshot(K_START, current, stop);
            if (prior_snapshot.m_phase != DraftAttemptPhase::PENDING) {
                return prior_snapshot;
            }
        } else {
            require(current.m_commit.m_generation == attempt.m_base_generation,
                    "Draft attempt base differs; reopen",
                    ErrorCode::RESYNC_REQUIRED);
        }
        const std::vector<std::filesystem::path> K_PROTECTED { before.source_directory(),
                                                              after.source_directory(),
                                                              journal.source_directory(),
                                                              draft_directory };
        take(publish_immutable_records(
            attempt.m_directory,
            { attempt.m_attempt.m_value * 2 - 1 },
            attempt.m_base_generation,
            { { key("attempt-", attempt.m_attempt.m_value), encode(K_START) } },
            K_PROTECTED,
            stop));
        auto decision = pending_outcome(attempt, K_START);
        if (!decision.has_value()) {
            std::optional<ErrorCode> error;
            try {
                if (observed) {
                    observed(DraftAttemptProgress::INTENT_DURABLE);
                }
                const auto K_EXECUTED = publish_draft_metadata_mutation(mutation,
                                                                        before,
                                                                        after,
                                                                        journal,
                                                                        draft_directory,
                                                                        expected,
                                                                        stop,
                                                                        mutation_observed);
                if (!K_EXECUTED) {
                    error = K_EXECUTED.error().m_code;
                }
            } catch (...) {
                error = ErrorCode::BACKEND_FAILURE;
            }
            decision = encode(outcome(K_START, error));
        }
        // Persist cancellation with a fresh token. A failed finalization leaves
        // PENDING committed history; physical intent recovery preserves decision.
        const auto K_COMMIT = take(publish_immutable_records(
            attempt.m_directory,
            { attempt.m_attempt.m_value * 2 },
            attempt.m_base_generation + 1,
            { { key("outcome-", attempt.m_attempt.m_value), *decision } },
            K_PROTECTED,
            {},
            [&](std::size_t) {
                if (observed) {
                    observed(DraftAttemptProgress::OUTCOME_RECORD_WRITTEN);
                }
            }));
        current = take(read_immutable_records(attempt.m_directory));
        require(current.m_commit.m_generation == K_COMMIT.m_generation &&
                    current.m_commit.m_manifest_sha256 == K_COMMIT.m_manifest_sha256,
                "Draft attempt history advanced; reopen",
                ErrorCode::RESYNC_REQUIRED);
        history(current, {});
        return snapshot(K_START, current, {});
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Draft attempt allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<DraftAttemptSnapshot>
read_draft_metadata_attempt(const DraftMetadataAttempt& attempt,
                            const DraftMetadataMutation& mutation,
                            const AdmittedAssetReceipt& before,
                            const AdmittedAssetReceipt& after,
                            const AdmittedManualActionJournal& journal,
                            const std::filesystem::path& draft_directory,
                            const RecordCommit& selected,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop) {
    try {
        const auto K_IDENTITY =
            take(inspect_draft_metadata_mutation(mutation, before, after, journal, expected, stop));
        const auto K_START = intent(attempt, mutation, K_IDENTITY, draft_directory);
        digest_text(selected.m_manifest_sha256);
        const auto K_STORE = take(read_immutable_records(attempt.m_directory, stop));
        require(K_STORE.m_commit.m_generation == selected.m_generation &&
                    K_STORE.m_commit.m_manifest_sha256 == selected.m_manifest_sha256,
                "Draft attempt commit differs",
                ErrorCode::RESYNC_REQUIRED);
        history(K_STORE, stop);
        return snapshot(K_START, K_STORE, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Draft attempt read allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
