#include "../fixtures/draft_mutation_fixture.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace world_explorer;
using namespace world_explorer::draft_mutation_test;

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

Tree sources(const DraftFixture& fixture) {
    Tree result;
    for (const auto& [K_NAME, K_VALUE] : tree(fixture.input().source_directory())) {
        result["before/" + K_NAME] = K_VALUE;
    }
    for (const auto& [K_NAME, K_VALUE] : tree(fixture.output().source_directory())) {
        result["after/" + K_NAME] = K_VALUE;
    }
    for (const auto& [K_NAME, K_VALUE] : tree(fixture.journal().source_directory())) {
        result["journal/" + K_NAME] = K_VALUE;
    }
    return result;
}

Json config(const DraftFixture& fixture) {
    auto targets = Json::array();
    for (const auto& ref : fixture.m_mutation.m_targets) {
        targets.push_back(region_ref(ref));
    }
    return { { "before", fixture.input().source_directory().string() },
            { "before_sha", fixture.input().receipt_sha256() },
            { "after", fixture.output().source_directory().string() },
            { "after_sha", fixture.output().receipt_sha256() },
            { "journal", fixture.journal().source_directory().string() },
            { "journal_generation", fixture.journal().commit().m_generation },
            { "journal_sha", fixture.journal().commit().m_manifest_sha256 },
            { "output", fixture.m_root.string() },
            { "action_id", fixture.m_mutation.m_action.m_value },
            { "kind", static_cast<std::uint8_t>(fixture.m_mutation.m_kind) },
            { "operation_id", fixture.m_mutation.m_operation.m_value },
            { "base_generation", fixture.m_mutation.m_base_generation },
            { "targets", targets } };
}

}  // namespace

TEST_CASE("versioned draft admission bindings preserve opaque v1 and bind retry identity",
          "[draft-mutations]") {
    DraftFixture fixture;
    fixture.m_mutation.m_admission_sha256 = hash("synthetic typed admission binding");
    const auto K_COMMIT = fixture.publish();
    REQUIRE(K_COMMIT);
    REQUIRE(fixture.read(*K_COMMIT));
    const auto K_STORE = read_immutable_records(fixture.m_root);
    REQUIRE(K_STORE);
    const auto& bytes = K_STORE->m_records.at("mutation-1");
    const auto K_DOC = Json::parse(bytes);
    CHECK(K_DOC.at("schema_version").get<std::uint64_t>() == 2);
    CHECK(K_DOC.at("admission_sha256").get<std::string>() ==
          fixture.m_mutation.m_admission_sha256.value());
    fixture.m_mutation.m_admission_sha256.reset();
    REQUIRE(!fixture.read(*K_COMMIT));
    REQUIRE(!fixture.publish());
    fixture.m_mutation.m_admission_sha256 = "invalid";
    fixture.m_root = fixture.m_workspace.m_root / "invalid-binding";
    REQUIRE(!fixture.publish());
    CHECK(!std::filesystem::exists(fixture.m_root));
}

TEST_CASE(
    "draft replacement publishes exact revised closure with immutable intent result and ancestry",
    "[draft-mutations]") {
    const auto K_KIND = GENERATE(ManualActionKind::EDIT_MASK,
                                 ManualActionKind::EDIT_GEOMETRY,
                                 ManualActionKind::EDIT_MATERIAL);
    DraftFixture fixture(0uz, K_KIND);
    const auto K_SOURCES = sources(fixture);
    const auto K_COMMIT = fixture.publish();
    INFO((K_COMMIT ? "published" : K_COMMIT.error().m_message));
    REQUIRE(K_COMMIT);
    CHECK(K_COMMIT->m_generation == 2);
    const auto K_READ = fixture.read(*K_COMMIT);
    REQUIRE(K_READ);
    CHECK(K_READ->m_phase == DraftMutationPhase::COMMITTED);
    CHECK(K_READ->m_changes.size() == 2uz);
    CHECK(K_READ->m_output_bytes.size() == 2uz);
    for (const auto& change : K_READ->m_changes) {
        CHECK(sha256(K_READ->m_output_bytes.at(change.m_after.m_product)) ==
              change.m_after.m_content_sha256);
        CHECK(change.m_after.m_revision == change.m_before.m_revision + 1);
        const auto K_BYTES =
            fixture.output().copy_blob(change.m_after.m_content_sha256, 64uz * 1024uz);
        REQUIRE(K_BYTES);
        CHECK(*K_BYTES == K_READ->m_output_bytes.at(change.m_after.m_product));
    }
    CHECK(sources(fixture) == K_SOURCES);
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    const auto K_REPLAY = fixture.publish({}, [](DraftMutationProgress, std::size_t) {
        throw std::runtime_error("completed replay must not call observer");
    });
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_manifest_sha256 == K_COMMIT->m_manifest_sha256);
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
    std::filesystem::remove_all(fixture.m_root);
    CHECK(K_READ->m_output_bytes.size() == 2uz);
}

