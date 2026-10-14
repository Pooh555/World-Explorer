#include "pose_fixture.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <thread>

using namespace world_explorer;
namespace {
using test::path_utf8;
using test::pose_fixture;
constexpr std::size_t REPLAY_METADATA_BYTES = std::size_t{256} * 1024;
constexpr std::size_t REPLAY_MEMORY_BYTES = REPLAY_METADATA_BYTES + 6;
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
// Entirely in-memory diagnostic input; these hashes do not identify a real capture.
std::shared_ptr<completed_pose_dataset> replay_dataset(std::size_t count = 3) {
    auto dataset = std::make_shared<completed_pose_dataset>();
    dataset->revision = {1};
    dataset->submap = {55};
    dataset->source_sha256 = std::string(64, 'a');
    dataset->pose_completion_sha256 = std::string(64, 'b');
    dataset->decode_completion_sha256 = std::string(64, 'c');
    dataset->decode_directory = "synthetic-unused-decode";
    dataset->calibrations = {
        {{7}, {711, 529, .25, .75}, 2, 1, calibration_model::OPENCV, {.1, -.2, .003, -.004}}};
    for (std::size_t index = 0; index < count; ++index) {
        const auto time = std::chrono::nanoseconds{static_cast<std::int64_t>(index) * 333333333};
        dataset->observations.push_back({{index + 1},
                                         {9},
                                         {7},
                                         {static_cast<std::int64_t>(index) * 10 - 7, 1001, 30000},
                                         time,
                                         std::string(64, 'd')});
        dataset->poses.push_back({{index + 1},
                                  {55},
                                  {{.5, .5, .5, .5}, {static_cast<double>(index), -2, 3}, 1},
                                  time,
                                  {1},
                                  tracking_state::TRACKING,
                                  {}});
    }
    return dataset;
}
void check_frame_preserved(const frame_observation& actual, const frame_packet& expected,
                           operation_id operation) {
    CHECK(actual.operation == operation);
    const auto& frame = actual.frame;
    CHECK(frame.id == expected.id);
    CHECK(frame.capture == expected.capture);
    CHECK(frame.calibration == expected.calibration);
    CHECK(frame.capture_time == expected.capture_time);
    CHECK(frame.original_timestamp.pts == expected.original_timestamp.pts);
    CHECK(frame.original_timestamp.time_base_numerator ==
          expected.original_timestamp.time_base_numerator);
    CHECK(frame.original_timestamp.time_base_denominator ==
          expected.original_timestamp.time_base_denominator);
    CHECK(frame.pixels.bytes == expected.pixels.bytes);
    CHECK(frame.pixels.width == expected.pixels.width);
    CHECK(frame.pixels.height == expected.pixels.height);
    CHECK(frame.pixels.row_stride == expected.pixels.row_stride);
    CHECK(frame.pixels.format == expected.pixels.format);
    CHECK(frame.measured_depth.has_value() == expected.measured_depth.has_value());
    if (frame.measured_depth && expected.measured_depth) {
        CHECK(frame.measured_depth->bytes == expected.measured_depth->bytes);
        CHECK(frame.measured_depth->width == expected.measured_depth->width);
        CHECK(frame.measured_depth->height == expected.measured_depth->height);
        CHECK(frame.measured_depth->row_stride == expected.measured_depth->row_stride);
        CHECK(frame.measured_depth->format == expected.measured_depth->format);
    }
    REQUIRE(frame.imu_samples.size() == expected.imu_samples.size());
    for (std::size_t index = 0; index < frame.imu_samples.size(); ++index) {
        CHECK(frame.imu_samples[index].capture_time == expected.imu_samples[index].capture_time);
        CHECK(frame.imu_samples[index].acceleration == expected.imu_samples[index].acceleration);
        CHECK(frame.imu_samples[index].angular_velocity ==
              expected.imu_samples[index].angular_velocity);
    }
}
pose_event single_replay_event(pose_estimator& replay) {
    const auto events = replay.drain_events({1});
    REQUIRE(events);
    REQUIRE(events->size() == 1);
    return events->front();
}
void check_replay_after_first(pose_estimator& replay, const completed_pose_dataset& dataset) {
    for (std::size_t index = 1; index < dataset.observations.size(); ++index) {
        auto next = observation(dataset, index);
        const auto admitted = replay.submit(next);
        REQUIRE(admitted);
        CHECK(admitted->value == index + 1);
        CHECK(!next.frame.pixels.bytes);
    }
    REQUIRE(replay.finish());
    REQUIRE(replay.finish());
    const auto events = replay.drain_events({std::numeric_limits<std::size_t>::max()});
    REQUIRE(events);
    REQUIRE(events->size() == dataset.observations.size() + 5);
    REQUIRE(std::holds_alternative<tracking_change>((*events)[0]));
    CHECK(std::get<tracking_change>((*events)[0]).state == tracking_state::INITIALIZING);
    REQUIRE(std::holds_alternative<submap_created>((*events)[1]));
    CHECK(std::get<submap_created>((*events)[1]).submap == dataset.submap);
    REQUIRE(std::holds_alternative<map_alignment>((*events)[2]));
    const auto alignment = std::get<map_alignment>((*events)[2]);
    CHECK(alignment.submap == dataset.submap);
    CHECK(alignment.revision == dataset.revision);
    CHECK(alignment.world_from_submap.translation == vec3d{});
    CHECK(alignment.world_from_submap.rotation.w == 1);
    CHECK(alignment.world_from_submap.scale == 1);
    for (std::size_t index = 0; index < dataset.observations.size(); ++index) {
        const auto found =
            std::find_if(dataset.poses.begin(), dataset.poses.end(), [&](const auto& pose) {
                return pose.frame == dataset.observations[index].id;
            });
        const auto& event = (*events)[index + 3];
        if (found == dataset.poses.end()) {
            REQUIRE(std::holds_alternative<tracking_change>(event));
            CHECK(std::get<tracking_change>(event).state == tracking_state::LOST);
        } else {
            REQUIRE(std::holds_alternative<pose_estimate>(event));
            const auto& pose = std::get<pose_estimate>(event);
            CHECK(pose.frame == found->frame);
            CHECK(pose.submap == found->submap);
            CHECK(pose.revision == found->revision);
            CHECK(pose.capture_time == found->capture_time);
            CHECK(pose.tracking == tracking_state::TRACKING);
            CHECK(!pose.covariance);
            CHECK(pose.submap_from_camera.translation == found->submap_from_camera.translation);
            CHECK(pose.submap_from_camera.rotation.w == found->submap_from_camera.rotation.w);
            CHECK(pose.submap_from_camera.rotation.x == found->submap_from_camera.rotation.x);
            CHECK(pose.submap_from_camera.rotation.y == found->submap_from_camera.rotation.y);
            CHECK(pose.submap_from_camera.rotation.z == found->submap_from_camera.rotation.z);
            CHECK(pose.submap_from_camera.scale == 1);
        }
    }
    REQUIRE(std::holds_alternative<tracking_change>((*events)[events->size() - 2]));
    CHECK(std::get<tracking_change>((*events)[events->size() - 2]).state ==
          tracking_state::FINISHED);
    CHECK(std::holds_alternative<estimator_finished>(events->back()));
    const auto empty = replay.drain_events({1});
    REQUIRE(empty);
    CHECK(empty->empty());
}
} // namespace
namespace {
batch_pose_config batch_config(const pose_fixture& fixture, const std::string& mode = "failure") {
    const auto script = fixture.root / "diagnostic batch worker.py";
    std::ofstream output(script);
    output
        << "import sys,time,json\nfrom pathlib import Path\n"
        << "sys.path.insert(0," << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump() << ")\n"
        << "import we_protocol\n"
        << "we_protocol.configure(sys.argv,backend='synthetic_failure_fixture',backend_version='1',"
           "sensors=['video'],devices=['cpu_poses'],checkpoint_semantics='diagnostic',"
           "checkpoint_rights='synthetic',limits={'max_frames':120,'max_source_bytes':17179869184,'"
           "max_output_bytes':34359738368})\n"
        << "request=json.loads(Path(sys.argv[sys.argv.index('--request')+1]).read_text())\n"
        << "root=Path(request['job_dir'])\n"
        << "with (root/'launches').open('a') as f: f.write('launch\\n')\n";
    if (mode == "wait")
        output << "time.sleep(30)\n";
    output << "we_protocol.failure('DIAGNOSTIC_FAILURE','poses','synthetic failure; no solver')\n"
              "sys.exit(2)\n";
    return {.decode_stage = fixture.decoded,
            .job_directory = fixture.root / "fresh job",
            .worker_script = script,
            .source_sha256 = fixture.source_digest,
            .worker_python = WE_TEST_PYTHON,
            .pose_python = WE_TEST_PYTHON};
}
} // namespace
TEST_CASE("batch admission writes RGB before consuming leases and bounds memory disk and order") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    const auto config = batch_config(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    CHECK((*batch)->capabilities().mode == estimator_mode::BATCH_SOLVE);
    CHECK_FALSE((*batch)->capabilities().streaming_tracking);
    CHECK_FALSE((*batch)->start({{std::size_t{4} * 1024 * 1024}, 2}));
    CHECK_FALSE(std::filesystem::exists(config.job_directory));
    REQUIRE((*batch)->start({{std::size_t{4} * 1024 * 1024}, 3}));
    CHECK_FALSE((*batch)->acquire_dataset());
    CHECK_FALSE((*batch)->finish());
    auto wrong_order = observation(**dataset, 1);
    CHECK_FALSE((*batch)->submit(wrong_order));
    CHECK(wrong_order.frame.pixels.bytes);
    auto first = observation(**dataset, 0);
    auto retry = first;
    auto retained = first.frame.pixels.bytes;
    std::filesystem::create_directory(config.job_directory / "input/1.ppm.tmp");
    CHECK_FALSE((*batch)->submit(first));
    CHECK(first.frame.pixels.bytes);
    CHECK(std::filesystem::is_directory(config.job_directory / "input/1.ppm.tmp"));
    std::filesystem::remove(config.job_directory / "input/1.ppm.tmp");
    const auto ticket = (*batch)->submit(first);
    REQUIRE(ticket);
    CHECK_FALSE(first.frame.pixels.bytes);
    CHECK(retained->size() == 6);
    std::ifstream input(config.job_directory / "input/1.ppm", std::ios::binary);
    std::string bytes(std::istreambuf_iterator<char>{input}, {});
    CHECK(bytes == std::string("P6\n2 1\n255\n") + std::string(6, char{17}));
    REQUIRE((*batch)->submit(first));
    CHECK((*batch)->submit(retry)->value == ticket->value);
    auto conflict = observation(**dataset, 0);
    conflict.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(6, std::byte{18});
    const auto rejected = (*batch)->submit(conflict);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::DUPLICATE_OPERATION);
    CHECK(conflict.frame.pixels.bytes);
    auto second = observation(**dataset, 1);
    auto wrong_metadata = second;
    wrong_metadata.frame.capture_time += std::chrono::nanoseconds{1};
    CHECK_FALSE((*batch)->submit(wrong_metadata));
    CHECK(wrong_metadata.frame.pixels.bytes);
    REQUIRE((*batch)->submit(second));
    const auto initial = (*batch)->drain_events({1000000});
    REQUIRE(initial);
    REQUIRE(initial->size() == 1);
    CHECK(std::get<tracking_change>(initial->front()).state == tracking_state::INITIALIZING);
    CHECK((*batch)->drain_events({1})->empty());

    auto disk_config = config;
    disk_config.job_directory = fixture.root / "disk limited";
    disk_config.journal_bytes = {std::size_t{2} * 1024 * 1024 + bytes.size()};
    auto disk = make_batch_pose_estimator(disk_config);
    REQUIRE(disk);
    REQUIRE((*disk)->start({{std::size_t{4} * 1024 * 1024}, 3}));
    auto disk_first = observation(**dataset, 0);
    REQUIRE((*disk)->submit(disk_first));
    auto disk_second = observation(**dataset, 1);
    const auto full = (*disk)->submit(disk_second);
    REQUIRE_FALSE(full);
    CHECK(full.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(disk_second.frame.pixels.bytes);
    CHECK_FALSE(std::filesystem::exists(disk_config.job_directory / "input/2.ppm"));

    auto memory_config = config;
    memory_config.job_directory = fixture.root / "memory limited";
    auto memory = make_batch_pose_estimator(memory_config);
    REQUIRE(memory);
    REQUIRE((*memory)->start({{std::size_t{2} * 1024 * 1024 + 5}, 3}));
    auto memory_first = observation(**dataset, 0);
    const auto exhausted = (*memory)->submit(memory_first);
    REQUIRE_FALSE(exhausted);
    CHECK(exhausted.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(memory_first.frame.pixels.bytes);
}
TEST_CASE("batch worker failure retains a checked journal and finish does not relaunch") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    const auto config = batch_config(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start({{std::size_t{4} * 1024 * 1024}, 3}));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**dataset, index);
        REQUIRE((*batch)->submit(frame));
    }
    const auto failed = (*batch)->finish();
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == error_code::BACKEND_FAILURE);
    CHECK(failed.error().message.find("DIAGNOSTIC_FAILURE") != std::string::npos);
    CHECK_FALSE((*batch)->finish());
    CHECK_FALSE((*batch)->drain_events({1}));
    CHECK_FALSE((*batch)->acquire_dataset());
    std::ifstream launches(config.job_directory / "launches");
    std::string line;
    REQUIRE(std::getline(launches, line));
    CHECK_FALSE(std::getline(launches, line));
    std::ifstream manifest(config.job_directory / "input/journal.json");
    const auto journal = nlohmann::json::parse(manifest);
    REQUIRE(journal.at("frames").size() == 3);
    CHECK(journal.at("source_sha256") == fixture.source_digest);
    for (const auto& frame : journal.at("frames")) {
        CHECK(frame.at("pixel_sha256") == sha256(std::vector<std::byte>(6, std::byte{17})));
        CHECK(frame.at("sha256") ==
              *sha256_file(config.job_directory / "input" / frame.at("file").get<std::string>(),
                           4096));
    }
    CHECK_FALSE(std::filesystem::exists(config.job_directory / "result.json"));
    auto duplicate = make_batch_pose_estimator(config);
    REQUIRE(duplicate);
    const auto occupied = (*duplicate)->start({{std::size_t{4} * 1024 * 1024}, 3});
    REQUIRE_FALSE(occupied);
    CHECK(occupied.error().code == error_code::WOULD_BLOCK);
    CHECK(std::filesystem::exists(config.job_directory / "input/journal.json"));
}
TEST_CASE("batch cancellation interrupts isolated finish without publishing success") {
    pose_fixture fixture;
    const auto dataset = fixture.load();
    REQUIRE(dataset);
    const auto config = batch_config(fixture, "wait");
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start({{std::size_t{4} * 1024 * 1024}, 3}));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**dataset, index);
        REQUIRE((*batch)->submit(frame));
    }
    result<void> finished;
    std::jthread running([&] { finished = (*batch)->finish(); });
    for (int iteration = 0;
         iteration < 200 && !std::filesystem::exists(config.job_directory / "launches");
         ++iteration)
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    const auto start = std::chrono::steady_clock::now();
    (*batch)->request_stop();
    running.join();
    REQUIRE_FALSE(finished);
    CHECK(finished.error().code == error_code::CANCELLED);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{3});
    CHECK_FALSE(std::filesystem::exists(config.job_directory / "result.json"));
    CHECK(std::filesystem::exists(config.job_directory / "input/journal.json"));
    auto retry = observation(**dataset, 0);
    CHECK_FALSE((*batch)->submit(retry));
    CHECK(retry.frame.pixels.bytes);
}

