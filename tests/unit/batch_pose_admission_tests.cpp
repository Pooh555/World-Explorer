#include "pose_fixture.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"

#include <bit>
#include <fstream>
#include <limits>
#include <map>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

using test::path_utf8;
using test::pose_fixture;
constexpr std::size_t K_METADATA_BYTES { 2uz * 1024uz * 1024uz };
constexpr PoseEstimatorConfig K_START {{K_METADATA_BYTES + 6}, 3};
constexpr std::size_t K_PPM_BYTES { std::string_view("P6\n2 1uz\n255\n").size() + 6uz };

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(output);
}

using FileTree = std::map<std::string, std::string>;

FileTree snapshot(const std::filesystem::path& root) {
    FileTree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = path_utf8(entry.path().lexically_relative(root));
        const auto K_STATUS = entry.symlink_status();
        if (std::filesystem::is_symlink(K_STATUS))
            result.emplace(K_NAME,
                           "link:" + path_utf8(std::filesystem::read_symlink(entry.path())));
        else if (std::filesystem::is_directory(K_STATUS)) result.emplace(K_NAME, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            REQUIRE(entry.file_size() <= 2uz * 1024 * 1024);
            std::ifstream input(entry.path(), std::ios::binary);
            REQUIRE(input);
            std::string bytes {std::istreambuf_iterator<char>(input), {}};
            REQUIRE(!input.bad());
            result.emplace(K_NAME, "file:" + bytes);
        }
    }
    return result;
}

void create_link(const std::filesystem::path& target,
                 const std::filesystem::path& link,
                 bool directory = false) {
    std::error_code error;
    if (directory) std::filesystem::create_directory_symlink(target, link, error);
    else std::filesystem::create_symlink(target, link, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation is unavailable to this Windows account");
#endif
    INFO(error.message());
    REQUIRE(!error);
}

BatchPoseConfig configuration(const pose_fixture& fixture) {
    const auto K_SCRIPT = fixture.root / "diagnostic worker.py";
    write_bytes(K_SCRIPT,
                "raise SystemExit('Synthetic admission test must not launch a worker')\n");
    write_bytes(fixture.root / "caller.txt", "caller data outside batch output");
    return {.m_decode_stage = fixture.decoded,
            .m_job_directory = fixture.root / "fresh batch",
            .m_worker_script = K_SCRIPT,
            .m_source_sha256 = fixture.source_digest,
            .m_worker_python = WE_TEST_PYTHON,
            .m_pose_python = WE_TEST_PYTHON};
}

FrameObservation observation(const CompletedPoseDataset& data, std::size_t index) {
    const auto& record = data.m_observations[index];
    return {{index + 1},
            {record.m_id,
             record.m_capture,
             record.m_capture_time,
             record.m_timestamp,
             record.m_calibration,
             {std::make_shared<const std::vector<std::byte>>(6, std::byte { 17 }),
              2,
              1,
              6,
              PixelFormat::RGB8},
             {},
             {}}};
}

template <class Value> void expect_error(const Result<Value>& outcome, ErrorCode code) {
    INFO((outcome ? "unexpected success" : outcome.error().m_message));
    REQUIRE(!outcome);
    CHECK(outcome.error().m_code == code);
}

void check_preserved(const FrameObservation& actual, const FrameObservation& before) {
    CHECK(actual.m_operation == before.m_operation);
    const auto& frame = actual.m_frame;
    const auto& saved = before.m_frame;
    CHECK(frame.m_id == saved.m_id);
    CHECK(frame.m_capture == saved.m_capture);
    CHECK(frame.m_capture_time == saved.m_capture_time);
    CHECK(frame.m_original_timestamp.m_pts == saved.m_original_timestamp.m_pts);
    CHECK(frame.m_original_timestamp.m_time_base_numerator ==
          saved.m_original_timestamp.m_time_base_numerator);
    CHECK(frame.m_original_timestamp.m_time_base_denominator ==
          saved.m_original_timestamp.m_time_base_denominator);
    CHECK(frame.m_calibration == saved.m_calibration);
    CHECK(frame.m_pixels.m_bytes == saved.m_pixels.m_bytes);
    if (frame.m_pixels.m_bytes && saved.m_pixels.m_bytes)
        CHECK(*frame.m_pixels.m_bytes == *saved.m_pixels.m_bytes);
    CHECK(frame.m_pixels.m_width == saved.m_pixels.m_width);
    CHECK(frame.m_pixels.m_height == saved.m_pixels.m_height);
    CHECK(frame.m_pixels.m_row_stride == saved.m_pixels.m_row_stride);
    CHECK(frame.m_pixels.m_format == saved.m_pixels.m_format);
    REQUIRE(frame.m_measured_depth.has_value() == saved.m_measured_depth.has_value());
    if (frame.m_measured_depth && saved.m_measured_depth) {
        CHECK(frame.m_measured_depth->m_bytes == saved.m_measured_depth->m_bytes);
        CHECK(frame.m_measured_depth->m_width == saved.m_measured_depth->m_width);
        CHECK(frame.m_measured_depth->m_height == saved.m_measured_depth->m_height);
        CHECK(frame.m_measured_depth->m_row_stride == saved.m_measured_depth->m_row_stride);
        CHECK(frame.m_measured_depth->m_format == saved.m_measured_depth->m_format);
    }
    REQUIRE(frame.m_imu_samples.size() == saved.m_imu_samples.size());
    for (std::size_t index { 0uz }; index < frame.m_imu_samples.size(); ++index) {
        CHECK(frame.m_imu_samples[index].m_capture_time ==
              saved.m_imu_samples[index].m_capture_time);
        CHECK(frame.m_imu_samples[index].m_acceleration ==
              saved.m_imu_samples[index].m_acceleration);
        CHECK(frame.m_imu_samples[index].m_angular_velocity ==
              saved.m_imu_samples[index].m_angular_velocity);
    }
}

}  // namespace

