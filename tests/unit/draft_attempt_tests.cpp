#include "../fixtures/draft_mutation_fixture.hpp"
#include "../fixtures/object_mask_edit_fixture.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace world_explorer;
using namespace world_explorer::draft_mutation_test;

namespace {

DraftMetadataAttempt
attempt(const DraftFixture& fixture, std::uint64_t id = 1, std::uint64_t base = 0) {
    return { { id }, base, fixture.m_workspace.m_root / "attempts" };
}

Result<DraftAttemptSnapshot> run(const DraftFixture& fixture,
                                 const DraftMetadataAttempt& selected,
                                 const std::stop_token& stop = {},
                                 const DraftMutationObserver& mutation_observed = {},
                                 const DraftAttemptObserver& observed = {}) {
    return attempt_draft_metadata({ fixture.m_mutation, fixture.m_root },
                                  selected,
                                  fixture.input(),
                                  fixture.output(),
                                  fixture.journal(),
                                  fixture.input().graph().m_context,
                                  stop,
                                  mutation_observed,
                                  observed);
}

Result<DraftAttemptSnapshot>
read(const DraftFixture& fixture, const DraftMetadataAttempt& selected, const RecordCommit& pin) {
    return read_draft_metadata_attempt(selected,
                                       fixture.m_mutation,
                                       fixture.input(),
                                       fixture.output(),
                                       fixture.journal(),
                                       fixture.m_root,
                                       pin,
                                       fixture.input().graph().m_context);
}

std::map<std::string, std::string> files(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            const auto K_SHA = sha256_file(entry.path(), 64uz * 1024uz * 1024uz);
            REQUIRE(K_SHA);
            result.emplace(entry.path().generic_string(), *K_SHA);
        }
    }
    return result;
}

RecordOperationBinding binding(const std::optional<RecordOperationBinding>& selected) {
    if (!selected.has_value()) {
        throw std::logic_error("Missing fixture operation binding");
    }
    return *selected;
}

}  // namespace

TEST_CASE("draft attempts bind exact committed result and replay without execution",
          "[draft-attempts]") {
    DraftFixture fixture;
    const auto K_ID = GENERATE(std::uint64_t { 1 }, std::numeric_limits<std::uint64_t>::max() / 2);
    const auto K_ATTEMPT = attempt(fixture, K_ID);
    const auto K_INPUT = files(fixture.input().source_directory());
    const auto K_OUTPUT = files(fixture.output().source_directory());
    const auto K_JOURNAL = files(fixture.journal().source_directory());
    const auto K_RESULT = run(fixture, K_ATTEMPT);
    INFO((K_RESULT ? "journaled" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_phase == DraftAttemptPhase::COMMITTED);
    CHECK(!K_RESULT->m_error.has_value());
    const auto K_DRAFT = read_immutable_records(fixture.m_root);
    REQUIRE(K_DRAFT);
    const auto K_TYPED = fixture.read(K_DRAFT->m_commit);
    REQUIRE(K_TYPED);
    CHECK(K_RESULT->m_mutation_intent_sha256 == K_TYPED->m_intent_sha256);
    CHECK(K_RESULT->m_mutation_result_sha256 == K_TYPED->m_result_sha256);
    REQUIRE(read(fixture, K_ATTEMPT, K_RESULT->m_commit));
    const auto K_FILES = files(fixture.m_workspace.m_root);
    const auto K_REPLAY = run(
        fixture,
        K_ATTEMPT,
        {},
        [](auto, auto) {
            throw std::runtime_error("Completed attempt must not execute");
        },
        [](auto) {
            throw std::runtime_error("Completed attempt must not notify");
        });
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_commit.m_manifest_sha256 == K_RESULT->m_commit.m_manifest_sha256);
    CHECK(files(fixture.m_workspace.m_root) == K_FILES);
    CHECK(files(fixture.input().source_directory()) == K_INPUT);
    CHECK(files(fixture.output().source_directory()) == K_OUTPUT);
    CHECK(files(fixture.journal().source_directory()) == K_JOURNAL);
}