TEST_CASE("owned batch resume verifies admitted RGB and preserves caller data on mismatch",
          "[stage-chain]") {
    pose_fixture fixture;
    const auto dataset = fixture.load().value();
    auto config = batch_config(fixture);
    auto initial = make_batch_pose_estimator(config).value();
    REQUIRE(initial->start({{std::size_t{4} * 1024 * 1024}, 3}));
    auto first = observation(*dataset, 0);
    REQUIRE(initial->submit(first));
    const auto asset = config.job_directory / "input/1.ppm";
    const auto hash = sha256_file(asset, 4096).value();
    initial.reset();
    config.resume_input = true;
    SECTION("identical replay") {
        auto resumed = make_batch_pose_estimator(config).value();
        REQUIRE(resumed->start({{std::size_t{4} * 1024 * 1024}, 3}));
        auto replay = observation(*dataset, 0);
        REQUIRE(resumed->submit(replay));
        CHECK_FALSE(replay.frame.pixels.bytes);
        CHECK(sha256_file(asset, 4096).value() == hash);
    }
    SECTION("different content") {
        std::ofstream(asset) << "changed RGB";
        const auto retained = sha256_file(asset, 4096).value();
        auto resumed = make_batch_pose_estimator(config).value();
        REQUIRE(resumed->start({{std::size_t{4} * 1024 * 1024}, 3}));
        auto replay = observation(*dataset, 0);
        CHECK_FALSE(resumed->submit(replay));
        CHECK(replay.frame.pixels.bytes);
        CHECK(sha256_file(asset, 4096).value() == retained);
    }
    SECTION("uncommitted temporary input") {
        std::ofstream(config.job_directory / "input/2.ppm.tmp") << "partial";
        auto resumed = make_batch_pose_estimator(config).value();
        CHECK_FALSE(resumed->start({{std::size_t{4} * 1024 * 1024}, 3}));
        CHECK(sha256_file(asset, 4096).value() == hash);
    }
}
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
TEST_CASE("replay factory rejects unsupported profiles and malformed producer identities",
          "[replay_admission][dataset]") {
    const auto original = replay_dataset();
    auto candidate = std::make_shared<completed_pose_dataset>(*original);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(field);
    switch (field) {
    case 0:
        candidate.reset();
        break;
    case 1:
        candidate->source_sha256.pop_back();
        break;
    case 2:
        candidate->source_sha256[0] = 'G';
        break;
    case 3:
        candidate->pose_completion_sha256.pop_back();
        break;
    case 4:
        candidate->pose_completion_sha256[0] = 'G';
        break;
    case 5:
        candidate->decode_completion_sha256.pop_back();
        break;
    case 6:
        candidate->decode_completion_sha256[0] = 'G';
        break;
    case 7:
        candidate->decode_directory = std::string(4097, 'x');
        break;
    case 8:
        candidate->calibrations.clear();
        break;
    case 9:
        candidate->calibrations.push_back(candidate->calibrations[0]);
        break;
    case 10:
        candidate->calibrations[0].width = 0;
        break;
    case 11:
        candidate->calibrations[0].width = 8193;
        break;
    case 12:
        candidate->calibrations[0].height = 8193;
        break;
    case 13:
        candidate->submap = {};
        break;
    case 14:
        candidate->epoch = {1};
        break;
    case 15:
        candidate->revision = {};
        break;
    case 16:
        candidate->revision = {2};
        break;
    case 17:
        candidate->scale = scale_status::METRIC_MEASURED;
        break;
    default:
        FAIL("unknown replay dataset mutation");
    }
    const auto rejected = make_completed_pose_replay(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    if (candidate)
        *candidate = *original;
    else
        candidate = std::make_shared<completed_pose_dataset>(*original);
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto first = observation(*original, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *original);
}

TEST_CASE("replay factory enforces selection counts and the unchanged registration boundary",
          "[replay_admission][limits]") {
    auto candidate = replay_dataset();
    const auto field = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(field);
    if (field == 0)
        candidate->observations.resize(2);
    else if (field == 1)
        candidate = replay_dataset(121);
    else if (field == 2)
        candidate->poses.resize(2);
    else if (field == 3)
        candidate->poses.push_back(candidate->poses[0]);
    else {
        candidate = replay_dataset(10);
        candidate->poses.resize(8);
    }
    const auto observations = candidate->observations.size();
    const auto poses = candidate->poses.size();
    const auto rejected = make_completed_pose_replay(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(candidate->observations.size() == observations);
    CHECK(candidate->poses.size() == poses);
    CHECK(candidate->scale == scale_status::ARBITRARY);
}

TEST_CASE("replay factory rejects invalid duplicate and mixed-capture observations",
          "[replay_admission][observations]") {
    const auto original = replay_dataset();
    auto candidate = std::make_shared<completed_pose_dataset>(*original);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    auto& frame = candidate->observations[1];
    switch (field) {
    case 0:
        frame.id = {};
        break;
    case 1:
        frame.capture = {};
        break;
    case 2:
        frame.calibration = {999};
        break;
    case 3:
        frame.capture_time = std::chrono::nanoseconds{-1};
        break;
    case 4:
        frame.timestamp.time_base_numerator = 0;
        break;
    case 5:
        frame.timestamp.time_base_numerator = -1;
        break;
    case 6:
        frame.timestamp.time_base_denominator = 0;
        break;
    case 7:
        frame.timestamp.time_base_denominator = -1;
        break;
    case 8:
        frame.image_sha256.pop_back();
        break;
    case 9:
        frame.image_sha256[63] = 'G';
        break;
    case 10:
        frame.id = candidate->observations[0].id;
        break;
    case 11:
        frame.capture = {10};
        break;
    default:
        FAIL("unknown replay observation mutation");
    }
    const auto rejected = make_completed_pose_replay(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    candidate->observations = original->observations;
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto first = observation(*original, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *original);
}

TEST_CASE("replay factory rejects invalid pose dependencies transforms and unsupported uncertainty",
          "[replay_admission][poses]") {
    const auto original = replay_dataset();
    auto candidate = std::make_shared<completed_pose_dataset>(*original);
    auto& pose = candidate->poses[1];
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    switch (field) {
    case 0:
        pose.frame = {999};
        break;
    case 1:
        pose.frame = candidate->poses[0].frame;
        break;
    case 2:
        pose.capture_time += std::chrono::nanoseconds{1};
        break;
    case 3:
        pose.submap = {56};
        break;
    case 4:
        pose.revision = {};
        break;
    case 5:
        pose.revision = {2};
        break;
    case 6:
        pose.tracking = tracking_state::LOST;
        break;
    case 7:
        pose.covariance.emplace();
        break;
    case 8:
        pose.submap_from_camera.translation.x = std::numeric_limits<double>::infinity();
        break;
    case 9:
        pose.submap_from_camera.rotation = {1, 1, 0, 0};
        break;
    case 10:
        pose.submap_from_camera.scale = 2;
        break;
    case 11:
        pose.submap_from_camera.scale = 0;
        break;
    default:
        FAIL("unknown replay pose mutation");
    }
    const auto rejected = make_completed_pose_replay(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    candidate->poses = original->poses;
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto first = observation(*original, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *original);
}

TEST_CASE("failed replay start retains capabilities and permits corrected exact-memory admission",
          "[replay_admission][start]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    const auto before_start = (*replay)->drain_events({1});
    REQUIRE(!before_start);
    CHECK(before_start.error().code == error_code::INVALID_ARGUMENT);
    auto config = pose_estimator_config{{REPLAY_MEMORY_BYTES}, 3};
    const auto field = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(field);
    if (field == 0)
        config.max_pending_observations = 0;
    else if (field == 1)
        config.max_pending_observations = 4;
    else if (field == 2)
        config.memory.bytes = 0;
    else if (field == 3)
        config.memory.bytes = REPLAY_METADATA_BYTES - 1;
    else
        config.memory.bytes = REPLAY_METADATA_BYTES;
    const auto rejected = (*replay)->start(config);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_ARGUMENT);
    CHECK((*replay)->capabilities().observation_capacity == 3);
    auto first = observation(*dataset, 0);
    const auto saved = first.frame;
    const auto failed_submit = (*replay)->submit(first);
    REQUIRE(!failed_submit);
    CHECK(failed_submit.error().code == error_code::INVALID_ARGUMENT);
    check_frame_preserved(first, saved, {1});
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, *dataset);
}

TEST_CASE("replay frame admission preserves rejected headers payloads and operation IDs",
          "[replay_admission][frames]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto candidate = observation(*dataset, 0);
    auto code = error_code::INVALID_DATA;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    CAPTURE(field);
    switch (field) {
    case 0:
        candidate.frame.id = {999};
        break;
    case 1:
        candidate.frame.capture = {10};
        break;
    case 2:
        candidate.frame.calibration = {8};
        break;
    case 3:
        candidate.frame.capture_time += std::chrono::nanoseconds{1};
        break;
    case 4:
        ++candidate.frame.original_timestamp.pts;
        break;
    case 5:
        ++candidate.frame.original_timestamp.time_base_numerator;
        break;
    case 6:
        ++candidate.frame.original_timestamp.time_base_denominator;
        break;
    case 7:
        ++candidate.frame.pixels.width;
        break;
    case 8:
        ++candidate.frame.pixels.height;
        break;
    case 9:
        ++candidate.frame.pixels.row_stride;
        break;
    case 10:
        candidate.frame.pixels.format = pixel_format::GRAY8;
        code = error_code::UNSUPPORTED_OPERATION;
        break;
    case 11:
        candidate.frame.measured_depth = candidate.frame.pixels;
        code = error_code::UNSUPPORTED_OPERATION;
        break;
    case 12:
        candidate.frame.imu_samples.push_back({std::chrono::nanoseconds{7}, {1, 2, 3}, {4, 5, 6}});
        code = error_code::UNSUPPORTED_OPERATION;
        break;
    case 13:
        candidate.frame.pixels.bytes.reset();
        break;
    case 14:
        candidate.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>();
        break;
    case 15:
        candidate.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(5);
        break;
    case 16:
        candidate.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(7);
        break;
    case 17:
        candidate.operation = {};
        code = error_code::INVALID_ARGUMENT;
        break;
    case 18:
        candidate.frame.pixels.format = std::bit_cast<pixel_format>(std::uint8_t{255});
        code = error_code::UNSUPPORTED_OPERATION;
        break;
    default:
        FAIL("unknown replay frame mutation");
    }
    const auto saved = candidate.frame;
    const auto operation = candidate.operation;
    const auto rejected = (*replay)->submit(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == code);
    check_frame_preserved(candidate, saved, operation);
    auto repaired = observation(*dataset, 0);
    const auto admitted = (*replay)->submit(repaired);
    REQUIRE(admitted);
    CHECK(admitted->value == 1);
    check_replay_after_first(**replay, *dataset);
}

TEST_CASE("replay retry collisions preserve acknowledged tickets and emit each pose once",
          "[replay_admission][retry]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto first = observation(*dataset, 0);
    const auto ticket = (*replay)->submit(first);
    REQUIRE(ticket);
    auto candidate = observation(*dataset, 0);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(field);
    switch (field) {
    case 0:
        candidate.frame.id = {2};
        break;
    case 1:
        candidate.frame.capture = {10};
        break;
    case 2:
        candidate.frame.calibration = {8};
        break;
    case 3:
        candidate.frame.capture_time += std::chrono::nanoseconds{1};
        break;
    case 4:
        ++candidate.frame.original_timestamp.pts;
        break;
    case 5:
        ++candidate.frame.original_timestamp.time_base_numerator;
        break;
    case 6:
        ++candidate.frame.original_timestamp.time_base_denominator;
        break;
    case 7:
        ++candidate.frame.pixels.width;
        break;
    case 8:
        ++candidate.frame.pixels.height;
        break;
    case 9:
        ++candidate.frame.pixels.row_stride;
        break;
    case 10:
        candidate.frame.pixels.format = pixel_format::GRAY8;
        break;
    case 11:
        candidate.frame.measured_depth = candidate.frame.pixels;
        break;
    case 12:
        candidate.frame.imu_samples.push_back({std::chrono::nanoseconds{7}, {1, 2, 3}, {4, 5, 6}});
        break;
    case 13:
        candidate.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(5);
        break;
    case 14:
        candidate.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(7);
        break;
    case 15:
        candidate.frame.pixels.bytes =
            std::make_shared<const std::vector<std::byte>>(6, std::byte{18});
        break;
    case 16:
        candidate.operation = {99};
        break;
    default:
        FAIL("unknown replay retry mutation");
    }
    const auto saved = candidate.frame;
    const auto operation = candidate.operation;
    const auto rejected = (*replay)->submit(candidate);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::DUPLICATE_OPERATION);
    check_frame_preserved(candidate, saved, operation);
    auto authentic = observation(*dataset, 0);
    const auto repeated = (*replay)->submit(authentic);
    REQUIRE(repeated);
    CHECK(*repeated == *ticket);
    CHECK(!authentic.frame.pixels.bytes);
    const auto consumed_retry = (*replay)->submit(first);
    REQUIRE(consumed_retry);
    CHECK(*consumed_retry == *ticket);
    check_replay_after_first(**replay, *dataset);
}

TEST_CASE("replay queue slots are released only by observation events and finish closes admission",
          "[replay_admission][events]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 1}));
    auto first = observation(*dataset, 0);
    const auto lease = first.frame.pixels.bytes;
    const auto first_ticket = (*replay)->submit(first);
    REQUIRE(first_ticket);
    auto second = observation(*dataset, 1);
    const auto saved = second.frame;
    const auto premature_finish = (*replay)->finish();
    REQUIRE(!premature_finish);
    CHECK(premature_finish.error().code == error_code::INVALID_ARGUMENT);
    const auto zero = (*replay)->drain_events({0});
    REQUIRE(zero);
    CHECK(zero->empty());
    for (int index = 0; index < 3; ++index) {
        const auto full = (*replay)->submit(second);
        REQUIRE(!full);
        CHECK(full.error().code == error_code::WOULD_BLOCK);
        check_frame_preserved(second, saved, {2});
        const auto event = single_replay_event(**replay);
        if (index == 0) {
            REQUIRE(std::holds_alternative<tracking_change>(event));
            CHECK(std::get<tracking_change>(event).state == tracking_state::INITIALIZING);
        } else if (index == 1)
            CHECK(std::holds_alternative<submap_created>(event));
        else
            CHECK(std::holds_alternative<map_alignment>(event));
    }
    const auto still_full = (*replay)->submit(second);
    REQUIRE(!still_full);
    CHECK(still_full.error().code == error_code::WOULD_BLOCK);
    const auto pose = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<pose_estimate>(pose));
    CHECK(std::get<pose_estimate>(pose).frame == frame_id{1});
    REQUIRE((*replay)->submit(second));
    REQUIRE(std::holds_alternative<pose_estimate>(single_replay_event(**replay)));
    auto third = observation(*dataset, 2);
    REQUIRE((*replay)->submit(third));
    REQUIRE((*replay)->finish());
    auto closed = observation(*dataset, 0);
    closed.operation = {99};
    const auto closed_saved = closed.frame;
    const auto rejected = (*replay)->submit(closed);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_ARGUMENT);
    check_frame_preserved(closed, closed_saved, {99});
    auto authentic = observation(*dataset, 0);
    const auto retry = (*replay)->submit(authentic);
    REQUIRE(retry);
    CHECK(*retry == *first_ticket);
    CHECK(!authentic.frame.pixels.bytes);
    const auto final_pose = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<pose_estimate>(final_pose));
    CHECK(std::get<pose_estimate>(final_pose).frame == frame_id{3});
    const auto finished = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<tracking_change>(finished));
    CHECK(std::get<tracking_change>(finished).state == tracking_state::FINISHED);
    CHECK(std::holds_alternative<estimator_finished>(single_replay_event(**replay)));
    const auto empty = (*replay)->drain_events({1});
    REQUIRE(empty);
    CHECK(empty->empty());
    replay->reset();
    REQUIRE(lease);
    CHECK(lease->size() == 6);
    CHECK((*lease)[0] == std::byte{17});
    CHECK(std::get<pose_estimate>(pose).submap_from_camera.translation == vec3d{0, -2, 3});
}