TEST_CASE("batch factory validates configuration before creating output",
          "[batch_admission][config]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_ORIGINAL = configuration(fixture);
    auto config = K_ORIGINAL;
    if (K_FIELD == 0) config.m_source_sha256.clear();
    if (K_FIELD == 1) config.m_source_sha256[0] = 'X';
    if (K_FIELD == 2) config.m_job_directory.clear();
    if (K_FIELD == 3) config.m_worker_python.clear();
    if (K_FIELD == 4) config.m_pose_python.clear();
    if (K_FIELD == 5) config.m_worker_python = std::string(4097, 'x');
    if (K_FIELD == 6) config.m_pose_python = std::string(4097, 'x');
    if (K_FIELD == 7) config.m_worker_python.push_back('\0');
    if (K_FIELD == 8) config.m_pose_python.push_back('\0');
    if (K_FIELD == 9) config.m_cpu_threads = 0;
    if (K_FIELD == 10) config.m_cpu_threads = 5;
    if (K_FIELD == 11) config.m_journal_bytes.m_bytes = K_METADATA_BYTES;
    if (K_FIELD == 12) config.m_journal_bytes.m_bytes = 32ULL * 1024 * 1024 * 1024 + 1;
    if (K_FIELD == 13) config.m_recipe = std::bit_cast<PoseRecipe>(std::uint8_t { 255 });
    if (K_FIELD == 14) config.m_worker_script = fixture.root / "missing worker";
    if (K_FIELD == 15) config.m_worker_script = fixture.decoded;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config), ErrorCode::INVALID_ARGUMENT);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    REQUIRE(make_batch_pose_estimator(K_ORIGINAL));
    CHECK(snapshot(fixture.root) == K_BEFORE);
}

TEST_CASE("batch factory propagates cancellation and decoded selection rejection",
          "[batch_admission][config]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    auto config = configuration(fixture);
    std::stop_source stop;
    if (K_FIELD == 0) stop.request_stop();
    if (K_FIELD == 1) config.m_decode_stage = fixture.root / "missing decode";
    if (K_FIELD == 2) {
        std::ifstream input(fixture.decoded / "result.json");
        auto value = nlohmann::json::parse(input);
        input.close();
        value["frames"].erase(2);
        write_bytes(fixture.decoded / "result.json", value.dump());
        fixture.inventory(fixture.decoded);
    }
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config, stop.get_token()),
                 K_FIELD == 0 ? ErrorCode::CANCELLED : ErrorCode::INVALID_DATA);
    CHECK(snapshot(fixture.root) == K_BEFORE);
}

TEST_CASE("batch start and pre-start operations preserve caller data on rejection",
          "[batch_admission][start]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    auto frame = observation(**K_DATA, 0);
    const auto K_SAVED = frame;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), ErrorCode::INVALID_ARGUMENT);
    expect_error((*batch)->finish(), ErrorCode::INVALID_ARGUMENT);
    expect_error((*batch)->drain_events({1}), ErrorCode::INVALID_ARGUMENT);
    expect_error((*batch)->acquire_dataset(), ErrorCode::WOULD_BLOCK);
    check_preserved(frame, K_SAVED);
    auto start = K_START;
    if (K_FIELD == 0) start.m_max_pending_observations = 2;
    if (K_FIELD == 1) start.m_max_pending_observations = 121;
    if (K_FIELD == 2) start.m_memory.m_bytes = K_METADATA_BYTES;
    if (K_FIELD == 3) start.m_memory.m_bytes = 0;
    if (K_FIELD == 4) (*batch)->request_stop();
    expect_error((*batch)->start(start),
                 K_FIELD == 4 ? ErrorCode::CANCELLED : ErrorCode::INVALID_ARGUMENT);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    if (K_FIELD == 4) {
        expect_error((*batch)->submit(frame), ErrorCode::CANCELLED);
        expect_error((*batch)->finish(), ErrorCode::CANCELLED);
        expect_error((*batch)->drain_events({1}), ErrorCode::CANCELLED);
        check_preserved(frame, K_SAVED);
        CHECK(snapshot(fixture.root) == K_BEFORE);
    } else {
        REQUIRE((*batch)->start(K_START));
        const auto K_STARTED = snapshot(fixture.root);
        expect_error((*batch)->start(K_START), ErrorCode::INVALID_ARGUMENT);
        CHECK(snapshot(fixture.root) == K_STARTED);
        REQUIRE((*batch)->submit(frame));
        CHECK_FALSE(frame.m_frame.m_pixels.m_bytes);
    }
}

