#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <map>
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

void create_link(const std::filesystem::path& target,
                 const std::filesystem::path& link,
                 bool directory = false) {
    std::error_code error;
    if (directory) {
        std::filesystem::create_directory_symlink(target, link, error);
    } else {
        std::filesystem::create_symlink(target, link, error);
    }
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation is unavailable to this Windows account");
#endif
    INFO(error.message());
    REQUIRE_FALSE(error);
}

using FileTree = std::map<std::string, std::string>;

FileTree snapshot(const std::filesystem::path& root) {
    FileTree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        const auto K_STATUS = entry.symlink_status();
        if (std::filesystem::is_symlink(K_STATUS)) {
            result.emplace(K_NAME,
                           "link:" + std::filesystem::read_symlink(entry.path()).generic_string());
        } else if (std::filesystem::is_directory(K_STATUS)) {
            result.emplace(K_NAME, "directory");
        } else {
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            REQUIRE(entry.file_size() <= 2uz * 1024 * 1024);
            std::ifstream input(entry.path(), std::ios::binary);
            REQUIRE(input);
            std::string bytes { std::istreambuf_iterator<char>(input), {} };
            REQUIRE(!input.bad());
            result.emplace(K_NAME, "file:" + bytes);
        }
    }
    return result;
}

struct AdmissionFixture {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_decoded_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string m_key = std::string(64, 'a'), m_source = std::string(64, 'b');
    std::filesystem::path m_stage = m_root / ("decode-" + m_key), m_decoder = m_root / "decoder.py";
    nlohmann::json m_metadata;

    AdmissionFixture() {
        std::filesystem::create_directories(m_stage / "images");
        // Real tiny PNG; no source video, solver or saved job is used.
        constexpr std::array<unsigned char, 72> K_PNG {
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130 };
        const std::string K_PNG_CONTENT(reinterpret_cast<const char*>(K_PNG.data()), K_PNG.size());
        for (const auto* name : { "first.png", "second.png" }) {
            write_bytes(m_stage / "images" / name, K_PNG_CONTENT);
        }
        m_metadata = { { "selection", "timestamp_sampling_v1" },
                      { "crop", "none" },
                      { "resize", "none" },
                      { "source_frame_count", 11 },
                      { "frames", nlohmann::json::array() } };
        for (const auto K_INDEX : { 0, 10 }) {
            m_metadata["frames"].push_back(
                { { "source_index", K_INDEX },
                 { "frame_id", m_source.substr(0, 16) + "-" + std::to_string(K_INDEX) },
                 { "calibration_id", m_source.substr(0, 16) + "-lens-0" },
                 { "pts", K_INDEX },
                 { "time_base", "1/30" },
                 { "capture_time_ns", K_INDEX == 0 ? 0 : 333333333 },
                 { "file", K_INDEX == 0 ? "images/first.png" : "images/second.png" },
                 { "sha256", sha256(std::as_bytes(std::span(K_PNG))) } });
        }
        publish();
        good_decoder();
        write_bytes(m_root / "caller.txt", "caller-owned data outside the stage");
    }

    ~AdmissionFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    DecodedSourceConfig config() const {
        return { m_stage, m_source, { WE_TEST_PYTHON, m_decoder.string() }, { 6 } };
    }

    void good_decoder() const {
        write_bytes(m_decoder,
                    "import sys\nsys.stdout.buffer.write(bytes([17,34,51,68,85,102]))\n");
    }

    void publish() {
        write_json(m_stage / "result.json", m_metadata);
        auto files = nlohmann::json::array();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_stage)) {
            if (!entry.is_regular_file() || entry.path().filename() == "completion.json") {
                continue;
            }
            const auto K_DIGEST = sha256_file(entry.path(), 2uz * 1024 * 1024);
            REQUIRE(K_DIGEST);
            files.push_back({ { "path", entry.path().lexically_relative(m_stage).generic_string() },
                             { "bytes", entry.file_size() },
                             { "sha256", *K_DIGEST } });
        }
        write_json(
            m_stage / "completion.json",
            { { "schema_version", 1 }, { "status", "complete" }, { "key", m_key }, { "files", files } });
    }

    template <class Mutation> void completion(Mutation mutate) const {
        auto value = read_json(m_stage / "completion.json");
        mutate(value);
        write_json(m_stage / "completion.json", value);
    }

    void dimensions(const char* name, std::pair<std::uint32_t, std::uint32_t> size) {
        const auto [K_WIDTH, K_HEIGHT] = size;
        const auto K_PATH = m_stage / "images" / name;
        std::ifstream input(K_PATH, std::ios::binary);
        std::string bytes { std::istreambuf_iterator<char>(input), {} };
        REQUIRE(bytes.size() >= 24);
        for (unsigned index { 0 }; index < 4; ++index) {
            bytes[16 + index] = static_cast<char>((K_WIDTH >> (24 - index * 8)) & 255U);
            bytes[20 + index] = static_cast<char>((K_HEIGHT >> (24 - index * 8)) & 255U);
        }
        write_bytes(K_PATH, bytes);
        const auto K_DIGEST = sha256(std::as_bytes(std::span(bytes)));
        for (auto& frame : m_metadata["frames"]) {
            if (frame["file"] == std::string("images/") + name) {
                frame["sha256"] = K_DIGEST;
            }
        }
        publish();
    }

    void repair(const FileTree& original) const {
        // Explicit test-only repair of this fresh owned directory, after checking no writes.
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
        for (const auto& [K_NAME, K_VALUE] : original) {
            if (K_VALUE == "directory") {
                std::filesystem::create_directories(m_root / K_NAME);
            } else {
                REQUIRE(K_VALUE.starts_with("file:"));
                std::filesystem::create_directories((m_root / K_NAME).parent_path());
                write_bytes(m_root / K_NAME, std::string_view(K_VALUE).substr(5));
            }
        }
    }
};

