#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/application/capsule_session.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>
#include <limits>
#include <numbers>

using namespace world_explorer;
using namespace world_explorer::testing;
using namespace std::chrono_literals;

namespace {

struct SessionFixture {
    std::filesystem::path m_path {
        std::filesystem::temp_directory_path() /
        ("we-capsule-session-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) };
    nlohmann::json m_document;
    std::size_t m_requests {};

    SessionFixture() {
        std::filesystem::create_directory(m_path);
        const auto K_SOURCE = std::make_shared<const SceneState>(collision_source(true));
        const auto K_WRITTEN = write_bundle(m_path / "source.wexplore", K_SOURCE);
        REQUIRE(K_WRITTEN);
        auto support = collision_wall_document(*K_SOURCE);
        auto& cells = support["grid"]["states"].get_ref<std::string&>();
        for (std::size_t y { 0uz }; y < 4uz; ++y) {
            cells[1uz + 8uz * (y + 4uz * 4uz)] = '?';
        }
        const auto K_SUPPORT = support.dump();
        std::ofstream output(m_path / "support.json", std::ios::binary);
        output << K_SUPPORT;
        output.close();
        REQUIRE(output);
        const auto K_HASH = sha256_file(m_path / "source.wexplore/manifest.json", 1024uz * 1024uz);
        REQUIRE(K_HASH);
        m_document = { { "kind", "SYNTHETIC_CAPSULE_SESSION_V1" },
                      { "source_bundle", "source.wexplore" },
                      { "source_manifest_sha256", *K_HASH },
                      { "support", "support.json" },
                      { "support_sha256", collision_digest(K_SUPPORT) },
                      { "capsule",
                       { { "feet", { 2.5, 0, 1.5 } },
                        { "radius", .25 },
                        { "height", 1.7 },
                        { "eye_height", 1.6 },
                        { "speed", 3 },
                        { "yaw_radians", std::numbers::pi / 2 } } } };
    }

    ~SessionFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    std::pair<std::filesystem::path, std::string> request(std::string_view text) {
        const auto K_PATH = m_path / ("request-" + std::to_string(m_requests++) + ".json");
        std::ofstream output(K_PATH, std::ios::binary);
        output << text;
        output.close();
        REQUIRE(output);
        return { K_PATH, collision_digest(text) };
    }

    Result<std::unique_ptr<SyntheticCapsuleSession>> open() {
        const auto [K_PATH, K_HASH] = request(m_document.dump());
        return SyntheticCapsuleSession::open(K_PATH, K_HASH);
    }
};

FlyInput forward_input() {
    FlyInput input;
    input.m_captured = true;
    input.m_forward = 1;
    return input;
}

void same_pose(const CapsuleSessionFrame& before, const CapsuleSessionFrame& after) {
    CHECK(before.m_feet.m_x == after.m_feet.m_x);
    CHECK(before.m_feet.m_y == after.m_feet.m_y);
    CHECK(before.m_feet.m_z == after.m_feet.m_z);
    CHECK(before.m_camera.m_position.m_y == after.m_camera.m_position.m_y);
    CHECK(before.m_camera.m_yaw == after.m_camera.m_yaw);
    CHECK(before.m_camera.m_pitch == after.m_camera.m_pitch);
    CHECK(before.m_simulated_ticks == after.m_simulated_ticks);
}

}  // namespace

TEST_CASE("application capsule admits pinned bytes and drives a bounded horizontal camera",
          "[capsule-session]") {
    SessionFixture fixture;
    auto session = fixture.open();
    REQUIRE(session);
    const auto K_SOURCE = (*session)->source();
    CHECK(walking_block_reason(*K_SOURCE) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
    auto input = forward_input();
    for (std::size_t frame { 0uz }; frame < 50uz; ++frame) {
        const auto K_FRAME = (*session)->advance(*K_SOURCE, input, 10ms);
        REQUIRE(K_FRAME);
        CHECK(K_FRAME->m_status == CapsuleSessionStatus::ACTIVE);
        CHECK(K_FRAME->m_feet.m_x < 2.75);
        CHECK(K_FRAME->m_camera.m_position.m_y == Catch::Approx(1.6));
    }
    CHECK((*session)->frame().m_feet.m_x > 2.70);
    CHECK((*session)->frame().m_simulated_ticks == 60U);
    input.m_forward = -1;
    input.m_mouse_y = 400;
    input.m_up = 1;
    input.m_wheel = 10;
    const auto K_REVERSE = (*session)->advance(*K_SOURCE, input, 10ms);
    REQUIRE(K_REVERSE);
    CHECK(K_REVERSE->m_feet.m_x == Catch::Approx(2.72).margin(1e-5));
    CHECK(K_REVERSE->m_feet.m_y == 0);
    CHECK(K_REVERSE->m_camera.m_position.m_y == Catch::Approx(1.6));
    CHECK(K_REVERSE->m_camera.m_pitch < -.5);
    CHECK(K_REVERSE->m_camera.m_speed == 3);
}

TEST_CASE("application capsule pauses without accumulating focus or dialog movement",
          "[capsule-session]") {
    SessionFixture fixture;
    auto session = fixture.open();
    REQUIRE(session);
    const auto K_SOURCE = (*session)->source();
    REQUIRE((*session)->advance(*K_SOURCE, forward_input(), 8ms));
    const auto K_BEFORE = (*session)->frame();
    for (std::size_t mode { 0uz }; mode < 3uz; ++mode) {
        auto input = forward_input();
        input.m_mouse_x = 100;
        input.m_captured = mode != 0uz;
        input.m_focused = mode != 1uz;
        input.m_text_input = mode == 2uz;
        const auto K_PAUSED = (*session)->advance(*K_SOURCE, input, 1s);
        REQUIRE(K_PAUSED);
        same_pose(K_BEFORE, *K_PAUSED);
    }
    const auto K_NEXT = (*session)->advance(*K_SOURCE, forward_input(), 333334ns);
    REQUIRE(K_NEXT);
    CHECK(K_NEXT->m_simulated_ticks == 1U);
    CHECK(K_NEXT->m_dropped_ticks == 0U);
}

TEST_CASE("application capsule source correction permanently invalidates a session",
          "[capsule-session]") {
    const unsigned K_MUTATION { GENERATE(0U, 1U, 2U) };
    SessionFixture fixture;
    auto session = fixture.open();
    REQUIRE(session);
    const auto K_SOURCE = (*session)->source();
    REQUIRE((*session)->advance(*K_SOURCE, forward_input(), 8ms));
    const auto K_BEFORE = (*session)->frame();
    auto corrected = *K_SOURCE;
    if (K_MUTATION == 0U) {
        ++corrected.m_epoch.m_value;
    } else if (K_MUTATION == 1U) {
        ++corrected.m_revision.m_value;
    } else {
        auto wall =
            std::make_shared<SceneChunk>(*corrected.m_chunks.at(ChunkId { 9007199254740994ULL }));
        ++wall->m_pose_revision.m_value;
        corrected.m_chunks[wall->m_id] = wall;
    }
    REQUIRE(!((*session)->advance(corrected, forward_input(), 1s)));
    same_pose(K_BEFORE, (*session)->frame());
    CHECK((*session)->frame().m_status == CapsuleSessionStatus::INVALIDATED);
    const auto K_RETRY = (*session)->advance(*K_SOURCE, forward_input(), 333334ns);
    REQUIRE(!(K_RETRY));
    CHECK(K_RETRY.error().m_code == ErrorCode::RESYNC_REQUIRED);
    (*session)->stop();
    CHECK((*session)->frame().m_status == CapsuleSessionStatus::INVALIDATED);
}

TEST_CASE("application capsule cancellation and stop preserve the last pose", "[capsule-session]") {
    const bool K_CANCEL { GENERATE(false, true) };
    SessionFixture fixture;
    auto session = fixture.open();
    REQUIRE(session);
    const auto K_SOURCE = (*session)->source();
    REQUIRE((*session)->advance(*K_SOURCE, forward_input(), 10ms));
    const auto K_BEFORE = (*session)->frame();
    std::stop_source cancellation;
    if (K_CANCEL) {
        cancellation.request_stop();
    } else {
        (*session)->stop();
    }
    auto input = forward_input();
    input.m_mouse_x = 300;
    const auto K_STOPPED = (*session)->advance(*K_SOURCE, input, 1s, cancellation.get_token());
    REQUIRE(!(K_STOPPED));
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    same_pose(K_BEFORE, (*session)->frame());
    CHECK((*session)->frame().m_status == CapsuleSessionStatus::STOPPED);
    REQUIRE(!((*session)->advance(*K_SOURCE, forward_input(), 1s)));
    CHECK(walking_block_reason(*K_SOURCE) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("application capsule rejects malformed requests without disturbing a prior owner",
          "[capsule-session]") {
    const unsigned K_MUTATION { GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U) };
    SessionFixture fixture;
    auto session = fixture.open();
    REQUIRE(session);
    switch (K_MUTATION) {
    case 0:
        fixture.m_document["kind"] = "UNKNOWN";
        break;
    case 1:
        fixture.m_document["support_sha256"] = std::string(64uz, '0');
        break;
    case 2:
        fixture.m_document["source_manifest_sha256"] = std::string(64uz, '0');
        break;
    case 3:
        fixture.m_document["capsule"]["radius"] = "0.25";
        break;
    case 4:
        fixture.m_document["capsule"]["eye_height"] = 1.7;
        break;
    case 5:
        fixture.m_document["capsule"]["speed"] = 11;
        break;
    case 6:
        fixture.m_document["capsule"]["yaw_radians"] = 4;
        break;
    case 7:
        fixture.m_document["capsule"]["feet"] = { 1.5, 0, 4.5 };
        break;
    case 8:
        fixture.m_document["capsule"]["extra"] = true;
        break;
    case 9:
        fixture.m_document["support"] = "missing.json";
        break;
    default:
        FAIL("Unknown request mutation");
    }
    REQUIRE(!fixture.open());
    CHECK((*session)->frame().m_status == CapsuleSessionStatus::ACTIVE);
    REQUIRE((*session)->advance(*(*session)->source(), forward_input(), 10ms));
}

TEST_CASE("application capsule validates strict JSON pins and frame input transactionally",
          "[capsule-session]") {
    SessionFixture fixture;
    const auto [K_PATH, K_HASH] = fixture.request(fixture.m_document.dump());
    REQUIRE(!(SyntheticCapsuleSession::open(K_PATH, std::string(64uz, '0'))));
    std::stop_source cancelled;
    cancelled.request_stop();
    REQUIRE(!(SyntheticCapsuleSession::open(K_PATH, K_HASH, cancelled.get_token())));
    const auto [K_DUPLICATE, K_DUPLICATE_HASH] = fixture.request("{\"kind\":0,\"kind\":1}");
    REQUIRE(!(SyntheticCapsuleSession::open(K_DUPLICATE, K_DUPLICATE_HASH)));
    auto session = fixture.open();
    REQUIRE(session);
    const auto K_SOURCE = (*session)->source();
    REQUIRE((*session)->advance(*K_SOURCE, forward_input(), 8ms));
    const auto K_BEFORE = (*session)->frame();
    auto input = forward_input();
    input.m_mouse_x = 100;
    input.m_forward = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!((*session)->advance(*K_SOURCE, input, 1s)));
    REQUIRE(!((*session)->advance(*K_SOURCE, forward_input(), -1ns)));
    REQUIRE(!((*session)->advance(*K_SOURCE, forward_input(), 61s)));
    same_pose(K_BEFORE, (*session)->frame());
    CHECK((*session)->frame().m_status == CapsuleSessionStatus::ACTIVE);
    const auto K_NEXT = (*session)->advance(*K_SOURCE, forward_input(), 333334ns);
    REQUIRE(K_NEXT);
    CHECK(K_NEXT->m_simulated_ticks == 1U);
}

TEST_CASE("shared CLI replay retains unknown blocking and explicit synthetic labels",
          "[capsule-session]") {
    SessionFixture fixture;
    fixture.m_document["capsule"]["feet"] = { 1.5, 0, 3.5 };
    fixture.m_document["capsule"]["yaw_radians"] = std::numbers::pi;
    const auto [K_PATH, K_HASH] = fixture.request(fixture.m_document.dump());
    const auto K_REPLAY = replay_capsule_session(K_PATH, K_HASH, forward_input(), 10ms, 100uz);
    REQUIRE(K_REPLAY);
    const auto K_REPORT = nlohmann::json::parse(*K_REPLAY);
    CHECK(K_REPORT.at("kind") == "SYNTHETIC_CAPSULE_REPLAY_V1");
    CHECK(!(K_REPORT.at("walking_enabled").get<bool>()));
    CHECK(!(K_REPORT.at("measured_collision").get<bool>()));
    const auto& last = K_REPORT.at("samples").back();
    CHECK(last.at("clearance") == "UNKNOWN");
    CHECK(last.at("tick") == 120U);
    CHECK(last.at("feet").at(2uz).get<double>() < 3.75);
    REQUIRE(!(replay_capsule_session(K_PATH, K_HASH, forward_input(), 10ms, 0uz)));
    REQUIRE(!(replay_capsule_session(K_PATH, K_HASH, forward_input(), 10ms, 601uz)));
}