TEST_CASE("batch ordered RGB admission rejects changed headers and unsupported sensors",
          "[batch_admission][observation]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    auto frame = observation(**K_DATA, 0);
    if (K_FIELD == 0) frame.m_operation = {};
    if (K_FIELD == 1) ++frame.m_frame.m_id.m_value;
    if (K_FIELD == 2) ++frame.m_frame.m_capture.m_value;
    if (K_FIELD == 3) ++frame.m_frame.m_calibration.m_value;
    if (K_FIELD == 4) frame.m_frame.m_capture_time += std::chrono::nanoseconds(1);
    if (K_FIELD == 5) ++frame.m_frame.m_original_timestamp.m_pts;
    if (K_FIELD == 6) ++frame.m_frame.m_original_timestamp.m_time_base_numerator;
    if (K_FIELD == 7) ++frame.m_frame.m_original_timestamp.m_time_base_denominator;
    if (K_FIELD == 8) ++frame.m_frame.m_pixels.m_width;
    if (K_FIELD == 9) ++frame.m_frame.m_pixels.m_height;
    if (K_FIELD == 10) ++frame.m_frame.m_pixels.m_row_stride;
    if (K_FIELD == 11) frame.m_frame.m_pixels.m_bytes.reset();
    if (K_FIELD == 12)
        frame.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(5);
    if (K_FIELD == 13) frame.m_frame.m_pixels.m_format = PixelFormat::GRAY8;
    if (K_FIELD == 14) frame.m_frame.m_measured_depth = frame.m_frame.m_pixels;
    if (K_FIELD == 15)
        frame.m_frame.m_imu_samples = { { std::chrono::nanoseconds { 4 }, { 1, 2, 3 }, { 4, 5, 6 } } };
    if (K_FIELD == 16) frame = observation(**K_DATA, 1);
    const auto K_SAVED = frame;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(frame),
                 K_FIELD == 0                     ? ErrorCode::INVALID_ARGUMENT
                 : K_FIELD >= 13 && K_FIELD <= 15 ? ErrorCode::UNSUPPORTED_OPERATION
                                                  : ErrorCode::INVALID_DATA);
    check_preserved(frame, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    auto corrected = observation(**K_DATA, 0);
    const auto K_LEASE = corrected.m_frame.m_pixels.m_bytes;
    const auto K_TICKET = (*batch)->submit(corrected);
    REQUIRE(K_TICKET);
    CHECK(K_TICKET->m_value == 1);
    CHECK_FALSE(corrected.m_frame.m_pixels.m_bytes);
    REQUIRE(K_LEASE);
    CHECK(*K_LEASE == std::vector<std::byte>(6, std::byte { 17 }));
    const auto K_ACCEPTED = snapshot(fixture.root);
    const auto K_RETRY = (*batch)->submit(corrected);
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_value == K_TICKET->m_value);
    CHECK(snapshot(fixture.root) == K_ACCEPTED);
}

TEST_CASE("batch retries preserve receipts and files when headers or pixel content differ",
          "[batch_admission][retry]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    auto first = observation(**K_DATA, 0);
    auto changed = first;
    const auto K_LEASE = first.m_frame.m_pixels.m_bytes;
    const auto K_TICKET = (*batch)->submit(first);
    REQUIRE(K_TICKET);
    if (K_FIELD == 0) ++changed.m_frame.m_capture.m_value;
    if (K_FIELD == 1)
        changed.m_frame.m_pixels.m_bytes = std::make_shared<const std::vector<std::byte>>(5);
    if (K_FIELD == 2)
        changed.m_frame.m_pixels.m_bytes =
            std::make_shared<const std::vector<std::byte>>(6, std::byte { 18 });
    if (K_FIELD == 3) changed.m_frame.m_measured_depth = changed.m_frame.m_pixels;
    if (K_FIELD == 4) changed.m_frame.m_imu_samples.push_back({});
    if (K_FIELD == 5) {
        changed.m_frame.m_pixels.m_bytes.reset();
        ++changed.m_frame.m_original_timestamp.m_pts;
    }
    const auto K_SAVED = changed;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(changed), ErrorCode::DUPLICATE_OPERATION);
    check_preserved(changed, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    const auto K_RETRIED = (*batch)->submit(first);
    REQUIRE(K_RETRIED);
    CHECK(K_RETRIED->m_value == K_TICKET->m_value);
    auto intact = observation(**K_DATA, 0);
    const auto K_WITH_PIXELS = (*batch)->submit(intact);
    REQUIRE(K_WITH_PIXELS);
    CHECK(K_WITH_PIXELS->m_value == K_TICKET->m_value);
    CHECK_FALSE(intact.m_frame.m_pixels.m_bytes);
    CHECK(*K_LEASE == std::vector<std::byte>(6, std::byte { 17 }));
    CHECK(snapshot(fixture.root) == K_BEFORE);
    auto second = observation(**K_DATA, 1);
    const auto K_NEXT = (*batch)->submit(second);
    REQUIRE(K_NEXT);
    CHECK(K_NEXT->m_value == 2);
}