template <class Value> void expect_error(const Result<Value>& result, ErrorCode code) {
    INFO((result ? "unexpected success" : result.error().m_message));
    REQUIRE_FALSE(result);
    CHECK(result.error().m_code == code);
}

SourceReset next_reset(FrameSource& source) {
    const auto K_EVENT = source.read_next({});
    REQUIRE(K_EVENT);
    REQUIRE(std::holds_alternative<SourceReset>(*K_EVENT));
    return std::get<SourceReset>(*K_EVENT);
}

FramePacket next_frame(FrameSource& source) {
    const auto K_EVENT = source.read_next({});
    REQUIRE(K_EVENT);
    REQUIRE(std::holds_alternative<FramePacket>(*K_EVENT));
    REQUIRE(std::get<FramePacket>(*K_EVENT).m_pixels.m_bytes);
    return std::get<FramePacket>(*K_EVENT);
}

template <class Mutation>
void reject_and_repair(ErrorCode code, Mutation mutate, const std::stop_token& stop = {}) {
    AdmissionFixture fixture;
    auto retained = open_decoded_frame_source(fixture.config());
    REQUIRE(retained);
    CHECK(next_reset(**retained).m_epoch.m_value == 0);
    const auto K_LEASE = next_frame(**retained);
    REQUIRE(K_LEASE.m_pixels.m_bytes);
    const auto K_PIXELS = *K_LEASE.m_pixels.m_bytes;
    const auto K_ORIGINAL = snapshot(fixture.m_root);
    auto config = fixture.config();
    mutate(fixture, config);
    const auto K_BEFORE = snapshot(fixture.m_root);
    expect_error(open_decoded_frame_source(config, stop), code);
    CHECK(snapshot(fixture.m_root) == K_BEFORE);
    expect_error(inspect_decoded_frame_source(config, stop), code);
    CHECK(snapshot(fixture.m_root) == K_BEFORE);
    CHECK(*K_LEASE.m_pixels.m_bytes == K_PIXELS);
    fixture.repair(K_ORIGINAL);
    CHECK(snapshot(fixture.m_root) == K_ORIGINAL);
    const auto K_REPAIRED = inspect_decoded_frame_source(fixture.config());
    REQUIRE(K_REPAIRED);
    REQUIRE(K_REPAIRED->m_observations.size() == 2);
    CHECK(K_REPAIRED->m_observations[0].m_id == K_LEASE.m_id);
    CHECK(next_frame(**retained).m_id == K_REPAIRED->m_observations[1].m_id);
    retained->reset();
    CHECK(*K_LEASE.m_pixels.m_bytes == K_PIXELS);
    CHECK(snapshot(fixture.m_root) == K_ORIGINAL);
}

}  // namespace

TEST_CASE("decoded source configuration and cancellation reject without caller writes",
          "[decoded_admission][config]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_FIELD);
    std::stop_source stop;
    if (K_FIELD >= 6) {
        stop.request_stop();
    }
    reject_and_repair(
        K_FIELD >= 6 ? ErrorCode::CANCELLED : ErrorCode::INVALID_ARGUMENT,
        [K_FIELD](const AdmissionFixture&, DecodedSourceConfig& config) {
            if (K_FIELD == 0 || K_FIELD == 7) {
                config.m_source_sha256.clear();
            }
            if (K_FIELD == 1) {
                config.m_source_sha256.pop_back();
            }
            if (K_FIELD == 2) {
                config.m_source_sha256[0] = 'G';
            }
            if (K_FIELD == 3) {
                config.m_decoder_arguments.clear();
            }
            if (K_FIELD == 4) {
                config.m_max_image_bytes.m_bytes = 0;
            }
            if (K_FIELD == 5) {
                config.m_max_image_bytes.m_bytes = 64uz * 1024 * 1024 + 1;
            }
        },
        stop.get_token());
}

