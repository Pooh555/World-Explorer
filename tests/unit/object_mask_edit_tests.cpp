#include "../fixtures/object_mask_edit_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;
using namespace world_explorer::object_mask_edit_test;

namespace {

std::map<std::string, std::string> source_files(const MaskEditFixture& fixture) {
    std::map<std::string, std::string> result;
    for (const auto& root : { fixture.input().source_directory(),
                             fixture.output().source_directory(),
                             fixture.journal().source_directory() }) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                const auto K_SHA = sha256_file(entry.path(), 64uz * 1024uz * 1024uz);
                REQUIRE(K_SHA);
                result.emplace(entry.path().generic_string(), *K_SHA);
            }
        }
    }
    return result;
}

}  // namespace

TEST_CASE("typed mask edit publishes only admitted inference mask and retains owned assistance",
          "[object-mask-edit]") {
    MaskEditFixture fixture;
    const auto K_SOURCES = source_files(fixture);
    const auto K_COMMIT = fixture.publish();
    INFO((K_COMMIT ? "published" : K_COMMIT.error().m_message));
    REQUIRE(K_COMMIT);
    const auto K_READ = fixture.read(*K_COMMIT);
    REQUIRE(K_READ);
    CHECK(K_READ->selection().m_product == fixture.m_command.m_after.m_product);
    CHECK(K_READ->coverage() == std::array<std::uint64_t, 4> { 0, 2, 4, 2 });
    CHECK(K_READ->frame().m_view == fixture.m_command.m_after.m_view);
    CHECK(K_READ->manual_ancestor());
    CHECK(K_READ->commit().m_manifest_sha256 == K_COMMIT->m_manifest_sha256);
    CHECK(K_READ->result_sha256().size() == 64uz);
    const auto K_REPLAY = fixture.publish({}, [](DraftMutationProgress, std::size_t) {
        throw std::runtime_error("Completed replay must not notify");
    });
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_manifest_sha256 == K_COMMIT->m_manifest_sha256);
    CHECK(source_files(fixture) == K_SOURCES);
    std::filesystem::remove_all(fixture.m_workspace.m_root);
    std::filesystem::remove_all(fixture.input().source_directory());
    std::filesystem::remove_all(fixture.output().source_directory());
    CHECK(K_READ->mask().size() == 4uz);
    CHECK(!K_READ->frame().m_image.empty());
}

TEST_CASE("typed mask edit rejects malformed payload and semantic changes before any intent",
          "[object-mask-edit]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    MaskEditFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_new_mask["runs"][1uz]["first"] = 1;
        break;
    case 1:
        fixture.m_new_mask["runs"][0uz]["count"] = 0;
        break;
    case 2:
        fixture.m_new_mask["runs"][0uz]["class"] = "MEASURED_FULL_EXTENT";
        break;
    case 3:
        fixture.m_new_mask["grid"]["width"] = 5;
        break;
    case 4:
        fixture.m_new_mask["manual_ancestor"] = false;
        break;
    case 5:
        fixture.m_new_record["classes"][0uz]["confidence"] = 1.5;
        break;
    case 6:
        fixture.m_new_record["classes"][0uz]["label"] = "table";
        break;
    case 7:
        fixture.m_new_record["associations"][0uz]["track_id"] = 999;
        break;
    case 8:
        fixture.m_new_record["revision"] = 3;
        fixture.m_command.m_after.m_revision = 3;
        break;
    case 9:
        fixture.m_new_record["view"]["frame_id"] = 999;
        break;
    default:
        FAIL("Unknown synthetic mutation case");
    }
    fixture.save_after();
    const auto K_RESULT = fixture.publish();
    INFO((K_RESULT ? "unexpected success" : K_RESULT.error().m_message));
    CHECK(!K_RESULT);
    CHECK(!std::filesystem::exists(fixture.m_command.m_draft.m_directory));
}