TEST_CASE("batch memory and first RGB asset budgets admit the exact bound",
          "[batch_admission][budget]") {
    const auto K_FIELD = GENERATE(0, 1);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    config.m_journal_bytes.m_bytes = K_METADATA_BYTES + K_PPM_BYTES - (K_FIELD == 1 ? 1 : 0);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    auto start = K_START;
    if (K_FIELD == 0) --start.m_memory.m_bytes;
    REQUIRE((*batch)->start(start));
    auto frame = observation(**K_DATA, 0);
    const auto K_SAVED = frame;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), ErrorCode::RESOURCE_EXHAUSTED);
    check_preserved(frame, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    config.m_job_directory = fixture.root / "exact budget";
    config.m_journal_bytes.m_bytes = K_METADATA_BYTES + K_PPM_BYTES;
    auto exact = make_batch_pose_estimator(config);
    REQUIRE(exact);
    REQUIRE((*exact)->start(K_START));
    REQUIRE((*exact)->submit(frame));
    CHECK_FALSE(frame.m_frame.m_pixels.m_bytes);
    CHECK(std::filesystem::file_size(config.m_job_directory / "input/1.ppm") == K_PPM_BYTES);
    auto second = observation(**K_DATA, 1);
    const auto K_RETAINED = second;
    const auto K_FULL = snapshot(fixture.root);
    expect_error((*exact)->submit(second), ErrorCode::RESOURCE_EXHAUSTED);
    check_preserved(second, K_RETAINED);
    CHECK(snapshot(fixture.root) == K_FULL);
}

TEST_CASE("batch cumulative disk admission and full capacity preserve receipts for resume",
          "[batch_admission][budget]") {
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    config.m_journal_bytes.m_bytes = K_METADATA_BYTES + 3 * K_PPM_BYTES - 1;
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    for (std::size_t index { 0uz }; index < 2uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*batch)->submit(frame));
    }
    auto third = observation(**K_DATA, 2);
    const auto K_SAVED = third;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(third), ErrorCode::RESOURCE_EXHAUSTED);
    check_preserved(third, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    batch->reset();
    config.m_resume_input = true;
    ++config.m_journal_bytes.m_bytes;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(K_START));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        const auto K_TICKET = (*resumed)->submit(frame);
        REQUIRE(K_TICKET);
        CHECK(K_TICKET->m_value == index + 1);
        CHECK_FALSE(frame.m_frame.m_pixels.m_bytes);
    }
    auto extra = observation(**K_DATA, 0);
    extra.m_operation = { 4 };
    const auto K_RETAINED = extra;
    const auto K_FULL = snapshot(fixture.root);
    expect_error((*resumed)->submit(extra), ErrorCode::WOULD_BLOCK);
    check_preserved(extra, K_RETAINED);
    auto retry = observation(**K_DATA, 0);
    const auto K_TICKET = (*resumed)->submit(retry);
    REQUIRE(K_TICKET);
    CHECK(K_TICKET->m_value == 1);
    CHECK(snapshot(fixture.root) == K_FULL);
    const auto K_INITIAL = (*resumed)->drain_events({0});
    REQUIRE(K_INITIAL);
    CHECK(K_INITIAL->empty());
    const auto K_EVENT = (*resumed)->drain_events({std::numeric_limits<std::size_t>::max()});
    REQUIRE(K_EVENT);
    REQUIRE(K_EVENT->size() == 1);
    CHECK(std::get<TrackingChange>(K_EVENT->front()).m_state == TrackingState::INITIALIZING);
    const auto K_EMPTY = (*resumed)->drain_events({1});
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->empty());
    CHECK(snapshot(fixture.root) == K_FULL);
}