TEST_CASE("decoded stage completion requires bounded regular committed metadata",
          "[decoded_admission][completion]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig& config) {
            const auto K_PATH = fixture.m_stage / "completion.json";
            if (K_FIELD < 4) {
                fixture.completion([K_FIELD](auto& value) {
                    if (K_FIELD == 0) {
                        value["schema_version"] = 2;
                    }
                    if (K_FIELD == 1) {
                        value["status"] = "cancelled";
                    }
                    if (K_FIELD == 2) {
                        value["key"] = std::string(64, 'X');
                    }
                    if (K_FIELD == 3) {
                        value["key"] = std::string(64, 'c');
                    }
                });
            } else if (K_FIELD == 4) {
                write_bytes(K_PATH, "{");
            } else if (K_FIELD == 5) {
                write_bytes(K_PATH, std::string(1024 * 1024 + 1, ' '));
            } else if (K_FIELD == 6) {
                std::filesystem::remove(K_PATH);
                std::filesystem::create_directory(K_PATH);
            } else if (K_FIELD == 7) {
                auto nested = nlohmann::json::object();
                for (int depth { 0 }; depth < 40; ++depth) {
                    nested = { { "nested", nested } };
                }
                write_json(K_PATH, nested);
            } else if (K_FIELD == 8) {
                fixture.completion([](auto& value) {
                    value.erase("key");
                });
            } else if (K_FIELD == 9) {
                config.m_stage_directory = fixture.m_root / "missing";
            } else if (K_FIELD == 10) {
                std::filesystem::copy_file(K_PATH, fixture.m_root / "linked-completion.json");
                std::filesystem::remove(K_PATH);
                create_link(fixture.m_root / "linked-completion.json", K_PATH);
            } else {
                std::filesystem::remove(K_PATH);
            }
        });
}

TEST_CASE("decoded inventory count hashes types and metadata receipt are checked",
          "[decoded_admission][inventory]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    reject_and_repair(K_FIELD < 3 ? ErrorCode::RESOURCE_EXHAUSTED : ErrorCode::INVALID_DATA,
                      [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
                          fixture.completion([K_FIELD](auto& value) {
                              auto& files = value["files"];
                              if (K_FIELD == 0) {
                                  files = nlohmann::json::object();
                              } else if (K_FIELD == 1) {
                                  files.clear();
                              } else if (K_FIELD == 2) {
                                  const auto K_ENTRY = files[0];
                                  files = std::vector<nlohmann::json>(4097, K_ENTRY);
                              } else if (K_FIELD == 3) {
                                  files[0]["sha256"] = std::string(64, 'Z');
                              } else if (K_FIELD == 4) {
                                  files.push_back(files[0]);
                              } else if (K_FIELD == 5) {
                                  files[0]["bytes"] = -1;
                              } else if (K_FIELD == 6) {
                                  files[0]["bytes"] = 1.5;
                              } else if (K_FIELD == 7) {
                                  files[0]["path"] = false;
                              } else {
                                  for (auto& file : files) {
                                      if (file["path"] == "result.json") {
                                          if (K_FIELD == 8) {
                                              file["path"] = "missing.json";
                                          }
                                          if (K_FIELD == 9) {
                                              file["bytes"] = 0;
                                          }
                                          if (K_FIELD == 10) {
                                              file["sha256"] = std::string(64, '0');
                                          }
                                          if (K_FIELD == 11) {
                                              file.erase("sha256");
                                          }
                                      }
                                  }
                              }
                          });
                      });
}

TEST_CASE("decoded replay profile and native journal provenance are explicit",
          "[decoded_admission][profile]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    reject_and_repair(K_FIELD < 3 ? ErrorCode::UNSUPPORTED_OPERATION : ErrorCode::INVALID_DATA,
                      [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
                          if (K_FIELD == 0) {
                              fixture.m_metadata["selection"] = "future-profile";
                          }
                          if (K_FIELD == 1) {
                              fixture.m_metadata["crop"] = "center";
                          }
                          if (K_FIELD == 2) {
                              fixture.m_metadata["resize"] = "half";
                          }
                          if (K_FIELD >= 3) {
                              fixture.m_metadata["selection"] = "native_rgb_journal_v1";
                              fixture.m_metadata["source_sha256"] = fixture.m_source;
                              fixture.m_metadata["journal_sha256"] = std::string(64, 'c');
                              if (K_FIELD == 3) {
                                  fixture.m_metadata["source_sha256"] = std::string(64, 'd');
                              }
                              if (K_FIELD == 4) {
                                  fixture.m_metadata["journal_sha256"] = "invalid";
                              }
                              if (K_FIELD == 5) {
                                  fixture.m_metadata.erase("journal_sha256");
                              }
                          }
                          fixture.publish();
                      });
}

