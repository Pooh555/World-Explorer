#include "../fixtures/asset_region_fixture.hpp"
#include "world_explorer/application/manual_actions.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <limits>
#include <string_view>

using namespace world_explorer;
using namespace world_explorer::asset_region_test;

namespace {

using Tree = std::map<std::string, std::string>;

Tree tree(const std::filesystem::path& root) {
    Tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        if (entry.is_symlink()) {
            result[K_NAME] = "link:" + std::filesystem::read_symlink(entry.path()).generic_string();
        } else if (entry.is_directory()) {
            result[K_NAME] = "directory";
        } else {
            const auto K_SHA = sha256_file(entry.path(), 128uz * 1024uz * 1024uz);
            REQUIRE(K_SHA);
            result[K_NAME] = *K_SHA;
        }
    }
    return result;
}

struct ActionFixture {
    RegionFixture m_source;
    RecipeFixture m_destination;
    AdmittedAssetReceipt m_receipt { m_source.freeze() };
    std::filesystem::path m_root { m_destination.m_root / "journal" };

    ManualActionRequest request(ManualActionKind kind = ManualActionKind::EDIT_MASK,
                                std::uint64_t id = 1) const {
        return { { id },
                kind,
                "synthetic-actor",
                1000,
                500,
                200,
                { m_receipt.graph().m_products[0uz].m_product },
                "synthetic declared action" };
    }

    Result<RecordCommit> append(const ManualActionRequest& request,
                                OperationId operation = { 1 },
                                std::uint64_t base = 0,
                                const std::stop_token& stop = {},
                                const std::function<void(std::size_t)>& observed = {}) const {
        return record_manual_action({ request, m_root, operation, base },
                                    m_receipt,
                                    m_source.m_graph.m_context,
                                    stop,
                                    observed);
    }
};

Tree tree(const ActionFixture& fixture) {
    Tree result;
    for (const auto& [K_NAME, K_VALUE] : tree(fixture.m_source.m_directory.m_root)) {
        result["source/" + K_NAME] = K_VALUE;
    }
    for (const auto& [K_NAME, K_VALUE] : tree(fixture.m_destination.m_root)) {
        result["destination/" + K_NAME] = K_VALUE;
    }
    return result;
}

std::vector<std::byte> bytes(const std::string& text) {
    const auto K_VIEW = std::as_bytes(std::span(text));
    return { K_VIEW.begin(), K_VIEW.end() };
}

Json document(const std::vector<std::byte>& data) {
    return Json::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

}  // namespace

TEST_CASE(
    "manual journal derives scope and accounts setup evaluation and output assistance separately",
    "[manual-actions]") {
    ActionFixture fixture;
    RecordCommit commit;
    for (std::size_t index { 0uz }; index < 11uz; ++index) {
        const auto K_REQUEST = fixture.request(static_cast<ManualActionKind>(index), index + 1uz);
        const auto K_COMMIT = fixture.append(K_REQUEST, { index + 1uz }, index);
        REQUIRE(K_COMMIT);
        commit = *K_COMMIT;
    }
    const auto K_JOURNAL = read_manual_action_journal(fixture.m_root, commit);
    REQUIRE(K_JOURNAL);
    CHECK(K_JOURNAL->records().size() == 11uz);
    CHECK(K_JOURNAL->accounting().m_setup_count == 3uz);
    CHECK(K_JOURNAL->accounting().m_setup_active_ms == 600);
    CHECK(K_JOURNAL->accounting().m_evaluation_count == 1uz);
    CHECK(K_JOURNAL->accounting().m_evaluation_active_ms == 200);
    CHECK(K_JOURNAL->accounting().m_assistance_count == 7uz);
    CHECK(K_JOURNAL->accounting().m_assistance_active_ms == 1400);
    const auto K_SNAPSHOT = read_immutable_records(fixture.m_root);
    REQUIRE(K_SNAPSHOT);
    const auto& raw = K_SNAPSHOT->m_records.at("manual-4");
    const auto K_DOC = document(raw);
    CHECK(K_DOC.at("kind").get<std::string>() == "ANNOTATE_EVALUATION");
    CHECK(K_DOC.at("scope").get<std::string>() == "EVALUATION_ONLY");
    CHECK(K_DOC.at("receipt_sha256").get<std::string>() == fixture.m_receipt.receipt_sha256());
    CHECK(K_JOURNAL->records()[3uz].m_sha256 == sha256(raw));
    for (std::size_t index { 0uz }; index < 11uz; ++index) {
        const auto K_BOUND = inference_manual_action(*K_JOURNAL,
                                                     { index + 1uz },
                                                     fixture.m_receipt,
                                                     fixture.request().m_targets[0uz],
                                                     fixture.m_source.m_graph.m_context);
        CHECK(K_BOUND.has_value() == (index >= 4uz));
    }
    std::filesystem::remove_all(fixture.m_root);
    CHECK(K_JOURNAL->records()[0uz].m_request.m_detail == "synthetic declared action");
    CHECK(K_JOURNAL->commit().m_generation == 11);
}