TEST_CASE("batch resumed start rejects foreign entries locks and linked inputs without writes",
          "[batch_admission][resume]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(K_START));
    auto first = observation(**K_DATA, 0);
    REQUIRE((*initial)->submit(first));
    initial->reset();
    config.m_resume_input = true;
    std::filesystem::path obstruction;
    if (K_FIELD == 0) obstruction = config.m_job_directory / "foreign.txt";
    if (K_FIELD == 1) obstruction = config.m_job_directory / "job.lock";
    if (K_FIELD == 2) obstruction = config.m_job_directory / "input/2.ppm.tmp";
    if (K_FIELD == 3) obstruction = config.m_job_directory / "input/2.ppm";
    if (K_FIELD == 4) obstruction = config.m_job_directory / "request.json";
    if (K_FIELD == 5) obstruction = config.m_job_directory / "input/2.ppm";
    if (K_FIELD < 3) write_bytes(obstruction, "caller-owned obstruction");
    if (K_FIELD == 3) std::filesystem::create_directory(obstruction);
    if (K_FIELD == 4 || K_FIELD == 5) create_link(fixture.root / "caller.txt", obstruction);
    if (K_FIELD == 6) {
        obstruction = config.m_job_directory / "input";
        std::filesystem::rename(obstruction, fixture.root / "caller input");
        create_link(fixture.root / "caller input", obstruction, true);
    }
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*resumed)->start(K_START),
                 K_FIELD == 1 ? ErrorCode::WOULD_BLOCK : ErrorCode::INVALID_DATA);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    std::filesystem::remove(obstruction);
    if (K_FIELD == 6) std::filesystem::rename(fixture.root / "caller input", obstruction);
    REQUIRE((*resumed)->start(K_START));
    const auto K_READY = snapshot(fixture.root);
    auto replay = observation(**K_DATA, 0);
    REQUIRE((*resumed)->submit(replay));
    CHECK_FALSE(replay.m_frame.m_pixels.m_bytes);
    CHECK(snapshot(fixture.root) == K_READY);
}

TEST_CASE("batch resumed RGB rejects changed shape content or symlink and permits corrected retry",
          "[batch_admission][resume]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(K_START));
    auto first = observation(**K_DATA, 0);
    REQUIRE((*initial)->submit(first));
    initial->reset();
    config.m_resume_input = true;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(K_START));
    const auto K_PATH = config.m_job_directory / "input/1.ppm";
    if (K_FIELD < 2)
        write_bytes(K_PATH, std::string(K_FIELD == 0 ? K_PPM_BYTES - 1 : K_PPM_BYTES, 'x'));
    else {
        std::filesystem::remove(K_PATH);
        create_link(fixture.root / "caller.txt", K_PATH);
    }
    auto replay = observation(**K_DATA, 0);
    const auto K_SAVED = replay;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*resumed)->submit(replay), ErrorCode::INVALID_DATA);
    check_preserved(replay, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    std::filesystem::remove(K_PATH);
    write_bytes(K_PATH, std::string("P6\n2 1\n255\n") + std::string(6, char { 17 }));
    const auto K_READY = snapshot(fixture.root);
    const auto K_TICKET = (*resumed)->submit(replay);
    REQUIRE(K_TICKET);
    CHECK(K_TICKET->m_value == 1);
    CHECK_FALSE(replay.m_frame.m_pixels.m_bytes);
    CHECK(snapshot(fixture.root) == K_READY);
}

TEST_CASE("batch factory rejects linked job roots without accessing the target",
          "[batch_admission][paths]") {
    pose_fixture fixture;
    auto config = configuration(fixture);
    const auto K_TARGET = fixture.root / "caller directory";
    std::filesystem::create_directory(K_TARGET);
    write_bytes(K_TARGET / "caller.txt", "retained caller data");
    create_link(K_TARGET, config.m_job_directory, true);
    config.m_resume_input = true;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config), ErrorCode::INVALID_DATA);
    CHECK(snapshot(fixture.root) == K_BEFORE);
}

TEST_CASE("batch failed publication preserves obstructions and freezes closed admission failure",
          "[batch_admission][publication]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*batch)->submit(frame));
    }
    const auto K_JOURNAL = K_CONFIG.m_job_directory / "input/journal.json";
    const auto K_REQUEST = K_CONFIG.m_job_directory / "batch-request.json";
    if (K_FIELD == 0) write_bytes(K_JOURNAL, "caller journal");
    if (K_FIELD == 1) write_bytes(K_JOURNAL.string() + ".tmp", "caller temporary journal");
    if (K_FIELD == 2)
        create_link(fixture.root / "missing caller asset", K_JOURNAL.string() + ".tmp");
    if (K_FIELD == 3) write_bytes(K_REQUEST, "caller request");
    if (K_FIELD == 4)
        create_link(fixture.root / "missing caller asset", K_REQUEST.string() + ".tmp");
    if (K_FIELD >= 5) {
        const auto K_PATH =
            K_FIELD == 5 ? K_CONFIG.m_job_directory / "input" : K_CONFIG.m_job_directory;
        std::filesystem::rename(K_PATH, fixture.root / "caller output");
        create_link(fixture.root / "caller output", K_PATH, true);
    }
    const auto K_BEFORE = snapshot(fixture.root);
    const auto K_FAILED = (*batch)->finish();
    expect_error(K_FAILED, K_FIELD >= 5 ? ErrorCode::INVALID_DATA : ErrorCode::IO_ERROR);
    auto after = snapshot(fixture.root);
    if (K_FIELD == 3 || K_FIELD == 4) {
        // Journal publication succeeds before the independent request obstruction.
        REQUIRE(after.erase(path_utf8(K_JOURNAL.lexically_relative(fixture.root))) == 1);
    }
    CHECK(after == K_BEFORE);
    const auto K_FROZEN = snapshot(fixture.root);
    const auto K_RETRIED = (*batch)->finish();
    expect_error(K_RETRIED, K_FAILED.error().m_code);
    CHECK(K_RETRIED.error().m_message == K_FAILED.error().m_message);
    expect_error((*batch)->drain_events({1}), K_FAILED.error().m_code);
    expect_error((*batch)->acquire_dataset(), K_FAILED.error().m_code);
    auto new_operation = observation(**K_DATA, 0);
    new_operation.m_operation = { 4 };
    const auto K_SAVED = new_operation;
    expect_error((*batch)->submit(new_operation), ErrorCode::INVALID_ARGUMENT);
    check_preserved(new_operation, K_SAVED);
    auto acknowledged = observation(**K_DATA, 0);
    const auto K_TICKET = (*batch)->submit(acknowledged);
    REQUIRE(K_TICKET);
    CHECK(K_TICKET->m_value == 1);
    CHECK_FALSE(acknowledged.m_frame.m_pixels.m_bytes);
    CHECK(snapshot(fixture.root) == K_FROZEN);
}