TEST_CASE("decoded source counts and rehashed observation identities are bounded",
          "[decoded_admission][identity]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    reject_and_repair(K_FIELD < 5 ? ErrorCode::RESOURCE_EXHAUSTED : ErrorCode::INVALID_DATA,
                      [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
                          auto& frames = fixture.m_metadata["frames"];
                          if (K_FIELD == 0) {
                              fixture.m_metadata["source_frame_count"] = 0;
                          }
                          if (K_FIELD == 1) {
                              fixture.m_metadata["source_frame_count"] = 36001;
                          }
                          if (K_FIELD == 2) {
                              frames = nlohmann::json::object();
                          }
                          if (K_FIELD == 3) {
                              frames.clear();
                          }
                          if (K_FIELD == 4) {
                              frames = std::vector<nlohmann::json>(121, frames[0]);
                          }
                          if (K_FIELD == 5) {
                              frames[1]["source_index"] = 11;
                          }
                          if (K_FIELD == 6) {
                              frames[1]["source_index"] = 0;
                          }
                          if (K_FIELD == 7) {
                              frames[0]["frame_id"] = "different-frame";
                          }
                          if (K_FIELD == 8) {
                              frames[0]["calibration_id"] =
                                  fixture.m_source.substr(0, 16) + "-lens-1";
                          }
                          if (K_FIELD == 9) {
                              frames[0]["source_index"] = -1;
                          }
                          if (K_FIELD == 10) {
                              frames[0].erase("frame_id");
                          }
                          if (K_FIELD == 11) {
                              fixture.m_metadata["source_frame_count"] = "11";
                          }
                          fixture.publish();
                      });
}

TEST_CASE("decoded legacy timestamps reject malformed overflow and nonmonotonic records",
          "[decoded_admission][time]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(K_FIELD);
    reject_and_repair(ErrorCode::INVALID_DATA,
                      [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
                          auto& frame = fixture.m_metadata["frames"][1];
                          constexpr std::array K_BASES { "30",
                                                        "111111111111111111111111111111111/30",
                                                        "x/30",
                                                        "1x/30",
                                                        "1/x",
                                                        "1/30x",
                                                        "0/30",
                                                        "1/0",
                                                        "-1/30",
                                                        "1/-30" };
                          if (K_FIELD < 10) {
                              frame["time_base"] = K_BASES[static_cast<std::size_t>(K_FIELD)];
                          }
                          if (K_FIELD == 10) {
                              frame["pts"] = -1;
                          }
                          if (K_FIELD == 11) {
                              frame["pts"] = std::numeric_limits<std::uint64_t>::max();
                          }
                          if (K_FIELD == 12) {
                              frame["pts"] = std::numeric_limits<std::int64_t>::max();
                          }
                          if (K_FIELD == 13) {
                              frame["pts"] = 1201;
                              frame["time_base"] = "1/1";
                          }
                          if (K_FIELD == 14) {
                              frame["capture_time_ns"] = 333333334;
                          }
                          if (K_FIELD == 15) {
                              frame["pts"] = 0;
                              frame["capture_time_ns"] = 0;
                          }
                          if (K_FIELD == 16) {
                              frame["capture_time_ns"] = -1;
                          }
                          if (K_FIELD == 17) {
                              frame["time_base"] = "2147483648/1";
                          }
                          fixture.publish();
                      });
}

TEST_CASE("decoded image references reject unsafe and linked paths without touching targets",
          "[decoded_admission][paths]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
            auto& name = fixture.m_metadata["frames"][0]["file"];
            if (K_FIELD == 0) {
                name = "";
            }
            if (K_FIELD == 1) {
                name = std::string(4097, 'x');
            }
            if (K_FIELD == 2) {
                name = std::string("images/first.png\0suffix", 23);
            }
            if (K_FIELD == 3) {
                name = "images\\first.png";
            }
            if (K_FIELD == 4) {
                name = "C:first.png";
            }
            if (K_FIELD == 5) {
                name = "/first.png";
            }
            if (K_FIELD == 6) {
                name = "./images/first.png";
            }
            if (K_FIELD == 7) {
                name = "images/../images/first.png";
            }
            if (K_FIELD == 8) {
                name = "images/missing.png";
            }
            if (K_FIELD == 9) {
                name = "images";
            }
            fixture.publish();
            if (K_FIELD == 10) {
                const auto K_PATH = fixture.m_stage / "images/first.png";
                std::filesystem::remove(K_PATH);
                create_link(fixture.m_stage / "images/second.png", K_PATH);
            }
            if (K_FIELD == 11) {
                std::filesystem::rename(fixture.m_stage / "images",
                                        fixture.m_root / "outside-images");
                create_link(fixture.m_root / "outside-images", fixture.m_stage / "images", true);
            }
        });
}

