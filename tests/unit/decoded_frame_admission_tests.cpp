#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

using namespace world_explorer;
namespace {
void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(output);
}
nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input);
    return nlohmann::json::parse(input);
}
void write_json(const std::filesystem::path& path, const nlohmann::json& value) {
    write_bytes(path, value.dump());
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
    REQUIRE_FALSE(error);
}
using file_tree = std::map<std::string, std::string>;
file_tree snapshot(const std::filesystem::path& root) {
    file_tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).generic_string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status))
            result.emplace(name,
                           "link:" + std::filesystem::read_symlink(entry.path()).generic_string());
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
struct admission_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_decoded_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string key = std::string(64, 'a'), source = std::string(64, 'b');
    std::filesystem::path stage = root / ("decode-" + key), decoder = root / "decoder.py";
    nlohmann::json metadata;
    admission_fixture() {
        std::filesystem::create_directories(stage / "images");
        // Real tiny PNG; no source video, solver or saved job is used.
        constexpr std::array<unsigned char, 72> PNG{
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130};
        const std::string png(reinterpret_cast<const char*>(PNG.data()), PNG.size());
        for (const auto* name : {"first.png", "second.png"})
            write_bytes(stage / "images" / name, png);
        metadata = {{"selection", "timestamp_sampling_v1"},
                    {"crop", "none"},
                    {"resize", "none"},
                    {"source_frame_count", 11},
                    {"frames", nlohmann::json::array()}};
        for (const auto index : {0, 10})
            metadata["frames"].push_back(
                {{"source_index", index},
                 {"frame_id", source.substr(0, 16) + "-" + std::to_string(index)},
                 {"calibration_id", source.substr(0, 16) + "-lens-0"},
                 {"pts", index},
                 {"time_base", "1/30"},
                 {"capture_time_ns", index == 0 ? 0 : 333333333},
                 {"file", index == 0 ? "images/first.png" : "images/second.png"},
                 {"sha256", sha256(std::as_bytes(std::span(png)))}});
        publish();
        good_decoder();
        write_bytes(root / "caller.txt", "caller-owned data outside the stage");
    }
    ~admission_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    decoded_source_config config() const {
        return {stage, source, {WE_TEST_PYTHON, decoder.string()}, {6}};
    }
    void good_decoder() const {
        write_bytes(decoder, "import sys\nsys.stdout.buffer.write(bytes([17,34,51,68,85,102]))\n");
    }
    void publish() {
        write_json(stage / "result.json", metadata);
        auto files = nlohmann::json::array();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(stage)) {
            if (!entry.is_regular_file() || entry.path().filename() == "completion.json")
                continue;
            const auto digest = sha256_file(entry.path(), std::size_t{2} * 1024 * 1024);
            REQUIRE(digest);
            files.push_back({{"path", entry.path().lexically_relative(stage).generic_string()},
                             {"bytes", entry.file_size()},
                             {"sha256", *digest}});
        }
        write_json(stage / "completion.json",
                   {{"schema_version", 1}, {"status", "complete"}, {"key", key}, {"files", files}});
    }
    template <class mutation> void completion(mutation mutate) const {
        auto value = read_json(stage / "completion.json");
        mutate(value);
        write_json(stage / "completion.json", value);
    }
    void dimensions(const char* name, std::pair<std::uint32_t, std::uint32_t> size) {
        const auto [width, height] = size;
        const auto path = stage / "images" / name;
        std::ifstream input(path, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(input), {}};
        REQUIRE(bytes.size() >= 24);
        for (unsigned index = 0; index < 4; ++index) {
            bytes[16 + index] = static_cast<char>((width >> (24 - index * 8)) & 255U);
            bytes[20 + index] = static_cast<char>((height >> (24 - index * 8)) & 255U);
        }
        write_bytes(path, bytes);
        const auto digest = sha256(std::as_bytes(std::span(bytes)));
        for (auto& frame : metadata["frames"])
            if (frame["file"] == std::string("images/") + name)
                frame["sha256"] = digest;
        publish();
    }
    void repair(const file_tree& original) const {
        // Explicit test-only repair of this fresh owned directory, after checking no writes.
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        for (const auto& [name, value] : original) {
            if (value == "directory")
                std::filesystem::create_directories(root / name);
            else {
                REQUIRE(value.starts_with("file:"));
                std::filesystem::create_directories((root / name).parent_path());
                write_bytes(root / name, std::string_view(value).substr(5));
            }
        }
    }
};
template <class value> void expect_error(const result<value>& result, error_code code) {
    INFO((result ? "unexpected success" : result.error().message));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
}
source_reset next_reset(frame_source& source) {
    const auto event = source.read_next({});
    REQUIRE(event);
    REQUIRE(std::holds_alternative<source_reset>(*event));
    return std::get<source_reset>(*event);
}
frame_packet next_frame(frame_source& source) {
    const auto event = source.read_next({});
    REQUIRE(event);
    REQUIRE(std::holds_alternative<frame_packet>(*event));
    REQUIRE(std::get<frame_packet>(*event).pixels.bytes);
    return std::get<frame_packet>(*event);
}
template <class mutation>
void reject_and_repair(error_code code, mutation mutate, const std::stop_token& stop = {}) {
    admission_fixture fixture;
    auto retained = open_decoded_frame_source(fixture.config());
    REQUIRE(retained);
    CHECK(next_reset(**retained).epoch.value == 0);
    const auto lease = next_frame(**retained);
    REQUIRE(lease.pixels.bytes);
    const auto pixels = *lease.pixels.bytes;
    const auto original = snapshot(fixture.root);
    auto config = fixture.config();
    mutate(fixture, config);
    const auto before = snapshot(fixture.root);
    expect_error(open_decoded_frame_source(config, stop), code);
    CHECK(snapshot(fixture.root) == before);
    expect_error(inspect_decoded_frame_source(config, stop), code);
    CHECK(snapshot(fixture.root) == before);
    CHECK(*lease.pixels.bytes == pixels);
    fixture.repair(original);
    CHECK(snapshot(fixture.root) == original);
    const auto repaired = inspect_decoded_frame_source(fixture.config());
    REQUIRE(repaired);
    REQUIRE(repaired->observations.size() == 2);
    CHECK(repaired->observations[0].id == lease.id);
    CHECK(next_frame(**retained).id == repaired->observations[1].id);
    retained->reset();
    CHECK(*lease.pixels.bytes == pixels);
    CHECK(snapshot(fixture.root) == original);
}
} // namespace