namespace {

void diagnostic_worker(const BatchPoseConfig& config, std::string_view mode) {
    std::string script =
        "import sys,json,shutil\nfrom pathlib import Path\n"
        "sys.path.insert(0," +
        nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump() +
        ")\n"
        "import we_protocol\nmode=" +
        nlohmann::json(mode).dump() +
        "\n"
        "original=we_protocol.event_writer.table\n"
        "def table(self,builder,name,values):\n"
        " if name=='handshake':\n"
        "  if mode=='no-batch': values=values|{'Batch':False}\n"
        "  if mode=='streaming': values=values|{'StreamingTracking':True}\n"
        " return original(self,builder,name,values)\n"
        "we_protocol.event_writer.table=table\n"
        "we_protocol.configure(sys.argv,backend='synthetic_batch_fixture',backend_version='1',"
        "sensors=['video'],devices=['cpu_poses'],checkpoint_semantics='diagnostic',"
        "checkpoint_rights='synthetic',limits={'max_frames':2 if mode=='capacity' else 120,"
        "'max_source_bytes':17179869184,'max_output_bytes':34359738368})\n"
        "request=json.loads(Path(sys.argv[sys.argv.index('--request')+1]).read_text())\n"
        "root=Path(request['job_dir'])\n"
        "with (root/'launches').open('ab') as output: output.write(b'launch\\n')\n"
        "if mode=='success':\n"
        " source=Path(__file__).parent\n"
        " shutil.copytree(source/'stages',root/'stages')\n"
        " for name in ['decode','poses','result']:\n"
        "  value=json.loads((source/(name+'.json')).read_text())\n"
        "  value['output']=str(root/'stages'/Path(value['output']).name)\n"
        "  (root/(name+'.json')).write_text(json.dumps(value))\n"
        " we_protocol.completed('poses',root/'result.json')\n"
        "elif mode in ['wrong-stage','missing-output','nonzero']:\n"
        " we_protocol.completed('decode' if mode=='wrong-stage' else "
        "'poses',root/'batch-request.json')\n"
        " if mode=='nonzero': sys.exit(7)\n"
        "else:\n"
        " we_protocol.failure(mode,'poses','synthetic failure; no solver')\n"
        " sys.exit(2)\n";
    write_bytes(config.m_worker_script, script);
}

void force_crlf_text_writes(const BatchPoseConfig& config) {
    std::ifstream input(config.m_worker_script, std::ios::binary);
    REQUIRE(input);
    const std::string K_SCRIPT {std::istreambuf_iterator<char>(input), {}};
    REQUIRE(!input.bad());
    input.close();
    // Reproduce Windows text-mode translation on every host. Binary files bypass it.
    write_bytes(config.m_worker_script,
                "import pathlib\n"
                "original_open=pathlib.Path.open\n"
                "def crlf_open(self,mode='r',*args,**kwargs):\n"
                " if 'b' not in mode and any(flag in mode for flag in 'awx') and "
                "kwargs.get('newline') is None:\n"
                "  kwargs['newline']='\\r\\n'\n"
                " return original_open(self,mode,*args,**kwargs)\n"
                "pathlib.Path.open=crlf_open\n" +
                    K_SCRIPT);
}

}  // namespace