TEST_CASE("decoded images require unique matching inventory entries and PNG headers",
          "[decoded_admission][images]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig&) {
            auto& frames = fixture.m_metadata["frames"];
            if (K_FIELD == 0) {
                frames[1]["file"] = frames[0]["file"];
            }
            if (K_FIELD == 1) {
                std::filesystem::rename(fixture.m_stage / "images/first.png",
                                        fixture.m_stage / "images/first.jpg");
                frames[0]["file"] = "images/first.jpg";
            }
            if (K_FIELD == 2) {
                frames[0]["sha256"] = std::string(64, '0');
            }
            if (K_FIELD == 3 || K_FIELD == 4) {
                const std::string K_BYTES = K_FIELD == 3 ? "short" : std::string(24, 'x');
                write_bytes(fixture.m_stage / "images/first.png", K_BYTES);
                frames[0]["sha256"] = sha256(std::as_bytes(std::span(K_BYTES)));
            }
            fixture.publish();
            if (K_FIELD >= 5) {
                fixture.completion([K_FIELD](auto& value) {
                    for (auto& entry : value["files"]) {
                        if (entry["path"] == "images/first.png") {
                            if (K_FIELD == 5) {
                                entry["path"] = "unused.png";
                            }
                            if (K_FIELD == 6) {
                                entry["bytes"] = 0;
                            }
                            if (K_FIELD == 7) {
                                entry["bytes"] = 64uz * 1024 * 1024 + 1;
                            }
                            if (K_FIELD == 8) {
                                entry["bytes"] = 73;
                            }
                        }
                    }
                });
            }
            if (K_FIELD == 9) {
                write_bytes(fixture.m_stage / "result.json", "{}");
            }
            if (K_FIELD == 10) {
                const auto K_PATH = fixture.m_stage / "result.json";
                std::filesystem::copy_file(K_PATH, fixture.m_root / "linked-result.json");
                std::filesystem::remove(K_PATH);
                create_link(fixture.m_root / "linked-result.json", K_PATH);
            }
        });
}

TEST_CASE("decoded RGB dimensions enforce exact budgets and one calibration size",
          "[decoded_admission][budget]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    reject_and_repair(K_FIELD == 6 ? ErrorCode::INVALID_DATA : ErrorCode::RESOURCE_EXHAUSTED,
                      [K_FIELD](AdmissionFixture& fixture, DecodedSourceConfig& config) {
                          constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> K_SIZES {
                              { { 0, 1 }, { 2, 0 }, { 8193, 1 }, { 1, 8193 }, { 8192, 2049 }, { 2, 1 } } };
                          if (K_FIELD < 6) {
                              const auto [K_WIDTH, K_HEIGHT] =
                                  K_SIZES[static_cast<std::size_t>(K_FIELD)];
                              fixture.dimensions("first.png", { K_WIDTH, K_HEIGHT });
                              config.m_max_image_bytes.m_bytes =
                                  K_FIELD == 5 ? 5 : 64uz * 1024 * 1024;
                          } else {
                              fixture.dimensions("second.png", { 1, 1 });
                          }
                      });
}

