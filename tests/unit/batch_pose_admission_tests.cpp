#include "pose_fixture.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <limits>
#include <map>

using namespace world_explorer;
namespace {
using test::path_utf8;
using test::pose_fixture;
constexpr std::size_t METADATA_BYTES = std::size_t{2} * 1024 * 1024;
constexpr pose_estimator_config START{{METADATA_BYTES + 6}, 3};
constexpr std::size_t PPM_BYTES = std::string_view("P6\n2 1\n255\n").size() + 6;
void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(output);
}
using file_tree = std::map<std::string, std::string>;
file_tree snapshot(const std::filesystem::path& root) {
    file_tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = path_utf8(entry.path().lexically_relative(root));
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status))
            result.emplace(name, "link:" + path_utf8(std::filesystem::read_symlink(entry.path())));
        else if (std::filesystem::is_directory(status))
            result.emplace(name, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(status));
            REQUIRE(entry.file_size() <= std::size_t{2} * 1024 * 1024);
            std::ifstream input(entry.path(), std::ios::binary);
            REQUIRE(input);
            std::string bytes{std::istreambuf_iterator<char>(input), {}};
            REQUIRE(!input.bad());
            result.emplace(name, "file:" + bytes);
        }
    }
    return result;
}
void create_link(const std::filesystem::path& target, const std::filesystem::path& link,
                 bool directory = false) {
    std::error_code error;
    if (directory)
        std::filesystem::create_directory_symlink(target, link, error);
    else
        std::filesystem::create_symlink(target, link, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation is unavailable to this Windows account");
#endif
    INFO(error.message());
    REQUIRE(!error);
}
batch_pose_config configuration(const pose_fixture& fixture) {
    const auto script = fixture.root / "diagnostic worker.py";
    write_bytes(script, "raise SystemExit('Synthetic admission test must not launch a worker')\n");
    write_bytes(fixture.root / "caller.txt", "caller data outside batch output");
    return {.decode_stage = fixture.decoded,
            .job_directory = fixture.root / "fresh batch",
            .worker_script = script,
            .source_sha256 = fixture.source_digest,
            .worker_python = WE_TEST_PYTHON,
            .pose_python = WE_TEST_PYTHON};
}
frame_observation observation(const completed_pose_dataset& data, std::size_t index) {
    const auto& record = data.observations[index];
    return {{index + 1},
            {record.id,
             record.capture,
             record.capture_time,
             record.timestamp,
             record.calibration,
             {std::make_shared<const std::vector<std::byte>>(6, std::byte{17}), 2, 1, 6,
              pixel_format::RGB8},
             {},
             {}}};
}
template <class value> void expect_error(const result<value>& outcome, error_code code) {
    INFO((outcome ? "unexpected success" : outcome.error().message));
    REQUIRE(!outcome);
    CHECK(outcome.error().code == code);
}
void check_preserved(const frame_observation& actual, const frame_observation& before) {
    CHECK(actual.operation == before.operation);
    const auto& frame = actual.frame;
    const auto& saved = before.frame;
    CHECK(frame.id == saved.id);
    CHECK(frame.capture == saved.capture);
    CHECK(frame.capture_time == saved.capture_time);
    CHECK(frame.original_timestamp.pts == saved.original_timestamp.pts);
    CHECK(frame.original_timestamp.time_base_numerator ==
          saved.original_timestamp.time_base_numerator);
    CHECK(frame.original_timestamp.time_base_denominator ==
          saved.original_timestamp.time_base_denominator);
    CHECK(frame.calibration == saved.calibration);
    CHECK(frame.pixels.bytes == saved.pixels.bytes);
    if (frame.pixels.bytes && saved.pixels.bytes)
        CHECK(*frame.pixels.bytes == *saved.pixels.bytes);
    CHECK(frame.pixels.width == saved.pixels.width);
    CHECK(frame.pixels.height == saved.pixels.height);
    CHECK(frame.pixels.row_stride == saved.pixels.row_stride);
    CHECK(frame.pixels.format == saved.pixels.format);
    REQUIRE(frame.measured_depth.has_value() == saved.measured_depth.has_value());
    if (frame.measured_depth && saved.measured_depth) {
        CHECK(frame.measured_depth->bytes == saved.measured_depth->bytes);
        CHECK(frame.measured_depth->width == saved.measured_depth->width);
        CHECK(frame.measured_depth->height == saved.measured_depth->height);
        CHECK(frame.measured_depth->row_stride == saved.measured_depth->row_stride);
        CHECK(frame.measured_depth->format == saved.measured_depth->format);
    }
    REQUIRE(frame.imu_samples.size() == saved.imu_samples.size());
    for (std::size_t index = 0; index < frame.imu_samples.size(); ++index) {
        CHECK(frame.imu_samples[index].capture_time == saved.imu_samples[index].capture_time);
        CHECK(frame.imu_samples[index].acceleration == saved.imu_samples[index].acceleration);
        CHECK(frame.imu_samples[index].angular_velocity ==
              saved.imu_samples[index].angular_velocity);
    }
}
} // namespace

