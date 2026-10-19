#include "pose_fixture.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <thread>

using namespace world_explorer;

namespace {

using test::path_utf8;
using test::pose_fixture;
constexpr std::size_t K_REPLAY_METADATA_BYTES { 256uz * 1024uz };
constexpr std::size_t K_REPLAY_MEMORY_BYTES { K_REPLAY_METADATA_BYTES + 6uz };

FrameObservation observation(const CompletedPoseDataset& dataset, std::size_t index) {
    const auto& record = dataset.m_observations[index];
    FramePacket frame { record.m_id,
                       record.m_capture,
                       record.m_capture_time,
                       record.m_timestamp,
                       record.m_calibration,
                       { std::make_shared<const std::vector<std::byte>>(6, std::byte { 17 }),
                        2,
                        1,
                        6,
                        PixelFormat::RGB8 },
                       {},
                       {} };
    return { { index + 1 }, std::move(frame) };
}

// Entirely in-memory diagnostic input; these hashes do not identify a real capture.
std::shared_ptr<CompletedPoseDataset> replay_dataset(std::size_t count = 3) {
    auto dataset = std::make_shared<CompletedPoseDataset>();
    dataset->m_revision = { 1 };
    dataset->m_submap = { 55 };
    dataset->m_source_sha256 = std::string(64, 'a');
    dataset->m_pose_completion_sha256 = std::string(64, 'b');
    dataset->m_decode_completion_sha256 = std::string(64, 'c');
    dataset->m_decode_directory = "synthetic-unused-decode";
    dataset->m_calibrations = {
        { { 7 }, { 711, 529, .25, .75 }, 2, 1, CalibrationModel::OPENCV, { .1, -.2, .003, -.004 } } };
    for (std::size_t index { 0uz }; index < count; ++index) {
        const auto K_TIME = std::chrono::nanoseconds { static_cast<std::int64_t>(index) * 333333333 };
        dataset->m_observations.push_back({ { index + 1 },
                                           { 9 },
                                           { 7 },
                                           { static_cast<std::int64_t>(index) * 10 - 7, 1001, 30000 },
                                           K_TIME,
                                           std::string(64, 'd') });
        dataset->m_poses.push_back({ { index + 1 },
                                    { 55 },
                                    { { .5, .5, .5, .5 }, { static_cast<double>(index), -2, 3 }, 1 },
                                    K_TIME,
                                    { 1 },
                                    TrackingState::TRACKING,
                                    {} });
    }
    return dataset;
}

void check_frame_preserved(const FrameObservation& actual,
                           const FramePacket& expected,
                           OperationId operation) {
    CHECK(actual.m_operation == operation);
    const auto& frame = actual.m_frame;
    CHECK(frame.m_id == expected.m_id);
    CHECK(frame.m_capture == expected.m_capture);
    CHECK(frame.m_calibration == expected.m_calibration);
    CHECK(frame.m_capture_time == expected.m_capture_time);
    CHECK(frame.m_original_timestamp.m_pts == expected.m_original_timestamp.m_pts);
    CHECK(frame.m_original_timestamp.m_time_base_numerator ==
          expected.m_original_timestamp.m_time_base_numerator);
    CHECK(frame.m_original_timestamp.m_time_base_denominator ==
          expected.m_original_timestamp.m_time_base_denominator);
    CHECK(frame.m_pixels.m_bytes == expected.m_pixels.m_bytes);
    CHECK(frame.m_pixels.m_width == expected.m_pixels.m_width);
    CHECK(frame.m_pixels.m_height == expected.m_pixels.m_height);
    CHECK(frame.m_pixels.m_row_stride == expected.m_pixels.m_row_stride);
    CHECK(frame.m_pixels.m_format == expected.m_pixels.m_format);
    CHECK(frame.m_measured_depth.has_value() == expected.m_measured_depth.has_value());
    if (frame.m_measured_depth && expected.m_measured_depth) {
        CHECK(frame.m_measured_depth->m_bytes == expected.m_measured_depth->m_bytes);
        CHECK(frame.m_measured_depth->m_width == expected.m_measured_depth->m_width);
        CHECK(frame.m_measured_depth->m_height == expected.m_measured_depth->m_height);
        CHECK(frame.m_measured_depth->m_row_stride == expected.m_measured_depth->m_row_stride);
        CHECK(frame.m_measured_depth->m_format == expected.m_measured_depth->m_format);
    }
    REQUIRE(frame.m_imu_samples.size() == expected.m_imu_samples.size());
    for (std::size_t index { 0uz }; index < frame.m_imu_samples.size(); ++index) {
        CHECK(frame.m_imu_samples[index].m_capture_time ==
              expected.m_imu_samples[index].m_capture_time);
        CHECK(frame.m_imu_samples[index].m_acceleration ==
              expected.m_imu_samples[index].m_acceleration);
        CHECK(frame.m_imu_samples[index].m_angular_velocity ==
              expected.m_imu_samples[index].m_angular_velocity);
    }
}

PoseEvent single_replay_event(PoseEstimator& replay) {
    const auto K_EVENTS = replay.drain_events({ 1 });
    REQUIRE(K_EVENTS);
    REQUIRE(K_EVENTS->size() == 1);
    return K_EVENTS->front();
}

void check_replay_after_first(PoseEstimator& replay, const CompletedPoseDataset& dataset) {
    for (std::size_t index { 1uz }; index < dataset.m_observations.size(); ++index) {
        auto next = observation(dataset, index);
        const auto K_ADMITTED = replay.submit(next);
        REQUIRE(K_ADMITTED);
        CHECK(K_ADMITTED->m_value == index + 1);
        CHECK(!next.m_frame.m_pixels.m_bytes);
    }
    REQUIRE(replay.finish());
    REQUIRE(replay.finish());
    const auto K_EVENTS = replay.drain_events({ std::numeric_limits<std::size_t>::max() });
    REQUIRE(K_EVENTS);
    REQUIRE(K_EVENTS->size() == dataset.m_observations.size() + 5);
    REQUIRE(std::holds_alternative<TrackingChange>((*K_EVENTS)[0]));
    CHECK(std::get<TrackingChange>((*K_EVENTS)[0]).m_state == TrackingState::INITIALIZING);
    REQUIRE(std::holds_alternative<SubmapCreated>((*K_EVENTS)[1]));
    CHECK(std::get<SubmapCreated>((*K_EVENTS)[1]).m_submap == dataset.m_submap);
    REQUIRE(std::holds_alternative<MapAlignment>((*K_EVENTS)[2]));
    const auto K_ALIGNMENT = std::get<MapAlignment>((*K_EVENTS)[2]);
    CHECK(K_ALIGNMENT.m_submap == dataset.m_submap);
    CHECK(K_ALIGNMENT.m_revision == dataset.m_revision);
    CHECK(K_ALIGNMENT.m_world_from_submap.m_translation == Vec3d {});
    CHECK(K_ALIGNMENT.m_world_from_submap.m_rotation.m_w == 1);
    CHECK(K_ALIGNMENT.m_world_from_submap.m_scale == 1);
    for (std::size_t index { 0uz }; index < dataset.m_observations.size(); ++index) {
        const auto K_FOUND =
            std::find_if(dataset.m_poses.begin(), dataset.m_poses.end(), [&](const auto& pose) {
                return pose.m_frame == dataset.m_observations[index].m_id;
            });
        const auto& event = (*K_EVENTS)[index + 3];
        if (K_FOUND == dataset.m_poses.end()) {
            REQUIRE(std::holds_alternative<TrackingChange>(event));
            CHECK(std::get<TrackingChange>(event).m_state == TrackingState::LOST);
        } else {
            REQUIRE(std::holds_alternative<PoseEstimate>(event));
            const auto& pose = std::get<PoseEstimate>(event);
            CHECK(pose.m_frame == K_FOUND->m_frame);
            CHECK(pose.m_submap == K_FOUND->m_submap);
            CHECK(pose.m_revision == K_FOUND->m_revision);
            CHECK(pose.m_capture_time == K_FOUND->m_capture_time);
            CHECK(pose.m_tracking == TrackingState::TRACKING);
            CHECK(!pose.m_covariance);
            CHECK(pose.m_submap_from_camera.m_translation ==
                  K_FOUND->m_submap_from_camera.m_translation);
            CHECK(pose.m_submap_from_camera.m_rotation.m_w ==
                  K_FOUND->m_submap_from_camera.m_rotation.m_w);
            CHECK(pose.m_submap_from_camera.m_rotation.m_x ==
                  K_FOUND->m_submap_from_camera.m_rotation.m_x);
            CHECK(pose.m_submap_from_camera.m_rotation.m_y ==
                  K_FOUND->m_submap_from_camera.m_rotation.m_y);
            CHECK(pose.m_submap_from_camera.m_rotation.m_z ==
                  K_FOUND->m_submap_from_camera.m_rotation.m_z);
            CHECK(pose.m_submap_from_camera.m_scale == 1);
        }
    }
    REQUIRE(std::holds_alternative<TrackingChange>((*K_EVENTS)[K_EVENTS->size() - 2]));
    CHECK(std::get<TrackingChange>((*K_EVENTS)[K_EVENTS->size() - 2]).m_state ==
          TrackingState::FINISHED);
    CHECK(std::holds_alternative<EstimatorFinished>(K_EVENTS->back()));
    const auto K_EMPTY = replay.drain_events({ 1 });
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->empty());
}

}  // namespace