TEST_CASE("decoded catalog admits exact selection time and pixel limits without launching decoder",
          "[decoded_admission][catalog]") {
    const auto K_NATIVE = GENERATE(false, true);
    AdmissionFixture fixture;
    auto frames = nlohmann::json::array();
    for (int index { 0 }; index < 120; ++index) {
        auto frame = fixture.m_metadata["frames"][0];
        const auto K_NAME = "images/" + std::to_string(index) + ".png";
        std::filesystem::copy_file(fixture.m_stage / "images/first.png", fixture.m_stage / K_NAME);
        frame["file"] = K_NAME;
        const auto K_SOURCE_INDEX = index == 119 ? 35999 : index;
        frame["source_index"] = K_SOURCE_INDEX;
        frame["frame_id"] = fixture.m_source.substr(0, 16) + "-" + std::to_string(K_SOURCE_INDEX);
        frame["pts"] = index == 119 ? 1200 : index;
        frame["time_base"] = "1/1";
        frame["capture_time_ns"] = (index == 119 ? 1200LL : index) * 1000000000;
        frames.push_back(frame);
    }
    fixture.m_metadata["frames"] = frames;
    fixture.m_metadata["source_frame_count"] = 36000;
    if (K_NATIVE) {
        fixture.m_metadata["selection"] = "native_rgb_journal_v1";
        fixture.m_metadata["source_sha256"] = fixture.m_source;
        fixture.m_metadata["journal_sha256"] = std::string(64, 'c');
    }
    fixture.publish();
    // An impossible executable proves metadata inspection never starts a decoder.
    auto config = fixture.config();
    config.m_decoder_arguments = { (fixture.m_root / "absent-decoder").string() };
    const auto K_BEFORE = snapshot(fixture.m_root);
    const auto K_CATALOG = inspect_decoded_frame_source(config);
    REQUIRE(K_CATALOG);
    REQUIRE(K_CATALOG->m_observations.size() == 120);
    CHECK(K_CATALOG->m_observations.back().m_capture_time.count() == 1200000000000LL);
    CHECK(K_CATALOG->m_observations.front().m_width == 2);
    CHECK(K_CATALOG->m_observations.front().m_height == 1);
    CHECK(K_CATALOG->m_journal_sha256 ==
          (K_NATIVE ? std::optional { std::string(64, 'c') } : std::nullopt));
    CHECK(snapshot(fixture.m_root) == K_BEFORE);
}

TEST_CASE("decoded failed reads and seeks retain pending reset position and existing leases",
          "[decoded_admission][retry]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(K_FIELD);
    AdmissionFixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    const auto K_INITIAL = next_reset(**source);
    const auto K_FIRST = next_frame(**source);
    const auto K_PIXELS = *K_FIRST.m_pixels.m_bytes;
    REQUIRE((*source)->seek({ 0, 1, 1 }));
    const auto K_ORIGINAL = snapshot(fixture.m_root);
    auto code = ErrorCode::INVALID_DATA;
    std::stop_source cancellation;
    if (K_FIELD == 0) {
        cancellation.request_stop();
    }
    if (K_FIELD == 1) {
        write_bytes(fixture.m_stage / "completion.json", "{}");
    }
    if (K_FIELD == 2) {
        std::filesystem::remove(fixture.m_stage / "completion.json");
    }
    if (K_FIELD == 3) {
        std::filesystem::remove(fixture.m_stage / "completion.json");
        create_link(fixture.m_stage / "result.json", fixture.m_stage / "completion.json");
    }
    if (K_FIELD == 4) {
        write_bytes(fixture.m_stage / "completion.json", std::string(1024 * 1024 + 1, 'x'));
        code = ErrorCode::RESOURCE_EXHAUSTED;
    }
    if (K_FIELD < 5) {
        const auto K_BEFORE = snapshot(fixture.m_root);
        expect_error((*source)->read_next(cancellation.get_token()),
                     K_FIELD == 0 ? ErrorCode::CANCELLED : code);
        if (K_FIELD > 0) {
            expect_error((*source)->seek({ 0, 1, 1 }), code);
        }
        CHECK(snapshot(fixture.m_root) == K_BEFORE);
        fixture.repair(K_ORIGINAL);
    } else {
        constexpr std::array<SourceTimestamp, 5> K_BAD {
            { { -1, 1, 1 },
             { 1, 0, 1 },
             { 1201, 1, 1 },
             { 1, 1, 0 },
             { std::numeric_limits<std::int64_t>::max(), 1, 1 } } };
        expect_error((*source)->seek(K_BAD[static_cast<std::size_t>(K_FIELD - 5)]),
                     ErrorCode::INVALID_ARGUMENT);
    }
    const auto K_RESET = next_reset(**source);
    CHECK(K_RESET.m_epoch.m_value == 1);
    CHECK(K_RESET.m_capture == K_INITIAL.m_capture);
    CHECK(next_frame(**source).m_id == K_FIRST.m_id);
    const auto K_SECOND = next_frame(**source);
    CHECK(K_SECOND.m_id != K_FIRST.m_id);
    CHECK(*K_FIRST.m_pixels.m_bytes == K_PIXELS);
    for (int repeat { 0 }; repeat < 2; ++repeat) {
        const auto K_END = (*source)->read_next({});
        REQUIRE(K_END);
        CHECK(std::holds_alternative<SourceEnd>(*K_END));
    }
    CHECK(snapshot(fixture.m_root) == K_ORIGINAL);
}