TEST_CASE("batch factory validates configuration before creating output",
          "[batch_admission][config]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(field);
    pose_fixture fixture;
    const auto original = configuration(fixture);
    auto config = original;
    if (field == 0)
        config.source_sha256.clear();
    if (field == 1)
        config.source_sha256[0] = 'X';
    if (field == 2)
        config.job_directory.clear();
    if (field == 3)
        config.worker_python.clear();
    if (field == 4)
        config.pose_python.clear();
    if (field == 5)
        config.worker_python = std::string(4097, 'x');
    if (field == 6)
        config.pose_python = std::string(4097, 'x');
    if (field == 7)
        config.worker_python.push_back('\0');
    if (field == 8)
        config.pose_python.push_back('\0');
    if (field == 9)
        config.cpu_threads = 0;
    if (field == 10)
        config.cpu_threads = 5;
    if (field == 11)
        config.journal_bytes.bytes = METADATA_BYTES;
    if (field == 12)
        config.journal_bytes.bytes = 32ULL * 1024 * 1024 * 1024 + 1;
    if (field == 13)
        config.recipe = std::bit_cast<pose_recipe>(std::uint8_t{255});
    if (field == 14)
        config.worker_script = fixture.root / "missing worker";
    if (field == 15)
        config.worker_script = fixture.decoded;
    const auto before = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config), error_code::INVALID_ARGUMENT);
    CHECK(snapshot(fixture.root) == before);
    REQUIRE(make_batch_pose_estimator(original));
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("batch factory propagates cancellation and decoded selection rejection",
          "[batch_admission][config]") {
    const auto field = GENERATE(0, 1, 2);
    CAPTURE(field);
    pose_fixture fixture;
    auto config = configuration(fixture);
    std::stop_source stop;
    if (field == 0)
        stop.request_stop();
    if (field == 1)
        config.decode_stage = fixture.root / "missing decode";
    if (field == 2) {
        std::ifstream input(fixture.decoded / "result.json");
        auto value = nlohmann::json::parse(input);
        input.close();
        value["frames"].erase(2);
        write_bytes(fixture.decoded / "result.json", value.dump());
        fixture.inventory(fixture.decoded);
    }
    const auto before = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config, stop.get_token()),
                 field == 0 ? error_code::CANCELLED : error_code::INVALID_DATA);
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("batch start and pre-start operations preserve caller data on rejection",
          "[batch_admission][start]") {
    const auto field = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    auto frame = observation(**data, 0);
    const auto saved = frame;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), error_code::INVALID_ARGUMENT);
    expect_error((*batch)->finish(), error_code::INVALID_ARGUMENT);
    expect_error((*batch)->drain_events({1}), error_code::INVALID_ARGUMENT);
    expect_error((*batch)->acquire_dataset(), error_code::WOULD_BLOCK);
    check_preserved(frame, saved);
    auto start = START;
    if (field == 0)
        start.max_pending_observations = 2;
    if (field == 1)
        start.max_pending_observations = 121;
    if (field == 2)
        start.memory.bytes = METADATA_BYTES;
    if (field == 3)
        start.memory.bytes = 0;
    if (field == 4)
        (*batch)->request_stop();
    expect_error((*batch)->start(start),
                 field == 4 ? error_code::CANCELLED : error_code::INVALID_ARGUMENT);
    CHECK(snapshot(fixture.root) == before);
    if (field == 4) {
        expect_error((*batch)->submit(frame), error_code::CANCELLED);
        expect_error((*batch)->finish(), error_code::CANCELLED);
        expect_error((*batch)->drain_events({1}), error_code::CANCELLED);
        check_preserved(frame, saved);
        CHECK(snapshot(fixture.root) == before);
    } else {
        REQUIRE((*batch)->start(START));
        const auto started = snapshot(fixture.root);
        expect_error((*batch)->start(START), error_code::INVALID_ARGUMENT);
        CHECK(snapshot(fixture.root) == started);
        REQUIRE((*batch)->submit(frame));
        CHECK_FALSE(frame.frame.pixels.bytes);
    }
}