namespace {

BatchPoseConfig batch_config(const pose_fixture& fixture, const std::string& mode = "failure") {
    const auto K_SCRIPT = fixture.root / "diagnostic batch worker.py";
    std::ofstream output(K_SCRIPT);
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
    if (mode == "wait") {
        output << "time.sleep(30)\n";
    }
    output << "we_protocol.failure('DIAGNOSTIC_FAILURE','poses','synthetic failure; no solver')\n"
              "sys.exit(2)\n";
    return { .m_decode_stage = fixture.decoded,
            .m_job_directory = fixture.root / "fresh job",
            .m_worker_script = K_SCRIPT,
            .m_source_sha256 = fixture.source_digest,
            .m_worker_python = WE_TEST_PYTHON,
            .m_pose_python = WE_TEST_PYTHON };
}

}  // namespace

TEST_CASE("batch admission writes RGB before consuming leases and bounds memory disk and order") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    const auto K_CONFIG = batch_config(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    CHECK((*batch)->capabilities().m_mode == EstimatorMode::BATCH_SOLVE);
    CHECK_FALSE((*batch)->capabilities().m_streaming_tracking);
    CHECK_FALSE((*batch)->start({ { 4uz * 1024 * 1024 }, 2 }));
    CHECK_FALSE(std::filesystem::exists(K_CONFIG.m_job_directory));
    REQUIRE((*batch)->start({ { 4uz * 1024 * 1024 }, 3 }));
    CHECK_FALSE((*batch)->acquire_dataset());
    CHECK_FALSE((*batch)->finish());
    auto wrong_order = observation(**K_DATASET, 1);
    CHECK_FALSE((*batch)->submit(wrong_order));
    CHECK(wrong_order.m_frame.m_pixels.m_bytes);
    auto first = observation(**K_DATASET, 0);
    auto retry = first;
    auto retained = first.m_frame.m_pixels.m_bytes;
    std::filesystem::create_directory(K_CONFIG.m_job_directory / "input/1.ppm.tmp");
    CHECK_FALSE((*batch)->submit(first));
    CHECK(first.m_frame.m_pixels.m_bytes);
    CHECK(std::filesystem::is_directory(K_CONFIG.m_job_directory / "input/1.ppm.tmp"));
    std::filesystem::remove(K_CONFIG.m_job_directory / "input/1.ppm.tmp");
    const auto K_TICKET = (*batch)->submit(first);
    REQUIRE(K_TICKET);
    CHECK_FALSE(first.m_frame.m_pixels.m_bytes);
    CHECK(retained->size() == 6);
    std::ifstream input(K_CONFIG.m_job_directory / "input/1.ppm", std::ios::binary);
    std::string bytes(std::istreambuf_iterator<char> { input }, {});
    CHECK(bytes == std::string("P6\n2 1\n255\n") + std::string(6, char { 17 }));
    REQUIRE((*batch)->submit(first));
    CHECK((*batch)->submit(retry)->m_value == K_TICKET->m_value);
    auto conflict = observation(**K_DATASET, 0);
    conflict.m_frame.m_pixels.m_bytes =
        std::make_shared<const std::vector<std::byte>>(6, std::byte { 18 });
    const auto K_REJECTED = (*batch)->submit(conflict);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(conflict.m_frame.m_pixels.m_bytes);
    auto second = observation(**K_DATASET, 1);
    auto wrong_metadata = second;
    wrong_metadata.m_frame.m_capture_time += std::chrono::nanoseconds { 1 };
    CHECK_FALSE((*batch)->submit(wrong_metadata));
    CHECK(wrong_metadata.m_frame.m_pixels.m_bytes);
    REQUIRE((*batch)->submit(second));
    const auto K_INITIAL = (*batch)->drain_events({ 1000000 });
    REQUIRE(K_INITIAL);
    REQUIRE(K_INITIAL->size() == 1);
    CHECK(std::get<TrackingChange>(K_INITIAL->front()).m_state == TrackingState::INITIALIZING);
    CHECK((*batch)->drain_events({ 1 })->empty());

    auto disk_config = K_CONFIG;
    disk_config.m_job_directory = fixture.root / "disk limited";
    disk_config.m_journal_bytes = { 2uz * 1024 * 1024 + bytes.size() };
    auto disk = make_batch_pose_estimator(disk_config);
    REQUIRE(disk);
    REQUIRE((*disk)->start({ { 4uz * 1024 * 1024 }, 3 }));
    auto disk_first = observation(**K_DATASET, 0);
    REQUIRE((*disk)->submit(disk_first));
    auto disk_second = observation(**K_DATASET, 1);
    const auto K_FULL = (*disk)->submit(disk_second);
    REQUIRE_FALSE(K_FULL);
    CHECK(K_FULL.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(disk_second.m_frame.m_pixels.m_bytes);
    CHECK_FALSE(std::filesystem::exists(disk_config.m_job_directory / "input/2.ppm"));

    auto memory_config = K_CONFIG;
    memory_config.m_job_directory = fixture.root / "memory limited";
    auto memory = make_batch_pose_estimator(memory_config);
    REQUIRE(memory);
    REQUIRE((*memory)->start({ { 2uz * 1024 * 1024 + 5 }, 3 }));
    auto memory_first = observation(**K_DATASET, 0);
    const auto K_EXHAUSTED = (*memory)->submit(memory_first);
    REQUIRE_FALSE(K_EXHAUSTED);
    CHECK(K_EXHAUSTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(memory_first.m_frame.m_pixels.m_bytes);
}

TEST_CASE("batch worker failure retains a checked journal and finish does not relaunch") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    const auto K_CONFIG = batch_config(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start({ { 4uz * 1024 * 1024 }, 3 }));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATASET, index);
        REQUIRE((*batch)->submit(frame));
    }
    const auto K_FAILED = (*batch)->finish();
    REQUIRE_FALSE(K_FAILED);
    CHECK(K_FAILED.error().m_code == ErrorCode::BACKEND_FAILURE);
    CHECK(K_FAILED.error().m_message.find("DIAGNOSTIC_FAILURE") != std::string::npos);
    CHECK_FALSE((*batch)->finish());
    CHECK_FALSE((*batch)->drain_events({ 1 }));
    CHECK_FALSE((*batch)->acquire_dataset());
    std::ifstream launches(K_CONFIG.m_job_directory / "launches");
    std::string line;
    REQUIRE(std::getline(launches, line));
    CHECK_FALSE(std::getline(launches, line));
    std::ifstream manifest(K_CONFIG.m_job_directory / "input/journal.json");
    const auto K_JOURNAL = nlohmann::json::parse(manifest);
    REQUIRE(K_JOURNAL.at("frames").size() == 3);
    CHECK(K_JOURNAL.at("source_sha256") == fixture.source_digest);
    for (const auto& frame : K_JOURNAL.at("frames")) {
        CHECK(frame.at("pixel_sha256") == sha256(std::vector<std::byte>(6, std::byte { 17 })));
        CHECK(frame.at("sha256") ==
              *sha256_file(K_CONFIG.m_job_directory / "input" / frame.at("file").get<std::string>(),
                           4096));
    }
    CHECK_FALSE(std::filesystem::exists(K_CONFIG.m_job_directory / "result.json"));
    auto duplicate = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(duplicate);
    const auto K_OCCUPIED = (*duplicate)->start({ { 4uz * 1024 * 1024 }, 3 });
    REQUIRE_FALSE(K_OCCUPIED);
    CHECK(K_OCCUPIED.error().m_code == ErrorCode::WOULD_BLOCK);
    CHECK(std::filesystem::exists(K_CONFIG.m_job_directory / "input/journal.json"));
}