TEST_CASE("draft replacement rejects stale unrelated missing and falsely attributed graph "
          "revisions before writes",
          "[draft-mutations]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    DraftFixture fixture;
    auto& root = fixture.m_after.m_graph.m_products[2uz];
    auto& descendant = fixture.m_after.m_graph.m_products[3uz];
    switch (K_CASE) {
    case 0:
        root.m_product.m_revision = 3;
        break;
    case 1:
        descendant.m_product.m_revision = 1;
        break;
    case 2:
        root.m_manual_action_sha256.reset();
        root.m_manual_ancestor = false;
        descendant.m_manual_ancestor = false;
        break;
    case 3:
        root.m_manual_action_sha256 = hash("foreign action");
        fixture.m_after.m_data[hash("foreign action")] = "foreign action";
        break;
    case 4:
        ++fixture.m_after.m_graph.m_products[1uz].m_product.m_revision;
        break;
    case 5:
        root.m_dependencies.pop_back();
        break;
    case 6:
        root.m_recipe_sha256 = hash("changed recipe");
        fixture.m_after.m_data[hash("changed recipe")] = "changed recipe";
        break;
    case 7:
        fixture.m_after.m_graph.m_products.push_back(
            fixture.m_after.node(99, "foreign extra product"));
        break;
    case 8:
        root.m_product.m_content_sha256 = fixture.m_mutation.m_targets[0uz].m_content_sha256;
        break;
    case 9:
        fixture.m_mutation.m_targets[0uz].m_revision = 2;
        break;
    case 10:
        fixture.m_mutation.m_kind = ManualActionKind::EDIT_MASK;
        break;
    case 11: {
        const std::string K_BYTES(64uz * 1024uz + 1uz, 'x');
        root.m_product.m_content_sha256 = hash(K_BYTES);
        fixture.m_after.m_data[hash(K_BYTES)] = K_BYTES;
        break;
    }
    default:
        throw std::logic_error("Unexpected case");
    }
    fixture.save_after();
    const auto K_SOURCES = sources(fixture);
    const auto K_WORKSPACE = tree(fixture.m_workspace.m_root);
    CHECK(!fixture.publish());
    CHECK(!std::filesystem::exists(fixture.m_root));
    CHECK(sources(fixture) == K_SOURCES);
    CHECK(tree(fixture.m_workspace.m_root) == K_WORKSPACE);
}

TEST_CASE("draft replacement excludes setup and evaluation actions", "[draft-mutations]") {
    const auto K_KIND =
        GENERATE(ManualActionKind::SELECT_INPUT, ManualActionKind::ANNOTATE_EVALUATION);
    DraftFixture fixture(0uz, K_KIND);
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    CHECK(!fixture.publish());
    fixture.m_mutation.m_kind = ManualActionKind::EDIT_GEOMETRY;
    CHECK(!fixture.publish());
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
    CHECK(!std::filesystem::exists(fixture.m_root));
}

TEST_CASE("draft replacement identity and generation bounds refuse without changing sources",
          "[draft-mutations]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    DraftFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_mutation.m_operation = {};
        break;
    case 1:
        fixture.m_mutation.m_operation = { std::numeric_limits<std::uint64_t>::max() };
        break;
    case 2:
        fixture.m_mutation.m_base_generation = 1;
        break;
    case 3:
        fixture.m_mutation.m_base_generation = 1024;
        break;
    case 4:
        fixture.m_mutation.m_action = {};
        break;
    case 5:
        fixture.m_mutation.m_targets.push_back(fixture.m_mutation.m_targets[0uz]);
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    CHECK(!fixture.publish());
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
}

