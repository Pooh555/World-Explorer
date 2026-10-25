#include "world_explorer/application/draft_mutations.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

int main(int argc, char** argv) {
    using namespace world_explorer;
    if (argc != 3) {
        return 64;
    }
    try {
        const auto K_BYTES = asset_files::read_bytes(argv[1], 32uz * 1024uz, {});
        if (!K_BYTES) {
            return 2;
        }
        const auto K_DOC = nlohmann::json::parse(
            std::string_view(reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size()));
        const AssetDependencyContext K_CONTEXT { { 3 }, { 5 } };
        const auto K_BEFORE = read_asset_receipt(
            { K_DOC.at("before").get<std::string>(), K_DOC.at("before_sha").get<std::string>() },
            K_CONTEXT);
        const auto K_AFTER = read_asset_receipt(
            { K_DOC.at("after").get<std::string>(), K_DOC.at("after_sha").get<std::string>() },
            K_CONTEXT);
        const RecordCommit K_SELECTED { K_DOC.at("journal_generation").get<std::uint64_t>(),
                                       K_DOC.at("journal_sha").get<std::string>() };
        if (!K_BEFORE || !K_AFTER) {
            return 2;
        }
        DraftMetadataMutation mutation {
            { K_DOC.at("action_id").get<std::uint64_t>() },
            static_cast<ManualActionKind>(K_DOC.at("kind").get<std::uint8_t>()),
            { K_DOC.at("operation_id").get<std::uint64_t>() },
            K_DOC.at("base_generation").get<std::uint64_t>(),
            {} };
        for (const auto& row : K_DOC.at("targets")) {
            mutation.m_targets.push_back({ { row.at("product_id").get<std::uint64_t>() },
                                          row.at("revision").get<std::uint64_t>(),
                                          row.at("content_sha256").get<std::string>() });
        }
        if (K_DOC.contains("admission_sha256")) {
            mutation.m_admission_sha256 = K_DOC.at("admission_sha256").get<std::string>();
        }
        if (K_DOC.contains("metadata")) {
            for (const auto& row : K_DOC.at("metadata")) {
                const auto K_TEXT = row.at("text").get<std::string>();
                const auto K_VIEW = std::as_bytes(std::span(K_TEXT));
                mutation.m_metadata.push_back(
                    { row.at("key").get<std::string>(), { K_VIEW.begin(), K_VIEW.end() } });
            }
        }
        const std::filesystem::path K_OUTPUT { K_DOC.at("output").get<std::string>() };
        const std::string_view K_MODE(argv[2]);
        if (K_MODE == "journal") {
            const ManualActionRequest K_REQUEST { mutation.m_action,
                                                 mutation.m_kind,
                                                 "synthetic-oracle",
                                                 1000,
                                                 500,
                                                 200,
                                                 mutation.m_targets,
                                                 "synthetic draft replacement" };
            const auto K_COMMIT = append_manual_action(
                K_REQUEST, *K_BEFORE, K_DOC.at("journal").get<std::string>(), { 1 }, 0, K_CONTEXT);
            if (!K_COMMIT) {
                std::cerr << K_COMMIT.error().m_message << '\n';
                return 2;
            }
            std::cout << nlohmann::json({ { "generation", K_COMMIT->m_generation },
                                         { "manifest_sha256", K_COMMIT->m_manifest_sha256 } })
                             .dump()
                      << '\n';
            return 0;
        }
        const auto K_JOURNAL =
            read_manual_action_journal(K_DOC.at("journal").get<std::string>(), K_SELECTED);
        if (!K_JOURNAL) {
            return 2;
        }
        if (K_DOC.contains("attempt_directory")) {
            const DraftMetadataAttempt K_ATTEMPT { { K_DOC.at("attempt_id").get<std::uint64_t>() },
                                                  K_DOC.at("attempt_base").get<std::uint64_t>(),
                                                  K_DOC.at("attempt_directory").get<std::string>() };
            std::stop_source cancellation;
            const auto K_RESULT = attempt_draft_metadata(
                { mutation, K_OUTPUT },
                K_ATTEMPT,
                *K_BEFORE,
                *K_AFTER,
                *K_JOURNAL,
                K_CONTEXT,
                cancellation.get_token(),
                [&](auto, auto) {
                    if (K_MODE == "attempt-cancelled-outcome") {
                        cancellation.request_stop();
                    }
                },
                [&](auto phase) {
                    if ((K_MODE == "attempt-intent" &&
                         phase == DraftAttemptProgress::INTENT_DURABLE) ||
                        ((K_MODE == "attempt-outcome" || K_MODE == "attempt-cancelled-outcome") &&
                         phase == DraftAttemptProgress::OUTCOME_RECORD_WRITTEN)) {
                        std::_Exit(77);
                    }
                });
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << nlohmann::json(
                             { { "generation", K_RESULT->m_commit.m_generation },
                              { "manifest_sha256", K_RESULT->m_commit.m_manifest_sha256 },
                              { "phase", static_cast<std::uint8_t>(K_RESULT->m_phase) },
                              { "intent_sha256", K_RESULT->m_intent_sha256 },
                              { "mutation_intent_sha256", K_RESULT->m_mutation_intent_sha256 },
                              { "mutation_result_sha256", K_RESULT->m_mutation_result_sha256 } })
                             .dump()
                      << '\n';
            return 0;
        }
        const auto K_COMMIT = replace_draft_metadata(
            { mutation, K_OUTPUT },
            *K_BEFORE,
            *K_AFTER,
            *K_JOURNAL,
            K_CONTEXT,
            {},
            [&](DraftMutationProgress progress, std::size_t count) {
                if ((K_MODE == "intent" && progress == DraftMutationProgress::INTENT_DURABLE) ||
                    (K_MODE == "metadata" &&
                     progress == DraftMutationProgress::RESULT_RECORD_WRITTEN &&
                     count == K_DOC.at("metadata_crash_count").get<std::size_t>()) ||
                    (K_MODE == "output" &&
                     progress == DraftMutationProgress::RESULT_RECORD_WRITTEN && count == 1uz)) {
                    std::_Exit(77);
                }
            });
        if (!K_COMMIT) {
            std::cerr << K_COMMIT.error().m_message << '\n';
            return 2;
        }
        const auto K_READ = read_draft_metadata_mutation(
            mutation, *K_BEFORE, *K_AFTER, *K_JOURNAL, K_OUTPUT, *K_COMMIT, K_CONTEXT);
        if (!K_READ) {
            std::cerr << K_READ.error().m_message << '\n';
            return 3;
        }
        nlohmann::json result { { "generation", K_COMMIT->m_generation },
                               { "manifest_sha256", K_COMMIT->m_manifest_sha256 },
                               { "intent_sha256", K_READ->m_intent_sha256 },
                               { "result_sha256", K_READ->m_result_sha256 },
                               { "outputs", nlohmann::json::array() } };
        for (const auto& change : K_READ->m_changes) {
            result["outputs"].push_back(change.m_after.m_content_sha256);
        }
        std::cout << result.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