TEST_CASE("draft cancellation is terminal for an attempt and new attempt resumes same mutation",
          "[draft-attempts]") {
    const auto K_PHASE = GENERATE(DraftMutationProgress::INTENT_DURABLE,
                                  DraftMutationProgress::RESULT_RECORD_WRITTEN);
    DraftFixture fixture;
    const auto K_ATTEMPT = attempt(fixture);
    std::stop_source cancellation;
    const auto K_CANCELLED =
        run(fixture, K_ATTEMPT, cancellation.get_token(), [&](auto phase, auto) {
            if (phase == K_PHASE) {
                cancellation.request_stop();
            }
        });
    REQUIRE(K_CANCELLED);
    CHECK(K_CANCELLED->m_phase == DraftAttemptPhase::CANCELLED);
    CHECK(K_CANCELLED->m_error == ErrorCode::CANCELLED);
    CHECK(K_CANCELLED->m_mutation_result_sha256.empty());
    const auto K_DRAFT = read_immutable_records(fixture.m_root);
    REQUIRE(K_DRAFT);
    const auto K_PENDING = fixture.read(K_DRAFT->m_commit);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_phase == DraftMutationPhase::PREPARED);
    CHECK(K_PENDING->m_output_bytes.empty());
    const auto K_AGAIN = run(fixture, K_ATTEMPT);
    REQUIRE(K_AGAIN);
    CHECK(K_AGAIN->m_phase == DraftAttemptPhase::CANCELLED);
    const auto K_RECOVERED = run(fixture, attempt(fixture, 2, 2));
    REQUIRE(K_RECOVERED);
    CHECK(K_RECOVERED->m_phase == DraftAttemptPhase::COMMITTED);
    const auto K_OLD_PIN = read(fixture, K_ATTEMPT, K_CANCELLED->m_commit);
    REQUIRE(!K_OLD_PIN);
    CHECK(K_OLD_PIN.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_OLD_OUTCOME = read(fixture, K_ATTEMPT, K_RECOVERED->m_commit);
    REQUIRE(K_OLD_OUTCOME);
    CHECK(K_OLD_OUTCOME->m_phase == DraftAttemptPhase::CANCELLED);
}

TEST_CASE("draft observer failures produce failed attempt without inventing a committed result",
          "[draft-attempts]") {
    const auto K_PHASE = GENERATE(0, 1, 2);
    DraftFixture fixture;
    const auto K_RESULT = run(
        fixture,
        attempt(fixture),
        {},
        [&](auto phase, auto) {
            if ((K_PHASE == 1 && phase == DraftMutationProgress::INTENT_DURABLE) ||
                (K_PHASE == 2 && phase == DraftMutationProgress::RESULT_RECORD_WRITTEN)) {
                throw std::runtime_error("Synthetic mutation observer failure");
            }
        },
        [&](auto phase) {
            if (K_PHASE == 0 && phase == DraftAttemptProgress::INTENT_DURABLE) {
                throw std::runtime_error("Synthetic attempt observer failure");
            }
        });
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_phase == DraftAttemptPhase::FAILED);
    CHECK(K_RESULT->m_error == ErrorCode::BACKEND_FAILURE);
    CHECK(K_RESULT->m_mutation_result_sha256.empty());
    REQUIRE(read(fixture, attempt(fixture), K_RESULT->m_commit));
    const auto K_RETRY = run(fixture, attempt(fixture, 2, 2));
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_phase == DraftAttemptPhase::COMMITTED);
}

