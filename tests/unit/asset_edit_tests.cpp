#include "../fixtures/asset_edit_fixture.hpp"
#include "../fixtures/draft_mutation_fixture.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <span>
#include <string_view>

using namespace world_explorer;
using namespace world_explorer::asset_edit_test;

TEST_CASE("v3 attached metadata enforces count bytes keys and exact replay identity",
          "[asset-edit][draft-mutations]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    draft_mutation_test::DraftFixture fixture;
    fixture.m_mutation.m_admission_sha256 = hash("synthetic attached metadata binding");
    const std::vector<std::byte> K_BYTES(64uz * 1024uz, std::byte { 65 });
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        fixture.m_mutation.m_metadata.push_back({ "metadata-" + std::to_string(index), K_BYTES });
    }
    switch (K_CASE) {
    case 0:
        break;
    case 1:
        fixture.m_mutation.m_metadata.push_back({ "metadata-4", K_BYTES });
        break;
    case 2:
        fixture.m_mutation.m_metadata.front().m_bytes.push_back(std::byte { 65 });
        break;
    case 3:
        fixture.m_mutation.m_metadata.front().m_key = "product-1000-2";
        break;
    case 4:
        fixture.m_mutation.m_metadata.back().m_key = "metadata-0";
        break;
    case 5:
        fixture.m_mutation.m_admission_sha256.reset();
        break;
    default:
        FAIL("Unknown attached metadata case");
    }
    const auto K_RESULT = fixture.publish();
    if (K_CASE != 0) {
        CHECK(!K_RESULT);
        CHECK(!std::filesystem::exists(fixture.m_root));
        return;
    }
    REQUIRE(K_RESULT);
    const auto K_READ = fixture.read(*K_RESULT);
    REQUIRE(K_READ);
    CHECK(K_READ->m_metadata_bytes.size() == 4uz);
    CHECK(K_READ->m_metadata_bytes.at("metadata-0") == K_BYTES);
    fixture.m_mutation.m_metadata.front().m_bytes.front() = std::byte { 66 };
    CHECK(!fixture.publish());
    CHECK(!fixture.read(*K_RESULT));
}

TEST_CASE("typed asset edits commit definition bytes and real mesh material payloads together",
          "[asset-edit]") {
    const auto K_KIND = GENERATE(ManualActionKind::EDIT_GEOMETRY, ManualActionKind::EDIT_MATERIAL);
    AssetEditFixture fixture(K_KIND);
    const auto K_ATTEMPT = fixture.run();
    INFO((K_ATTEMPT ? "attempt recorded" : K_ATTEMPT.error().m_message));
    REQUIRE(K_ATTEMPT);
    CHECK(K_ATTEMPT->m_phase == DraftAttemptPhase::COMMITTED);
    const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_STORE);
    const auto K_READ = fixture.read(K_STORE->m_commit);
    INFO((K_READ ? "read committed" : K_READ.error().m_message));
    REQUIRE(K_READ);
    CHECK(K_READ->definition().reference() == fixture.m_command.m_after.m_definition);
    CHECK(K_READ->definition().definition().m_manual_ancestor);
    CHECK(K_READ->payloads().m_mesh.m_triangles.size() == 1uz);
    CHECK(K_READ->payloads().m_material.m_roughness ==
          (K_KIND == ManualActionKind::EDIT_MATERIAL ? 0.75 : 0.5));
    CHECK(K_STORE->m_records.at("metadata-asset-42-2") == fixture.m_command.m_after.m_bytes);
    const auto K_DOC = Json::parse(
        std::string_view(reinterpret_cast<const char*>(K_STORE->m_records.at("mutation-1").data()),
                         K_STORE->m_records.at("mutation-1").size()));
    CHECK(K_DOC.at("schema_version").get<std::uint64_t>() == 3);
    REQUIRE(fixture.run());
    std::filesystem::remove_all(fixture.m_workspace.m_root);
    std::filesystem::remove_all(fixture.input().source_directory());
    std::filesystem::remove_all(fixture.output().source_directory());
    CHECK(K_READ->payloads().m_mesh.m_positions.size() == 3uz);
}