TEST_CASE("batch worker failure mapping retains one launch journal and immutable retry failure",
          "[batch_admission][worker][batch_marker]") {
    const auto K_WINDOWS_TEXT = GENERATE(false, true);
    const auto K_MODE = GENERATE("CANCELLED",
                                 "RESOURCE_LIMIT",
                                 "UNSUPPORTED",
                                 "BAD_INPUT",
                                 "CORRUPT_ASSET",
                                 "DIAGNOSTIC_FAILURE",
                                 "wrong-stage",
                                 "missing-output",
                                 "nonzero",
                                 "no-batch",
                                 "streaming",
                                 "capacity");
    CAPTURE(K_MODE, K_WINDOWS_TEXT);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    diagnostic_worker(config, K_MODE);
    if (K_WINDOWS_TEXT) force_crlf_text_writes(config);
    std::size_t handshakes {};
    config.m_observed = [&](const WorkerEvent& event) {
        if (std::holds_alternative<WorkerHandshake>(event)) ++handshakes;
    };
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*batch)->submit(frame));
    }
    const std::string_view K_NAME(K_MODE);
    const bool K_INSUFFICIENT =
        K_NAME == "no-batch" || K_NAME == "streaming" || K_NAME == "capacity";
    const auto K_CODE =
        K_NAME == "CANCELLED"                       ? ErrorCode::CANCELLED
        : K_NAME == "RESOURCE_LIMIT"                ? ErrorCode::RESOURCE_EXHAUSTED
        : K_NAME == "UNSUPPORTED" || K_INSUFFICIENT ? ErrorCode::UNSUPPORTED_OPERATION
        : K_NAME == "BAD_INPUT" || K_NAME == "CORRUPT_ASSET" || K_NAME == "wrong-stage" ||
                K_NAME == "missing-output" || K_NAME == "nonzero"
            ? ErrorCode::INVALID_DATA
            : ErrorCode::BACKEND_FAILURE;
    const auto K_FAILED = (*batch)->finish();
    expect_error(K_FAILED, K_CODE);
    CHECK(handshakes == 1);
    const auto K_FROZEN = snapshot(fixture.root);
    const auto K_RETRY = (*batch)->finish();
    expect_error(K_RETRY, K_CODE);
    CHECK(K_RETRY.error().m_message == K_FAILED.error().m_message);
    expect_error((*batch)->acquire_dataset(), K_CODE);
    expect_error((*batch)->drain_events({1}), K_INSUFFICIENT ? ErrorCode::CANCELLED : K_CODE);
    CHECK(snapshot(fixture.root) == K_FROZEN);
    CHECK(std::filesystem::is_regular_file(config.m_job_directory / "input/journal.json"));
    if (!K_INSUFFICIENT)
        CHECK(K_FROZEN.at(path_utf8(
                  (config.m_job_directory / "launches").lexically_relative(fixture.root))) ==
              "file:launch\n");
}

TEST_CASE("batch synthetic completion drains ordered events once and retains its acquired dataset",
          "[batch_admission][worker]") {
    const auto K_RECIPE = GENERATE(PoseRecipe::SEQUENTIAL, PoseRecipe::EXHAUSTIVE_GUIDED);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    config.m_recipe = K_RECIPE;
    diagnostic_worker(config, "success");
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    CHECK((*batch)->capabilities().m_observation_capacity == 3);
    CHECK((*batch)->capabilities().m_mode == EstimatorMode::BATCH_SOLVE);
    CHECK_FALSE((*batch)->capabilities().m_streaming_tracking);
    CHECK_FALSE((*batch)->capabilities().m_covariance);
    REQUIRE((*batch)->start(K_START));
    const auto K_INITIAL = (*batch)->drain_events({1});
    REQUIRE(K_INITIAL);
    REQUIRE(K_INITIAL->size() == 1);
    CHECK(std::get<TrackingChange>(K_INITIAL->front()).m_state == TrackingState::INITIALIZING);
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*batch)->submit(frame));
    }
    REQUIRE((*batch)->finish());
    const auto K_RETAINED = (*batch)->acquire_dataset();
    REQUIRE(K_RETAINED);
    const auto K_FROZEN = snapshot(fixture.root);
    REQUIRE((*batch)->finish());
    CHECK(snapshot(fixture.root) == K_FROZEN);
    const auto K_ZERO = (*batch)->drain_events({0});
    REQUIRE(K_ZERO);
    CHECK(K_ZERO->empty());
    std::vector<PoseEvent> events;
    for (int index { 0 }; index < 7; ++index) {
        auto event = (*batch)->drain_events({1});
        REQUIRE(event);
        REQUIRE(event->size() == 1);
        events.push_back(event->front());
    }
    CHECK(std::get<SubmapCreated>(events[0]).m_submap == (*K_DATA)->m_submap);
    CHECK(std::get<MapAlignment>(events[1]).m_world_from_submap.m_scale == 1);
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        const auto& pose = std::get<PoseEstimate>(events[index + 2]);
        CHECK(pose.m_frame == (*K_DATA)->m_poses[index].m_frame);
        CHECK(pose.m_capture_time == (*K_DATA)->m_poses[index].m_capture_time);
        CHECK(pose.m_submap_from_camera.m_translation ==
              (*K_DATA)->m_poses[index].m_submap_from_camera.m_translation);
        CHECK(pose.m_revision == (*K_DATA)->m_revision);
        CHECK(pose.m_tracking == TrackingState::TRACKING);
        CHECK_FALSE(pose.m_covariance);
    }
    CHECK(std::get<TrackingChange>(events[5]).m_state == TrackingState::FINISHED);
    CHECK(std::holds_alternative<EstimatorFinished>(events[6]));
    const auto K_EMPTY = (*batch)->drain_events({std::numeric_limits<std::size_t>::max()});
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->empty());
    batch->reset();
    CHECK((*K_RETAINED)->m_source_sha256 == fixture.source_digest);
    CHECK((*K_RETAINED)->m_observations.size() == 3);
    CHECK((*K_RETAINED)->m_poses.size() == 3);
    CHECK(snapshot(fixture.root) == K_FROZEN);
}

