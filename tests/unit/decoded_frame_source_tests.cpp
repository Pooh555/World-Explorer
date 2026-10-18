#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <array>
#include <fstream>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

struct DecodedFixture {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_replay_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string m_key = std::string(64, 'a');
    std::string m_source_digest = std::string(64, 'b');
    std::filesystem::path m_stage = m_root / ("decode-" + m_key);
    std::filesystem::path m_decoder = m_root / "diagnostic decoder.py";
    nlohmann::json m_metadata;

    DecodedFixture() {
        std::filesystem::create_directories(m_stage / "images");
        constexpr std::array<unsigned char, 72> K_PNG {
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130};
        const auto K_DIGEST = sha256(std::as_bytes(std::span(K_PNG)));
        for (const auto* name : {"first.png", "second.png"}) {
            std::ofstream image(m_stage / "images" / name, std::ios::binary);
            image.write(reinterpret_cast<const char*>(K_PNG.data()),
                        static_cast<std::streamsize>(K_PNG.size()));
        }
        m_metadata = { { "selection", "timestamp_sampling_v1" },
                      { "crop", "none" },
                      { "resize", "none" },
                      { "source_frame_count", 11 },
                      { "frames", nlohmann::json::array() } };
        std::size_t count {};
        for (const auto K_INDEX : {0, 10}) {
            m_metadata["frames"].push_back(
                {{"source_index", K_INDEX},
                 {"frame_id", m_source_digest.substr(0, 16) + "-" + std::to_string(K_INDEX)},
                 {"calibration_id", m_source_digest.substr(0, 16) + "-lens-0"},
                 {"pts", K_INDEX},
                 {"time_base", "1/30"},
                 {"capture_time_ns", K_INDEX == 0 ? 0 : 333333333},
                 {"file", count++ == 0 ? "images/first.png" : "images/second.png"},
                 {"sha256", K_DIGEST}});
        }
        publish();
        write_decoder();
    }

    ~DecodedFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    void write_decoder() const {
        std::ofstream script(m_decoder);
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
        std::ofstream(m_stage / "result.json") << m_metadata.dump();
        auto inventory = nlohmann::json::array();
        for (const auto* name : {"result.json", "images/first.png", "images/second.png"}) {
            const auto K_PATH = m_stage / name;
            inventory.push_back({{"path", name},
                                 {"bytes", std::filesystem::file_size(K_PATH)},
                                 {"sha256", *sha256_file(K_PATH, 4096)}});
        }
        std::ofstream(m_stage / "completion.json") << nlohmann::json {
            {"schema_version", 1},
            {"status", "complete"},
            {"key", m_key},
            {"files", inventory}}.dump();
    }

    DecodedSourceConfig config(const std::string& mode = "good") const {
        return {.m_stage_directory = m_stage,
                .m_source_sha256 = m_source_digest,
                .m_decoder_arguments = { WE_TEST_PYTHON, m_decoder.string(), mode },
                .m_max_image_bytes = { 6 }};
    }
};

FramePacket next_frame(FrameSource& source) {
    const auto K_EVENT = source.read_next({});
    REQUIRE(K_EVENT);
    REQUIRE(std::holds_alternative<FramePacket>(*K_EVENT));
    return std::get<FramePacket>(*K_EVENT);
}

}  // namespace

TEST_CASE("decoded replay preserves leased RGB pixels stable IDs timestamps and seek resets") {
    DecodedFixture fixture;
    SECTION("UTF-8 image filename") {
        const auto K_FILENAME = std::filesystem::path(u8"images/first_é_房.png");
        const auto K_ENCODED = K_FILENAME.generic_u8string();
        const std::string K_NAME(K_ENCODED.begin(), K_ENCODED.end());
        std::filesystem::copy_file(fixture.m_stage / "images/first.png",
                                   fixture.m_stage / K_FILENAME);
        fixture.m_metadata["frames"][0]["file"] = K_NAME;
        fixture.publish();
        nlohmann::json completion;
        std::ifstream(fixture.m_stage / "completion.json") >> completion;
        const auto K_PATH = fixture.m_stage / K_FILENAME;
        completion["files"].push_back({{"path", K_NAME},
                                       {"bytes", std::filesystem::file_size(K_PATH)},
                                       {"sha256", *sha256_file(K_PATH, 4096)}});
        std::ofstream(fixture.m_stage / "completion.json") << completion.dump();
    }
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK((*source)->capabilities().m_seekable);
    CHECK_FALSE((*source)->capabilities().m_measured_depth);
    CHECK_FALSE((*source)->capabilities().m_imu);
    CHECK((*source)->capabilities().m_max_image_bytes.m_bytes == 6);
    const auto K_RESET = (*source)->read_next({});
    REQUIRE(K_RESET);
    const auto K_SEGMENT = std::get<SourceReset>(*K_RESET);
    CHECK(K_SEGMENT.m_epoch.m_value == 0);
    const auto K_FIRST = next_frame(**source);
    const auto K_SECOND = next_frame(**source);
    CHECK(K_FIRST.m_id != K_SECOND.m_id);
    CHECK(K_FIRST.m_capture == K_SEGMENT.m_capture);
    CHECK(K_FIRST.m_capture == K_SECOND.m_capture);
    CHECK(K_FIRST.m_calibration == K_SECOND.m_calibration);
    CHECK(K_FIRST.m_original_timestamp.m_pts == 0);
    CHECK(K_SECOND.m_original_timestamp.m_pts == 10);
    CHECK(K_SECOND.m_original_timestamp.m_time_base_numerator == 1);
    CHECK(K_SECOND.m_original_timestamp.m_time_base_denominator == 30);
    CHECK(K_SECOND.m_capture_time.count() == 333333333);
    REQUIRE(K_FIRST.m_pixels.m_bytes);
    CHECK(K_FIRST.m_pixels.m_width == 2);
    CHECK(K_FIRST.m_pixels.m_height == 1);
    CHECK(K_FIRST.m_pixels.m_row_stride == 6);
    CHECK(K_FIRST.m_pixels.m_format == PixelFormat::RGB8);
    CHECK_FALSE(K_FIRST.m_measured_depth);
    CHECK(K_FIRST.m_imu_samples.empty());
    REQUIRE(std::holds_alternative<SourceEnd>(*(*source)->read_next({})));
    REQUIRE((*source)->seek({1, 1, 3}));
    const auto K_SOUGHT_RESET = (*source)->read_next({});
    REQUIRE(K_SOUGHT_RESET);
    CHECK(std::get<SourceReset>(*K_SOUGHT_RESET).m_epoch.m_value == 1);
    CHECK(next_frame(**source).m_id == K_SECOND.m_id);
    REQUIRE((*source)->seek({40, 1, 30}));
    REQUIRE(std::holds_alternative<SourceReset>(*(*source)->read_next({})));
    REQUIRE(std::holds_alternative<SourceEnd>(*(*source)->read_next({})));
    auto reopened = open_decoded_frame_source(fixture.config());
    REQUIRE(reopened);
    REQUIRE((*reopened)->read_next({}));
    CHECK(next_frame(**reopened).m_id == K_FIRST.m_id);
    source->reset();
    CHECK(*K_FIRST.m_pixels.m_bytes == std::vector<std::byte> {std::byte { 17 },
                                                               std::byte { 34 },
                                                               std::byte { 51 },
                                                               std::byte { 68 },
                                                               std::byte { 85 },
                                                               std::byte { 102 }});
}