TEST_CASE("batch ordered RGB admission rejects changed headers and unsupported sensors",
          "[batch_admission][observation]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    auto frame = observation(**data, 0);
    if (field == 0)
        frame.operation = {};
    if (field == 1)
        ++frame.frame.id.value;
    if (field == 2)
        ++frame.frame.capture.value;
    if (field == 3)
        ++frame.frame.calibration.value;
    if (field == 4)
        frame.frame.capture_time += std::chrono::nanoseconds(1);
    if (field == 5)
        ++frame.frame.original_timestamp.pts;
    if (field == 6)
        ++frame.frame.original_timestamp.time_base_numerator;
    if (field == 7)
        ++frame.frame.original_timestamp.time_base_denominator;
    if (field == 8)
        ++frame.frame.pixels.width;
    if (field == 9)
        ++frame.frame.pixels.height;
    if (field == 10)
        ++frame.frame.pixels.row_stride;
    if (field == 11)
        frame.frame.pixels.bytes.reset();
    if (field == 12)
        frame.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(5);
    if (field == 13)
        frame.frame.pixels.format = pixel_format::GRAY8;
    if (field == 14)
        frame.frame.measured_depth = frame.frame.pixels;
    if (field == 15)
        frame.frame.imu_samples = {{std::chrono::nanoseconds{4}, {1, 2, 3}, {4, 5, 6}}};
    if (field == 16)
        frame = observation(**data, 1);
    const auto saved = frame;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), field == 0 ? error_code::INVALID_ARGUMENT
                                          : field >= 13 && field <= 15
                                              ? error_code::UNSUPPORTED_OPERATION
                                              : error_code::INVALID_DATA);
    check_preserved(frame, saved);
    CHECK(snapshot(fixture.root) == before);
    auto corrected = observation(**data, 0);
    const auto lease = corrected.frame.pixels.bytes;
    const auto ticket = (*batch)->submit(corrected);
    REQUIRE(ticket);
    CHECK(ticket->value == 1);
    CHECK_FALSE(corrected.frame.pixels.bytes);
    REQUIRE(lease);
    CHECK(*lease == std::vector<std::byte>(6, std::byte{17}));
    const auto accepted = snapshot(fixture.root);
    const auto retry = (*batch)->submit(corrected);
    REQUIRE(retry);
    CHECK(retry->value == ticket->value);
    CHECK(snapshot(fixture.root) == accepted);
}

TEST_CASE("batch retries preserve receipts and files when headers or pixel content differ",
          "[batch_admission][retry]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    auto first = observation(**data, 0);
    auto changed = first;
    const auto lease = first.frame.pixels.bytes;
    const auto ticket = (*batch)->submit(first);
    REQUIRE(ticket);
    if (field == 0)
        ++changed.frame.capture.value;
    if (field == 1)
        changed.frame.pixels.bytes = std::make_shared<const std::vector<std::byte>>(5);
    if (field == 2)
        changed.frame.pixels.bytes =
            std::make_shared<const std::vector<std::byte>>(6, std::byte{18});
    if (field == 3)
        changed.frame.measured_depth = changed.frame.pixels;
    if (field == 4)
        changed.frame.imu_samples.push_back({});
    if (field == 5) {
        changed.frame.pixels.bytes.reset();
        ++changed.frame.original_timestamp.pts;
    }
    const auto saved = changed;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(changed), error_code::DUPLICATE_OPERATION);
    check_preserved(changed, saved);
    CHECK(snapshot(fixture.root) == before);
    const auto retried = (*batch)->submit(first);
    REQUIRE(retried);
    CHECK(retried->value == ticket->value);
    auto intact = observation(**data, 0);
    const auto with_pixels = (*batch)->submit(intact);
    REQUIRE(with_pixels);
    CHECK(with_pixels->value == ticket->value);
    CHECK_FALSE(intact.frame.pixels.bytes);
    CHECK(*lease == std::vector<std::byte>(6, std::byte{17}));
    CHECK(snapshot(fixture.root) == before);
    auto second = observation(**data, 1);
    const auto next = (*batch)->submit(second);
    REQUIRE(next);
    CHECK(next->value == 2);
}

