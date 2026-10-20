#include "../fixtures/depth_space_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <limits>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

struct Scratch {
    std::filesystem::path m_path =
        std::filesystem::temp_directory_path() /
        ("we-space-admission-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

}  // namespace

TEST_CASE("depth-space reload admits immutable complete typed evidence with wide frame identities",
          "[depth-space-admission]") {
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    const auto K_SOURCE_HASH = scene_content_hash(*fixture.m_source);
    const auto K_RESULT = fixture.load();
    INFO((K_RESULT ? "" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto& evidence = **K_RESULT;
    CHECK(evidence.frames().size() == 2uz);
    CHECK(evidence.frames()[1uz].m_value == 18446744073709551614ULL);
    CHECK(evidence.cells().size() == 1024uz);
    CHECK(evidence.relations().size() == 2048uz);
    CHECK(evidence.grid().m_dimensions == std::array<std::size_t, 3uz> { 32uz, 32uz, 1uz });
    CHECK(evidence.cells()[0uz].state() == DepthEvidenceState::UNKNOWN);
    CHECK(evidence.cells()[1uz].state() == DepthEvidenceState::FRONT_ONLY);
    CHECK(evidence.cells()[2uz].state() == DepthEvidenceState::SURFACE_OR_BEHIND);
    CHECK(evidence.cells()[3uz].state() == DepthEvidenceState::CONFLICT);
    CHECK(evidence.uncertainty_available()[0uz] == 0);
    CHECK(evidence.uncertainty_available()[1uz] == 1);
    CHECK(evidence.report_sha256() == fixture.m_pin);
    CHECK(scene_content_hash(*evidence.diagnostic()) == scene_content_hash(*fixture.m_diagnostic));
    CHECK(evidence.diagnostic()->m_provenance.at("source_kind") == "synthetic_diagnostic");
    CHECK(evidence.validate_source(*fixture.m_source));
    CHECK(scene_content_hash(*fixture.m_source) == K_SOURCE_HASH);
    const auto K_SOURCE_WRITTEN =
        write_bundle(scratch.m_path / "source.wexplore", fixture.m_source);
    REQUIRE(K_SOURCE_WRITTEN);
    const auto K_PIN =
        sha256_file(scratch.m_path / "source.wexplore/manifest.json", 1024uz * 1024uz);
    REQUIRE(K_PIN);
    const auto K_OPEN = open_depth_space({ scratch.m_path / "source.wexplore",
                                          fixture.m_path,
                                          *K_PIN,
                                          fixture.m_pin,
                                          std::string(DepthSpaceFixture::K_REQUEST) });
    REQUIRE(K_OPEN);
    CHECK((*K_OPEN)->categories() == evidence.categories());
}

TEST_CASE(
    "depth-space reload rejects rehashed malformed policy grid counts identities and dependencies",
    "[depth-space-admission]") {
    const auto K_MODE = GENERATE(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    const auto K_BEFORE = fixture.load();
    REQUIRE(K_BEFORE);
    auto& report = fixture.m_report;
    switch (K_MODE) {
    case 0:
        report["extra"] = 1;
        break;
    case 1:
        report["kind"] = "OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V2";
        break;
    case 2:
        report["walking_enabled"] = true;
        break;
    case 3:
        report["measured_collision"] = 0;
        break;
    case 4:
        report["scale"] = "METRIC_MEASURED";
        break;
    case 5:
        report["grid"]["dimensions"][0uz] = 31;
        break;
    case 6:
        report["grid"]["dimensions"][0uz] = 32.;
        break;
    case 7:
        report["grid"]["dimensions"][1uz] = std::numeric_limits<std::uint64_t>::max();
        break;
    case 8:
        report["grid"]["origin"][0uz] = .01;
        break;
    case 9:
        report["recipe"]["world_guard"] = .02;
        break;
    case 10:
        report["recipe"]["sigma_multiplier"] = 0;
        break;
    case 11:
        report["recipe"]["minimum_support"] = 1;
        break;
    case 12:
        report["view_counts"][0uz]["frame"] = report["view_counts"][1uz]["frame"];
        break;
    case 13:
        report["view_counts"][0uz]["uncertainty_available"] = true;
        break;
    case 14:
        report["view_counts"][0uz]["counts"][0uz] = 1024;
        break;
    case 15:
        report["categories_unknown_front_blocked_conflict"][0uz] = 0;
        break;
    case 16:
        report["source_receipt"]["frames"][0uz]["depth_sha256"] = std::string(64uz, 'f');
        break;
    case 17:
        report["source_receipt"]["pose_revision"] = 5;
        break;
    case 18:
        report["source_receipt"]["depth_recipe_sha256"] = std::string(64uz, 'f');
        break;
    case 19:
        report["view_counts"].erase(0uz);
        break;
    case 20:
        report["relations_bytes"] = -1;
        break;
    case 21:
        report["relation_codes"][2uz] = "FREE";
        break;
    case 22:
        report["diagnostic_manifest_sha256"] = std::string(64uz, 'g');
        break;
    case 23:
        report["request_sha256"] = std::string(64uz, 'f');
        break;
    default:
        throw std::runtime_error("Unexpected depth-space mutation");
    }
    fixture.publish();
    CHECK(!(fixture.load()));
    CHECK((*K_BEFORE)->cells()[1uz].state() == DepthEvidenceState::FRONT_ONLY);
    CHECK((*K_BEFORE)->validate_source(*fixture.m_source));
}

TEST_CASE("depth-space reload rejects raw NUL duplicate keys nesting trailing data and stale pins",
          "[depth-space-admission]") {
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4, 5);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    auto text = fixture.m_report.dump();
    if (K_MODE == 0) {
        text += '\0';
    }
    if (K_MODE == 1) {
        text.insert(1uz, "\"kind\":\"OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V1\",");
    }
    if (K_MODE == 2) {
        text += "{}";
    }
    if (K_MODE == 3) {
        text = std::string(20uz, '[') + "0" + std::string(20uz, ']');
    }
    if (K_MODE == 4) {
        text.replace(text.find("\"walking_enabled\":false"), 23uz, "\"walking_enabled\":NaN");
    }
    depth_space_write(fixture.m_path / "manifest.json", text);
    if (K_MODE != 5) {
        fixture.m_pin = depth_space_digest(text);
    } else {
        fixture.m_pin = std::string(64uz, '0');
    }
    CHECK(!(fixture.load()));
}

TEST_CASE(
    "depth-space ledger checks complete lengths codes totals and cell-specific native agreement",
    "[depth-space-admission]") {
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    if (K_MODE == 0) {
        fixture.m_relations[0uz] = static_cast<char>(5);
    }
    if (K_MODE == 1) {
        fixture.m_relations.pop_back();
    }
    if (K_MODE == 2) {
        fixture.m_relations += '\0';
    }
    if (K_MODE == 3) {
        fixture.m_relations[0uz] = static_cast<char>(2);
    }
    // Swap two cells while preserving every view count and aggregate category.
    if (K_MODE == 4) {
        std::swap(fixture.m_relations[0uz], fixture.m_relations[1uz]);
    }
    fixture.publish();
    CHECK(!(fixture.load()));
}

TEST_CASE("depth-space native snapshot cannot be replaced by a rehashed altered overlay or source",
          "[depth-space-admission]") {
    const auto K_MODE = GENERATE(0, 1, 2);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    auto altered = std::make_shared<SceneState>(*fixture.m_diagnostic);
    if (K_MODE == 0) {
        altered->m_provenance["depth_space"] = "Free space ready";
    } else {
        const auto K_ID = K_MODE == 1 ? ChunkId { 1 } : fixture.m_source->m_chunks.begin()->first;
        auto chunk = std::make_shared<SceneChunk>(*altered->m_chunks.at(K_ID));
        if (K_MODE == 1) {
            chunk->m_splats[0uz].m_opacity = .9F;
        } else {
            ++chunk->m_generation;
        }
        altered->m_chunks[K_ID] = chunk;
    }
    REQUIRE(write_bundle(fixture.m_path / "diagnostic.wexplore", altered));
    const auto K_PIN =
        sha256_file(fixture.m_path / "diagnostic.wexplore/manifest.json", 1024uz * 1024uz);
    REQUIRE(K_PIN);
    fixture.m_report["diagnostic_manifest_sha256"] = *K_PIN;
    fixture.publish();
    CHECK(!(fixture.load()));
}

TEST_CASE(
    "depth-space evidence invalidates scene identity epoch revision and pose or chunk changes",
    "[depth-space-admission]") {
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    const auto K_EVIDENCE = fixture.load();
    REQUIRE(K_EVIDENCE);
    auto current = *fixture.m_source;
    auto expected = ErrorCode::RESYNC_REQUIRED;
    if (K_MODE == 0) {
        ++current.m_id.m_value;
    }
    if (K_MODE == 1) {
        ++current.m_epoch.m_value;
        expected = ErrorCode::WRONG_EPOCH;
    }
    if (K_MODE == 2) {
        ++current.m_revision.m_value;
        expected = ErrorCode::WRONG_BASE;
    }
    if (K_MODE == 3) {
        auto chunk = std::make_shared<SceneChunk>(*current.m_chunks.begin()->second);
        ++chunk->m_generation;
        current.m_chunks[chunk->m_id] = chunk;
    }
    if (K_MODE == 4) {
        auto chunk = std::make_shared<SceneChunk>(*current.m_chunks.begin()->second);
        if (!chunk->m_mesh || !chunk->m_mesh->m_observed) {
            throw std::runtime_error("Missing observed fixture");
        }
        ++chunk->m_pose_revision.m_value;
        auto& observed = *chunk->m_mesh->m_observed;
        ++observed.m_context.m_pose_revision.m_value;
        auto receipt = nlohmann::json::parse(observed.m_receipt_json);
        receipt["pose_revision"] = observed.m_context.m_pose_revision.m_value;
        observed.m_receipt_json = receipt.dump();
        observed.m_context.m_receipt_sha256 = depth_space_digest(observed.m_receipt_json);
        current.m_chunks[chunk->m_id] = chunk;
    }
    const auto K_CHECK = (*K_EVIDENCE)->validate_source(current);
    REQUIRE(!K_CHECK);
    CHECK(K_CHECK.error().m_code == expected);
    CHECK((*K_EVIDENCE)->validate_source(*fixture.m_source));
    CHECK(!(load_depth_space_evidence({ std::make_shared<SceneState>(current),
                                       fixture.m_path,
                                       fixture.m_pin,
                                       std::string(DepthSpaceFixture::K_REQUEST) })));
}

TEST_CASE(
    "depth-space loader bounds files rejects links and handles cancellation without publication",
    "[depth-space-admission]") {
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path / "fixture");
    if (K_MODE == 0) {
        std::filesystem::resize_file(fixture.m_path / "manifest.json",
                                     K_DEPTH_SPACE_REPORT_BYTES + 1uz);
    }
    if (K_MODE == 1) {
        std::filesystem::resize_file(fixture.m_path / "relations.u8",
                                     K_DEPTH_SPACE_LEDGER_BYTES + 1uz);
    }
    if (K_MODE == 2) {
        std::filesystem::remove(fixture.m_path / "relations.u8");
    }
    if (K_MODE == 3) {
        const auto K_REAL = scratch.m_path / "ledger";
        std::filesystem::rename(fixture.m_path / "relations.u8", K_REAL);
        std::error_code error;
        std::filesystem::create_symlink(K_REAL, fixture.m_path / "relations.u8", error);
        if (error) {
            SKIP("Native symlink privileges unavailable");
        }
    }
    if (K_MODE == 4) {
        std::stop_source cancelled;
        cancelled.request_stop();
        const auto K_RESULT = fixture.load(cancelled.get_token());
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        return;
    }
    CHECK(!(fixture.load()));
}

TEST_CASE("depth-space diagnostic construction rejects incomplete and overflowing view evidence",
          "[depth-space-admission]") {
    Scratch scratch;
    DepthSpaceFixture fixture(scratch.m_path);
    std::vector<DepthCellEvidence> cells(1024uz);
    CHECK(!(make_depth_space_diagnostic(fixture.m_source, cells)));
    for (auto& cell : cells) {
        cell.m_counts[0uz] = 2uz;
    }
    REQUIRE(make_depth_space_diagnostic(fixture.m_source, cells));
    cells[0uz].m_counts[1uz] = std::numeric_limits<std::size_t>::max();
    CHECK(!(make_depth_space_diagnostic(fixture.m_source, cells)));
    cells.pop_back();
    CHECK(!(make_depth_space_diagnostic(fixture.m_source, cells)));
}