TEST_CASE("batch cancellation interrupts isolated finish without publishing success") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    const auto K_CONFIG = batch_config(fixture, "wait");
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start({ { 4uz * 1024 * 1024 }, 3 }));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATASET, index);
        REQUIRE((*batch)->submit(frame));
    }
    Result<void> finished;
    std::jthread running([&] {
        finished = (*batch)->finish();
    });
    for (int iteration { 0 };
         iteration < 200 && !std::filesystem::exists(K_CONFIG.m_job_directory / "launches");
         ++iteration) {
        std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
    }
    const auto K_START = std::chrono::steady_clock::now();
    (*batch)->request_stop();
    running.join();
    REQUIRE_FALSE(finished);
    CHECK(finished.error().m_code == ErrorCode::CANCELLED);
    CHECK(std::chrono::steady_clock::now() - K_START < std::chrono::seconds { 3 });
    CHECK_FALSE(std::filesystem::exists(K_CONFIG.m_job_directory / "result.json"));
    CHECK(std::filesystem::exists(K_CONFIG.m_job_directory / "input/journal.json"));
    auto retry = observation(**K_DATASET, 0);
    CHECK_FALSE((*batch)->submit(retry));
    CHECK(retry.m_frame.m_pixels.m_bytes);
}

TEST_CASE("owned batch resume verifies admitted RGB and preserves caller data on mismatch",
          "[stage-chain]") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load().value();
    auto config = batch_config(fixture);
    auto initial = make_batch_pose_estimator(config).value();
    REQUIRE(initial->start({ { 4uz * 1024 * 1024 }, 3 }));
    auto first = observation(*K_DATASET, 0);
    REQUIRE(initial->submit(first));
    const auto K_ASSET = config.m_job_directory / "input/1.ppm";
    const auto K_HASH = sha256_file(K_ASSET, 4096).value();
    initial.reset();
    config.m_resume_input = true;
    SECTION("identical replay") {
        auto resumed = make_batch_pose_estimator(config).value();
        REQUIRE(resumed->start({ { 4uz * 1024 * 1024 }, 3 }));
        auto replay = observation(*K_DATASET, 0);
        REQUIRE(resumed->submit(replay));
        CHECK_FALSE(replay.m_frame.m_pixels.m_bytes);
        CHECK(sha256_file(K_ASSET, 4096).value() == K_HASH);
    }
    SECTION("different content") {
        std::ofstream(K_ASSET) << "changed RGB";
        const auto K_RETAINED = sha256_file(K_ASSET, 4096).value();
        auto resumed = make_batch_pose_estimator(config).value();
        REQUIRE(resumed->start({ { 4uz * 1024 * 1024 }, 3 }));
        auto replay = observation(*K_DATASET, 0);
        CHECK_FALSE(resumed->submit(replay));
        CHECK(replay.m_frame.m_pixels.m_bytes);
        CHECK(sha256_file(K_ASSET, 4096).value() == K_RETAINED);
    }
    SECTION("uncommitted temporary input") {
        std::ofstream(config.m_job_directory / "input/2.ppm.tmp") << "partial";
        auto resumed = make_batch_pose_estimator(config).value();
        CHECK_FALSE(resumed->start({ { 4uz * 1024 * 1024 }, 3 }));
        CHECK(sha256_file(K_ASSET, 4096).value() == K_HASH);
    }
}