TEST_CASE("decoded image integrity and decoder failures allow retry of the same frame",
          "[decoded_admission][decoder]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CAPTURE(K_FIELD);
    AdmissionFixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK(next_reset(**source).m_epoch.m_value == 0);
    const auto K_FIRST = next_frame(**source);
    const auto K_PIXELS = *K_FIRST.m_pixels.m_bytes;
    const auto K_ORIGINAL = snapshot(fixture.m_root);
    const auto K_IMAGE = fixture.m_stage / "images/second.png";
    auto code = ErrorCode::INVALID_DATA;
    if (K_FIELD == 0) {
        write_bytes(K_IMAGE, std::string(72, 'x'));
    }
    if (K_FIELD == 1) {
        write_bytes(K_IMAGE, std::string(73, 'x'));
        code = ErrorCode::RESOURCE_EXHAUSTED;
    }
    if (K_FIELD == 2) {
        std::filesystem::remove(K_IMAGE);
    }
    if (K_FIELD == 3) {
        write_bytes(fixture.m_decoder, "import sys\nsys.stdout.buffer.write(b'x')\n");
        code = ErrorCode::BACKEND_FAILURE;
    }
    if (K_FIELD == 4) {
        write_bytes(fixture.m_decoder, "import sys\nsys.exit(7)\n");
        code = ErrorCode::BACKEND_FAILURE;
    }
    if (K_FIELD == 5) {
        write_bytes(fixture.m_decoder, "import sys\nsys.stdout.buffer.write(b'1234567')\n");
        code = ErrorCode::RESOURCE_EXHAUSTED;
    }
    if (K_FIELD >= 6) {
        write_bytes(
            fixture.m_decoder,
            "import sys\nfrom pathlib import Path\np=Path(sys.argv[sys.argv.index('-i')+1])\n" +
                std::string(K_FIELD == 6   ? "p.write_bytes(b'x'*72)\n"
                            : K_FIELD == 7 ? "p.write_bytes(b'x'*73)\n"
                                           : "p.unlink()\n") +
                "sys.stdout.buffer.write(b'123456')\n");
        code = K_FIELD == 7   ? ErrorCode::RESOURCE_EXHAUSTED
               : K_FIELD == 8 ? ErrorCode::IO_ERROR
                              : ErrorCode::INVALID_DATA;
    }
    const auto K_BEFORE = snapshot(fixture.m_root);
    expect_error((*source)->read_next({}), code);
    // These three controlled decoders deliberately mutate only their selected input.
    // Every other failure must preserve the complete caller tree byte-for-byte.
    if (K_FIELD < 6) {
        CHECK(snapshot(fixture.m_root) == K_BEFORE);
    } else {
        auto after = snapshot(fixture.m_root);
        auto expected = K_BEFORE;
        const auto K_NAME = K_IMAGE.lexically_relative(fixture.m_root).generic_string();
        if (K_FIELD == 8) {
            expected.erase(K_NAME);
        } else {
            expected[K_NAME] = "file:" + std::string(K_FIELD == 6 ? 72 : 73, 'x');
        }
        CHECK(after == expected);
    }
    CHECK(*K_FIRST.m_pixels.m_bytes == K_PIXELS);
    fixture.repair(K_ORIGINAL);
    const auto K_SECOND = next_frame(**source);
    CHECK(K_SECOND.m_original_timestamp.m_pts == 10);
    CHECK(K_SECOND.m_capture_time.count() == 333333333);
    CHECK(K_SECOND.m_id != K_FIRST.m_id);
    source->reset();
    CHECK(*K_FIRST.m_pixels.m_bytes == K_PIXELS);
    CHECK(*K_SECOND.m_pixels.m_bytes == K_PIXELS);
    CHECK(snapshot(fixture.m_root) == K_ORIGINAL);
}

TEST_CASE("decoded legacy prefix and native full source identity retain their documented scope",
          "[decoded_admission][catalog]") {
    AdmissionFixture fixture;
    const auto K_ORIGINAL = inspect_decoded_frame_source(fixture.config());
    REQUIRE(K_ORIGINAL);
    CHECK_FALSE(K_ORIGINAL->m_journal_sha256);
    auto config = fixture.config();
    config.m_source_sha256.back() = 'c';
    // Legacy stages have only a 16-character source prefix, never independent full-SHA proof.
    const auto K_ALTERNATE = inspect_decoded_frame_source(config);
    REQUIRE(K_ALTERNATE);
    CHECK(K_ALTERNATE->m_capture != K_ORIGINAL->m_capture);
    CHECK(K_ALTERNATE->m_observations[0].m_id != K_ORIGINAL->m_observations[0].m_id);
    CHECK(K_ALTERNATE->m_observations[0].m_calibration !=
          K_ORIGINAL->m_observations[0].m_calibration);
    CHECK(K_ALTERNATE->m_observations[0].m_external_frame_id ==
          K_ORIGINAL->m_observations[0].m_external_frame_id);
    fixture.m_metadata["selection"] = "native_rgb_journal_v1";
    fixture.m_metadata["source_sha256"] = fixture.m_source;
    fixture.m_metadata["journal_sha256"] = std::string(64, 'c');
    fixture.publish();
    const auto K_BEFORE = snapshot(fixture.m_root);
    expect_error(inspect_decoded_frame_source(config), ErrorCode::INVALID_DATA);
    CHECK(snapshot(fixture.m_root) == K_BEFORE);
    const auto K_NATIVE = inspect_decoded_frame_source(fixture.config());
    REQUIRE(K_NATIVE);
    CHECK(K_NATIVE->m_capture == K_ORIGINAL->m_capture);
    CHECK(K_NATIVE->m_observations[0].m_id == K_ORIGINAL->m_observations[0].m_id);
    CHECK(K_NATIVE->m_journal_sha256 == std::optional { std::string(64, 'c') });
    CHECK_FALSE(K_ORIGINAL->m_journal_sha256);
    CHECK(K_ORIGINAL->m_completion_sha256 != K_NATIVE->m_completion_sha256);
}