TEST_CASE("decoded source configuration and cancellation reject without caller writes",
          "[decoded_admission][config]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(field);
    std::stop_source stop;
    if (field >= 6)
        stop.request_stop();
    reject_and_repair(
        field >= 6 ? error_code::CANCELLED : error_code::INVALID_ARGUMENT,
        [field](const admission_fixture&, decoded_source_config& config) {
            if (field == 0 || field == 7)
                config.source_sha256.clear();
            if (field == 1)
                config.source_sha256.pop_back();
            if (field == 2)
                config.source_sha256[0] = 'G';
            if (field == 3)
                config.decoder_arguments.clear();
            if (field == 4)
                config.max_image_bytes.bytes = 0;
            if (field == 5)
                config.max_image_bytes.bytes = std::size_t{64} * 1024 * 1024 + 1;
        },
        stop.get_token());
}

TEST_CASE("decoded stage completion requires bounded regular committed metadata",
          "[decoded_admission][completion]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    reject_and_repair(error_code::INVALID_DATA, [field](admission_fixture& fixture,
                                                        decoded_source_config& config) {
        const auto path = fixture.stage / "completion.json";
        if (field < 4) {
            fixture.completion([field](auto& value) {
                if (field == 0)
                    value["schema_version"] = 2;
                if (field == 1)
                    value["status"] = "cancelled";
                if (field == 2)
                    value["key"] = std::string(64, 'X');
                if (field == 3)
                    value["key"] = std::string(64, 'c');
            });
        } else if (field == 4)
            write_bytes(path, "{");
        else if (field == 5)
            write_bytes(path, std::string(1024 * 1024 + 1, ' '));
        else if (field == 6) {
            std::filesystem::remove(path);
            std::filesystem::create_directory(path);
        } else if (field == 7) {
            auto nested = nlohmann::json::object();
            for (int depth = 0; depth < 40; ++depth)
                nested = {{"nested", nested}};
            write_json(path, nested);
        } else if (field == 8) {
            fixture.completion([](auto& value) { value.erase("key"); });
        } else if (field == 9)
            config.stage_directory = fixture.root / "missing";
        else if (field == 10) {
            std::filesystem::copy_file(path, fixture.root / "linked-completion.json");
            std::filesystem::remove(path);
            create_link(fixture.root / "linked-completion.json", path);
        } else
            std::filesystem::remove(path);
    });
}

