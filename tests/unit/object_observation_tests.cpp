#include "../fixtures/object_observation_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::object_observation_test;

namespace {

using Tree = std::map<std::string, std::string>;

Tree tree(const std::filesystem::path& root) {
    Tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            const auto K_HASH = sha256_file(entry.path(), 64uz * 1024uz * 1024uz);
            REQUIRE(K_HASH);
            result[entry.path().lexically_relative(root).generic_string()] = *K_HASH;
        }
    }
    return result;
}

}  // namespace

TEST_CASE(
    "object observation freezes decoded map bindings masks and distinct evidence declarations",
    "[object-observation]") {
    STATIC_REQUIRE(!std::is_same_v<ObjectObservationId, ObjectTrackId>);
    STATIC_REQUIRE(!std::is_same_v<ObjectTrackId, AssetId>);
    ObjectFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    const auto K_ADMITTED = fixture.inspect(K_RECEIPT);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->record().m_selection.m_observation == ObjectObservationId { 91 });
    CHECK(K_ADMITTED->record().m_selection.m_view.m_frame_pose_revision == MapRevisionId { 4 });
    CHECK(K_ADMITTED->coverage() == std::array<std::uint64_t, 4> { 2, 2, 2, 2 });
    CHECK(K_ADMITTED->calibration().m_camera.m_fx == 500);
    CHECK(K_ADMITTED->pose().m_submap_from_camera.m_translation.m_x == 1);
    CHECK(K_ADMITTED->world_from_camera().m_translation.m_x == 12.5);
    CHECK(K_ADMITTED->world_from_camera().m_translation.m_y == -25);
    CHECK(K_ADMITTED->world_from_camera().m_translation.m_z == 22.5);
    CHECK(K_ADMITTED->record().m_classes[0uz].m_confidence == 0.75);
    CHECK(!K_ADMITTED->record().m_classes[1uz].m_confidence.has_value());
    CHECK(K_ADMITTED->record().m_associations[1uz].m_decision ==
          ObjectAssociationDecision::REJECTED);
    CHECK(!K_ADMITTED->record().m_evidence[1uz].m_accepted);
    CHECK(K_ADMITTED->receipt_sha256() == K_RECEIPT.receipt_sha256());
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
    auto copy = *K_ADMITTED;
    fixture.m_map->m_poses.at({ 10 }).m_submap_from_camera.m_translation.m_x = 99;
    fixture.m_selection.m_observation = { 92 };
    std::filesystem::remove_all(fixture.m_region.m_directory.m_root);
    fixture.m_region.m_directory.m_root.clear();
    CHECK(copy.pose().m_submap_from_camera.m_translation.m_x == 1);
    CHECK(copy.record().m_selection.m_observation == ObjectObservationId { 91 });
    auto selection = K_ADMITTED->record().m_selection;
    const auto K_RETRY = inspect_object_observation(K_RECEIPT, selection, { { 3 }, { 5 } });
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->coverage() == K_ADMITTED->coverage());
    copy = *K_RETRY;
    CHECK(copy.receipt_sha256() == K_RECEIPT.receipt_sha256());
}