TEST_CASE("batch memory and first RGB asset budgets admit the exact bound",
          "[batch_admission][budget]") {
    const auto field = GENERATE(0, 1);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    config.journal_bytes.bytes = METADATA_BYTES + PPM_BYTES - (field == 1 ? 1 : 0);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    auto start = START;
    if (field == 0)
        --start.memory.bytes;
    REQUIRE((*batch)->start(start));
    auto frame = observation(**data, 0);
    const auto saved = frame;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), error_code::RESOURCE_EXHAUSTED);
    check_preserved(frame, saved);
    CHECK(snapshot(fixture.root) == before);
    config.job_directory = fixture.root / "exact budget";
    config.journal_bytes.bytes = METADATA_BYTES + PPM_BYTES;
    auto exact = make_batch_pose_estimator(config);
    REQUIRE(exact);
    REQUIRE((*exact)->start(START));
    REQUIRE((*exact)->submit(frame));
    CHECK_FALSE(frame.frame.pixels.bytes);
    CHECK(std::filesystem::file_size(config.job_directory / "input/1.ppm") == PPM_BYTES);
    auto second = observation(**data, 1);
    const auto retained = second;
    const auto full = snapshot(fixture.root);
    expect_error((*exact)->submit(second), error_code::RESOURCE_EXHAUSTED);
    check_preserved(second, retained);
    CHECK(snapshot(fixture.root) == full);
}

TEST_CASE("batch cumulative disk admission and full capacity preserve receipts for resume",
          "[batch_admission][budget]") {
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    config.journal_bytes.bytes = METADATA_BYTES + 3 * PPM_BYTES - 1;
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    for (std::size_t index = 0; index < 2; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*batch)->submit(frame));
    }
    auto third = observation(**data, 2);
    const auto saved = third;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(third), error_code::RESOURCE_EXHAUSTED);
    check_preserved(third, saved);
    CHECK(snapshot(fixture.root) == before);
    batch->reset();
    config.resume_input = true;
    ++config.journal_bytes.bytes;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(START));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        const auto ticket = (*resumed)->submit(frame);
        REQUIRE(ticket);
        CHECK(ticket->value == index + 1);
        CHECK_FALSE(frame.frame.pixels.bytes);
    }
    auto extra = observation(**data, 0);
    extra.operation = {4};
    const auto retained = extra;
    const auto full = snapshot(fixture.root);
    expect_error((*resumed)->submit(extra), error_code::WOULD_BLOCK);
    check_preserved(extra, retained);
    auto retry = observation(**data, 0);
    const auto ticket = (*resumed)->submit(retry);
    REQUIRE(ticket);
    CHECK(ticket->value == 1);
    CHECK(snapshot(fixture.root) == full);
    const auto initial = (*resumed)->drain_events({0});
    REQUIRE(initial);
    CHECK(initial->empty());
    const auto event = (*resumed)->drain_events({std::numeric_limits<std::size_t>::max()});
    REQUIRE(event);
    REQUIRE(event->size() == 1);
    CHECK(std::get<tracking_change>(event->front()).state == tracking_state::INITIALIZING);
    const auto empty = (*resumed)->drain_events({1});
    REQUIRE(empty);
    CHECK(empty->empty());
    CHECK(snapshot(fixture.root) == full);
}