TEST_CASE("decoded inventory count hashes types and metadata receipt are checked",
          "[decoded_admission][inventory]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    reject_and_repair(field < 3 ? error_code::RESOURCE_EXHAUSTED : error_code::INVALID_DATA,
                      [field](admission_fixture& fixture, decoded_source_config&) {
                          fixture.completion([field](auto& value) {
                              auto& files = value["files"];
                              if (field == 0)
                                  files = nlohmann::json::object();
                              else if (field == 1)
                                  files.clear();
                              else if (field == 2) {
                                  const auto entry = files[0];
                                  files = std::vector<nlohmann::json>(4097, entry);
                              } else if (field == 3)
                                  files[0]["sha256"] = std::string(64, 'Z');
                              else if (field == 4)
                                  files.push_back(files[0]);
                              else if (field == 5)
                                  files[0]["bytes"] = -1;
                              else if (field == 6)
                                  files[0]["bytes"] = 1.5;
                              else if (field == 7)
                                  files[0]["path"] = false;
                              else {
                                  for (auto& file : files)
                                      if (file["path"] == "result.json") {
                                          if (field == 8)
                                              file["path"] = "missing.json";
                                          if (field == 9)
                                              file["bytes"] = 0;
                                          if (field == 10)
                                              file["sha256"] = std::string(64, '0');
                                          if (field == 11)
                                              file.erase("sha256");
                                      }
                              }
                          });
                      });
}

TEST_CASE("decoded replay profile and native journal provenance are explicit",
          "[decoded_admission][profile]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(field);
    reject_and_repair(field < 3 ? error_code::UNSUPPORTED_OPERATION : error_code::INVALID_DATA,
                      [field](admission_fixture& fixture, decoded_source_config&) {
                          if (field == 0)
                              fixture.metadata["selection"] = "future-profile";
                          if (field == 1)
                              fixture.metadata["crop"] = "center";
                          if (field == 2)
                              fixture.metadata["resize"] = "half";
                          if (field >= 3) {
                              fixture.metadata["selection"] = "native_rgb_journal_v1";
                              fixture.metadata["source_sha256"] = fixture.source;
                              fixture.metadata["journal_sha256"] = std::string(64, 'c');
                              if (field == 3)
                                  fixture.metadata["source_sha256"] = std::string(64, 'd');
                              if (field == 4)
                                  fixture.metadata["journal_sha256"] = "invalid";
                              if (field == 5)
                                  fixture.metadata.erase("journal_sha256");
                          }
                          fixture.publish();
                      });
}

TEST_CASE("decoded source counts and rehashed observation identities are bounded",
          "[decoded_admission][identity]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    reject_and_repair(field < 5 ? error_code::RESOURCE_EXHAUSTED : error_code::INVALID_DATA,
                      [field](admission_fixture& fixture, decoded_source_config&) {
                          auto& frames = fixture.metadata["frames"];
                          if (field == 0)
                              fixture.metadata["source_frame_count"] = 0;
                          if (field == 1)
                              fixture.metadata["source_frame_count"] = 36001;
                          if (field == 2)
                              frames = nlohmann::json::object();
                          if (field == 3)
                              frames.clear();
                          if (field == 4)
                              frames = std::vector<nlohmann::json>(121, frames[0]);
                          if (field == 5)
                              frames[1]["source_index"] = 11;
                          if (field == 6)
                              frames[1]["source_index"] = 0;
                          if (field == 7)
                              frames[0]["frame_id"] = "different-frame";
                          if (field == 8)
                              frames[0]["calibration_id"] =
                                  fixture.source.substr(0, 16) + "-lens-1";
                          if (field == 9)
                              frames[0]["source_index"] = -1;
                          if (field == 10)
                              frames[0].erase("frame_id");
                          if (field == 11)
                              fixture.metadata["source_frame_count"] = "11";
                          fixture.publish();
                      });
}