TEST_CASE("completed pose import preserves calibration model coefficients optical poses and IDs") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    CHECK((*K_DATASET)->m_observations.size() == 3);
    CHECK((*K_DATASET)->m_poses.size() == 3);
    CHECK((*K_DATASET)->m_scale == ScaleStatus::ARBITRARY);
    const auto& calibration = (*K_DATASET)->m_calibrations.front();
    CHECK(calibration.m_model == CalibrationModel::OPENCV);
    CHECK(calibration.m_distortion == std::array { .1, -.2, .003, -.004 });
    CHECK(calibration.m_camera.m_cx == .25);
    CHECK(calibration.m_camera.m_cy == .75);
    CHECK(calibration.m_image_transform == std::array { 1., 1., 0., 0. });
    const auto& pose = (*K_DATASET)->m_poses.front();
    CHECK(pose.m_submap_from_camera.m_translation == Vec3d { 1, 2, 3 });
    CHECK(transform_point(pose.m_submap_from_camera, { 1, 0, 0 }) == Vec3d { 1, 3, 3 });
    CHECK_FALSE(pose.m_covariance);
    CHECK(pose.m_capture_time == (*K_DATASET)->m_observations.front().m_capture_time);
    const auto K_AGAIN = fixture.load();
    REQUIRE(K_AGAIN);
    CHECK((*K_AGAIN)->m_submap == (*K_DATASET)->m_submap);
    CHECK((*K_AGAIN)->m_observations.front().m_id == (*K_DATASET)->m_observations.front().m_id);
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
        fixture.canonical["poses"][0]["world_from_camera"]["rotation_wxyz"] = { 2, 0, 0, 0 };
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
    SECTION("corrupt image") {
        std::ofstream(fixture.decoded / "images/0.png") << "changed";
    }
    SECTION("corrupt canonical asset") {
        std::ofstream(fixture.posed / "canonical_poses.json") << "{}";
    }
    SECTION("uncommitted asset") {
        std::ofstream(fixture.posed / "extra") << "x";
    }
    SECTION("source mismatch") {
        fixture.source_digest = std::string(64, 'd');
    }
    SECTION("missing inventory") {
        std::filesystem::remove(fixture.posed / "completion.json");
    }
    CHECK_FALSE(fixture.load());
}

TEST_CASE("replay factory rejects unsupported profiles and malformed producer identities",
          "[replay_admission][dataset]") {
    const auto K_ORIGINAL = replay_dataset();
    auto candidate = std::make_shared<CompletedPoseDataset>(*K_ORIGINAL);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        candidate.reset();
        break;
    case 1:
        candidate->m_source_sha256.pop_back();
        break;
    case 2:
        candidate->m_source_sha256[0] = 'G';
        break;
    case 3:
        candidate->m_pose_completion_sha256.pop_back();
        break;
    case 4:
        candidate->m_pose_completion_sha256[0] = 'G';
        break;
    case 5:
        candidate->m_decode_completion_sha256.pop_back();
        break;
    case 6:
        candidate->m_decode_completion_sha256[0] = 'G';
        break;
    case 7:
        candidate->m_decode_directory = std::string(4097, 'x');
        break;
    case 8:
        candidate->m_calibrations.clear();
        break;
    case 9:
        candidate->m_calibrations.push_back(candidate->m_calibrations[0]);
        break;
    case 10:
        candidate->m_calibrations[0].m_width = 0;
        break;
    case 11:
        candidate->m_calibrations[0].m_width = 8193;
        break;
    case 12:
        candidate->m_calibrations[0].m_height = 8193;
        break;
    case 13:
        candidate->m_submap = {};
        break;
    case 14:
        candidate->m_epoch = { 1 };
        break;
    case 15:
        candidate->m_revision = {};
        break;
    case 16:
        candidate->m_revision = { 2 };
        break;
    case 17:
        candidate->m_scale = ScaleStatus::METRIC_MEASURED;
        break;
    default:
        FAIL("unknown replay dataset mutation");
    }
    const auto K_REJECTED = make_completed_pose_replay(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    if (candidate) {
        *candidate = *K_ORIGINAL;
    } else {
        candidate = std::make_shared<CompletedPoseDataset>(*K_ORIGINAL);
    }
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto first = observation(*K_ORIGINAL, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *K_ORIGINAL);
}