TEST_CASE("batch resumed publication reuses identical manifests and preserves changed caller files",
          "[batch_admission][publication][batch_marker]") {
    const auto K_WINDOWS_TEXT = GENERATE(false, true);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD, K_WINDOWS_TEXT);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    auto config = configuration(fixture);
    diagnostic_worker(config, "DIAGNOSTIC_FAILURE");
    if (K_WINDOWS_TEXT) force_crlf_text_writes(config);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(K_START));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*initial)->submit(frame));
    }
    expect_error((*initial)->finish(), ErrorCode::BACKEND_FAILURE);
    initial->reset();
    // Explicitly remove this diagnostic worker's marker before owned resume admission.
    std::filesystem::remove(config.m_job_directory / "launches");
    config.m_resume_input = true;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(K_START));
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto frame = observation(**K_DATA, index);
        REQUIRE((*resumed)->submit(frame));
    }
    const auto K_JOURNAL = config.m_job_directory / "input/journal.json";
    const auto K_REQUEST = config.m_job_directory / "batch-request.json";
    if (K_FIELD == 0) write_bytes(K_JOURNAL, "changed caller journal");
    if (K_FIELD == 1) write_bytes(K_REQUEST, "changed caller request");
    if (K_FIELD == 2) {
        std::filesystem::remove(K_JOURNAL);
        create_link(fixture.root / "caller.txt", K_JOURNAL);
    }
    if (K_FIELD == 3) write_bytes(K_JOURNAL, std::string(512 * 1024 + 1, 'x'));
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*resumed)->finish(),
                 K_FIELD == 4 ? ErrorCode::BACKEND_FAILURE : ErrorCode::IO_ERROR);
    auto after = snapshot(fixture.root);
    if (K_FIELD == 4) {
        const auto K_MARKER =
            path_utf8((config.m_job_directory / "launches").lexically_relative(fixture.root));
        CHECK(after.at(K_MARKER) == "file:launch\n");
        after.erase(K_MARKER);
    }
    CHECK(after == K_BEFORE);
}

TEST_CASE("batch cancellation after admission preserves persisted input and caller retry lease",
          "[batch_admission][cancel]") {
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    auto first = observation(**K_DATA, 0);
    const auto K_LEASE = first.m_frame.m_pixels.m_bytes;
    REQUIRE((*batch)->submit(first));
    auto next = observation(**K_DATA, 1);
    const auto K_SAVED = next;
    const auto K_BEFORE = snapshot(fixture.root);
    (*batch)->request_stop();
    expect_error((*batch)->submit(next), ErrorCode::CANCELLED);
    expect_error((*batch)->submit(first), ErrorCode::CANCELLED);
    expect_error((*batch)->finish(), ErrorCode::CANCELLED);
    expect_error((*batch)->drain_events({1}), ErrorCode::CANCELLED);
    check_preserved(next, K_SAVED);
    batch->reset();
    CHECK(*K_LEASE == std::vector<std::byte>(6, std::byte { 17 }));
    CHECK(snapshot(fixture.root) == K_BEFORE);
}

TEST_CASE("batch RGB admission rejects dangling output links and linked input directories",
          "[batch_admission][paths]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    CAPTURE(K_FIELD);
    pose_fixture fixture;
    const auto K_DATA = fixture.load();
    REQUIRE(K_DATA);
    const auto K_CONFIG = configuration(fixture);
    auto batch = make_batch_pose_estimator(K_CONFIG);
    REQUIRE(batch);
    REQUIRE((*batch)->start(K_START));
    const auto K_INPUT = K_CONFIG.m_job_directory / "input";
    if (K_FIELD < 2)
        create_link(fixture.root / "missing caller output",
                    K_INPUT / (K_FIELD == 0 ? "1.ppm.tmp" : "1.ppm"));
    else {
        std::filesystem::rename(K_INPUT, fixture.root / "caller directory");
        create_link(fixture.root / "caller directory", K_INPUT, true);
    }
    auto frame = observation(**K_DATA, 0);
    const auto K_SAVED = frame;
    const auto K_BEFORE = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), ErrorCode::INVALID_DATA);
    check_preserved(frame, K_SAVED);
    CHECK(snapshot(fixture.root) == K_BEFORE);
    if (K_FIELD < 2) std::filesystem::remove(K_INPUT / (K_FIELD == 0 ? "1.ppm.tmp" : "1.ppm"));
    else {
        std::filesystem::remove(K_INPUT);
        std::filesystem::rename(fixture.root / "caller directory", K_INPUT);
    }
    const auto K_ACCEPTED = (*batch)->submit(frame);
    REQUIRE(K_ACCEPTED);
    CHECK(K_ACCEPTED->m_value == 1);
    CHECK_FALSE(frame.m_frame.m_pixels.m_bytes);
    CHECK(K_SAVED.m_frame.m_pixels.m_bytes->size() == 6);
}