TEST_CASE("decoded legacy timestamps reject malformed overflow and nonmonotonic records",
          "[decoded_admission][time]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(field);
    reject_and_repair(
        error_code::INVALID_DATA, [field](admission_fixture& fixture, decoded_source_config&) {
            auto& frame = fixture.metadata["frames"][1];
            constexpr std::array BASES{"30",    "111111111111111111111111111111111/30",
                                       "x/30",  "1x/30",
                                       "1/x",   "1/30x",
                                       "0/30",  "1/0",
                                       "-1/30", "1/-30"};
            if (field < 10)
                frame["time_base"] = BASES[static_cast<std::size_t>(field)];
            if (field == 10)
                frame["pts"] = -1;
            if (field == 11)
                frame["pts"] = std::numeric_limits<std::uint64_t>::max();
            if (field == 12)
                frame["pts"] = std::numeric_limits<std::int64_t>::max();
            if (field == 13) {
                frame["pts"] = 1201;
                frame["time_base"] = "1/1";
            }
            if (field == 14)
                frame["capture_time_ns"] = 333333334;
            if (field == 15) {
                frame["pts"] = 0;
                frame["capture_time_ns"] = 0;
            }
            if (field == 16)
                frame["capture_time_ns"] = -1;
            if (field == 17)
                frame["time_base"] = "2147483648/1";
            fixture.publish();
        });
}

TEST_CASE("decoded image references reject unsafe and linked paths without touching targets",
          "[decoded_admission][paths]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    reject_and_repair(
        error_code::INVALID_DATA, [field](admission_fixture& fixture, decoded_source_config&) {
            auto& name = fixture.metadata["frames"][0]["file"];
            if (field == 0)
                name = "";
            if (field == 1)
                name = std::string(4097, 'x');
            if (field == 2)
                name = std::string("images/first.png\0suffix", 23);
            if (field == 3)
                name = "images\\first.png";
            if (field == 4)
                name = "C:first.png";
            if (field == 5)
                name = "/first.png";
            if (field == 6)
                name = "./images/first.png";
            if (field == 7)
                name = "images/../images/first.png";
            if (field == 8)
                name = "images/missing.png";
            if (field == 9)
                name = "images";
            fixture.publish();
            if (field == 10) {
                const auto path = fixture.stage / "images/first.png";
                std::filesystem::remove(path);
                create_link(fixture.stage / "images/second.png", path);
            }
            if (field == 11) {
                std::filesystem::rename(fixture.stage / "images", fixture.root / "outside-images");
                create_link(fixture.root / "outside-images", fixture.stage / "images", true);
            }
        });
}

TEST_CASE("decoded images require unique matching inventory entries and PNG headers",
          "[decoded_admission][images]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CAPTURE(field);
    reject_and_repair(error_code::INVALID_DATA,
                      [field](admission_fixture& fixture, decoded_source_config&) {
                          auto& frames = fixture.metadata["frames"];
                          if (field == 0)
                              frames[1]["file"] = frames[0]["file"];
                          if (field == 1) {
                              std::filesystem::rename(fixture.stage / "images/first.png",
                                                      fixture.stage / "images/first.jpg");
                              frames[0]["file"] = "images/first.jpg";
                          }
                          if (field == 2)
                              frames[0]["sha256"] = std::string(64, '0');
                          if (field == 3 || field == 4) {
                              const std::string bytes = field == 3 ? "short" : std::string(24, 'x');
                              write_bytes(fixture.stage / "images/first.png", bytes);
                              frames[0]["sha256"] = sha256(std::as_bytes(std::span(bytes)));
                          }
                          fixture.publish();
                          if (field >= 5)
                              fixture.completion([field](auto& value) {
                                  for (auto& entry : value["files"])
                                      if (entry["path"] == "images/first.png") {
                                          if (field == 5)
                                              entry["path"] = "unused.png";
                                          if (field == 6)
                                              entry["bytes"] = 0;
                                          if (field == 7)
                                              entry["bytes"] = std::size_t{64} * 1024 * 1024 + 1;
                                          if (field == 8)
                                              entry["bytes"] = 73;
                                      }
                              });
                          if (field == 9)
                              write_bytes(fixture.stage / "result.json", "{}");
                          if (field == 10) {
                              const auto path = fixture.stage / "result.json";
                              std::filesystem::copy_file(path, fixture.root / "linked-result.json");
                              std::filesystem::remove(path);
                              create_link(fixture.root / "linked-result.json", path);
                          }
                      });
}