TEST_CASE("replay factory enforces selection counts and the unchanged registration boundary",
          "[replay_admission][limits]") {
    auto candidate = replay_dataset();
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    if (K_FIELD == 0) {
        candidate->m_observations.resize(2);
    } else if (K_FIELD == 1) {
        candidate = replay_dataset(121);
    } else if (K_FIELD == 2) {
        candidate->m_poses.resize(2);
    } else if (K_FIELD == 3) {
        candidate->m_poses.push_back(candidate->m_poses[0]);
    } else {
        candidate = replay_dataset(10);
        candidate->m_poses.resize(8);
    }
    const auto K_OBSERVATIONS = candidate->m_observations.size();
    const auto K_POSES = candidate->m_poses.size();
    const auto K_REJECTED = make_completed_pose_replay(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(candidate->m_observations.size() == K_OBSERVATIONS);
    CHECK(candidate->m_poses.size() == K_POSES);
    CHECK(candidate->m_scale == ScaleStatus::ARBITRARY);
}

TEST_CASE("replay factory rejects invalid duplicate and mixed-capture observations",
          "[replay_admission][observations]") {
    const auto K_ORIGINAL = replay_dataset();
    auto candidate = std::make_shared<CompletedPoseDataset>(*K_ORIGINAL);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    auto& frame = candidate->m_observations[1];
    switch (K_FIELD) {
    case 0:
        frame.m_id = {};
        break;
    case 1:
        frame.m_capture = {};
        break;
    case 2:
        frame.m_calibration = { 999 };
        break;
    case 3:
        frame.m_capture_time = std::chrono::nanoseconds { -1 };
        break;
    case 4:
        frame.m_timestamp.m_time_base_numerator = 0;
        break;
    case 5:
        frame.m_timestamp.m_time_base_numerator = -1;
        break;
    case 6:
        frame.m_timestamp.m_time_base_denominator = 0;
        break;
    case 7:
        frame.m_timestamp.m_time_base_denominator = -1;
        break;
    case 8:
        frame.m_image_sha256.pop_back();
        break;
    case 9:
        frame.m_image_sha256[63] = 'G';
        break;
    case 10:
        frame.m_id = candidate->m_observations[0].m_id;
        break;
    case 11:
        frame.m_capture = { 10 };
        break;
    default:
        FAIL("unknown replay observation mutation");
    }
    const auto K_REJECTED = make_completed_pose_replay(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    candidate->m_observations = K_ORIGINAL->m_observations;
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto first = observation(*K_ORIGINAL, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *K_ORIGINAL);
}

TEST_CASE("replay factory rejects invalid pose dependencies transforms and unsupported uncertainty",
          "[replay_admission][poses]") {
    const auto K_ORIGINAL = replay_dataset();
    auto candidate = std::make_shared<CompletedPoseDataset>(*K_ORIGINAL);
    auto& pose = candidate->m_poses[1];
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        pose.m_frame = { 999 };
        break;
    case 1:
        pose.m_frame = candidate->m_poses[0].m_frame;
        break;
    case 2:
        pose.m_capture_time += std::chrono::nanoseconds { 1 };
        break;
    case 3:
        pose.m_submap = { 56 };
        break;
    case 4:
        pose.m_revision = {};
        break;
    case 5:
        pose.m_revision = { 2 };
        break;
    case 6:
        pose.m_tracking = TrackingState::LOST;
        break;
    case 7:
        pose.m_covariance.emplace();
        break;
    case 8:
        pose.m_submap_from_camera.m_translation.m_x = std::numeric_limits<double>::infinity();
        break;
    case 9:
        pose.m_submap_from_camera.m_rotation = { 1, 1, 0, 0 };
        break;
    case 10:
        pose.m_submap_from_camera.m_scale = 2;
        break;
    case 11:
        pose.m_submap_from_camera.m_scale = 0;
        break;
    default:
        FAIL("unknown replay pose mutation");
    }
    const auto K_REJECTED = make_completed_pose_replay(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    candidate->m_poses = K_ORIGINAL->m_poses;
    auto repaired = make_completed_pose_replay(candidate);
    REQUIRE(repaired);
    REQUIRE((*repaired)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto first = observation(*K_ORIGINAL, 0);
    REQUIRE((*repaired)->submit(first));
    check_replay_after_first(**repaired, *K_ORIGINAL);
}

TEST_CASE("failed replay start retains capabilities and permits corrected exact-memory admission",
          "[replay_admission][start]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    const auto K_BEFORE_START = (*replay)->drain_events({ 1 });
    REQUIRE(!K_BEFORE_START);
    CHECK(K_BEFORE_START.error().m_code == ErrorCode::INVALID_ARGUMENT);
    auto config = PoseEstimatorConfig { { K_REPLAY_MEMORY_BYTES }, 3 };
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    if (K_FIELD == 0) {
        config.m_max_pending_observations = 0;
    } else if (K_FIELD == 1) {
        config.m_max_pending_observations = 4;
    } else if (K_FIELD == 2) {
        config.m_memory.m_bytes = 0;
    } else if (K_FIELD == 3) {
        config.m_memory.m_bytes = K_REPLAY_METADATA_BYTES - 1;
    } else {
        config.m_memory.m_bytes = K_REPLAY_METADATA_BYTES;
    }
    const auto K_REJECTED = (*replay)->start(config);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK((*replay)->capabilities().m_observation_capacity == 3);
    auto first = observation(*K_DATASET, 0);
    const auto K_SAVED = first.m_frame;
    const auto K_FAILED_SUBMIT = (*replay)->submit(first);
    REQUIRE(!K_FAILED_SUBMIT);
    CHECK(K_FAILED_SUBMIT.error().m_code == ErrorCode::INVALID_ARGUMENT);
    check_frame_preserved(first, K_SAVED, { 1 });
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, *K_DATASET);
}

TEST_CASE("replay frame admission preserves rejected headers payloads and operation IDs",
          "[replay_admission][frames]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto candidate = observation(*K_DATASET, 0);
    auto code = ErrorCode::INVALID_DATA;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        candidate.m_frame.m_id = { 999 };
        break;
    case 1:
        candidate.m_frame.m_capture = { 10 };
        break;
    case 2:
        candidate.m_frame.m_calibration = { 8 };
        break;
    case 3:
        candidate.m_frame.m_capture_time += std::chrono::nanoseconds { 1 };
        break;
    case 4:
        ++candidate.m_frame.m_original_timestamp.m_pts;
        break;
    case 5:
        ++candidate.m_frame.m_original_timestamp.m_time_base_numerator;
        break;
    case 6:
        ++candidate.m_frame.m_original_timestamp.m_time_base_denominator;
        break;
    case 7:
        ++candidate.m_frame.m_pixels.m_width;
        break;
    case 8:
        ++candidate.m_frame.m_pixels.m_height;
        break;
    case 9:
        ++candidate.m_frame.m_pixels.m_row_stride;
        break;
    case 10:
        candidate.m_frame.m_pixels.m_format = PixelFormat::GRAY8;
        code = ErrorCode::UNSUPPORTED_OPERATION;
        break;
    case 11:
        candidate.m_frame.m_measured_depth = candidate.m_frame.m_pixels;
        code = ErrorCode::UNSUPPORTED_OPERATION;
        break;
    case 12:
        candidate.m_frame.m_imu_samples.push_back(
            { std::chrono::nanoseconds { 7 }, { 1, 2, 3 }, { 4, 5, 6 } });
        code = ErrorCode::UNSUPPORTED_OPERATION;
        break;
    case 13:
        candidate.m_frame.m_pixels.m_bytes.reset();
        break;
    case 14:
        candidate.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>();
        break;
    case 15:
        candidate.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(5);
        break;
    case 16:
        candidate.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(7);
        break;
    case 17:
        candidate.m_operation = {};
        code = ErrorCode::INVALID_ARGUMENT;
        break;
    case 18:
        candidate.m_frame.m_pixels.m_format = std::bit_cast<PixelFormat>(std::uint8_t { 255 });
        code = ErrorCode::UNSUPPORTED_OPERATION;
        break;
    default:
        FAIL("unknown replay frame mutation");
    }
    const auto K_SAVED = candidate.m_frame;
    const auto K_OPERATION = candidate.m_operation;
    const auto K_REJECTED = (*replay)->submit(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == code);
    check_frame_preserved(candidate, K_SAVED, K_OPERATION);
    auto repaired = observation(*K_DATASET, 0);
    const auto K_ADMITTED = (*replay)->submit(repaired);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->m_value == 1);
    check_replay_after_first(**replay, *K_DATASET);
}