TEST_CASE("batch resumed start rejects foreign entries locks and linked inputs without writes",
          "[batch_admission][resume]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(START));
    auto first = observation(**data, 0);
    REQUIRE((*initial)->submit(first));
    initial->reset();
    config.resume_input = true;
    std::filesystem::path obstruction;
    if (field == 0)
        obstruction = config.job_directory / "foreign.txt";
    if (field == 1)
        obstruction = config.job_directory / "job.lock";
    if (field == 2)
        obstruction = config.job_directory / "input/2.ppm.tmp";
    if (field == 3)
        obstruction = config.job_directory / "input/2.ppm";
    if (field == 4)
        obstruction = config.job_directory / "request.json";
    if (field == 5)
        obstruction = config.job_directory / "input/2.ppm";
    if (field < 3)
        write_bytes(obstruction, "caller-owned obstruction");
    if (field == 3)
        std::filesystem::create_directory(obstruction);
    if (field == 4 || field == 5)
        create_link(fixture.root / "caller.txt", obstruction);
    if (field == 6) {
        obstruction = config.job_directory / "input";
        std::filesystem::rename(obstruction, fixture.root / "caller input");
        create_link(fixture.root / "caller input", obstruction, true);
    }
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    const auto before = snapshot(fixture.root);
    expect_error((*resumed)->start(START),
                 field == 1 ? error_code::WOULD_BLOCK : error_code::INVALID_DATA);
    CHECK(snapshot(fixture.root) == before);
    std::filesystem::remove(obstruction);
    if (field == 6)
        std::filesystem::rename(fixture.root / "caller input", obstruction);
    REQUIRE((*resumed)->start(START));
    const auto ready = snapshot(fixture.root);
    auto replay = observation(**data, 0);
    REQUIRE((*resumed)->submit(replay));
    CHECK_FALSE(replay.frame.pixels.bytes);
    CHECK(snapshot(fixture.root) == ready);
}

TEST_CASE("batch resumed RGB rejects changed shape content or symlink and permits corrected retry",
          "[batch_admission][resume]") {
    const auto field = GENERATE(0, 1, 2);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(START));
    auto first = observation(**data, 0);
    REQUIRE((*initial)->submit(first));
    initial->reset();
    config.resume_input = true;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(START));
    const auto path = config.job_directory / "input/1.ppm";
    if (field < 2)
        write_bytes(path, std::string(field == 0 ? PPM_BYTES - 1 : PPM_BYTES, 'x'));
    else {
        std::filesystem::remove(path);
        create_link(fixture.root / "caller.txt", path);
    }
    auto replay = observation(**data, 0);
    const auto saved = replay;
    const auto before = snapshot(fixture.root);
    expect_error((*resumed)->submit(replay), error_code::INVALID_DATA);
    check_preserved(replay, saved);
    CHECK(snapshot(fixture.root) == before);
    std::filesystem::remove(path);
    write_bytes(path, std::string("P6\n2 1\n255\n") + std::string(6, char{17}));
    const auto ready = snapshot(fixture.root);
    const auto ticket = (*resumed)->submit(replay);
    REQUIRE(ticket);
    CHECK(ticket->value == 1);
    CHECK_FALSE(replay.frame.pixels.bytes);
    CHECK(snapshot(fixture.root) == ready);
}