TEST_CASE(
    "draft cancellation and observer failure preserve a prepared intent with no exposed outputs",
    "[draft-mutations]") {
    const auto K_PROGRESS = GENERATE(DraftMutationProgress::INTENT_DURABLE,
                                     DraftMutationProgress::RESULT_RECORD_WRITTEN);
    const auto K_THROW = GENERATE(false, true);
    DraftFixture fixture;
    std::stop_source source;
    const auto K_SOURCES = sources(fixture);
    const auto K_RESULT =
        fixture.publish(source.get_token(), [&](DraftMutationProgress progress, std::size_t) {
            if (progress != K_PROGRESS) {
                return;
            }
            if (K_THROW) {
                throw std::runtime_error("synthetic draft observer failure");
            }
            source.request_stop();
        });
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == (K_THROW ? ErrorCode::BACKEND_FAILURE : ErrorCode::CANCELLED));
    const auto K_STORE = read_immutable_records(fixture.m_root);
    REQUIRE(K_STORE);
    CHECK(K_STORE->m_commit.m_generation == 1);
    const auto K_PENDING = fixture.read(K_STORE->m_commit);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_phase == DraftMutationPhase::PREPARED);
    CHECK(K_PENDING->m_output_bytes.empty());
    CHECK(K_PENDING->m_result_sha256.empty());
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    auto changed = fixture.m_mutation;
    changed.m_operation = { 2 };
    CHECK(!replace_draft_metadata({ changed, fixture.m_root },
                                  fixture.input(),
                                  fixture.output(),
                                  fixture.journal(),
                                  fixture.m_before.m_graph.m_context));
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
    const auto K_RETRY = fixture.publish();
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_generation == 2);
    CHECK(sources(fixture) == K_SOURCES);
}

TEST_CASE("draft publication protects both receipts journal roots and foreign locked destinations",
          "[draft-mutations]") {
    DraftFixture fixture;
    const auto K_SOURCES = sources(fixture);
    for (const auto& path : { fixture.input().source_directory() / "nested",
                             fixture.output().source_directory() / "nested",
                             fixture.journal().source_directory() / "nested",
                             fixture.m_workspace.m_root }) {
        CHECK(!replace_draft_metadata({ fixture.m_mutation, path },
                                      fixture.input(),
                                      fixture.output(),
                                      fixture.journal(),
                                      fixture.m_before.m_graph.m_context));
        CHECK(sources(fixture) == K_SOURCES);
    }
    std::filesystem::create_directory(fixture.m_root);
    write(fixture.m_root / "foreign", "caller data");
    const auto K_FOREIGN = tree(fixture.m_workspace.m_root);
    CHECK(!fixture.publish());
    CHECK(tree(fixture.m_workspace.m_root) == K_FOREIGN);
    std::filesystem::remove(fixture.m_root / "foreign");
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    auto lock = FileLock::acquire(fixture.m_root / ".writer.lock");
    REQUIRE(lock);
    const auto K_RESULT = fixture.publish();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::WOULD_BLOCK);
    lock->reset();
    auto after = tree(fixture.m_workspace.m_root);
    after.erase("draft/.writer.lock");
    CHECK(after == K_BEFORE);
    std::error_code error;
    const auto K_LINK = fixture.m_workspace.m_root / "linked-draft";
    std::filesystem::create_directory_symlink(fixture.m_root, K_LINK, error);
    if (!error) {
        const auto K_TREE = tree(fixture.m_workspace.m_root);
        CHECK(!replace_draft_metadata({ fixture.m_mutation, K_LINK },
                                      fixture.input(),
                                      fixture.output(),
                                      fixture.journal(),
                                      fixture.m_before.m_graph.m_context));
        CHECK(tree(fixture.m_workspace.m_root) == K_TREE);
    }
}