TEST_CASE(
    "decoded replay rejects invalid identities inventory arithmetic and budgets before decode") {
    DecodedFixture fixture;
    auto config = fixture.config();
    SECTION("raw pixel budget") {
        config.m_max_image_bytes.m_bytes = 5;
    }
    SECTION("source identity") {
        config.m_source_sha256 = std::string(64, 'c');
    }
    SECTION("nonmonotonic timestamps") {
        fixture.m_metadata["frames"][1]["pts"] = 0;
        fixture.publish();
    }
    SECTION("integer overflow") {
        fixture.m_metadata["frames"][1]["pts"] = UINT64_MAX;
        fixture.publish();
    }
    SECTION("zero timebase") {
        fixture.m_metadata["frames"][0]["time_base"] = "1/0";
        fixture.publish();
    }
    SECTION("escaping path") {
        fixture.m_metadata["frames"][0]["file"] = "../first.png";
        fixture.publish();
    }
    SECTION("duplicate frame") {
        fixture.m_metadata["frames"][1] = fixture.m_metadata["frames"][0];
        fixture.publish();
    }
    SECTION("changed metadata") {
        std::ofstream(fixture.m_stage / "result.json") << "{}";
    }
    SECTION("truncated PNG") {
        std::ofstream(fixture.m_stage / "images/first.png") << "x";
        fixture.publish();
    }
    CHECK_FALSE(open_decoded_frame_source(config));
}

TEST_CASE("decoded replay preserves position on cancellation corruption and decoder failure") {
    DecodedFixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED = (*source)->read_next(cancellation.get_token());
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    REQUIRE(std::holds_alternative<SourceReset>(*(*source)->read_next({})));
    CHECK_FALSE((*source)->seek({1, 0, 1}));
    SECTION("changed image") {
        std::ofstream(fixture.m_stage / "images/first.png", std::ios::binary) << "changed";
        CHECK_FALSE((*source)->read_next({}));
    }
    SECTION("changed completion") {
        std::ofstream(fixture.m_stage / "completion.json") << "{}";
        CHECK_FALSE((*source)->read_next({}));
    }
    SECTION("short decoder output can retry same frame") {
        std::ofstream(fixture.m_decoder) << "import sys\nsys.stdout.buffer.write(b'x')\n";
        const auto K_SHORT_OUTPUT = (*source)->read_next({});
        REQUIRE_FALSE(K_SHORT_OUTPUT);
        CHECK(K_SHORT_OUTPUT.error().m_code == ErrorCode::BACKEND_FAILURE);
        fixture.write_decoder();
        const auto K_FIRST = next_frame(**source);
        CHECK(K_FIRST.m_original_timestamp.m_pts == 0);
    }
}

TEST_CASE("decoded replay bounds decoder output and cancels a blocked image read") {
    DecodedFixture fixture;
    const auto K_MODE = GENERATE("large", "wait");
    auto source = open_decoded_frame_source(fixture.config(K_MODE));
    REQUIRE(source);
    REQUIRE((*source)->read_next({}));
    std::stop_source cancellation;
    std::jthread stopper([&](const std::stop_token& stop) {
        if (std::string_view(K_MODE) == "wait") {
            for (int index { 0 }; index < 10 && !stop.stop_requested(); ++index)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested()) cancellation.request_stop();
        }
    });
    const auto K_START = std::chrono::steady_clock::now();
    const auto K_FAILED = (*source)->read_next(cancellation.get_token());
    REQUIRE_FALSE(K_FAILED);
    CHECK(K_FAILED.error().m_code == (std::string_view(K_MODE) == "large"
                                          ? ErrorCode::RESOURCE_EXHAUSTED
                                          : ErrorCode::CANCELLED));
    CHECK(std::chrono::steady_clock::now() - K_START < std::chrono::seconds(4));
}