TEST_CASE("batch factory rejects linked job roots without accessing the target",
          "[batch_admission][paths]") {
    pose_fixture fixture;
    auto config = configuration(fixture);
    const auto target = fixture.root / "caller directory";
    std::filesystem::create_directory(target);
    write_bytes(target / "caller.txt", "retained caller data");
    create_link(target, config.job_directory, true);
    config.resume_input = true;
    const auto before = snapshot(fixture.root);
    expect_error(make_batch_pose_estimator(config), error_code::INVALID_DATA);
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("batch failed publication preserves obstructions and freezes closed admission failure",
          "[batch_admission][publication]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*batch)->submit(frame));
    }
    const auto journal = config.job_directory / "input/journal.json";
    const auto request = config.job_directory / "batch-request.json";
    if (field == 0)
        write_bytes(journal, "caller journal");
    if (field == 1)
        write_bytes(journal.string() + ".tmp", "caller temporary journal");
    if (field == 2)
        create_link(fixture.root / "missing caller asset", journal.string() + ".tmp");
    if (field == 3)
        write_bytes(request, "caller request");
    if (field == 4)
        create_link(fixture.root / "missing caller asset", request.string() + ".tmp");
    if (field >= 5) {
        const auto path = field == 5 ? config.job_directory / "input" : config.job_directory;
        std::filesystem::rename(path, fixture.root / "caller output");
        create_link(fixture.root / "caller output", path, true);
    }
    const auto before = snapshot(fixture.root);
    const auto failed = (*batch)->finish();
    expect_error(failed, field >= 5 ? error_code::INVALID_DATA : error_code::IO_ERROR);
    auto after = snapshot(fixture.root);
    if (field == 3 || field == 4) {
        // Journal publication succeeds before the independent request obstruction.
        REQUIRE(after.erase(path_utf8(journal.lexically_relative(fixture.root))) == 1);
    }
    CHECK(after == before);
    const auto frozen = snapshot(fixture.root);
    const auto retried = (*batch)->finish();
    expect_error(retried, failed.error().code);
    CHECK(retried.error().message == failed.error().message);
    expect_error((*batch)->drain_events({1}), failed.error().code);
    expect_error((*batch)->acquire_dataset(), failed.error().code);
    auto new_operation = observation(**data, 0);
    new_operation.operation = {4};
    const auto saved = new_operation;
    expect_error((*batch)->submit(new_operation), error_code::INVALID_ARGUMENT);
    check_preserved(new_operation, saved);
    auto acknowledged = observation(**data, 0);
    const auto ticket = (*batch)->submit(acknowledged);
    REQUIRE(ticket);
    CHECK(ticket->value == 1);
    CHECK_FALSE(acknowledged.frame.pixels.bytes);
    CHECK(snapshot(fixture.root) == frozen);
}

namespace {
void diagnostic_worker(const batch_pose_config& config, std::string_view mode) {
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
    write_bytes(config.worker_script, script);
}
void force_crlf_text_writes(const batch_pose_config& config) {
    std::ifstream input(config.worker_script, std::ios::binary);
    REQUIRE(input);
    const std::string script{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(!input.bad());
    input.close();
    // Reproduce Windows text-mode translation on every host. Binary files bypass it.
    write_bytes(config.worker_script,
                "import pathlib\n"
                "original_open=pathlib.Path.open\n"
                "def crlf_open(self,mode='r',*args,**kwargs):\n"
                " if 'b' not in mode and any(flag in mode for flag in 'awx') and "
                "kwargs.get('newline') is None:\n"
                "  kwargs['newline']='\\r\\n'\n"
                " return original_open(self,mode,*args,**kwargs)\n"
                "pathlib.Path.open=crlf_open\n" +
                    script);
}
} // namespace

TEST_CASE("batch worker failure mapping retains one launch journal and immutable retry failure",
          "[batch_admission][worker][batch_marker]") {
    const auto windows_text = GENERATE(false, true);
    const auto mode = GENERATE("CANCELLED", "RESOURCE_LIMIT", "UNSUPPORTED", "BAD_INPUT",
                               "CORRUPT_ASSET", "DIAGNOSTIC_FAILURE", "wrong-stage",
                               "missing-output", "nonzero", "no-batch", "streaming", "capacity");
    CAPTURE(mode, windows_text);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    diagnostic_worker(config, mode);
    if (windows_text)
        force_crlf_text_writes(config);
    std::size_t handshakes{};
    config.observed = [&](const worker_event& event) {
        if (std::holds_alternative<worker_handshake>(event))
            ++handshakes;
    };
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*batch)->submit(frame));
    }
    const std::string_view name(mode);
    const bool insufficient = name == "no-batch" || name == "streaming" || name == "capacity";
    const auto code = name == "CANCELLED"                     ? error_code::CANCELLED
                      : name == "RESOURCE_LIMIT"              ? error_code::RESOURCE_EXHAUSTED
                      : name == "UNSUPPORTED" || insufficient ? error_code::UNSUPPORTED_OPERATION
                      : name == "BAD_INPUT" || name == "CORRUPT_ASSET" || name == "wrong-stage" ||
                              name == "missing-output" || name == "nonzero"
                          ? error_code::INVALID_DATA
                          : error_code::BACKEND_FAILURE;
    const auto failed = (*batch)->finish();
    expect_error(failed, code);
    CHECK(handshakes == 1);
    const auto frozen = snapshot(fixture.root);
    const auto retry = (*batch)->finish();
    expect_error(retry, code);
    CHECK(retry.error().message == failed.error().message);
    expect_error((*batch)->acquire_dataset(), code);
    expect_error((*batch)->drain_events({1}), insufficient ? error_code::CANCELLED : code);
    CHECK(snapshot(fixture.root) == frozen);
    CHECK(std::filesystem::is_regular_file(config.job_directory / "input/journal.json"));
    if (!insufficient)
        CHECK(frozen.at(path_utf8(
                  (config.job_directory / "launches").lexically_relative(fixture.root))) ==
              "file:launch\n");
}

