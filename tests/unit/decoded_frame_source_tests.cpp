#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace world_explorer;
namespace {
struct decoded_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_replay_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string key = std::string(64, 'a');
    std::string source_digest = std::string(64, 'b');
    std::filesystem::path stage = root / ("decode-" + key);
    std::filesystem::path decoder = root / "diagnostic decoder.py";
    nlohmann::json metadata;
    decoded_fixture() {
        std::filesystem::create_directories(stage / "images");
        constexpr std::array<unsigned char, 72> PNG{
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130};
        const auto digest = sha256(std::as_bytes(std::span(PNG)));
        for (const auto* name : {"first.png", "second.png"}) {
            std::ofstream image(stage / "images" / name, std::ios::binary);
            image.write(reinterpret_cast<const char*>(PNG.data()),
                        static_cast<std::streamsize>(PNG.size()));
        }
        metadata = {{"selection", "timestamp_sampling_v1"},
                    {"crop", "none"},
                    {"resize", "none"},
                    {"source_frame_count", 11},
                    {"frames", nlohmann::json::array()}};
        std::size_t count{};
        for (const auto index : {0, 10}) {
            metadata["frames"].push_back(
                {{"source_index", index},
                 {"frame_id", source_digest.substr(0, 16) + "-" + std::to_string(index)},
                 {"calibration_id", source_digest.substr(0, 16) + "-lens-0"},
                 {"pts", index},
                 {"time_base", "1/30"},
                 {"capture_time_ns", index == 0 ? 0 : 333333333},
                 {"file", count++ == 0 ? "images/first.png" : "images/second.png"},
                 {"sha256", digest}});
        }
        publish();
        write_decoder();
    }
    ~decoded_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void write_decoder() const {
        std::ofstream script(decoder);
        script << "import sys,time\nfrom pathlib import Path\n"
                  "mode=sys.argv[1]\n"
                  "if mode=='wait': time.sleep(20)\n"
                  "Path(sys.argv[sys.argv.index('-i')+1]).read_bytes()\n"
                  "print('decoder diagnostic',file=sys.stderr)\n"
                  "pixels=bytes([17,34,51,68,85,102])\n"
                  "sys.stdout.buffer.write(pixels[:2] if mode=='short' else "
                  "pixels+b'x' if mode=='large' else pixels)\n";
    }
    void publish() const {
        std::ofstream(stage / "result.json") << metadata.dump();
        auto inventory = nlohmann::json::array();
        for (const auto* name : {"result.json", "images/first.png", "images/second.png"}) {
            const auto path = stage / name;
            inventory.push_back({{"path", name},
                                 {"bytes", std::filesystem::file_size(path)},
                                 {"sha256", *sha256_file(path, 4096)}});
        }
        std::ofstream(stage / "completion.json") << nlohmann::json{
            {"schema_version", 1},
            {"status", "complete"},
            {"key", key},
            {"files", inventory}}.dump();
    }
    decoded_source_config config(const std::string& mode = "good") const {
        return {.stage_directory = stage,
                .source_sha256 = source_digest,
                .decoder_arguments = {WE_TEST_PYTHON, decoder.string(), mode},
                .max_image_bytes = {6}};
    }
};
frame_packet next_frame(frame_source& source) {
    const auto event = source.read_next({});
    REQUIRE(event);
    REQUIRE(std::holds_alternative<frame_packet>(*event));
    return std::get<frame_packet>(*event);
}
} // namespace
TEST_CASE("decoded replay preserves leased RGB pixels stable IDs timestamps and seek resets") {
    decoded_fixture fixture;
    SECTION("UTF-8 image filename") {
        const auto filename = std::filesystem::path(u8"images/first_é_房.png");
        const auto encoded = filename.generic_u8string();
        const std::string name(encoded.begin(), encoded.end());
        std::filesystem::copy_file(fixture.stage / "images/first.png", fixture.stage / filename);
        fixture.metadata["frames"][0]["file"] = name;
        fixture.publish();
        nlohmann::json completion;
        std::ifstream(fixture.stage / "completion.json") >> completion;
        const auto path = fixture.stage / filename;
        completion["files"].push_back({{"path", name},
                                       {"bytes", std::filesystem::file_size(path)},
                                       {"sha256", *sha256_file(path, 4096)}});
        std::ofstream(fixture.stage / "completion.json") << completion.dump();
    }
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK((*source)->capabilities().seekable);
    CHECK_FALSE((*source)->capabilities().measured_depth);
    CHECK_FALSE((*source)->capabilities().imu);
    CHECK((*source)->capabilities().max_image_bytes.bytes == 6);
    const auto reset = (*source)->read_next({});
    REQUIRE(reset);
    const auto segment = std::get<source_reset>(*reset);
    CHECK(segment.epoch.value == 0);
    const auto first = next_frame(**source);
    const auto second = next_frame(**source);
    CHECK(first.id != second.id);
    CHECK(first.capture == segment.capture);
    CHECK(first.capture == second.capture);
    CHECK(first.calibration == second.calibration);
    CHECK(first.original_timestamp.pts == 0);
    CHECK(second.original_timestamp.pts == 10);
    CHECK(second.original_timestamp.time_base_numerator == 1);
    CHECK(second.original_timestamp.time_base_denominator == 30);
    CHECK(second.capture_time.count() == 333333333);
    REQUIRE(first.pixels.bytes);
    CHECK(first.pixels.width == 2);
    CHECK(first.pixels.height == 1);
    CHECK(first.pixels.row_stride == 6);
    CHECK(first.pixels.format == pixel_format::RGB8);
    CHECK_FALSE(first.measured_depth);
    CHECK(first.imu_samples.empty());
    REQUIRE(std::holds_alternative<source_end>(*(*source)->read_next({})));
    REQUIRE((*source)->seek({1, 1, 3}));
    const auto sought_reset = (*source)->read_next({});
    REQUIRE(sought_reset);
    CHECK(std::get<source_reset>(*sought_reset).epoch.value == 1);
    CHECK(next_frame(**source).id == second.id);
    REQUIRE((*source)->seek({40, 1, 30}));
    REQUIRE(std::holds_alternative<source_reset>(*(*source)->read_next({})));
    REQUIRE(std::holds_alternative<source_end>(*(*source)->read_next({})));
    auto reopened = open_decoded_frame_source(fixture.config());
    REQUIRE(reopened);
    REQUIRE((*reopened)->read_next({}));
    CHECK(next_frame(**reopened).id == first.id);
    source->reset();
    CHECK(*first.pixels.bytes == std::vector<std::byte>{std::byte{17}, std::byte{34}, std::byte{51},
                                                        std::byte{68}, std::byte{85},
                                                        std::byte{102}});
}
TEST_CASE(
    "decoded replay rejects invalid identities inventory arithmetic and budgets before decode") {
    decoded_fixture fixture;
    auto config = fixture.config();
    SECTION("raw pixel budget") { config.max_image_bytes.bytes = 5; }
    SECTION("source identity") { config.source_sha256 = std::string(64, 'c'); }
    SECTION("nonmonotonic timestamps") {
        fixture.metadata["frames"][1]["pts"] = 0;
        fixture.publish();
    }
    SECTION("integer overflow") {
        fixture.metadata["frames"][1]["pts"] = UINT64_MAX;
        fixture.publish();
    }
    SECTION("zero timebase") {
        fixture.metadata["frames"][0]["time_base"] = "1/0";
        fixture.publish();
    }
    SECTION("escaping path") {
        fixture.metadata["frames"][0]["file"] = "../first.png";
        fixture.publish();
    }
    SECTION("duplicate frame") {
        fixture.metadata["frames"][1] = fixture.metadata["frames"][0];
        fixture.publish();
    }
    SECTION("changed metadata") { std::ofstream(fixture.stage / "result.json") << "{}"; }
    SECTION("truncated PNG") {
        std::ofstream(fixture.stage / "images/first.png") << "x";
        fixture.publish();
    }
    CHECK_FALSE(open_decoded_frame_source(config));
}
TEST_CASE("decoded replay preserves position on cancellation corruption and decoder failure") {
    decoded_fixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = (*source)->read_next(cancellation.get_token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == error_code::CANCELLED);
    REQUIRE(std::holds_alternative<source_reset>(*(*source)->read_next({})));
    CHECK_FALSE((*source)->seek({1, 0, 1}));
    SECTION("changed image") {
        std::ofstream(fixture.stage / "images/first.png", std::ios::binary) << "changed";
        CHECK_FALSE((*source)->read_next({}));
    }
    SECTION("changed completion") {
        std::ofstream(fixture.stage / "completion.json") << "{}";
        CHECK_FALSE((*source)->read_next({}));
    }
    SECTION("short decoder output can retry same frame") {
        std::ofstream(fixture.decoder) << "import sys\nsys.stdout.buffer.write(b'x')\n";
        const auto short_output = (*source)->read_next({});
        REQUIRE_FALSE(short_output);
        CHECK(short_output.error().code == error_code::BACKEND_FAILURE);
        fixture.write_decoder();
        const auto first = next_frame(**source);
        CHECK(first.original_timestamp.pts == 0);
    }
}
TEST_CASE("decoded replay bounds decoder output and cancels a blocked image read") {
    decoded_fixture fixture;
    const auto mode = GENERATE("large", "wait");
    auto source = open_decoded_frame_source(fixture.config(mode));
    REQUIRE(source);
    REQUIRE((*source)->read_next({}));
    std::stop_source cancellation;
    std::jthread stopper([&](const std::stop_token& stop) {
        if (std::string_view(mode) == "wait") {
            for (int index = 0; index < 10 && !stop.stop_requested(); ++index)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested())
                cancellation.request_stop();
        }
    });
    const auto start = std::chrono::steady_clock::now();
    const auto failed = (*source)->read_next(cancellation.get_token());
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == (std::string_view(mode) == "large" ? error_code::RESOURCE_EXHAUSTED
                                                                    : error_code::CANCELLED));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(4));
}