TEST_CASE("typed asset payload rejects malformed mesh channels and hidden representation switches "
          "before writes",
          "[asset-edit]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    AssetEditFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_next_geometry["triangles"][0uz][2uz] = 99;
        break;
    case 1:
        fixture.m_next_geometry["triangles"][0uz][2uz] = 1;
        break;
    case 2:
        fixture.m_next_geometry["normals"][0uz] = { 0, 0, 0 };
        break;
    case 3:
        fixture.m_next_geometry["normals"][0uz] = { 0, 0, -1 };
        break;
    case 4:
        fixture.m_next_geometry["uv"][1uz] = { 0, 0 };
        break;
    case 5:
        fixture.m_next_material["roughness"] = -0.1;
        break;
    case 6:
        fixture.m_next_material["base_color"][0uz] = 1.1;
        break;
    case 7:
        fixture.m_next_material["emission"][0uz] = 65;
        break;
    case 8:
        fixture.m_next_material["normal"] = { 1, 0, 0 };
        break;
    case 9:
        fixture.m_next_geometry["surface"] = "MEASURED_CLOSED_SOLID";
        break;
    case 10:
        fixture.m_next_geometry["variant_id"] = 8;
        break;
    case 11:
        fixture.m_next_material["color_space"] = "CAPTURED_RADIANCE_IS_ALBEDO";
        break;
    default:
        FAIL("Unknown static payload mutation");
    }
    fixture.save_after();
    const auto K_RESULT = fixture.run();
    INFO((K_RESULT ? "unexpected success" : K_RESULT.error().m_message));
    CHECK(!K_RESULT);
    CHECK(!std::filesystem::exists(fixture.m_attempt.m_directory));
    CHECK(!std::filesystem::exists(fixture.m_command.m_draft.m_directory));
}

TEST_CASE("typed asset edits retain pending state without definitions until successful retry",
          "[asset-edit]") {
    const auto K_POINT = GENERATE(0uz, 1uz, 9uz);
    AssetEditFixture fixture;
    std::stop_source cancellation;
    const auto K_CANCEL = fixture.run(cancellation.get_token(), [&](auto phase, auto count) {
        if ((K_POINT == 0uz && phase == DraftMutationProgress::INTENT_DURABLE) ||
            (K_POINT != 0uz && phase == DraftMutationProgress::RESULT_RECORD_WRITTEN &&
             count == K_POINT)) {
            cancellation.request_stop();
        }
    });
    REQUIRE(K_CANCEL);
    CHECK(K_CANCEL->m_phase == DraftAttemptPhase::CANCELLED);
    const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_STORE);
    CHECK(K_STORE->m_records.size() == 1uz);
    const auto K_PENDING = fixture.read(K_STORE->m_commit);
    REQUIRE(!K_PENDING);
    CHECK(K_PENDING.error().m_code == ErrorCode::WOULD_BLOCK);
    fixture.m_attempt.m_attempt = { 2 };
    fixture.m_attempt.m_base_generation = 2;
    const auto K_RETRY = fixture.run();
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_phase == DraftAttemptPhase::COMMITTED);
    const auto K_FINAL = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_FINAL);
    REQUIRE(fixture.read(K_FINAL->m_commit));
    CHECK(!fixture.read(K_STORE->m_commit));
}

TEST_CASE("typed asset definition blobs remain hidden through native publication crash recovery",
          "[asset-edit]") {
    AssetEditFixture fixture;
    std::stop_source cancellation;
    REQUIRE(fixture.run(cancellation.get_token(), [&](auto, auto) {
        cancellation.request_stop();
    }));
    const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_STORE);
    const auto& bytes = K_STORE->m_records.at("mutation-1");
    const auto K_INTENT =
        Json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto K_TEXT = [](const std::vector<std::byte>& data) {
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    };
    const Json K_CONFIG { { "before", fixture.input().source_directory().string() },
                         { "before_sha", fixture.input().receipt_sha256() },
                         { "after", fixture.output().source_directory().string() },
                         { "after_sha", fixture.output().receipt_sha256() },
                         { "journal", fixture.journal().source_directory().string() },
                         { "journal_generation", fixture.journal().commit().m_generation },
                         { "journal_sha", fixture.journal().commit().m_manifest_sha256 },
                         { "output", fixture.m_command.m_draft.m_directory.string() },
                         { "action_id", 1 },
                         { "operation_id", 1 },
                         { "kind", static_cast<std::uint8_t>(ManualActionKind::EDIT_GEOMETRY) },
                         { "base_generation", 0 },
                         { "targets", K_INTENT.at("targets") },
                         { "admission_sha256", K_INTENT.at("admission_sha256") },
                         { "metadata_crash_count", 9 },
                         { "metadata",
                          Json::array({ { { "key", "metadata-asset-42-1" },
                                        { "text", K_TEXT(fixture.m_command.m_before.m_bytes) } },
                                       { { "key", "metadata-asset-42-2" },
                                        { "text", K_TEXT(fixture.m_command.m_after.m_bytes) } } }) } };
    const auto K_PATH = fixture.m_workspace.m_root / "probe.json";
    write(K_PATH, K_CONFIG.dump());
    std::string diagnostic;
    const auto K_EXIT = execute_process(
        { WE_TEST_DRAFT_MUTATION_PROBE, K_PATH.string(), "metadata" }, {}, {}, [&](const auto& text) {
            diagnostic += text;
        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    const auto K_PENDING = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_records.size() == 1uz);
    CHECK(!fixture.read(K_PENDING->m_commit));
    fixture.m_attempt = { { 2 }, 2, fixture.m_attempt.m_directory };
    REQUIRE(fixture.run());
    const auto K_FINAL = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_FINAL);
    REQUIRE(fixture.read(K_FINAL->m_commit));
}