TEST_CASE("batch synthetic completion drains ordered events once and retains its acquired dataset",
          "[batch_admission][worker]") {
    const auto recipe = GENERATE(pose_recipe::SEQUENTIAL, pose_recipe::EXHAUSTIVE_GUIDED);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    config.recipe = recipe;
    diagnostic_worker(config, "success");
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    CHECK((*batch)->capabilities().observation_capacity == 3);
    CHECK((*batch)->capabilities().mode == estimator_mode::BATCH_SOLVE);
    CHECK_FALSE((*batch)->capabilities().streaming_tracking);
    CHECK_FALSE((*batch)->capabilities().covariance);
    REQUIRE((*batch)->start(START));
    const auto initial = (*batch)->drain_events({1});
    REQUIRE(initial);
    REQUIRE(initial->size() == 1);
    CHECK(std::get<tracking_change>(initial->front()).state == tracking_state::INITIALIZING);
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*batch)->submit(frame));
    }
    REQUIRE((*batch)->finish());
    const auto retained = (*batch)->acquire_dataset();
    REQUIRE(retained);
    const auto frozen = snapshot(fixture.root);
    REQUIRE((*batch)->finish());
    CHECK(snapshot(fixture.root) == frozen);
    const auto zero = (*batch)->drain_events({0});
    REQUIRE(zero);
    CHECK(zero->empty());
    std::vector<pose_event> events;
    for (int index = 0; index < 7; ++index) {
        auto event = (*batch)->drain_events({1});
        REQUIRE(event);
        REQUIRE(event->size() == 1);
        events.push_back(event->front());
    }
    CHECK(std::get<submap_created>(events[0]).submap == (*data)->submap);
    CHECK(std::get<map_alignment>(events[1]).world_from_submap.scale == 1);
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& pose = std::get<pose_estimate>(events[index + 2]);
        CHECK(pose.frame == (*data)->poses[index].frame);
        CHECK(pose.capture_time == (*data)->poses[index].capture_time);
        CHECK(pose.submap_from_camera.translation ==
              (*data)->poses[index].submap_from_camera.translation);
        CHECK(pose.revision == (*data)->revision);
        CHECK(pose.tracking == tracking_state::TRACKING);
        CHECK_FALSE(pose.covariance);
    }
    CHECK(std::get<tracking_change>(events[5]).state == tracking_state::FINISHED);
    CHECK(std::holds_alternative<estimator_finished>(events[6]));
    const auto empty = (*batch)->drain_events({std::numeric_limits<std::size_t>::max()});
    REQUIRE(empty);
    CHECK(empty->empty());
    batch->reset();
    CHECK((*retained)->source_sha256 == fixture.source_digest);
    CHECK((*retained)->observations.size() == 3);
    CHECK((*retained)->poses.size() == 3);
    CHECK(snapshot(fixture.root) == frozen);
}