TEST_CASE("decoded inspection admits exact inventory and header dimension limits",
          "[decoded_admission][budget]") {
    const auto K_SIZE =
        GENERATE(std::pair { 8192U, 2048U }, std::pair { 2048U, 8192U }, std::pair { 4096U, 4096U });
    AdmissionFixture fixture;
    // Synthetic PNG header diagnostics: pixel decoding is deliberately not claimed here.
    fixture.dimensions("first.png", K_SIZE);
    fixture.dimensions("second.png", K_SIZE);
    fixture.completion([](auto& value) {
        auto& files = value["files"];
        while (files.size() < 4096) {
            files.push_back({ { "path", "unused/" + std::to_string(files.size()) },
                             { "bytes", 0 },
                             { "sha256", std::string(64, '0') } });
        }
    });
    auto config = fixture.config();
    config.m_max_image_bytes.m_bytes = static_cast<std::size_t>(K_SIZE.first) * K_SIZE.second * 3;
    const auto K_BEFORE = snapshot(fixture.m_root);
    const auto K_CATALOG = inspect_decoded_frame_source(config);
    REQUIRE(K_CATALOG);
    CHECK(K_CATALOG->m_observations[0].m_width == K_SIZE.first);
    CHECK(K_CATALOG->m_observations[0].m_height == K_SIZE.second);
    --config.m_max_image_bytes.m_bytes;
    expect_error(inspect_decoded_frame_source(config), ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(snapshot(fixture.m_root) == K_BEFORE);
}

TEST_CASE("decoded in-flight cancellation retains the next frame and old RGB lease",
          "[decoded_admission][cancel]") {
    AdmissionFixture fixture;
    auto source = open_decoded_frame_source(fixture.config());
    REQUIRE(source);
    CHECK(next_reset(**source).m_epoch.m_value == 0);
    const auto K_FIRST = next_frame(**source);
    const auto K_PIXELS = *K_FIRST.m_pixels.m_bytes;
    write_bytes(fixture.m_decoder,
                "import sys,time\nfrom pathlib import Path\n"
                "p=Path(sys.argv[sys.argv.index('-i')+1])\n"
                "(p.parent.parent.parent/'started').write_text('ready')\n"
                "time.sleep(30)\n");
    const auto K_BEFORE = snapshot(fixture.m_root);
    std::stop_source cancellation;
    std::atomic<bool> started {};
    std::jthread observer([&](const std::stop_token& stop) {
        const auto K_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < K_DEADLINE) {
            std::error_code error;
            if (std::filesystem::is_regular_file(fixture.m_root / "started", error)) {
                started.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        cancellation.request_stop();
    });
    expect_error((*source)->read_next(cancellation.get_token()), ErrorCode::CANCELLED);
    observer.join();
    REQUIRE(started.load());
    auto expected = K_BEFORE;
    expected.emplace("started", "file:ready");
    CHECK(snapshot(fixture.m_root) == expected);
    CHECK(*K_FIRST.m_pixels.m_bytes == K_PIXELS);
    fixture.good_decoder();
    const auto K_SECOND = next_frame(**source);
    CHECK(K_SECOND.m_original_timestamp.m_pts == 10);
    CHECK(K_SECOND.m_id != K_FIRST.m_id);
    REQUIRE((*source)->seek({ 0, 1, 1 }));
    CHECK(next_reset(**source).m_epoch.m_value == 1);
    CHECK(next_frame(**source).m_id == K_FIRST.m_id);
    source->reset();
    CHECK(*K_FIRST.m_pixels.m_bytes == K_PIXELS);
    CHECK(*K_SECOND.m_pixels.m_bytes == K_PIXELS);
}