TEST_CASE("replay emits LOST for unregistered selections at exactly ninety percent registration",
          "[replay_admission][registration]") {
    const auto count = GENERATE(std::size_t{10}, std::size_t{120});
    auto dataset = replay_dataset(count);
    dataset->poses.resize(count * 9 / 10);
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, count}));
    auto first = observation(*dataset, 0);
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, *dataset);
}

TEST_CASE("replay freezes all producer records before mutable aliases are changed or released",
          "[replay_admission][ownership]") {
    auto producer = replay_dataset();
    const auto original = *producer;
    auto replay = make_completed_pose_replay(producer);
    REQUIRE(replay);
    producer->source_sha256.clear();
    producer->revision = {99};
    producer->submap = {99};
    producer->calibrations.clear();
    producer->observations.clear();
    producer->poses.clear();
    producer.reset();
    CHECK((*replay)->capabilities().observation_capacity == 3);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    auto first = observation(original, 0);
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, original);
}

TEST_CASE("replay cancellation takes precedence at every lifecycle boundary and retains leases",
          "[replay_admission][cancellation]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    const auto boundary = GENERATE(0, 1, 2, 3);
    CAPTURE(boundary);
    if (boundary > 0)
        REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    if (boundary > 1) {
        auto first = observation(*dataset, 0);
        REQUIRE((*replay)->submit(first));
    }
    if (boundary > 2) {
        for (std::size_t index = 1; index < 3; ++index) {
            auto next = observation(*dataset, index);
            REQUIRE((*replay)->submit(next));
        }
        REQUIRE((*replay)->finish());
    }
    std::jthread stopper([&] { (*replay)->request_stop(); });
    stopper.join();
    (*replay)->request_stop();
    auto candidate = observation(*dataset, 0);
    const auto saved = candidate.frame;
    const auto submit = (*replay)->submit(candidate);
    REQUIRE(!submit);
    CHECK(submit.error().code == error_code::CANCELLED);
    check_frame_preserved(candidate, saved, {1});
    const auto start = (*replay)->start({{REPLAY_MEMORY_BYTES}, 3});
    REQUIRE(!start);
    CHECK(start.error().code == error_code::CANCELLED);
    const auto drain = (*replay)->drain_events({1});
    REQUIRE(!drain);
    CHECK(drain.error().code == error_code::CANCELLED);
    const auto finish = (*replay)->finish();
    REQUIRE(!finish);
    CHECK(finish.error().code == error_code::CANCELLED);
}