TEST_CASE("batch resumed publication reuses identical manifests and preserves changed caller files",
          "[batch_admission][publication][batch_marker]") {
    const auto windows_text = GENERATE(false, true);
    const auto field = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(field, windows_text);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    auto config = configuration(fixture);
    diagnostic_worker(config, "DIAGNOSTIC_FAILURE");
    if (windows_text)
        force_crlf_text_writes(config);
    auto initial = make_batch_pose_estimator(config);
    REQUIRE(initial);
    REQUIRE((*initial)->start(START));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*initial)->submit(frame));
    }
    expect_error((*initial)->finish(), error_code::BACKEND_FAILURE);
    initial->reset();
    // Explicitly remove this diagnostic worker's marker before owned resume admission.
    std::filesystem::remove(config.job_directory / "launches");
    config.resume_input = true;
    auto resumed = make_batch_pose_estimator(config);
    REQUIRE(resumed);
    REQUIRE((*resumed)->start(START));
    for (std::size_t index = 0; index < 3; ++index) {
        auto frame = observation(**data, index);
        REQUIRE((*resumed)->submit(frame));
    }
    const auto journal = config.job_directory / "input/journal.json";
    const auto request = config.job_directory / "batch-request.json";
    if (field == 0)
        write_bytes(journal, "changed caller journal");
    if (field == 1)
        write_bytes(request, "changed caller request");
    if (field == 2) {
        std::filesystem::remove(journal);
        create_link(fixture.root / "caller.txt", journal);
    }
    if (field == 3)
        write_bytes(journal, std::string(512 * 1024 + 1, 'x'));
    const auto before = snapshot(fixture.root);
    expect_error((*resumed)->finish(),
                 field == 4 ? error_code::BACKEND_FAILURE : error_code::IO_ERROR);
    auto after = snapshot(fixture.root);
    if (field == 4) {
        const auto marker =
            path_utf8((config.job_directory / "launches").lexically_relative(fixture.root));
        CHECK(after.at(marker) == "file:launch\n");
        after.erase(marker);
    }
    CHECK(after == before);
}

TEST_CASE("batch cancellation after admission preserves persisted input and caller retry lease",
          "[batch_admission][cancel]") {
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    auto first = observation(**data, 0);
    const auto lease = first.frame.pixels.bytes;
    REQUIRE((*batch)->submit(first));
    auto next = observation(**data, 1);
    const auto saved = next;
    const auto before = snapshot(fixture.root);
    (*batch)->request_stop();
    expect_error((*batch)->submit(next), error_code::CANCELLED);
    expect_error((*batch)->submit(first), error_code::CANCELLED);
    expect_error((*batch)->finish(), error_code::CANCELLED);
    expect_error((*batch)->drain_events({1}), error_code::CANCELLED);
    check_preserved(next, saved);
    batch->reset();
    CHECK(*lease == std::vector<std::byte>(6, std::byte{17}));
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("batch RGB admission rejects dangling output links and linked input directories",
          "[batch_admission][paths]") {
    const auto field = GENERATE(0, 1, 2);
    CAPTURE(field);
    pose_fixture fixture;
    const auto data = fixture.load();
    REQUIRE(data);
    const auto config = configuration(fixture);
    auto batch = make_batch_pose_estimator(config);
    REQUIRE(batch);
    REQUIRE((*batch)->start(START));
    const auto input = config.job_directory / "input";
    if (field < 2)
        create_link(fixture.root / "missing caller output",
                    input / (field == 0 ? "1.ppm.tmp" : "1.ppm"));
    else {
        std::filesystem::rename(input, fixture.root / "caller directory");
        create_link(fixture.root / "caller directory", input, true);
    }
    auto frame = observation(**data, 0);
    const auto saved = frame;
    const auto before = snapshot(fixture.root);
    expect_error((*batch)->submit(frame), error_code::INVALID_DATA);
    check_preserved(frame, saved);
    CHECK(snapshot(fixture.root) == before);
    if (field < 2)
        std::filesystem::remove(input / (field == 0 ? "1.ppm.tmp" : "1.ppm"));
    else {
        std::filesystem::remove(input);
        std::filesystem::rename(fixture.root / "caller directory", input);
    }
    const auto accepted = (*batch)->submit(frame);
    REQUIRE(accepted);
    CHECK(accepted->value == 1);
    CHECK_FALSE(frame.frame.pixels.bytes);
    CHECK(saved.frame.pixels.bytes->size() == 6);
}
