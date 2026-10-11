#include "events_generated.h"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace world_explorer;
namespace {
std::vector<std::byte> handshake_frame(std::uint16_t major = 1,
                                       std::string_view backend = "diagnostic") {
    flatbuffers::FlatBufferBuilder builder;
    const auto name = builder.CreateString(backend);
    const auto version = builder.CreateString("test-1");
    const auto sensors = builder.CreateVector(std::vector{builder.CreateString("video")});
    const auto devices = builder.CreateVector(std::vector{builder.CreateString("cpu")});
    const auto checkpoint = builder.CreateString("completed stages");
    const auto rights = builder.CreateString("local diagnostic data");
    const auto body = wire::Createhandshake(builder, name, version, sensors, true, false, devices,
                                            checkpoint, rights, WORKER_MESSAGE_LIMIT, 120,
                                            16ULL * 1024 * 1024 * 1024, 32ULL * 1024 * 1024 * 1024);
    wire::FinishSizePrefixedenvelopeBuffer(
        builder,
        wire::Createenvelope(builder, major, 0, 0, wire::payload::handshake, body.Union()));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
std::vector<std::byte> completion_frame(std::uint64_t sequence = 1) {
    flatbuffers::FlatBufferBuilder builder;
    const auto body = wire::Createcompleted(builder, builder.CreateString("capabilities"));
    wire::FinishSizePrefixedenvelopeBuffer(
        builder,
        wire::Createenvelope(builder, 1, 0, sequence, wire::payload::completed, body.Union()));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
struct temporary_worker {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("we_wire_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    temporary_worker() { std::filesystem::create_directories(directory); }
    ~temporary_worker() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
} // namespace
TEST_CASE(
    "worker wire verifies fragmented frames and enforces handshake sequence and terminal state") {
    worker_stream_decoder decoder;
    const auto handshake = handshake_frame();
    std::size_t events{};
    const auto received = [&](const worker_event& event) {
        ++events;
        if (const auto* capabilities = std::get_if<worker_handshake>(&event)) {
            CHECK(capabilities->backend == "diagnostic");
            CHECK(capabilities->batch);
            CHECK_FALSE(capabilities->streaming_tracking);
        }
    };
    for (const auto& byte : handshake)
        REQUIRE(decoder.feed(std::span(&byte, 1), received));
    CHECK(events == 1);
    CHECK_FALSE(decoder.finish(0));
    SECTION("successful terminal") {
        REQUIRE(decoder.feed(completion_frame(), received));
        CHECK(events == 2);
        REQUIRE(decoder.finish(0));
        CHECK_FALSE(decoder.finish(7));
        CHECK_FALSE(decoder.feed(handshake, received));
    }
    SECTION("duplicate handshake") { CHECK_FALSE(decoder.feed(handshake, received)); }
    SECTION("sequence gap") { CHECK_FALSE(decoder.feed(completion_frame(2), received)); }
    SECTION("truncated terminal") {
        auto completion = completion_frame();
        completion.pop_back();
        REQUIRE(decoder.feed(completion, received));
        CHECK_FALSE(decoder.finish(0));
    }
}
TEST_CASE("worker wire rejects untrusted lengths offsets versions and UTF-8 without publication") {
    worker_stream_decoder decoder;
    auto bytes = handshake_frame();
    SECTION("over budget length") { std::fill_n(bytes.begin(), 4, std::byte{0xff}); }
    SECTION("invalid root offset") { std::fill_n(bytes.begin() + 4, 4, std::byte{0xff}); }
    SECTION("wrong identifier") { bytes[8] = std::byte{'X'}; }
    SECTION("unsupported version") { bytes = handshake_frame(2); }
    SECTION("invalid UTF-8") { bytes = handshake_frame(1, std::string_view("\xff", 1)); }
    SECTION("terminal before handshake") { bytes = completion_frame(0); }
    std::size_t events{};
    CHECK_FALSE(decoder.feed(bytes, [&](const worker_event&) { ++events; }));
    CHECK(events == 0);
    CHECK_FALSE(decoder.finish(0));
    CHECK_FALSE(decoder.feed(handshake_frame(), {}));
}
TEST_CASE("native framed worker separates diagnostics and verifies completion manifests") {
    temporary_worker worker;
    const auto script = worker.directory / "worker with spaces.py";
    const auto artifact = worker.directory / "manifest.json";
    const auto mode = GENERATE("valid", "truncated", "corrupt", "corrupt_hang", "silent", "failure",
                               "missing", "changed");
    std::ofstream file(script);
    file << "import sys, time\nfrom pathlib import Path\nsys.path.insert(0, "
         << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump()
         << ")\nimport we_protocol as p\n"
            "p.configure(sys.argv,backend='diagnostic',backend_version='1',sensors=['video'],"
            "devices=['cpu'],"
            "checkpoint_semantics='completed',checkpoint_rights='local diagnostic data',"
            "limits={'max_frames':120,'max_source_bytes':1024,'max_output_bytes':1024})\n"
            "mode=sys.argv[1]\npath=Path(sys.argv[2])\n"
            "with p.diagnostic_context(): print('stderr diagnostic survives')\n"
            "if mode == 'silent': raise SystemExit(0)\n"
            "if mode == 'failure':\n p.failure('BACKEND_FAILED','test','explicit failure'); raise "
            "SystemExit(2)\n"
            "if mode == 'truncated': sys.stdout.buffer.write(b'\\x20\\x00'); raise SystemExit(0)\n"
            "if mode == 'corrupt': sys.stdout.buffer.write(b'\\xff'*4); raise SystemExit(0)\n"
            "if mode == 'corrupt_hang': sys.stdout.buffer.write(b'\\xff'*4); "
            "sys.stdout.buffer.flush(); time.sleep(20); raise SystemExit(0)\n"
            "path.write_text('{}')\n"
            "class mutating_stream:\n"
            " def write(self,data):\n"
            "  if mode == 'missing': path.unlink()\n"
            "  if mode == 'changed': path.write_text('changed')\n"
            "  return sys.stdout.buffer.write(data)\n"
            " def flush(self): sys.stdout.buffer.flush()\n"
            "p.WRITER.stream=mutating_stream()\np.completed('test',path)\n";
    file.close();
    std::string log;
    const auto started = std::chrono::steady_clock::now();
    const auto result = run_worker({script, WE_TEST_PYTHON}, {mode, artifact.string()}, {},
                                   [&](const std::string& text) { log += text; });
    INFO(log);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(4));
    REQUIRE(log.find("stderr diagnostic survives") != std::string::npos);
    if (std::string_view(mode) == "valid") {
        REQUIRE(result);
        CHECK(*result == 0);
    } else if (std::string_view(mode) == "failure") {
        REQUIRE(result);
        CHECK(*result == 2);
        CHECK(log.find("explicit failure") != std::string::npos);
    } else
        CHECK_FALSE(result);
}