TEST_CASE("object records reject malformed rehashed identity decisions and evidence",
          "[object-observation]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    ObjectFixture fixture;
    auto& doc = fixture.m_record;
    switch (K_CASE) {
    case 0:
        doc["extra"] = 1;
        break;
    case 1:
        doc["schema_version"] = 2;
        break;
    case 2:
        doc["observation_id"] = 92;
        break;
    case 3:
        doc["revision"] = 1.0;
        break;
    case 4:
        doc["view"]["capture_id"] = 9;
        break;
    case 5:
        doc["view"]["frame_pose_revision"] = 5;
        break;
    case 6:
        doc["world_epoch"] = 4;
        break;
    case 7:
        doc["classes"][0uz]["confidence"] = -0.1;
        break;
    case 8:
        doc["classes"][0uz]["confidence"] = 1.01;
        break;
    case 9:
        doc["classes"][0uz]["label"] = "bad\nlabel";
        break;
    case 10:
        doc["classes"][1uz]["label"] = "chair";
        break;
    case 11:
        doc["associations"][1uz]["decision"] = "ACCEPTED";
        break;
    case 12:
        doc["associations"][1uz]["track_id"] = 42;
        break;
    case 13:
        doc["associations"][0uz]["track_id"] = 0;
        break;
    case 14:
        doc["associations"][0uz]["decision"] = "VERIFIED";
        break;
    case 15:
        doc["evidence"][0uz]["accepted"] = 1;
        break;
    case 16:
        doc["evidence"][1uz]["product"] = doc["evidence"][0uz]["product"];
        break;
    case 17:
        doc["evidence"][0uz]["reason"] = "arbitrary approval";
        break;
    case 18:
        doc["manual_ancestor"] = true;
        break;
    case 19:
        doc["recipe_sha256"] = std::string(64uz, 'f');
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("object mask partitions keep unknown occluded and background separate and reject invalid "
          "coverage",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    ObjectFixture fixture;
    auto& doc = fixture.m_mask;
    switch (K_CASE) {
    case 0:
        doc["runs"][0uz]["count"] = 0;
        break;
    case 1:
        doc["runs"][1uz]["first"] = 1;
        break;
    case 2:
        doc["runs"][1uz]["first"] = 3;
        break;
    case 3:
        doc["runs"][3uz]["count"] = 3;
        break;
    case 4:
        doc["runs"].erase(3uz);
        break;
    case 5:
        doc["runs"][0uz]["class"] = "GENERATED";
        break;
    case 6:
        doc["runs"][0uz]["confidence"] = 1.0;
        break;
    case 7:
        doc["runs"][0uz]["first"] = -1;
        break;
    case 8:
        doc["grid"]["width"] = 5;
        break;
    case 9:
        doc["view"]["frame_id"] = 11;
        break;
    case 10:
        doc["manual_ancestor"] = true;
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
}

TEST_CASE(
    "object byte admission rejects duplicate keys NUL excessive nesting and oversized records",
    "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    ObjectFixture fixture;
    const auto K_BASE = fixture.freeze();
    (void)K_BASE;
    auto raw = fixture.m_record.dump();
    switch (K_CASE) {
    case 0:
        raw.insert(1uz, "\"observation_id\":91,");
        break;
    case 1:
        raw.push_back('\0');
        break;
    case 2:
        raw = std::string(17uz, '[') + "0" + std::string(17uz, ']');
        break;
    case 3:
        raw.assign(256uz * 1024uz + 1uz, ' ');
        break;
    case 4:
        raw = "{broken";
        break;
    case 5:
        raw = "{}";
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_RECEIPT = fixture.freeze(raw);
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
}

TEST_CASE("object image grids use integer centers and contained crop resize support",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    ObjectFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.set_grid({ 320, 240, { 0.5, 0.5, -0.25, -0.25 } });
        break;
    case 1:
        fixture.set_grid({ 100, 50, { 1, 1, -10, -20 } });
        break;
    case 2:
        fixture.set_grid({ 4, 2, { 0.5, 0.5, 0, 0 } });
        break;
    case 3:
        fixture.set_grid({ 4, 2, { -1, 1, 0, 0 } });
        break;
    case 4:
        fixture.set_grid({ 4, 2, { 1, 1, 1, 0 } });
        break;
    case 5:
        fixture.set_grid({ 641, 480, { 1, 1, 0, 0 } });
        break;
    case 6:
        fixture.set_grid({ 4, 2, { std::numeric_limits<double>::min(), 1, 0, 0 } });
        break;
    case 7:
        fixture.set_grid({ 0, 2, { 1, 1, 0, 0 } });
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_PIXELS = fixture.m_selection.m_grid.m_width * fixture.m_selection.m_grid.m_height;
    if (K_PIXELS) {
        fixture.partition(K_PIXELS, 1uz);
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    if (K_CASE <= 1) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->coverage()[2uz] == K_PIXELS);
    } else {
        CHECK(!K_RESULT);
    }
}

TEST_CASE("object selection binds original image pose calibration and correction context",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    ObjectFixture fixture;
    if (K_CASE == 0) {
        fixture.m_map->m_invalidated_frames.insert({ 10 });
    }
    if (K_CASE == 1) {
        fixture.m_map->m_poses.at({ 10 }).m_tracking = TrackingState::LOST;
    }
    if (K_CASE == 2) {
        fixture.m_map->m_observations.at({ 10 }).m_image_sha256.assign(64uz, 'a');
    }
    if (K_CASE == 3) {
        fixture.m_map->m_revision = { 6 };
    }
    const auto K_RECEIPT = fixture.freeze();
    auto selection = fixture.m_selection;
    auto expected = fixture.m_region.m_graph.m_context;
    switch (K_CASE) {
    case 4:
        selection.m_view.m_frame = { 20 };
        break;
    case 5:
        selection.m_view.m_calibration = { 9 };
        break;
    case 6:
        selection.m_view.m_frame_pose_revision = { 5 };
        break;
    case 7:
        selection.m_mask.m_revision = 2;
        break;
    case 8:
        selection.m_evidence[1uz] = selection.m_evidence[0uz];
        break;
    case 9:
        expected.m_pose_revision = { 6 };
        break;
    default:
        break;
    }
    const auto K_RESULT = inspect_object_observation(K_RECEIPT, selection, expected);
    REQUIRE(!K_RESULT);
    CHECK((K_RESULT.error().m_code == ErrorCode::RESYNC_REQUIRED ||
           K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT));
}

TEST_CASE("object graph requires exact independently selected mask and evidence dependencies",
          "[object-observation]") {
    const auto K_MASK = GENERATE(false, true);
    ObjectFixture fixture;
    fixture.m_change_graph = [K_MASK](AssetDependencyGraph& graph) {
        graph.m_products[K_MASK ? 5uz : 6uz].m_dependencies.pop_back();
    };
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("rejected object evidence retains declared manual ancestry", "[object-observation]") {
    const auto K_CLEAR = GENERATE(false, true);
    ObjectFixture fixture;
    const auto K_ACTION = hash("synthetic manual rejection; not authenticated");
    fixture.m_region.m_data[K_ACTION] = "synthetic manual rejection; not authenticated";
    fixture.m_region.m_graph.m_products[8uz].m_manual_action_sha256 = K_ACTION;
    fixture.m_region.m_graph.m_products[8uz].m_manual_ancestor = true;
    fixture.m_region.m_graph.m_products[6uz].m_manual_ancestor = true;
    fixture.m_record["manual_ancestor"] = !K_CLEAR;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    if (K_CLEAR) {
        CHECK(!K_RESULT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->record().m_manual_ancestor);
        CHECK(!K_RESULT->record().m_evidence[1uz].m_accepted);
    }
}

TEST_CASE("object masks admit exact run and pixel budgets and refuse excess before ownership",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    ObjectFixture fixture;
    if (K_CASE <= 1) {
        fixture.set_grid({ 128, 129, { 1, 1, 0, 0 } });
        fixture.partition(128uz * 129uz, K_CASE ? 16385uz : 16384uz);
    } else {
        fixture.m_map->m_calibrations.at({ 8 }).m_width = 8192;
        fixture.m_map->m_calibrations.at({ 8 }).m_height = 4096;
        fixture.set_grid({ K_CASE == 2 ? 4096uz : 4097uz, 4096, { 1, 1, 0, 0 } });
        fixture.partition(fixture.m_selection.m_grid.m_width * 4096uz, 1uz);
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    if (K_CASE == 0 || K_CASE == 2) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->coverage()[2uz] ==
              fixture.m_selection.m_grid.m_width * fixture.m_selection.m_grid.m_height);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
}

TEST_CASE("object admission cancellation preserves selected source and returns no partial handle",
          "[object-observation]") {
    ObjectFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    std::stop_source stop;
    stop.request_stop();
    const auto K_RESULT = fixture.inspect(K_RECEIPT, stop.get_token());
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
    const auto K_AGAIN = fixture.inspect(K_RECEIPT);
    REQUIRE(K_AGAIN);
}

TEST_CASE("object class track and evidence arrays admit exact budgets and refuse excess",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    ObjectFixture fixture;
    fixture.m_record["classes"] = Json::array();
    fixture.m_record["associations"] = Json::array();
    for (std::size_t index { 0uz }; index < (K_CASE == 1 ? 17uz : 16uz); ++index) {
        fixture.m_record["classes"].push_back(
            { { "label", "class-" + std::to_string(index) }, { "confidence", nullptr } });
    }
    for (std::size_t index { 0uz }; index < (K_CASE == 2 ? 17uz : 16uz); ++index) {
        fixture.m_record["associations"].push_back(
            { { "track_id", index + 1uz }, { "decision", "HYPOTHESIS" }, { "confidence", 0.0 } });
    }
    for (std::size_t index { 2uz }; index < (K_CASE == 3 ? 49uz : 48uz); ++index) {
        auto node =
            fixture.m_region.node(6000uz + index, "synthetic evidence " + std::to_string(index));
        fixture.m_selection.m_evidence.push_back(node.m_product);
        fixture.m_record["evidence"].push_back({ { "product", region_ref(node.m_product) },
                                                { "accepted", false },
                                                { "reason", "UNSUPPORTED" } });
        fixture.m_region.m_graph.m_products.push_back(std::move(node));
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->record().m_evidence.size() == 48uz);
        CHECK(K_RESULT->record().m_associations.size() == 16uz);
        CHECK(K_RESULT->record().m_classes.size() == 16uz);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
}

TEST_CASE("object pose dependencies decode actual bytes with bounded frame counts",
          "[object-observation]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    ObjectFixture fixture;
    if (K_CASE == 0) {
        fixture.m_pose_bytes = "synthetic corrupt pose bytes";
    } else {
        const auto K_LIMIT = K_CASE == 1 ? 4096uz : 4097uz;
        for (std::size_t index { 1uz }; index < K_LIMIT; ++index) {
            auto frame = fixture.m_map->m_observations.at({ 10 });
            frame.m_id = { 100uz + index };
            auto pose = fixture.m_map->m_poses.at({ 10 });
            pose.m_frame = frame.m_id;
            fixture.m_map->m_observations.emplace(frame.m_id, frame);
            fixture.m_map->m_poses.emplace(pose.m_frame, pose);
        }
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    if (K_CASE == 1) {
        REQUIRE(K_RESULT);
    } else {
        REQUIRE(!K_RESULT);
    }
}