TEST_CASE("manual journal retries bind exact action operation and base without rewriting history",
          "[manual-actions]") {
    ActionFixture fixture;
    auto request = fixture.request();
    request.m_targets.push_back(fixture.m_receipt.graph().m_products[1uz].m_product);
    const auto K_FIRST = fixture.append(request);
    REQUIRE(K_FIRST);
    const auto K_BEFORE = tree(fixture);
    std::ranges::reverse(request.m_targets);
    const auto K_REPLAY = fixture.append(request, { 1 }, 0, {}, [](std::size_t) {
        throw std::runtime_error("replay must not write");
    });
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_manifest_sha256 == K_FIRST->m_manifest_sha256);
    CHECK(tree(fixture) == K_BEFORE);
    auto changed = request;
    changed.m_detail += " changed";
    const auto K_COLLISION = fixture.append(changed);
    REQUIRE(!K_COLLISION);
    CHECK(K_COLLISION.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    const auto K_ID_REUSE = fixture.append(request, { 2 }, 1);
    REQUIRE(!K_ID_REUSE);
    CHECK(K_ID_REUSE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(tree(fixture) == K_BEFORE);
    const auto K_SECOND =
        fixture.append(fixture.request(ManualActionKind::SELECT_INPUT, 2), { 2 }, 1);
    REQUIRE(K_SECOND);
    const auto K_LATE_REPLAY = fixture.append(request);
    REQUIRE(K_LATE_REPLAY);
    CHECK(K_LATE_REPLAY->m_manifest_sha256 == K_SECOND->m_manifest_sha256);
    CHECK(!read_manual_action_journal(fixture.m_root, *K_FIRST));
    const auto K_STALE = fixture.append(fixture.request(ManualActionKind::EDIT_MASK, 3), { 3 }, 0);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("manual action invalid bounds identities times and stale targets preserve caller trees",
          "[manual-actions]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    ActionFixture fixture;
    auto request = fixture.request();
    switch (K_CASE) {
    case 0:
        request.m_action = {};
        break;
    case 1:
        request.m_kind = std::bit_cast<ManualActionKind>(std::uint8_t { 255 });
        break;
    case 2:
        request.m_actor = "../actor";
        break;
    case 3:
        request.m_actor.resize(65uz, 'a');
        break;
    case 4:
        request.m_active_ms = request.m_elapsed_ms + 1;
        break;
    case 5:
        request.m_elapsed_ms = 604800001;
        break;
    case 6:
        request.m_started_unix_ms = std::numeric_limits<std::uint64_t>::max();
        break;
    case 7:
        request.m_targets.clear();
        break;
    case 8:
        request.m_targets.push_back(request.m_targets[0uz]);
        break;
    case 9:
        request.m_targets.resize(17uz, request.m_targets[0uz]);
        break;
    case 10:
        request.m_targets[0uz].m_revision = 2;
        break;
    case 11:
        request.m_targets[0uz].m_product = { 99999 };
        break;
    case 12:
        request.m_targets[0uz].m_content_sha256 = std::string(64uz, 'f');
        break;
    case 13:
        request.m_detail.clear();
        break;
    case 14:
        request.m_detail.resize(4097uz, 'a');
        break;
    case 15:
        request.m_detail.push_back('\0');
        break;
    case 16:
        request.m_detail = std::string(1uz, static_cast<char>(255));
        break;
    case 17:
        request.m_detail = std::string(4096uz, '\x01');
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    const auto K_BEFORE = tree(fixture);
    const auto K_RESULT = fixture.append(request);
    CHECK(!K_RESULT);
    CHECK(tree(fixture) == K_BEFORE);
    CHECK(!std::filesystem::exists(fixture.m_root));
}

TEST_CASE(
    "manual action inference binding rejects evaluation setup missing source target and context",
    "[manual-actions]") {
    ActionFixture fixture;
    const auto K_FIRST = fixture.append(fixture.request());
    REQUIRE(K_FIRST);
    const auto K_JOURNAL = read_manual_action_journal(fixture.m_root, *K_FIRST);
    REQUIRE(K_JOURNAL);
    auto target = fixture.request().m_targets[0uz];
    auto context = fixture.m_source.m_graph.m_context;
    const auto K_BOUND =
        inference_manual_action(*K_JOURNAL, { 1 }, fixture.m_receipt, target, context);
    REQUIRE(K_BOUND);
    CHECK(*K_BOUND == K_JOURNAL->records()[0uz].m_sha256);
    CHECK(!inference_manual_action(*K_JOURNAL, { 2 }, fixture.m_receipt, target, context));
    target.m_revision = 2;
    CHECK(!inference_manual_action(*K_JOURNAL, { 1 }, fixture.m_receipt, target, context));
    target = fixture.request().m_targets[0uz];
    context.m_world_epoch = { 4 };
    CHECK(!inference_manual_action(*K_JOURNAL, { 1 }, fixture.m_receipt, target, context));
    const auto K_BEFORE = tree(fixture);
    CHECK(!record_manual_action(
        { fixture.request(ManualActionKind::EDIT_MASK, 2), fixture.m_root, { 2 }, 1 },
        fixture.m_receipt,
        context));
    CHECK(tree(fixture) == K_BEFORE);
    ActionFixture other;
    other.m_source.m_graph.m_products[0uz].m_recipe_sha256 = hash("alternate synthetic recipe");
    other.m_source.m_data[hash("alternate synthetic recipe")] = "alternate synthetic recipe";
    // Receipt substitution without changing target bytes still requires resync.
    const auto K_OTHER = other.m_source.freeze();
    CHECK(!inference_manual_action(
        *K_JOURNAL, { 1 }, K_OTHER, target, fixture.m_source.m_graph.m_context));
}

TEST_CASE("manual journal rejects rehashed foreign schemas scopes and noncanonical records",
          "[manual-actions]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    ActionFixture fixture;
    const auto K_FIRST = fixture.append(fixture.request());
    REQUIRE(K_FIRST);
    const auto K_ORIGINAL = read_immutable_records(fixture.m_root);
    REQUIRE(K_ORIGINAL);
    auto doc = document(K_ORIGINAL->m_records.at("manual-1"));
    std::string key { "manual-1" };
    switch (K_CASE) {
    case 0:
        doc["schema_version"] = 2;
        break;
    case 1:
        doc["scope"] = "SETUP";
        break;
    case 2:
        doc["action_id"] = 2;
        break;
    case 3:
        doc["elapsed_ms"] = 500.0;
        break;
    case 4:
        doc["active_ms"] = -1;
        break;
    case 5:
        doc["extra"] = true;
        break;
    case 6:
        doc["targets"].push_back(doc["targets"][0uz]);
        break;
    case 7:
        doc["kind"] = "UNKNOWN";
        break;
    case 8:
        doc["base_generation"] = 1;
        break;
    case 9:
        doc["operation_id"] = 0;
        break;
    case 10:
        key = "foreign";
        break;
    case 11:
    case 12:
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    auto text = doc.dump();
    if (K_CASE == 11) {
        text += ' ';
    }
    if (K_CASE == 12) {
        text.insert(1uz, "\"action_id\":1,");
    }
    const auto K_ROOT = fixture.m_destination.m_root / "forged";
    const auto K_FORGED = publish_immutable_records(K_ROOT, { 1 }, 0, { { key, bytes(text) } }, {});
    REQUIRE(K_FORGED);
    const auto K_BEFORE = tree(fixture);
    CHECK(!read_manual_action_journal(K_ROOT, *K_FORGED));
    CHECK(!record_manual_action({ fixture.request(ManualActionKind::EDIT_MASK, 2), K_ROOT, { 2 }, 1 },
                                fixture.m_receipt,
                                fixture.m_source.m_graph.m_context));
    CHECK(tree(fixture) == K_BEFORE);
}

TEST_CASE("manual journal preserves committed history on cancellation and observer failure",
          "[manual-actions]") {
    const auto K_THROW = GENERATE(false, true);
    ActionFixture fixture;
    const auto K_FIRST = fixture.append(fixture.request());
    REQUIRE(K_FIRST);
    std::stop_source source;
    const auto K_NEXT = fixture.request(ManualActionKind::EDIT_GEOMETRY, 2);
    const auto K_INTERRUPTED = fixture.append(K_NEXT, { 2 }, 1, source.get_token(), [&](std::size_t) {
        if (K_THROW) {
            throw std::runtime_error("synthetic observer failure");
        }
        source.request_stop();
    });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code ==
          (K_THROW ? ErrorCode::BACKEND_FAILURE : ErrorCode::CANCELLED));
    const auto K_REOPENED = read_manual_action_journal(fixture.m_root, *K_FIRST);
    REQUIRE(K_REOPENED);
    CHECK(K_REOPENED->records().size() == 1uz);
    auto altered = K_NEXT;
    altered.m_active_ms = 201;
    const auto K_BEFORE = tree(fixture);
    CHECK(!fixture.append(altered, { 2 }, 1));
    CHECK(tree(fixture) == K_BEFORE);
    const auto K_RESUMED = fixture.append(K_NEXT, { 2 }, 1);
    REQUIRE(K_RESUMED);
    CHECK(K_RESUMED->m_generation == 2);
    source.request_stop();
    const auto K_COMPLETE = tree(fixture);
    CHECK(!fixture.append(
        fixture.request(ManualActionKind::EDIT_MASK, 3), { 3 }, 2, source.get_token()));
    CHECK(!read_manual_action_journal(fixture.m_root, *K_RESUMED, source.get_token()));
    CHECK(tree(fixture) == K_COMPLETE);
}

TEST_CASE(
    "manual journal refuses foreign linked locked and overlapping destinations without repair",
    "[manual-actions]") {
    ActionFixture fixture;
    const auto K_SOURCE = tree(fixture);
    CHECK(!record_manual_action(
        { fixture.request(), fixture.m_receipt.source_directory() / "nested", { 1 }, 0 },
        fixture.m_receipt,
        fixture.m_source.m_graph.m_context));
    CHECK(tree(fixture) == K_SOURCE);
    std::filesystem::create_directory(fixture.m_root);
    write(fixture.m_root / "foreign", "preserve caller bytes");
    const auto K_FOREIGN = tree(fixture);
    CHECK(!fixture.append(fixture.request()));
    CHECK(tree(fixture) == K_FOREIGN);
    std::filesystem::remove(fixture.m_root / "foreign");
    const auto K_FIRST = fixture.append(fixture.request());
    REQUIRE(K_FIRST);
    const auto K_BEFORE = tree(fixture);
    auto lock = FileLock::acquire(fixture.m_root / ".writer.lock");
    REQUIRE(lock);
    const auto K_BLOCKED = fixture.append(fixture.request(ManualActionKind::EDIT_MASK, 2), { 2 }, 1);
    REQUIRE(!K_BLOCKED);
    CHECK(K_BLOCKED.error().m_code == ErrorCode::WOULD_BLOCK);
    lock->reset();
    CHECK(tree(fixture) == K_BEFORE);
    const auto K_LINK = fixture.m_destination.m_root / "linked-journal";
    std::error_code error;
    std::filesystem::create_directory_symlink(fixture.m_root, K_LINK, error);
    if (!error) {
        const auto K_LINKED = tree(fixture);
        CHECK(
            !record_manual_action({ fixture.request(ManualActionKind::EDIT_MASK, 2), K_LINK, { 2 }, 1 },
                                  fixture.m_receipt,
                                  fixture.m_source.m_graph.m_context));
        CHECK(tree(fixture) == K_LINKED);
    }
}

TEST_CASE("manual journal recovers matching action after controlled child process exit",
          "[manual-actions]") {
    ActionFixture fixture;
    std::string diagnostic;
    const auto K_EXIT = execute_process({ WE_TEST_ASSET_RECEIPT_PROBE,
                                         fixture.m_receipt.source_directory().string(),
                                         fixture.m_receipt.receipt_sha256(),
                                         fixture.m_root.string(),
                                         "manual-crash" },
                                        {},
                                        {},
                                        [&](const std::string& text) {
                                            diagnostic += text;
                                        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    const auto K_PARTIAL = read_manual_action_journal(fixture.m_root, {});
    REQUIRE(K_PARTIAL);
    CHECK(K_PARTIAL->records().empty());
    CHECK(std::filesystem::exists(fixture.m_root / "intents/23.json"));
    const auto K_RETRY = fixture.append(fixture.request(ManualActionKind::EDIT_MASK, 23), { 23 }, 0);
    REQUIRE(K_RETRY);
    const auto K_COMPLETE = read_manual_action_journal(fixture.m_root, *K_RETRY);
    REQUIRE(K_COMPLETE);
    CHECK(K_COMPLETE->records().size() == 1uz);
    CHECK(K_COMPLETE->accounting().m_assistance_active_ms == 200);
}

TEST_CASE("manual action admits exact target actor detail and declared time bounds",
          "[manual-actions]") {
    ActionFixture fixture;
    for (std::size_t index { 0uz }; index < 12uz; ++index) {
        fixture.m_source.m_graph.m_products.push_back(
            fixture.m_source.node(index + 2000uz, "synthetic bounded target"));
    }
    fixture.m_receipt = fixture.m_source.freeze();
    auto request = fixture.request();
    request.m_targets.clear();
    for (const auto& node : fixture.m_receipt.graph().m_products) {
        request.m_targets.push_back(node.m_product);
    }
    request.m_actor = std::string(64uz, 'a');
    request.m_detail = std::string(4096uz, 'b');
    request.m_elapsed_ms = 604800000;
    request.m_active_ms = 604800000;
    request.m_started_unix_ms = 253402300799999 - request.m_elapsed_ms;
    const auto K_COMMIT = fixture.append(request);
    REQUIRE(K_COMMIT);
    const auto K_JOURNAL = read_manual_action_journal(fixture.m_root, *K_COMMIT);
    REQUIRE(K_JOURNAL);
    CHECK(K_JOURNAL->records()[0uz].m_request.m_targets.size() == 16uz);
    CHECK(K_JOURNAL->records()[0uz].m_request.m_actor == request.m_actor);
    CHECK(K_JOURNAL->records()[0uz].m_request.m_detail == request.m_detail);
    CHECK(K_JOURNAL->accounting().m_assistance_active_ms == 604800000);
}