TEST_CASE("replay retry collisions preserve acknowledged tickets and emit each pose once",
          "[replay_admission][retry]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto first = observation(*K_DATASET, 0);
    const auto K_TICKET = (*replay)->submit(first);
    REQUIRE(K_TICKET);
    auto candidate = observation(*K_DATASET, 0);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        candidate.m_frame.m_id = { 2 };
        break;
    case 1:
        candidate.m_frame.m_capture = { 10 };
        break;
    case 2:
        candidate.m_frame.m_calibration = { 8 };
        break;
    case 3:
        candidate.m_frame.m_capture_time += std::chrono::nanoseconds { 1 };
        break;
    case 4:
        ++candidate.m_frame.m_original_timestamp.m_pts;
        break;
    case 5:
        ++candidate.m_frame.m_original_timestamp.m_time_base_numerator;
        break;
    case 6:
        ++candidate.m_frame.m_original_timestamp.m_time_base_denominator;
        break;
    case 7:
        ++candidate.m_frame.m_pixels.m_width;
        break;
    case 8:
        ++candidate.m_frame.m_pixels.m_height;
        break;
    case 9:
        ++candidate.m_frame.m_pixels.m_row_stride;
        break;
    case 10:
        candidate.m_frame.m_pixels.m_format = PixelFormat::GRAY8;
        break;
    case 11:
        candidate.m_frame.m_measured_depth = candidate.m_frame.m_pixels;
        break;
    case 12:
        candidate.m_frame.m_imu_samples.push_back(
            { std::chrono::nanoseconds { 7 }, { 1, 2, 3 }, { 4, 5, 6 } });
        break;
    case 13:
        candidate.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(5);
        break;
    case 14:
        candidate.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(7);
        break;
    case 15:
        candidate.m_frame.m_pixels.m_bytes =
            std::make_shared<const std::vector<std::byte>>(6, std::byte { 18 });
        break;
    case 16:
        candidate.m_operation = { 99 };
        break;
    default:
        FAIL("unknown replay retry mutation");
    }
    const auto K_SAVED = candidate.m_frame;
    const auto K_OPERATION = candidate.m_operation;
    const auto K_REJECTED = (*replay)->submit(candidate);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    check_frame_preserved(candidate, K_SAVED, K_OPERATION);
    auto authentic = observation(*K_DATASET, 0);
    const auto K_REPEATED = (*replay)->submit(authentic);
    REQUIRE(K_REPEATED);
    CHECK(*K_REPEATED == *K_TICKET);
    CHECK(!authentic.m_frame.m_pixels.m_bytes);
    const auto K_CONSUMED_RETRY = (*replay)->submit(first);
    REQUIRE(K_CONSUMED_RETRY);
    CHECK(*K_CONSUMED_RETRY == *K_TICKET);
    check_replay_after_first(**replay, *K_DATASET);
}

TEST_CASE("replay queue slots are released only by observation events and finish closes admission",
          "[replay_admission][events]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 1 }));
    auto first = observation(*K_DATASET, 0);
    const auto K_LEASE = first.m_frame.m_pixels.m_bytes;
    const auto K_FIRST_TICKET = (*replay)->submit(first);
    REQUIRE(K_FIRST_TICKET);
    auto second = observation(*K_DATASET, 1);
    const auto K_SAVED = second.m_frame;
    const auto K_PREMATURE_FINISH = (*replay)->finish();
    REQUIRE(!K_PREMATURE_FINISH);
    CHECK(K_PREMATURE_FINISH.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const auto K_ZERO = (*replay)->drain_events({ 0 });
    REQUIRE(K_ZERO);
    CHECK(K_ZERO->empty());
    for (int index { 0 }; index < 3; ++index) {
        const auto K_FULL = (*replay)->submit(second);
        REQUIRE(!K_FULL);
        CHECK(K_FULL.error().m_code == ErrorCode::WOULD_BLOCK);
        check_frame_preserved(second, K_SAVED, { 2 });
        const auto K_EVENT = single_replay_event(**replay);
        if (index == 0) {
            REQUIRE(std::holds_alternative<TrackingChange>(K_EVENT));
            CHECK(std::get<TrackingChange>(K_EVENT).m_state == TrackingState::INITIALIZING);
        } else if (index == 1) {
            CHECK(std::holds_alternative<SubmapCreated>(K_EVENT));
        } else {
            CHECK(std::holds_alternative<MapAlignment>(K_EVENT));
        }
    }
    const auto K_STILL_FULL = (*replay)->submit(second);
    REQUIRE(!K_STILL_FULL);
    CHECK(K_STILL_FULL.error().m_code == ErrorCode::WOULD_BLOCK);
    const auto K_POSE = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<PoseEstimate>(K_POSE));
    CHECK(std::get<PoseEstimate>(K_POSE).m_frame == FrameId { 1 });
    REQUIRE((*replay)->submit(second));
    REQUIRE(std::holds_alternative<PoseEstimate>(single_replay_event(**replay)));
    auto third = observation(*K_DATASET, 2);
    REQUIRE((*replay)->submit(third));
    REQUIRE((*replay)->finish());
    auto closed = observation(*K_DATASET, 0);
    closed.m_operation = { 99 };
    const auto K_CLOSED_SAVED = closed.m_frame;
    const auto K_REJECTED = (*replay)->submit(closed);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    check_frame_preserved(closed, K_CLOSED_SAVED, { 99 });
    auto authentic = observation(*K_DATASET, 0);
    const auto K_RETRY = (*replay)->submit(authentic);
    REQUIRE(K_RETRY);
    CHECK(*K_RETRY == *K_FIRST_TICKET);
    CHECK(!authentic.m_frame.m_pixels.m_bytes);
    const auto K_FINAL_POSE = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<PoseEstimate>(K_FINAL_POSE));
    CHECK(std::get<PoseEstimate>(K_FINAL_POSE).m_frame == FrameId { 3 });
    const auto K_FINISHED = single_replay_event(**replay);
    REQUIRE(std::holds_alternative<TrackingChange>(K_FINISHED));
    CHECK(std::get<TrackingChange>(K_FINISHED).m_state == TrackingState::FINISHED);
    CHECK(std::holds_alternative<EstimatorFinished>(single_replay_event(**replay)));
    const auto K_EMPTY = (*replay)->drain_events({ 1 });
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->empty());
    replay->reset();
    REQUIRE(K_LEASE);
    CHECK(K_LEASE->size() == 6);
    CHECK((*K_LEASE)[0] == std::byte { 17 });
    CHECK(std::get<PoseEstimate>(K_POSE).m_submap_from_camera.m_translation == Vec3d { 0, -2, 3 });
}

TEST_CASE("replay emits LOST for unregistered selections at exactly ninety percent registration",
          "[replay_admission][registration]") {
    const auto K_COUNT = GENERATE(10uz, 120uz);
    auto dataset = replay_dataset(K_COUNT);
    dataset->m_poses.resize(K_COUNT * 9 / 10);
    auto replay = make_completed_pose_replay(dataset);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, K_COUNT }));
    auto first = observation(*dataset, 0);
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, *dataset);
}

TEST_CASE("replay freezes all producer records before mutable aliases are changed or released",
          "[replay_admission][ownership]") {
    auto producer = replay_dataset();
    const auto K_ORIGINAL = *producer;
    auto replay = make_completed_pose_replay(producer);
    REQUIRE(replay);
    producer->m_source_sha256.clear();
    producer->m_revision = { 99 };
    producer->m_submap = { 99 };
    producer->m_calibrations.clear();
    producer->m_observations.clear();
    producer->m_poses.clear();
    producer.reset();
    CHECK((*replay)->capabilities().m_observation_capacity == 3);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    auto first = observation(K_ORIGINAL, 0);
    REQUIRE((*replay)->submit(first));
    check_replay_after_first(**replay, K_ORIGINAL);
}