TEST_CASE("decoded RGB dimensions enforce exact budgets and one calibration size",
          "[decoded_admission][budget]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    reject_and_repair(field == 6 ? error_code::INVALID_DATA : error_code::RESOURCE_EXHAUSTED,
                      [field](admission_fixture& fixture, decoded_source_config& config) {
                          constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> SIZES{
                              {{0, 1}, {2, 0}, {8193, 1}, {1, 8193}, {8192, 2049}, {2, 1}}};
                          if (field < 6) {
                              const auto [width, height] = SIZES[static_cast<std::size_t>(field)];
                              fixture.dimensions("first.png", {width, height});
                              config.max_image_bytes.bytes =
                                  field == 5 ? 5 : std::size_t{64} * 1024 * 1024;
                          } else
                              fixture.dimensions("second.png", {1, 1});
                      });
}

TEST_CASE("decoded catalog admits exact selection time and pixel limits without launching decoder",
          "[decoded_admission][catalog]") {
    const auto native = GENERATE(false, true);
    admission_fixture fixture;
    auto frames = nlohmann::json::array();
    for (int index = 0; index < 120; ++index) {
        auto frame = fixture.metadata["frames"][0];
        const auto name = "images/" + std::to_string(index) + ".png";
        std::filesystem::copy_file(fixture.stage / "images/first.png", fixture.stage / name);
        frame["file"] = name;
        const auto source_index = index == 119 ? 35999 : index;
        frame["source_index"] = source_index;
        frame["frame_id"] = fixture.source.substr(0, 16) + "-" + std::to_string(source_index);
        frame["pts"] = index == 119 ? 1200 : index;
        frame["time_base"] = "1/1";
        frame["capture_time_ns"] = (index == 119 ? 1200LL : index) * 1000000000;
        frames.push_back(frame);
    }
    fixture.metadata["frames"] = frames;
    fixture.metadata["source_frame_count"] = 36000;
    if (native) {
        fixture.metadata["selection"] = "native_rgb_journal_v1";
        fixture.metadata["source_sha256"] = fixture.source;
        fixture.metadata["journal_sha256"] = std::string(64, 'c');
    }
    fixture.publish();
    // An impossible executable proves metadata inspection never starts a decoder.
    auto config = fixture.config();
    config.decoder_arguments = {(fixture.root / "absent-decoder").string()};
    const auto before = snapshot(fixture.root);
    const auto catalog = inspect_decoded_frame_source(config);
    REQUIRE(catalog);
    REQUIRE(catalog->observations.size() == 120);
    CHECK(catalog->observations.back().capture_time.count() == 1200000000000LL);
    CHECK(catalog->observations.front().width == 2);
    CHECK(catalog->observations.front().height == 1);
    CHECK(catalog->journal_sha256 == (native ? std::optional{std::string(64, 'c')} : std::nullopt));
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("decoded failed reads and seeks retain pending reset position and existing leases",
          "[decoded_admission][retry]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(field);
    admission_fixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    const auto initial = next_reset(**source);
    const auto first = next_frame(**source);
    const auto pixels = *first.pixels.bytes;
    REQUIRE((*source)->seek({0, 1, 1}));
    const auto original = snapshot(fixture.root);
    auto code = error_code::INVALID_DATA;
    std::stop_source cancellation;
    if (field == 0)
        cancellation.request_stop();
    if (field == 1)
        write_bytes(fixture.stage / "completion.json", "{}");
    if (field == 2)
        std::filesystem::remove(fixture.stage / "completion.json");
    if (field == 3) {
        std::filesystem::remove(fixture.stage / "completion.json");
        create_link(fixture.stage / "result.json", fixture.stage / "completion.json");
    }
    if (field == 4) {
        write_bytes(fixture.stage / "completion.json", std::string(1024 * 1024 + 1, 'x'));
        code = error_code::RESOURCE_EXHAUSTED;
    }
    if (field < 5) {
        const auto before = snapshot(fixture.root);
        expect_error((*source)->read_next(cancellation.get_token()),
                     field == 0 ? error_code::CANCELLED : code);
        if (field > 0)
            expect_error((*source)->seek({0, 1, 1}), code);
        CHECK(snapshot(fixture.root) == before);
        fixture.repair(original);
    } else {
        constexpr std::array<source_timestamp, 5> BAD{
            {{-1, 1, 1},
             {1, 0, 1},
             {1201, 1, 1},
             {1, 1, 0},
             {std::numeric_limits<std::int64_t>::max(), 1, 1}}};
        expect_error((*source)->seek(BAD[static_cast<std::size_t>(field - 5)]),
                     error_code::INVALID_ARGUMENT);
    }
    const auto reset = next_reset(**source);
    CHECK(reset.epoch.value == 1);
    CHECK(reset.capture == initial.capture);
    CHECK(next_frame(**source).id == first.id);
    const auto second = next_frame(**source);
    CHECK(second.id != first.id);
    CHECK(*first.pixels.bytes == pixels);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto end = (*source)->read_next({});
        REQUIRE(end);
        CHECK(std::holds_alternative<source_end>(*end));
    }
    CHECK(snapshot(fixture.root) == original);
}