TEST_CASE("interrupted outcome finalization recovers the first durable decision before rerun",
          "[draft-attempts]") {
    const auto K_CANCEL = GENERATE(false, true);
    const auto K_REMOVE_BLOB = GENERATE(false, true);
    DraftFixture fixture;
    const auto K_ATTEMPT = attempt(fixture);
    std::stop_source cancellation;
    const auto K_INTERRUPTED = run(
        fixture,
        K_ATTEMPT,
        cancellation.get_token(),
        [&](auto, auto) {
            if (K_CANCEL) {
                cancellation.request_stop();
            }
        },
        [](auto phase) {
            if (phase == DraftAttemptProgress::OUTCOME_RECORD_WRITTEN) {
                throw std::runtime_error("Synthetic journal finalization interruption");
            }
        });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::BACKEND_FAILURE);
    const auto K_STORE = read_immutable_records(K_ATTEMPT.m_directory);
    REQUIRE(K_STORE);
    CHECK(K_STORE->m_commit.m_generation == 1);
    CHECK(K_STORE->m_records.size() == 1uz);
    const auto K_PENDING = read(fixture, K_ATTEMPT, K_STORE->m_commit);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_phase == DraftAttemptPhase::PENDING);
    CHECK(K_PENDING->m_mutation_result_sha256.empty());
    const auto K_BINDING = inspect_immutable_record_operation(K_ATTEMPT.m_directory, { 2 });
    REQUIRE(K_BINDING);
    REQUIRE(K_BINDING->has_value());
    CHECK(!binding(*K_BINDING).m_committed);
    REQUIRE(binding(*K_BINDING).m_records.size() == 1uz);
    if (K_REMOVE_BLOB) {
        // Fixture-owned pending bytes only: simulate loss before the blob install.
        const auto K_PATH = K_ATTEMPT.m_directory / "blobs" /
                            (binding(*K_BINDING).m_records.front().m_sha256 + ".bin");
        REQUIRE(std::filesystem::remove(K_PATH));
    }
    const auto K_RECOVERED = run(fixture, K_ATTEMPT, {}, [](auto, auto) {
        throw std::runtime_error("Must recover decision without rerunning mutation");
    });
    INFO((K_RECOVERED ? "recovered" : K_RECOVERED.error().m_message));
    REQUIRE(K_RECOVERED);
    CHECK(K_RECOVERED->m_phase ==
          (K_CANCEL ? DraftAttemptPhase::CANCELLED : DraftAttemptPhase::COMMITTED));
    const auto K_FINAL = inspect_immutable_record_operation(K_ATTEMPT.m_directory, { 2 });
    REQUIRE(K_FINAL);
    REQUIRE(K_FINAL->has_value());
    CHECK(binding(*K_FINAL).m_committed);
    const auto K_ABSENT = inspect_immutable_record_operation(K_ATTEMPT.m_directory, { 99 });
    REQUIRE(K_ABSENT);
    CHECK(!K_ABSENT->has_value());
}

TEST_CASE(
    "draft attempt admission rejects stale identities overlaps and invalid mutation before writes",
    "[draft-attempts]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    DraftFixture fixture;
    auto selected = attempt(fixture);
    switch (K_CASE) {
    case 0:
        selected.m_attempt = {};
        break;
    case 1:
        selected.m_base_generation = 1;
        break;
    case 2:
        selected.m_directory = fixture.m_root;
        break;
    case 3:
        selected.m_directory = fixture.input().source_directory();
        break;
    case 4:
        selected.m_directory = fixture.journal().source_directory();
        break;
    case 5:
        fixture.m_mutation.m_targets[0uz].m_revision = 99;
        break;
    case 6:
        selected.m_base_generation = 1024;
        break;
    case 7:
        fixture.m_root = selected.m_directory / "nested";
        break;
    case 8:
        selected.m_attempt = { std::numeric_limits<std::uint64_t>::max() };
        break;
    default:
        FAIL("Unknown synthetic attempt case");
    }
    const auto K_FILES = files(fixture.m_workspace.m_root);
    const auto K_INPUT = files(fixture.input().source_directory());
    const auto K_RESULT = run(fixture, selected);
    REQUIRE(!K_RESULT);
    CHECK(files(fixture.m_workspace.m_root) == K_FILES);
    CHECK(files(fixture.input().source_directory()) == K_INPUT);
}