TEST_CASE("replay cancellation takes precedence at every lifecycle boundary and retains leases",
          "[replay_admission][cancellation]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    const auto K_BOUNDARY = GENERATE(0, 1, 2, 3);
    CAPTURE(K_BOUNDARY);
    if (K_BOUNDARY > 0) {
        REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    }
    if (K_BOUNDARY > 1) {
        auto first = observation(*K_DATASET, 0);
        REQUIRE((*replay)->submit(first));
    }
    if (K_BOUNDARY > 2) {
        for (std::size_t index { 1uz }; index < 3uz; ++index) {
            auto next = observation(*K_DATASET, index);
            REQUIRE((*replay)->submit(next));
        }
        REQUIRE((*replay)->finish());
    }
    std::jthread stopper([&] {
        (*replay)->request_stop();
    });
    stopper.join();
    (*replay)->request_stop();
    auto candidate = observation(*K_DATASET, 0);
    const auto K_SAVED = candidate.m_frame;
    const auto K_SUBMIT = (*replay)->submit(candidate);
    REQUIRE(!K_SUBMIT);
    CHECK(K_SUBMIT.error().m_code == ErrorCode::CANCELLED);
    check_frame_preserved(candidate, K_SAVED, { 1 });
    const auto K_START = (*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 });
    REQUIRE(!K_START);
    CHECK(K_START.error().m_code == ErrorCode::CANCELLED);
    const auto K_DRAIN = (*replay)->drain_events({ 1 });
    REQUIRE(!K_DRAIN);
    CHECK(K_DRAIN.error().m_code == ErrorCode::CANCELLED);
    const auto K_FINISH = (*replay)->finish();
    REQUIRE(!K_FINISH);
    CHECK(K_FINISH.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("replay image budget rejects one byte short without consuming caller data",
          "[replay_admission][limits]") {
    const auto K_DATASET = replay_dataset();
    auto replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES - 1 }, 3 }));
    auto first = observation(*K_DATASET, 0);
    const auto K_SAVED = first.m_frame;
    const auto K_REJECTED = (*replay)->submit(first);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    check_frame_preserved(first, K_SAVED, { 1 });
    const auto K_ONLY_INITIAL = (*replay)->drain_events({ 100 });
    REQUIRE(K_ONLY_INITIAL);
    CHECK(K_ONLY_INITIAL->size() == 3);
    const auto K_FINISH = (*replay)->finish();
    REQUIRE(!K_FINISH);
    CHECK(K_FINISH.error().m_code == ErrorCode::INVALID_ARGUMENT);
    // Start configuration is immutable; a new instance admits the exact six-byte lease.
    replay = make_completed_pose_replay(K_DATASET);
    REQUIRE(replay);
    REQUIRE((*replay)->start({ { K_REPLAY_MEMORY_BYTES }, 3 }));
    const auto K_ADMITTED = (*replay)->submit(first);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->m_value == 1);
    check_replay_after_first(**replay, *K_DATASET);
}

TEST_CASE("completed replay enforces bounded admission retry identity and owned immutable events") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    auto mutable_dataset = std::make_shared<CompletedPoseDataset>(**K_DATASET);
    const std::shared_ptr<const CompletedPoseDataset> K_INPUT = mutable_dataset;
    auto replay = make_completed_pose_replay(K_INPUT);
    REQUIRE(replay);
    mutable_dataset->m_poses.front().m_submap_from_camera.m_translation = { 100, 200, 300 };
    CHECK((*replay)->capabilities().m_mode == EstimatorMode::COMPLETED_REPLAY);
    CHECK_FALSE((*replay)->capabilities().m_streaming_tracking);
    CHECK_FALSE((*replay)->capabilities().m_covariance);
    CHECK_FALSE((*replay)->finish());
    REQUIRE((*replay)->start({ { 1024uz * 1024 }, 1 }));
    CHECK((*replay)->capabilities().m_observation_capacity == 1);
    CHECK_FALSE((*replay)->start({ { 1024uz * 1024 }, 1 }));
    auto first = observation(**K_DATASET, 0);
    const auto K_LEASE = first.m_frame.m_pixels.m_bytes;
    const auto K_ADMITTED = (*replay)->submit(first);
    REQUIRE(K_ADMITTED);
    CHECK_FALSE(first.m_frame.m_pixels.m_bytes);
    REQUIRE((*replay)->submit(first));
    auto retry = observation(**K_DATASET, 0);
    const auto K_REPEATED = (*replay)->submit(retry);
    REQUIRE(K_REPEATED);
    CHECK(*K_REPEATED == *K_ADMITTED);
    CHECK_FALSE(retry.m_frame.m_pixels.m_bytes);
    auto conflicting = observation(**K_DATASET, 0);
    conflicting.m_frame.m_pixels.m_bytes =
        std::make_shared<const std::vector<std::byte>>(6, std::byte { 18 });
    const auto K_CONFLICT = (*replay)->submit(conflicting);
    REQUIRE_FALSE(K_CONFLICT);
    CHECK(K_CONFLICT.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(conflicting.m_frame.m_pixels.m_bytes);
    auto second = observation(**K_DATASET, 1);
    const auto K_RETAINED = second.m_frame.m_pixels.m_bytes;
    const auto K_FULL = (*replay)->submit(second);
    REQUIRE_FALSE(K_FULL);
    CHECK(K_FULL.error().m_code == ErrorCode::WOULD_BLOCK);
    CHECK(second.m_frame.m_pixels.m_bytes == K_RETAINED);
    const auto K_ZERO = (*replay)->drain_events({ 0 });
    REQUIRE(K_ZERO);
    CHECK(K_ZERO->empty());
    const auto K_INITIAL = (*replay)->drain_events({ 3 });
    REQUIRE(K_INITIAL);
    CHECK(K_INITIAL->size() == 3);
    CHECK(std::holds_alternative<MapAlignment>(K_INITIAL->back()));
    const auto K_EVENT = (*replay)->drain_events({ 1 });
    REQUIRE(K_EVENT);
    REQUIRE(K_EVENT->size() == 1);
    CHECK(std::get<PoseEstimate>(K_EVENT->front()).m_frame == first.m_frame.m_id);
    REQUIRE((*replay)->submit(second));
    REQUIRE((*replay)->drain_events({ 1 }));
    auto third = observation(**K_DATASET, 2);
    REQUIRE((*replay)->submit(third));
    REQUIRE((*replay)->finish());
    REQUIRE((*replay)->finish());
    const auto K_TERMINAL = (*replay)->drain_events({ 1000000 });
    REQUIRE(K_TERMINAL);
    CHECK(K_TERMINAL->size() == 3);
    CHECK(std::holds_alternative<EstimatorFinished>(K_TERMINAL->back()));
    const auto K_EMPTY = (*replay)->drain_events({ 10 });
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->empty());
    replay->reset();
    CHECK(K_LEASE->size() == 6);
    CHECK(std::get<PoseEstimate>(K_EVENT->front()).m_submap_from_camera.m_translation ==
          Vec3d { 1, 2, 3 });
}