TEST_CASE("draft child process crashes retain prepared history and exact output recovery",
          "[draft-mutations]") {
    const auto K_MODE = GENERATE("intent", "output");
    DraftFixture fixture;
    const auto K_CONFIG = fixture.m_workspace.m_root / "probe.json";
    write(K_CONFIG, config(fixture).dump());
    const auto K_SOURCES = sources(fixture);
    std::string diagnostic;
    const auto K_EXIT = execute_process({ WE_TEST_DRAFT_MUTATION_PROBE, K_CONFIG.string(), K_MODE },
                                        {},
                                        {},
                                        [&](const std::string& text) {
                                            diagnostic += text;
                                        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    const auto K_STORE = read_immutable_records(fixture.m_root);
    REQUIRE(K_STORE);
    const auto K_PENDING = fixture.read(K_STORE->m_commit);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_phase == DraftMutationPhase::PREPARED);
    CHECK(K_PENDING->m_output_bytes.empty());
    const auto K_RETRY = fixture.publish();
    REQUIRE(K_RETRY);
    const auto K_FINAL = fixture.read(*K_RETRY);
    REQUIRE(K_FINAL);
    CHECK(K_FINAL->m_phase == DraftMutationPhase::COMMITTED);
    CHECK(sources(fixture) == K_SOURCES);
}

TEST_CASE("draft history accepts sequential revisions and rejects stale branches pins and foreign "
          "records",
          "[draft-mutations]") {
    DraftFixture fixture;
    const auto K_FIRST = fixture.publish();
    REQUIRE(K_FIRST);
    const auto K_OLD_MUTATION = fixture.m_mutation;
    const auto K_INPUT = fixture.input();
    const auto K_OUTPUT = fixture.output();
    const auto K_JOURNAL = fixture.journal();
    fixture.advance();
    const auto K_SECOND = fixture.publish();
    INFO((K_SECOND ? "published" : K_SECOND.error().m_message));
    REQUIRE(K_SECOND);
    CHECK(K_SECOND->m_generation == 4);
    CHECK(!fixture.read(*K_FIRST));
    auto wrong_pin = *K_SECOND;
    wrong_pin.m_manifest_sha256 = std::string(64uz, 'f');
    CHECK(!fixture.read(wrong_pin));
    const auto K_REPLAY = replace_draft_metadata({ K_OLD_MUTATION, fixture.m_root },
                                                 K_INPUT,
                                                 K_OUTPUT,
                                                 K_JOURNAL,
                                                 fixture.m_before.m_graph.m_context);
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_manifest_sha256 == K_SECOND->m_manifest_sha256);
    const auto K_READ = fixture.read(*K_SECOND);
    REQUIRE(K_READ);
    auto stale = fixture.m_mutation;
    stale.m_base_generation = 4;
    const auto K_BEFORE = tree(fixture.m_workspace.m_root);
    CHECK(!replace_draft_metadata({ stale, fixture.m_root },
                                  K_INPUT,
                                  fixture.output(),
                                  fixture.journal(),
                                  fixture.m_before.m_graph.m_context));
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
    DraftFixture branch;
    const ManualActionRequest K_SETUP { { 2 },
                                       ManualActionKind::SELECT_INPUT,
                                       "synthetic-actor",
                                       1000,
                                       0,
                                       0,
                                       branch.m_mutation.m_targets,
                                       "synthetic independent branch setup" };
    const auto K_SETUP_COMMIT = append_manual_action(K_SETUP,
                                                     branch.input(),
                                                     branch.journal().source_directory(),
                                                     { 2 },
                                                     1,
                                                     branch.m_before.m_graph.m_context);
    REQUIRE(K_SETUP_COMMIT);
    branch.configure(ManualActionKind::EDIT_GEOMETRY, 3, 4);
    const auto K_BRANCH_SOURCES = sources(branch);
    const auto K_BRANCH = replace_draft_metadata({ branch.m_mutation, fixture.m_root },
                                                 branch.input(),
                                                 branch.output(),
                                                 branch.journal(),
                                                 branch.m_before.m_graph.m_context);
    REQUIRE(!K_BRANCH);
    CHECK(K_BRANCH.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(sources(branch) == K_BRANCH_SOURCES);
    CHECK(tree(fixture.m_workspace.m_root) == K_BEFORE);
    const auto K_FOREIGN =
        publish_immutable_records(fixture.m_root, { 99 }, 4, { { "foreign", { std::byte { 1 } } } }, {});
    REQUIRE(K_FOREIGN);
    const auto K_FOREIGN_TREE = tree(fixture.m_workspace.m_root);
    CHECK(!fixture.read(*K_FOREIGN));
    CHECK(!fixture.publish());
    CHECK(tree(fixture.m_workspace.m_root) == K_FOREIGN_TREE);
}

TEST_CASE("draft closure budget admits sixteen revisions and refuses the seventeenth",
          "[draft-mutations]") {
    const auto K_EXTRA = GENERATE(14uz, 15uz);
    DraftFixture fixture(K_EXTRA);
    const auto K_SOURCES = sources(fixture);
    const auto K_RESULT = fixture.publish();
    INFO((K_RESULT ? "published" : K_RESULT.error().m_message));
    if (K_EXTRA == 14uz) {
        REQUIRE(K_RESULT);
        const auto K_READ = fixture.read(*K_RESULT);
        REQUIRE(K_READ);
        CHECK(K_READ->m_output_bytes.size() == 16uz);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
        CHECK(!std::filesystem::exists(fixture.m_root));
    }
    CHECK(sources(fixture) == K_SOURCES);
}

TEST_CASE("draft payload and operation bounds admit their exact limits", "[draft-mutations]") {
    DraftFixture fixture;
    const std::string K_BODY(64uz * 1024uz, 'x');
    fixture.m_after.m_graph.m_products[2uz].m_product.m_content_sha256 = hash(K_BODY);
    fixture.m_after.m_data[hash(K_BODY)] = K_BODY;
    fixture.save_after();
    fixture.m_mutation.m_operation = { std::numeric_limits<std::uint64_t>::max() / 2 };
    const auto K_COMMIT = fixture.publish();
    REQUIRE(K_COMMIT);
    const auto K_READ = fixture.read(*K_COMMIT);
    REQUIRE(K_READ);
    CHECK(K_READ->m_output_bytes.at({ 1000 }).size() == 64uz * 1024uz);
}