TEST_CASE("replay image budget rejects one byte short without consuming caller data",
          "[replay_admission][limits]") {
    const auto dataset = replay_dataset();
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES - 1}, 3}));
    auto first = observation(*dataset, 0);
    const auto saved = first.frame;
    const auto rejected = (*replay)->submit(first);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    check_frame_preserved(first, saved, {1});
    const auto only_initial = (*replay)->drain_events({100});
    REQUIRE(only_initial);
    CHECK(only_initial->size() == 3);
    const auto finish = (*replay)->finish();
    REQUIRE(!finish);
    CHECK(finish.error().code == error_code::INVALID_ARGUMENT);
    // Start configuration is immutable; a new instance admits the exact six-byte lease.
    replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({{REPLAY_MEMORY_BYTES}, 3}));
    const auto admitted = (*replay)->submit(first);
    REQUIRE(admitted);
    CHECK(admitted->value == 1);
    check_replay_after_first(**replay, *dataset);
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

TEST_CASE("canonical export verifies the retained synthetic job without launching a worker",
          "[poses][application]") {
    pose_fixture fixture;
    const auto destination = fixture.root / "canonical.weposes";
    const auto exported = export_completed_pose_asset(
        {{fixture.root, fixture.root / "source.mp4", {}}, destination}, {});
    REQUIRE(exported);
    const auto loaded = read_pose_asset(destination, *exported);
    REQUIRE(loaded);
    CHECK(loaded->snapshot->observations.size() == 3);
    CHECK(loaded->snapshot->poses.size() == 3);
    CHECK(loaded->snapshot->calibrations.begin()->second.distortion[0] == .1);
    CHECK(loaded->provenance.source_sha256 == fixture.source_digest);
    CHECK_FALSE(loaded->provenance.journal_sha256);
    CHECK_FALSE(export_completed_pose_asset(
        {{fixture.root, fixture.root / "source.mp4", {}}, destination}, {}));
    CHECK(read_pose_asset(destination, *exported));
    std::ofstream(fixture.root / "source.mp4") << "changed synthetic input";
    CHECK_FALSE(export_completed_pose_asset(
        {{fixture.root, fixture.root / "source.mp4", {}}, fixture.root / "changed.weposes"}, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "changed.weposes"));
}
TEST_CASE("completed poses preserve and cross-check native journal provenance",
          "[poses][provenance]") {
    pose_fixture fixture;
    auto metadata = nlohmann::json::parse(std::ifstream(fixture.decoded / "result.json"));
    const std::string journal(64, 'e');
    metadata["selection"] = "native_rgb_journal_v1";
    metadata["source_sha256"] = fixture.source_digest;
    metadata["journal_sha256"] = journal;
    std::ofstream(fixture.decoded / "result.json") << metadata.dump();
    fixture.inventory(fixture.decoded);
    CHECK_FALSE(fixture.load());
    auto completion = nlohmann::json::parse(std::ifstream(fixture.root / "result.json"));
    completion["journal_sha256"] = journal;
    std::ofstream(fixture.root / "result.json") << completion.dump();
    const auto loaded = fixture.load();
    REQUIRE(loaded);
    CHECK((*loaded)->journal_sha256 == journal);
    completion["journal_sha256"] = std::string(64, 'f');
    std::ofstream(fixture.root / "result.json") << completion.dump();
    CHECK_FALSE(fixture.load());
}
