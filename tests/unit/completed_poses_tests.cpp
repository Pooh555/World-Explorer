#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace world_explorer;
namespace {
std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
struct pose_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_pose_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string decode_key = std::string(64, 'a'), pose_key = std::string(64, 'c');
    std::filesystem::path decoded = root / "stages" / ("decode-" + decode_key);
    std::filesystem::path posed = root / "stages" / ("poses-" + pose_key);
    std::string source_digest;
    nlohmann::json canonical;
    pose_fixture() {
        std::filesystem::create_directories(decoded / "images");
        std::filesystem::create_directories(posed);
        std::ofstream(root / "source.mp4") << "synthetic diagnostic; not a video";
        source_digest = *sha256_file(root / "source.mp4", 4096);
        constexpr std::array<unsigned char, 72> PNG{
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130};
        nlohmann::json frames = nlohmann::json::array(), poses = nlohmann::json::array();
        for (int index = 0; index < 3; ++index) {
            const auto name = "images/" + std::to_string(index) + ".png";
            std::ofstream image(decoded / name, std::ios::binary);
            image.write(reinterpret_cast<const char*>(PNG.data()),
                        static_cast<std::streamsize>(PNG.size()));
            image.close();
            const auto external = source_digest.substr(0, 16) + "-" + std::to_string(index * 10);
            const auto calibration = source_digest.substr(0, 16) + "-lens-0";
            frames.push_back(
                {{"file", name},
                 {"sha256", *sha256_file(decoded / name, 4096)},
                 {"frame_id", external},
                 {"calibration_id", calibration},
                 {"source_index", index * 10},
                 {"pts", index * 10},
                 {"time_base", "1/30"},
                 {"capture_time_ns", static_cast<std::int64_t>(index) * 1000000000 / 3}});
            poses.push_back({{"image_id", index + 1},
                             {"backend_rig_frame_id", index + 1},
                             {"frame_id", external},
                             {"calibration_id", calibration},
                             {"camera_id", 1},
                             {"pose_revision", 1},
                             {"state", "final"},
                             {"pts", index * 10},
                             {"time_base", "1/30"},
                             {"world_from_camera",
                              {{"translation", {1. + index, 2. - index, 3. + index}},
                               {"rotation_wxyz", {0.5, 0.5, 0.5, 0.5}}}}});
        }
        std::ofstream(decoded / "result.json") << nlohmann::json{
            {"selection", "timestamp_sampling_v1"},
            {"crop", "none"},
            {"resize", "none"},
            {"source_frame_count", 21},
            {"frames", frames}}.dump();
        const auto camera =
            nlohmann::json{{"camera_id", 1},
                           {"revision", 1},
                           {"model", "OPENCV"},
                           {"width", 2},
                           {"height", 1},
                           {"backend_params_centers", "half_pixel"},
                           {"canonical_pixel_centers", "integer"},
                           {"calibration_matrix", {{711., 0., .25}, {0., 529., .75}, {0., 0., 1.}}},
                           {"backend_params", {711., 529., .75, 1.25, .1, -.2, .003, -.004}}};
        canonical = {{"schema_version", 1},
                     {"epoch", 0},
                     {"revision", 1},
                     {"scale_status", "ARBITRARY"},
                     {"gravity", "unknown"},
                     {"world_up", "+y"},
                     {"backend_world_rotation_wxyz", {0, 1, 0, 0}},
                     {"registered_frames", 3},
                     {"points", 20},
                     {"mean_reprojection_error", .6},
                     {"cameras", nlohmann::json::array({camera})},
                     {"poses", poses}};
        std::ofstream(posed / "result.json")
            << nlohmann::json{{"canonical_poses", "canonical_poses.json"},
                              {"registered_frames", 3},
                              {"scale_status", "ARBITRARY"},
                              {"geometry", "UNAVAILABLE"}}
                   .dump();
        std::ofstream(root / "decode.json")
            << nlohmann::json{{"key", decode_key}, {"output", path_utf8(decoded)}}.dump();
        std::ofstream(root / "poses.json")
            << nlohmann::json{{"key", pose_key}, {"output", path_utf8(posed)}}.dump();
        std::ofstream(root / "result.json") << nlohmann::json{
            {"schema_version", 1},
            {"status", "complete"},
            {"stage", "poses"},
            {"source_sha256", source_digest},
            {"output", path_utf8(posed)}}.dump();
        publish();
    }
    ~pose_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void inventory(const std::filesystem::path& directory) const {
        auto files = nlohmann::json::array();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().filename() == "completion.json")
                continue;
            files.push_back({{"path", path_utf8(entry.path().lexically_relative(directory))},
                             {"bytes", entry.file_size()},
                             {"sha256", *sha256_file(entry.path(), std::size_t{1024} * 1024)}});
        }
        std::ofstream(directory / "completion.json") << nlohmann::json{
            {"schema_version", 1},
            {"status", "complete"},
            {"key", directory == decoded ? decode_key : pose_key},
            {"files", files}}.dump();
    }
    void publish() const {
        std::ofstream(posed / "canonical_poses.json") << canonical.dump();
        inventory(decoded);
        inventory(posed);
    }
    result<std::shared_ptr<const completed_pose_dataset>> load() const {
        return load_completed_poses({root, source_digest});
    }
};
frame_observation observation(const completed_pose_dataset& dataset, std::size_t index) {
    const auto& record = dataset.observations[index];
    frame_packet frame{record.id,
                       record.capture,
                       record.capture_time,
                       record.timestamp,
                       record.calibration,
                       {std::make_shared<const std::vector<std::byte>>(6, std::byte{17}), 2, 1, 6,
                        pixel_format::RGB8},
                       {},
                       {}};
    return {{index + 1}, std::move(frame)};
}
} // namespace
TEST_CASE("completed pose import preserves calibration model coefficients optical poses and IDs") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    CHECK((*dataset)->observations.size() == 3);
    CHECK((*dataset)->poses.size() == 3);
    CHECK((*dataset)->scale == scale_status::ARBITRARY);
    const auto& calibration = (*dataset)->calibrations.front();
    CHECK(calibration.model == calibration_model::OPENCV);
    CHECK(calibration.distortion == std::array{.1, -.2, .003, -.004});
    CHECK(calibration.camera.cx == .25);
    CHECK(calibration.camera.cy == .75);
    CHECK(calibration.image_transform == std::array{1., 1., 0., 0.});
    const auto& pose = (*dataset)->poses.front();
    CHECK(pose.submap_from_camera.translation == vec3d{1, 2, 3});
    CHECK(transform_point(pose.submap_from_camera, {1, 0, 0}) == vec3d{1, 3, 3});
    CHECK_FALSE(pose.covariance);
    CHECK(pose.capture_time == (*dataset)->observations.front().capture_time);
    const auto again = fixture.load();
    REQUIRE(again);
    CHECK((*again)->submap == (*dataset)->submap);
    CHECK((*again)->observations.front().id == (*dataset)->observations.front().id);
}
TEST_CASE("completed pose import rejects corrupt assets and ambiguous camera or coordinate data") {
    pose_fixture fixture;
    SECTION("unknown model") {
        fixture.canonical["cameras"][0]["model"] = "OPENCV_FISHEYE";
        fixture.publish();
    }
    SECTION("principal point mismatch") {
        fixture.canonical["cameras"][0]["backend_params"][2] = 0;
        fixture.publish();
    }
    SECTION("quaternion normalization") {
        fixture.canonical["poses"][0]["world_from_camera"]["rotation_wxyz"] = {2, 0, 0, 0};
        fixture.publish();
    }
    SECTION("unknown frame") {
        fixture.canonical["poses"][0]["frame_id"] = "unknown";
        fixture.publish();
    }
    SECTION("duplicate frame") {
        fixture.canonical["poses"][1] = fixture.canonical["poses"][0];
        fixture.publish();
    }
    SECTION("timestamp mismatch") {
        fixture.canonical["poses"][1]["pts"] = 11;
        fixture.publish();
    }
    SECTION("wrong coordinate profile") {
        fixture.canonical["world_up"] = "-y";
        fixture.publish();
    }
    SECTION("corrupt image") { std::ofstream(fixture.decoded / "images/0.png") << "changed"; }
    SECTION("corrupt canonical asset") {
        std::ofstream(fixture.posed / "canonical_poses.json") << "{}";
    }
    SECTION("uncommitted asset") { std::ofstream(fixture.posed / "extra") << "x"; }
    SECTION("source mismatch") { fixture.source_digest = std::string(64, 'd'); }
    SECTION("missing inventory") { std::filesystem::remove(fixture.posed / "completion.json"); }
    CHECK_FALSE(fixture.load());
}
TEST_CASE("completed replay enforces bounded admission retry identity and owned immutable events") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    auto mutable_dataset = std::make_shared<completed_pose_dataset>(**dataset);
    const std::shared_ptr<const completed_pose_dataset> input = mutable_dataset;
    auto replay = make_completed_pose_replay(input);
    REQUIRE(replay);
    mutable_dataset->poses.front().submap_from_camera.translation = {100, 200, 300};
    CHECK((*replay)->capabilities().mode == estimator_mode::COMPLETED_REPLAY);
    CHECK_FALSE((*replay)->capabilities().streaming_tracking);
    CHECK_FALSE((*replay)->capabilities().covariance);
    CHECK_FALSE((*replay)->finish());
    REQUIRE((*replay)->start({{std::size_t{1024} * 1024}, 1}));
    CHECK((*replay)->capabilities().observation_capacity == 1);
    CHECK_FALSE((*replay)->start({{std::size_t{1024} * 1024}, 1}));
    auto first = observation(**dataset, 0);
    const auto lease = first.frame.pixels.bytes;
    const auto admitted = (*replay)->submit(first);
    REQUIRE(admitted);
    CHECK_FALSE(first.frame.pixels.bytes);
    REQUIRE((*replay)->submit(first));
    auto retry = observation(**dataset, 0);
    const auto repeated = (*replay)->submit(retry);
    REQUIRE(repeated);
    CHECK(*repeated == *admitted);
    CHECK_FALSE(retry.frame.pixels.bytes);
    auto conflicting = observation(**dataset, 0);
    conflicting.frame.pixels.bytes =
        std::make_shared<const std::vector<std::byte>>(6, std::byte{18});
    const auto conflict = (*replay)->submit(conflicting);
    REQUIRE_FALSE(conflict);
    CHECK(conflict.error().code == error_code::DUPLICATE_OPERATION);
    CHECK(conflicting.frame.pixels.bytes);
    auto second = observation(**dataset, 1);
    const auto retained = second.frame.pixels.bytes;
    const auto full = (*replay)->submit(second);
    REQUIRE_FALSE(full);
    CHECK(full.error().code == error_code::WOULD_BLOCK);
    CHECK(second.frame.pixels.bytes == retained);
    const auto zero = (*replay)->drain_events({0});
    REQUIRE(zero);
    CHECK(zero->empty());
    const auto initial = (*replay)->drain_events({3});
    REQUIRE(initial);
    CHECK(initial->size() == 3);
    CHECK(std::holds_alternative<map_alignment>(initial->back()));
    const auto event = (*replay)->drain_events({1});
    REQUIRE(event);
    REQUIRE(event->size() == 1);
    CHECK(std::get<pose_estimate>(event->front()).frame == first.frame.id);
    REQUIRE((*replay)->submit(second));
    REQUIRE((*replay)->drain_events({1}));
    auto third = observation(**dataset, 2);
    REQUIRE((*replay)->submit(third));
    REQUIRE((*replay)->finish());
    REQUIRE((*replay)->finish());
    const auto terminal = (*replay)->drain_events({1000000});
    REQUIRE(terminal);
    CHECK(terminal->size() == 3);
    CHECK(std::holds_alternative<estimator_finished>(terminal->back()));
    const auto empty = (*replay)->drain_events({10});
    REQUIRE(empty);
    CHECK(empty->empty());
    replay->reset();
    CHECK(lease->size() == 6);
    CHECK(std::get<pose_estimate>(event->front()).submap_from_camera.translation == vec3d{1, 2, 3});
}
TEST_CASE("completed pose import requires explicit selection when the decode pointer is stale") {
    pose_fixture fixture;
    std::ofstream(fixture.root / "decode.json")
        << nlohmann::json{{"key", std::string(64, 'd')},
                          {"output",
                           path_utf8(fixture.root / "stages" / ("decode-" + std::string(64, 'd')))}}
               .dump();
    CHECK_FALSE(fixture.load());
    const auto dataset =
        load_completed_poses({fixture.root, fixture.source_digest, fixture.decoded});
    REQUIRE(dataset);
    CHECK((*dataset)->observations.size() == 3);
    nlohmann::json pointer;
    std::ifstream(fixture.root / "decode.json") >> pointer;
    CHECK(pointer.at("key") == std::string(64, 'd'));
}
TEST_CASE("completed replay preserves failed observations and supports concurrent cancellation") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    auto replay = make_completed_pose_replay(*dataset);
    REQUIRE(replay);
    auto frame = observation(**dataset, 0);
    const auto pixels = frame.frame.pixels.bytes;
    SECTION("before start") { CHECK_FALSE((*replay)->submit(frame)); }
    SECTION("malformed metadata") {
        REQUIRE((*replay)->start({{std::size_t{1024} * 1024}, 1}));
        frame.frame.capture_time += std::chrono::nanoseconds(1);
        CHECK_FALSE((*replay)->submit(frame));
    }
    SECTION("memory budget") {
        REQUIRE((*replay)->start({{std::size_t{256} * 1024 + 1}, 1}));
        const auto result = (*replay)->submit(frame);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::RESOURCE_EXHAUSTED);
    }
    SECTION("cancellation") {
        REQUIRE((*replay)->start({{std::size_t{1024} * 1024}, 1}));
        std::jthread stopper([&] { (*replay)->request_stop(); });
        stopper.join();
        const auto result = (*replay)->submit(frame);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        CHECK_FALSE((*replay)->drain_events({1}));
        CHECK_FALSE((*replay)->finish());
    }
    CHECK(frame.frame.pixels.bytes == pixels);
}
TEST_CASE("map transactions retain distortion and reject calibration operation collisions") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    map_transaction transaction;
    transaction.operation = {1};
    transaction.new_revision = {1};
    transaction.calibrations = (*dataset)->calibrations;
    transaction.observations = (*dataset)->observations;
    transaction.poses = (*dataset)->poses;
    transaction.alignments.push_back({(*dataset)->submap, {1}, {}});
    map_store maps;
    REQUIRE(maps.commit(transaction));
    REQUIRE(maps.commit(transaction));
    const auto before = maps.acquire_snapshot();
    const auto id = transaction.calibrations.front().id;
    CHECK(before->calibrations.at(id).distortion == std::array{.1, -.2, .003, -.004});
    transaction.calibrations.front().distortion[0] = .2;
    const auto collision = maps.commit(transaction);
    REQUIRE_FALSE(collision);
    CHECK(collision.error().code == error_code::DUPLICATE_OPERATION);
    CHECK(maps.acquire_snapshot() == before);
    CHECK(before->poses.size() == 3);
    map_transaction bad_pose;
    bad_pose.operation = {2};
    bad_pose.base_revision = {1};
    bad_pose.new_revision = {2};
    auto pose = before->poses.begin()->second;
    pose.revision = {2};
    pose.capture_time += std::chrono::nanoseconds(1);
    bad_pose.poses.push_back(pose);
    CHECK_FALSE(maps.commit(bad_pose));
    CHECK(maps.acquire_snapshot() == before);
}