TEST_CASE("decoded image integrity and decoder failures allow retry of the same frame",
          "[decoded_admission][decoder]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CAPTURE(field);
    admission_fixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK(next_reset(**source).epoch.value == 0);
    const auto first = next_frame(**source);
    const auto pixels = *first.pixels.bytes;
    const auto original = snapshot(fixture.root);
    const auto image = fixture.stage / "images/second.png";
    auto code = error_code::INVALID_DATA;
    if (field == 0)
        write_bytes(image, std::string(72, 'x'));
    if (field == 1) {
        write_bytes(image, std::string(73, 'x'));
        code = error_code::RESOURCE_EXHAUSTED;
    }
    if (field == 2)
        std::filesystem::remove(image);
    if (field == 3) {
        write_bytes(fixture.decoder, "import sys\nsys.stdout.buffer.write(b'x')\n");
        code = error_code::BACKEND_FAILURE;
    }
    if (field == 4) {
        write_bytes(fixture.decoder, "import sys\nsys.exit(7)\n");
        code = error_code::BACKEND_FAILURE;
    }
    if (field == 5) {
        write_bytes(fixture.decoder, "import sys\nsys.stdout.buffer.write(b'1234567')\n");
        code = error_code::RESOURCE_EXHAUSTED;
    }
    if (field >= 6) {
        write_bytes(
            fixture.decoder,
            "import sys\nfrom pathlib import Path\np=Path(sys.argv[sys.argv.index('-i')+1])\n" +
                std::string(field == 6   ? "p.write_bytes(b'x'*72)\n"
                            : field == 7 ? "p.write_bytes(b'x'*73)\n"
                                         : "p.unlink()\n") +
                "sys.stdout.buffer.write(b'123456')\n");
        code = field == 7   ? error_code::RESOURCE_EXHAUSTED
               : field == 8 ? error_code::IO_ERROR
                            : error_code::INVALID_DATA;
    }
    const auto before = snapshot(fixture.root);
    expect_error((*source)->read_next({}), code);
    // These three controlled decoders deliberately mutate only their selected input.
    // Every other failure must preserve the complete caller tree byte-for-byte.
    if (field < 6)
        CHECK(snapshot(fixture.root) == before);
    else {
        auto after = snapshot(fixture.root);
        auto expected = before;
        const auto name = image.lexically_relative(fixture.root).generic_string();
        if (field == 8)
            expected.erase(name);
        else
            expected[name] = "file:" + std::string(field == 6 ? 72 : 73, 'x');
        CHECK(after == expected);
    }
    CHECK(*first.pixels.bytes == pixels);
    fixture.repair(original);
    const auto second = next_frame(**source);
    CHECK(second.original_timestamp.pts == 10);
    CHECK(second.capture_time.count() == 333333333);
    CHECK(second.id != first.id);
    source->reset();
    CHECK(*first.pixels.bytes == pixels);
    CHECK(*second.pixels.bytes == pixels);
    CHECK(snapshot(fixture.root) == original);
}