TEST_CASE("draft attempt rejects foreign journals altered selections and pending interleaving",
          "[draft-attempts]") {
    DraftFixture fixture;
    const auto K_ATTEMPT = attempt(fixture);
    const auto K_INTERRUPTED = run(fixture, K_ATTEMPT, {}, {}, [](auto phase) {
        if (phase == DraftAttemptProgress::OUTCOME_RECORD_WRITTEN) {
            throw std::runtime_error("Synthetic pending journal");
        }
    });
    REQUIRE(!K_INTERRUPTED);
    const auto K_FILES = files(fixture.m_workspace.m_root);
    CHECK(!run(fixture, attempt(fixture, 2, 2)));
    auto changed = K_ATTEMPT;
    changed.m_base_generation = 2;
    CHECK(!run(fixture, changed));
    fixture.m_mutation.m_operation = { 2 };
    CHECK(!run(fixture, K_ATTEMPT));
    fixture.m_mutation.m_operation = { 1 };
    CHECK(files(fixture.m_workspace.m_root) == K_FILES);
    REQUIRE(run(fixture, K_ATTEMPT));
    const auto K_FOREIGN = publish_immutable_records(
        K_ATTEMPT.m_directory, { 99 }, 2, { { "foreign", { std::byte { 1 } } } }, {});
    REQUIRE(K_FOREIGN);
    const auto K_FOREIGN_FILES = files(fixture.m_workspace.m_root);
    CHECK(!run(fixture, attempt(fixture, 2, 2)));
    CHECK(files(fixture.m_workspace.m_root) == K_FOREIGN_FILES);
}

TEST_CASE("typed mask attempts retain capture binding and exclude invalid bodies before journaling",
          "[draft-attempts][object-mask-edit]") {
    using object_mask_edit_test::MaskEditFixture;
    const auto K_INVALID = GENERATE(false, true);
    MaskEditFixture fixture;
    if (K_INVALID) {
        fixture.m_new_mask["runs"][0uz]["count"] = 0;
        fixture.save_after();
    }
    const auto K_BEFORE = fixture.m_before.inspect(fixture.input());
    const auto K_AFTER = fixture.m_after.inspect(fixture.output());
    REQUIRE(K_BEFORE);
    REQUIRE(K_AFTER);
    const DraftMetadataAttempt K_ATTEMPT { { 1 }, 0, fixture.m_workspace.m_root / "attempts" };
    const auto K_RESULT = attempt_object_mask_edit(fixture.m_command,
                                                   K_ATTEMPT,
                                                   fixture.input(),
                                                   fixture.output(),
                                                   fixture.journal(),
                                                   *K_BEFORE,
                                                   *K_AFTER,
                                                   fixture.input().graph().m_context);
    if (K_INVALID) {
        CHECK(!K_RESULT);
        CHECK(!std::filesystem::exists(K_ATTEMPT.m_directory));
        CHECK(!std::filesystem::exists(fixture.m_command.m_draft.m_directory));
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->m_phase == DraftAttemptPhase::COMMITTED);
        REQUIRE(read_object_mask_attempt(fixture.m_command,
                                         K_ATTEMPT,
                                         fixture.input(),
                                         fixture.output(),
                                         fixture.journal(),
                                         *K_BEFORE,
                                         *K_AFTER,
                                         K_RESULT->m_commit,
                                         fixture.input().graph().m_context));
        const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
        REQUIRE(K_STORE);
        REQUIRE(fixture.read(K_STORE->m_commit));
    }
}