TEST_CASE("completed pose import requires explicit selection when the decode pointer is stale") {
    pose_fixture fixture;
    std::ofstream(fixture.root / "decode.json") << nlohmann::json {
        { "key", std::string(64, 'd') },
        { "output",
         path_utf8(
             fixture.root / "stages" /
             ("decode-" + std::string(64, 'd'))) } }.dump();
    CHECK_FALSE(fixture.load());
    const auto K_DATASET =
        load_completed_poses({ fixture.root, fixture.source_digest, fixture.decoded });
    REQUIRE(K_DATASET);
    CHECK((*K_DATASET)->m_observations.size() == 3);
    nlohmann::json pointer;
    std::ifstream(fixture.root / "decode.json") >> pointer;
    CHECK(pointer.at("key") == std::string(64, 'd'));
}

TEST_CASE("completed replay preserves failed observations and supports concurrent cancellation") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    auto replay = make_completed_pose_replay(*K_DATASET);
    REQUIRE(replay);
    auto frame = observation(**K_DATASET, 0);
    const auto K_PIXELS = frame.m_frame.m_pixels.m_bytes;
    SECTION("before start") {
        CHECK_FALSE((*replay)->submit(frame));
    }
    SECTION("malformed metadata") {
        REQUIRE((*replay)->start({ { 1024uz * 1024 }, 1 }));
        frame.m_frame.m_capture_time += std::chrono::nanoseconds(1);
        CHECK_FALSE((*replay)->submit(frame));
    }
    SECTION("memory budget") {
        REQUIRE((*replay)->start({ { 256uz * 1024 + 1 }, 1 }));
        const auto K_RESULT = (*replay)->submit(frame);
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
    SECTION("cancellation") {
        REQUIRE((*replay)->start({ { 1024uz * 1024 }, 1 }));
        std::jthread stopper([&] {
            (*replay)->request_stop();
        });
        stopper.join();
        const auto K_RESULT = (*replay)->submit(frame);
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK_FALSE((*replay)->drain_events({ 1 }));
        CHECK_FALSE((*replay)->finish());
    }
    CHECK(frame.m_frame.m_pixels.m_bytes == K_PIXELS);
}

TEST_CASE("map transactions retain distortion and reject calibration operation collisions") {
    pose_fixture fixture;
    const auto K_DATASET = fixture.load();
    REQUIRE(K_DATASET);
    MapTransaction transaction;
    transaction.m_operation = { 1 };
    transaction.m_new_revision = { 1 };
    transaction.m_calibrations = (*K_DATASET)->m_calibrations;
    transaction.m_observations = (*K_DATASET)->m_observations;
    transaction.m_poses = (*K_DATASET)->m_poses;
    transaction.m_alignments.push_back({ (*K_DATASET)->m_submap, { 1 }, {} });
    MapStore maps;
    REQUIRE(maps.commit(transaction));
    REQUIRE(maps.commit(transaction));
    const auto K_BEFORE = maps.acquire_snapshot();
    const auto K_ID = transaction.m_calibrations.front().m_id;
    CHECK(K_BEFORE->m_calibrations.at(K_ID).m_distortion == std::array { .1, -.2, .003, -.004 });
    transaction.m_calibrations.front().m_distortion[0] = .2;
    const auto K_COLLISION = maps.commit(transaction);
    REQUIRE_FALSE(K_COLLISION);
    CHECK(K_COLLISION.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(maps.acquire_snapshot() == K_BEFORE);
    CHECK(K_BEFORE->m_poses.size() == 3);
    MapTransaction bad_pose;
    bad_pose.m_operation = { 2 };
    bad_pose.m_base_revision = { 1 };
    bad_pose.m_new_revision = { 2 };
    auto pose = K_BEFORE->m_poses.begin()->second;
    pose.m_revision = { 2 };
    pose.m_capture_time += std::chrono::nanoseconds(1);
    bad_pose.m_poses.push_back(pose);
    CHECK_FALSE(maps.commit(bad_pose));
    CHECK(maps.acquire_snapshot() == K_BEFORE);
}

TEST_CASE("canonical export verifies the retained synthetic job without launching a worker",
          "[poses][application]") {
    pose_fixture fixture;
    const auto K_DESTINATION = fixture.root / "canonical.weposes";
    const auto K_EXPORTED = export_completed_pose_asset(
        { { fixture.root, fixture.root / "source.mp4", {} }, K_DESTINATION }, {});
    REQUIRE(K_EXPORTED);
    const auto K_LOADED = read_pose_asset(K_DESTINATION, *K_EXPORTED);
    REQUIRE(K_LOADED);
    CHECK(K_LOADED->m_snapshot->m_observations.size() == 3);
    CHECK(K_LOADED->m_snapshot->m_poses.size() == 3);
    CHECK(K_LOADED->m_snapshot->m_calibrations.begin()->second.m_distortion[0] == .1);
    CHECK(K_LOADED->m_provenance.m_source_sha256 == fixture.source_digest);
    CHECK_FALSE(K_LOADED->m_provenance.m_journal_sha256);
    CHECK_FALSE(export_completed_pose_asset(
        { { fixture.root, fixture.root / "source.mp4", {} }, K_DESTINATION }, {}));
    CHECK(read_pose_asset(K_DESTINATION, *K_EXPORTED));
    std::ofstream(fixture.root / "source.mp4") << "changed synthetic input";
    CHECK_FALSE(export_completed_pose_asset(
        { { fixture.root, fixture.root / "source.mp4", {} }, fixture.root / "changed.weposes" }, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "changed.weposes"));
}

TEST_CASE("completed poses preserve and cross-check native journal provenance",
          "[poses][provenance]") {
    pose_fixture fixture;
    auto metadata = nlohmann::json::parse(std::ifstream(fixture.decoded / "result.json"));
    const std::string K_JOURNAL(64, 'e');
    metadata["selection"] = "native_rgb_journal_v1";
    metadata["source_sha256"] = fixture.source_digest;
    metadata["journal_sha256"] = K_JOURNAL;
    std::ofstream(fixture.decoded / "result.json") << metadata.dump();
    fixture.inventory(fixture.decoded);
    CHECK_FALSE(fixture.load());
    auto completion = nlohmann::json::parse(std::ifstream(fixture.root / "result.json"));
    completion["journal_sha256"] = K_JOURNAL;
    std::ofstream(fixture.root / "result.json") << completion.dump();
    const auto K_LOADED = fixture.load();
    REQUIRE(K_LOADED);
    CHECK((*K_LOADED)->m_journal_sha256 == K_JOURNAL);
    completion["journal_sha256"] = std::string(64, 'f');
    std::ofstream(fixture.root / "result.json") << completion.dump();
    CHECK_FALSE(fixture.load());
}