TEST_CASE("decoded legacy prefix and native full source identity retain their documented scope",
          "[decoded_admission][catalog]") {
    admission_fixture fixture;
    const auto original = inspect_decoded_frame_source(fixture.config());
    REQUIRE(original);
    CHECK_FALSE(original->journal_sha256);
    auto config = fixture.config();
    config.source_sha256.back() = 'c';
    // Legacy stages have only a 16-character source prefix, never independent full-SHA proof.
    const auto alternate = inspect_decoded_frame_source(config);
    REQUIRE(alternate);
    CHECK(alternate->capture != original->capture);
    CHECK(alternate->observations[0].id != original->observations[0].id);
    CHECK(alternate->observations[0].calibration != original->observations[0].calibration);
    CHECK(alternate->observations[0].external_frame_id ==
          original->observations[0].external_frame_id);
    fixture.metadata["selection"] = "native_rgb_journal_v1";
    fixture.metadata["source_sha256"] = fixture.source;
    fixture.metadata["journal_sha256"] = std::string(64, 'c');
    fixture.publish();
    const auto before = snapshot(fixture.root);
    expect_error(inspect_decoded_frame_source(config), error_code::INVALID_DATA);
    CHECK(snapshot(fixture.root) == before);
    const auto native = inspect_decoded_frame_source(fixture.config());
    REQUIRE(native);
    CHECK(native->capture == original->capture);
    CHECK(native->observations[0].id == original->observations[0].id);
    CHECK(native->journal_sha256 == std::optional{std::string(64, 'c')});
    CHECK_FALSE(original->journal_sha256);
    CHECK(original->completion_sha256 != native->completion_sha256);
}

TEST_CASE("decoded inspection admits exact inventory and header dimension limits",
          "[decoded_admission][budget]") {
    const auto size =
        GENERATE(std::pair{8192U, 2048U}, std::pair{2048U, 8192U}, std::pair{4096U, 4096U});
    admission_fixture fixture;
    // Synthetic PNG header diagnostics: pixel decoding is deliberately not claimed here.
    fixture.dimensions("first.png", size);
    fixture.dimensions("second.png", size);
    fixture.completion([](auto& value) {
        auto& files = value["files"];
        while (files.size() < 4096)
            files.push_back({{"path", "unused/" + std::to_string(files.size())},
                             {"bytes", 0},
                             {"sha256", std::string(64, '0')}});
    });
    auto config = fixture.config();
    config.max_image_bytes.bytes = static_cast<std::size_t>(size.first) * size.second * 3;
    const auto before = snapshot(fixture.root);
    const auto catalog = inspect_decoded_frame_source(config);
    REQUIRE(catalog);
    CHECK(catalog->observations[0].width == size.first);
    CHECK(catalog->observations[0].height == size.second);
    --config.max_image_bytes.bytes;
    expect_error(inspect_decoded_frame_source(config), error_code::RESOURCE_EXHAUSTED);
    CHECK(snapshot(fixture.root) == before);
}

TEST_CASE("decoded in-flight cancellation retains the next frame and old RGB lease",
          "[decoded_admission][cancel]") {
    admission_fixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK(next_reset(**source).epoch.value == 0);
    const auto first = next_frame(**source);
    const auto pixels = *first.pixels.bytes;
    write_bytes(fixture.decoder, "import sys,time\nfrom pathlib import Path\n"
                                 "p=Path(sys.argv[sys.argv.index('-i')+1])\n"
                                 "(p.parent.parent.parent/'started').write_text('ready')\n"
                                 "time.sleep(30)\n");
    const auto before = snapshot(fixture.root);
    std::stop_source cancellation;
    std::atomic<bool> started{};
    std::jthread observer([&](const std::stop_token& stop) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            if (std::filesystem::is_regular_file(fixture.root / "started", error)) {
                started.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        cancellation.request_stop();
    });
    expect_error((*source)->read_next(cancellation.get_token()), error_code::CANCELLED);
    observer.join();
    REQUIRE(started.load());
    auto expected = before;
    expected.emplace("started", "file:ready");
    CHECK(snapshot(fixture.root) == expected);
    CHECK(*first.pixels.bytes == pixels);
    fixture.good_decoder();
    const auto second = next_frame(**source);
    CHECK(second.original_timestamp.pts == 10);
    CHECK(second.id != first.id);
    REQUIRE((*source)->seek({0, 1, 1}));
    CHECK(next_reset(**source).epoch.value == 1);
    CHECK(next_frame(**source).id == first.id);
    source->reset();
    CHECK(*first.pixels.bytes == pixels);
    CHECK(*second.pixels.bytes == pixels);
}