TEST_CASE("typed asset edits reject wrong kind definition identity and stale selections",
          "[asset-edit]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    AssetEditFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_command.m_draft.m_mutation.m_kind = ManualActionKind::EDIT_MASK;
        break;
    case 1:
        fixture.m_command.m_before.m_definition.m_revision = 99;
        break;
    case 2:
        fixture.m_command.m_draft.m_mutation.m_targets = { fixture.m_after.m_definition.m_geometry };
        break;
    case 3:
        fixture.m_command.m_after.m_provenance[0uz].m_region.m_domain.m_elements = 2;
        break;
    case 4:
        fixture.m_after.m_definition.m_pivot.m_orientation_ambiguous = true;
        fixture.save_after();
        break;
    case 5:
        fixture.m_command.m_draft.m_mutation.m_admission_sha256 = hash("forged binding");
        break;
    default:
        FAIL("Unknown asset selection mutation");
    }
    CHECK(!fixture.run());
    CHECK(!std::filesystem::exists(fixture.m_attempt.m_directory));
}

TEST_CASE("typed asset edits refuse semantic noops and changes outside the declared edit kind",
          "[asset-edit]") {
    const auto K_KIND = GENERATE(ManualActionKind::EDIT_GEOMETRY, ManualActionKind::EDIT_MATERIAL);
    const auto K_NOOP = GENERATE(false, true);
    AssetEditFixture fixture(K_KIND);
    if (K_KIND == ManualActionKind::EDIT_GEOMETRY) {
        if (K_NOOP) {
            fixture.m_next_geometry = fixture.m_geometry;
        } else {
            fixture.m_next_material["roughness"] = 0.75;
        }
    } else if (K_NOOP) {
        fixture.m_next_material["roughness"] = 0.5;
    } else {
        fixture.m_next_geometry["positions"][1uz][0uz] = 2;
    }
    fixture.save_after();
    const auto K_RESULT = fixture.run();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_message ==
          "Asset edit changes wrong payload or has no semantic change");
    CHECK(!std::filesystem::exists(fixture.m_attempt.m_directory));
    CHECK(!std::filesystem::exists(fixture.m_command.m_draft.m_directory));
}

TEST_CASE(
    "typed asset history admits successive definitions and rejects semantic revision branching",
    "[asset-edit]") {
    AssetEditFixture fixture;
    REQUIRE(fixture.run());
    fixture.advance();
    const auto K_GOOD = fixture.m_command;
    fixture.m_before.m_definition.m_revision = 1;
    fixture.m_after.m_definition.m_revision = 2;
    const auto K_BEFORE = fixture.m_before.admit(fixture.input());
    REQUIRE(K_BEFORE);
    const auto K_VIEW = std::as_bytes(std::span(K_BEFORE->canonical_json()));
    fixture.m_command.m_before = {
        K_BEFORE->reference(), { K_VIEW.begin(), K_VIEW.end() }, fixture.m_before.m_provenance };
    fixture.save_after();
    const auto K_BRANCH = fixture.run();
    REQUIRE(!K_BRANCH);
    CHECK(K_BRANCH.error().m_code == ErrorCode::RESYNC_REQUIRED);
    fixture.m_before.m_definition.m_revision = 2;
    fixture.m_after.m_definition.m_revision = 3;
    fixture.m_command = K_GOOD;
    const auto K_SECOND = fixture.run();
    INFO((K_SECOND ? "second edit committed" : K_SECOND.error().m_message));
    REQUIRE(K_SECOND);
    const auto K_STORE = read_immutable_records(fixture.m_command.m_draft.m_directory);
    REQUIRE(K_STORE);
    REQUIRE(fixture.read(K_STORE->m_commit));
}