TEST_CASE("typed mask edit accepts a contained replacement grid with complete runs",
          "[object-mask-edit]") {
    MaskEditFixture fixture;
    fixture.m_command.m_after.m_grid = { 2, 2, { 0.5, 1, -0.25, 0 } };
    fixture.m_new_record["grid"] = object_grid(fixture.m_command.m_after.m_grid);
    fixture.m_new_mask["grid"] = object_grid(fixture.m_command.m_after.m_grid);
    fixture.m_new_mask["runs"] = Json::array({ { { "first", 0 }, { "count", 4 }, { "class", "VISIBLE" } } });
    fixture.save_after();
    const auto K_RESULT = fixture.publish();
    INFO((K_RESULT ? "published" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto K_READ = fixture.read(*K_RESULT);
    REQUIRE(K_READ);
    CHECK(K_READ->coverage()[0uz] == 4);
    CHECK(K_READ->selection().m_grid == fixture.m_command.m_after.m_grid);
}

TEST_CASE("typed mask edit refuses held out views and foreign roles receipts revisions targets",
          "[object-mask-edit]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    const auto K_ROLE = GENERATE(CaptureFrameRole::HELD_OUT, CaptureFrameRole::EVALUATION_ONLY);
    MaskEditFixture fixture(K_CASE == 0, K_ROLE);
    auto command = fixture.m_command;
    const auto K_BEFORE = fixture.m_before.inspect(fixture.input());
    const auto K_AFTER = fixture.m_after.inspect(fixture.output());
    REQUIRE(K_BEFORE);
    REQUIRE(K_AFTER);
    if (K_CASE == 1) {
        ++command.m_after.m_mask.m_revision;
    } else if (K_CASE == 2) {
        command.m_draft.m_mutation.m_targets = { command.m_before.m_product };
    } else if (K_CASE == 3) {
        command.m_draft.m_mutation.m_kind = ManualActionKind::ANNOTATE_EVALUATION;
    } else if (K_CASE == 4) {
        command.m_after.m_view = fixture.m_after.m_object.m_selection.m_view;
    } else if (K_CASE == 6) {
        ++command.m_before.m_grid.m_width;
    } else if (K_CASE == 7) {
        ++command.m_before.m_view.m_frame_pose_revision.m_value;
    }
    const auto K_RESULT = replace_object_mask(command,
                                              fixture.input(),
                                              fixture.output(),
                                              fixture.journal(),
                                              *K_BEFORE,
                                              K_CASE == 5 ? *K_BEFORE : *K_AFTER,
                                              fixture.input().graph().m_context);
    CHECK(!K_RESULT);
    CHECK(!std::filesystem::exists(command.m_draft.m_directory));
}

TEST_CASE("typed mask read rejects invalid bodies even after generic draft publication",
          "[object-mask-edit]") {
    MaskEditFixture fixture;
    fixture.m_new_mask["runs"][0uz]["count"] = 0;
    fixture.save_after();
    const auto K_COMMIT = replace_draft_metadata(fixture.m_command.m_draft,
                                                 fixture.input(),
                                                 fixture.output(),
                                                 fixture.journal(),
                                                 fixture.input().graph().m_context);
    REQUIRE(K_COMMIT);
    const auto K_READ = fixture.read(*K_COMMIT);
    REQUIRE(!K_READ);
    CHECK(K_READ.error().m_message == "Incomplete/overlapping object mask partition");
}

TEST_CASE("typed mask intent binds the selected capture identity and forbids opaque downgrade",
          "[object-mask-edit]") {
    MaskEditFixture fixture;
    const auto K_COMMIT = fixture.publish();
    REQUIRE(K_COMMIT);
    const auto K_BEFORE = inspect_capture_selection(fixture.input(),
                                                    fixture.m_alternate_capture,
                                                    fixture.m_before.annotations(fixture.input()),
                                                    fixture.input().graph().m_context);
    const auto K_AFTER = inspect_capture_selection(fixture.output(),
                                                   fixture.m_alternate_capture,
                                                   fixture.m_after.annotations(fixture.output()),
                                                   fixture.input().graph().m_context);
    REQUIRE(K_BEFORE);
    REQUIRE(K_AFTER);
    const auto K_SUBSTITUTED = read_object_mask_edit(fixture.m_command,
                                                     fixture.input(),
                                                     fixture.output(),
                                                     fixture.journal(),
                                                     *K_BEFORE,
                                                     *K_AFTER,
                                                     *K_COMMIT,
                                                     fixture.input().graph().m_context);
    REQUIRE(!K_SUBSTITUTED);
    CHECK(K_SUBSTITUTED.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_RETRY = replace_object_mask(fixture.m_command,
                                             fixture.input(),
                                             fixture.output(),
                                             fixture.journal(),
                                             *K_BEFORE,
                                             *K_AFTER,
                                             fixture.input().graph().m_context);
    REQUIRE(!K_RETRY);
    const auto K_OPAQUE = replace_draft_metadata(fixture.m_command.m_draft,
                                                 fixture.input(),
                                                 fixture.output(),
                                                 fixture.journal(),
                                                 fixture.input().graph().m_context);
    REQUIRE(!K_OPAQUE);
    REQUIRE(fixture.read(*K_COMMIT));
}

TEST_CASE("typed mask edit requires committed result and independently selected current pin",
          "[object-mask-edit]") {
    const auto K_PHASE = GENERATE(DraftMutationProgress::INTENT_DURABLE,
                                  DraftMutationProgress::RESULT_RECORD_WRITTEN);
    MaskEditFixture fixture;
    std::stop_source cancellation;
    const auto K_INTERRUPTED = fixture.publish(cancellation.get_token(), [&](auto phase, auto) {
        if (phase == K_PHASE) {
            cancellation.request_stop();
        }
    });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_STORE);
    CHECK(K_STORE->m_commit.m_generation == 1);
    const auto K_PENDING = fixture.read(K_STORE->m_commit);
    REQUIRE(!K_PENDING);
    CHECK(K_PENDING.error().m_code == ErrorCode::WOULD_BLOCK);
    const auto K_RESUMED = fixture.publish();
    REQUIRE(K_RESUMED);
    REQUIRE(fixture.read(*K_RESUMED));
    const auto K_STALE = fixture.read(K_STORE->m_commit);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("capture firewall rejects evaluation content recipes actions and indirect aliases",
          "[object-mask-edit][capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    MaskEditFixture fixture;
    auto& region = fixture.m_after.m_object.m_region;
    const auto K_EVAL = region.m_graph.m_products[5uz].m_product;
    auto& root = region.m_graph.m_products[fixture.m_record_index];
    if (K_CASE == 0) {
        root.m_recipe_sha256 = K_EVAL.m_content_sha256;
        fixture.m_new_record["recipe_sha256"] = K_EVAL.m_content_sha256;
    } else {
        auto alias = region.node(8000, "synthetic alias");
        alias.m_recipe_sha256 = hash("isolated synthetic alias recipe");
        region.m_data[alias.m_recipe_sha256] = "isolated synthetic alias recipe";
        if (K_CASE == 1) {
            alias.m_product.m_content_sha256 = K_EVAL.m_content_sha256;
        } else if (K_CASE == 2) {
            alias.m_manual_action_sha256 = K_EVAL.m_content_sha256;
            alias.m_manual_ancestor = true;
        } else {
            alias.m_dependencies = { K_EVAL };
        }
        fixture.m_command.m_after.m_evidence = { alias.m_product };
        fixture.m_new_record["evidence"] = Json::array({ { { "product", region_ref(alias.m_product) },
                                                         { "accepted", true },
                                                         { "reason", "SYNTHETIC_TEST" } } });
        region.m_graph.m_products.push_back(alias);
    }
    fixture.save_after();
    const auto K_CAPTURE = fixture.m_after.inspect(fixture.output());
    REQUIRE(K_CAPTURE);
    const auto K_RESULT = K_CAPTURE->validate_inference_dependency(
        fixture.output(), fixture.m_command.m_after.m_product, fixture.input().graph().m_context);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_message == "Evaluation bytes enter inference dependency closure");
    const auto K_PUBLISH = fixture.publish();
    REQUIRE(!K_PUBLISH);
    CHECK(K_PUBLISH.error().m_message == K_RESULT.error().m_message);
    CHECK(!std::filesystem::exists(fixture.m_command.m_draft.m_directory));
}