TEST_CASE("draft attempts record writer contention while journal contention leaves outcome pending",
          "[draft-attempts]") {
    DraftFixture fixture;
    std::stop_source cancellation;
    const auto K_FIRST = run(fixture, attempt(fixture), cancellation.get_token(), [&](auto, auto) {
        cancellation.request_stop();
    });
    REQUIRE(K_FIRST);
    const auto K_SECOND = attempt(fixture, 2, 2);
    {
        const auto K_LOCK = FileLock::acquire(fixture.m_root / ".writer.lock");
        REQUIRE(K_LOCK);
        const auto K_FAILED = run(fixture, K_SECOND);
        REQUIRE(K_FAILED);
        CHECK(K_FAILED->m_phase == DraftAttemptPhase::FAILED);
        CHECK(K_FAILED->m_error == ErrorCode::WOULD_BLOCK);
    }
    const auto K_THIRD = attempt(fixture, 3, 4);
    const auto K_FILES = files(fixture.m_workspace.m_root);
    {
        const auto K_LOCK = FileLock::acquire(K_THIRD.m_directory / ".writer.lock");
        REQUIRE(K_LOCK);
        const auto K_BLOCKED = run(fixture, K_THIRD);
        REQUIRE(!K_BLOCKED);
        CHECK(K_BLOCKED.error().m_code == ErrorCode::WOULD_BLOCK);
    }
    CHECK(files(fixture.m_workspace.m_root) == K_FILES);
    const auto K_RECOVERED = run(fixture, K_THIRD);
    REQUIRE(K_RECOVERED);
    CHECK(K_RECOVERED->m_phase == DraftAttemptPhase::COMMITTED);
}

TEST_CASE("draft attempt child crashes recover pending execution and immutable cancelled decision",
          "[draft-attempts]") {
    const auto K_MODE = GENERATE("attempt-intent", "attempt-outcome", "attempt-cancelled-outcome");
    DraftFixture fixture;
    const auto K_ATTEMPT = attempt(fixture);
    auto targets = Json::array();
    for (const auto& ref : fixture.m_mutation.m_targets) {
        targets.push_back(region_ref(ref));
    }
    const Json K_CONFIG { { "before", fixture.input().source_directory().string() },
                         { "before_sha", fixture.input().receipt_sha256() },
                         { "after", fixture.output().source_directory().string() },
                         { "after_sha", fixture.output().receipt_sha256() },
                         { "journal", fixture.journal().source_directory().string() },
                         { "journal_generation", fixture.journal().commit().m_generation },
                         { "journal_sha", fixture.journal().commit().m_manifest_sha256 },
                         { "output", fixture.m_root.string() },
                         { "action_id", 1 },
                         { "kind", static_cast<std::uint8_t>(fixture.m_mutation.m_kind) },
                         { "operation_id", 1 },
                         { "base_generation", 0 },
                         { "targets", targets },
                         { "attempt_directory", K_ATTEMPT.m_directory.string() },
                         { "attempt_id", 1 },
                         { "attempt_base", 0 } };
    const auto K_PATH = fixture.m_workspace.m_root / "probe.json";
    write(K_PATH, K_CONFIG.dump());
    const auto K_INPUT = files(fixture.input().source_directory());
    const auto K_OUTPUT = files(fixture.output().source_directory());
    const auto K_JOURNAL = files(fixture.journal().source_directory());
    std::string diagnostic;
    const auto K_EXIT = execute_process(
        { WE_TEST_DRAFT_MUTATION_PROBE, K_PATH.string(), K_MODE }, {}, {}, [&](const auto& text) {
            diagnostic += text;
        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    const auto K_STORE = read_immutable_records(K_ATTEMPT.m_directory);
    REQUIRE(K_STORE);
    const auto K_PENDING = read(fixture, K_ATTEMPT, K_STORE->m_commit);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_phase == DraftAttemptPhase::PENDING);
    const auto K_RECOVERED = run(fixture, K_ATTEMPT);
    REQUIRE(K_RECOVERED);
    const std::string K_NAME { K_MODE };
    CHECK(K_RECOVERED->m_phase == (K_NAME == "attempt-cancelled-outcome"
                                       ? DraftAttemptPhase::CANCELLED
                                       : DraftAttemptPhase::COMMITTED));
    CHECK(files(fixture.input().source_directory()) == K_INPUT);
    CHECK(files(fixture.output().source_directory()) == K_OUTPUT);
    CHECK(files(fixture.journal().source_directory()) == K_JOURNAL);
}
