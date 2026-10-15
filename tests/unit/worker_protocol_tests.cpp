#include "commands_generated.h"
#include "events_generated.h"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#ifdef __linux__
#include <csignal>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
using namespace world_explorer;
namespace {
constexpr std::string_view SESSION =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
std::vector<std::byte> handshake_frame(std::uint16_t major = 2,
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
        builder, wire::Createenvelope(builder, major, 0, 0, wire::payload::handshake, body.Union(),
                                      builder.CreateString(SESSION), 0));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
std::vector<std::byte> completion_frame(std::uint64_t sequence = 2) {
    flatbuffers::FlatBufferBuilder builder;
    const auto body = wire::Createcompleted(builder, builder.CreateString("capabilities"));
    wire::FinishSizePrefixedenvelopeBuffer(
        builder, wire::Createenvelope(builder, 2, 0, sequence, wire::payload::completed,
                                      body.Union(), builder.CreateString(SESSION), 1));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
std::vector<std::byte> acknowledgement_frame(std::uint64_t sequence = 1,
                                             wire::command_kind kind = wire::command_kind::START) {
    flatbuffers::FlatBufferBuilder builder;
    const bool cancel = kind == wire::command_kind::CANCEL;
    const auto body =
        wire::Createcommand_acknowledged(builder, cancel ? 1 : 0, cancel ? 2 : 1, kind);
    wire::FinishSizePrefixedenvelopeBuffer(
        builder, wire::Createenvelope(builder, 2, 0, sequence, wire::payload::command_acknowledged,
                                      body.Union(), builder.CreateString(SESSION), 1));
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
    worker_stream_decoder decoder{std::string(SESSION)};
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
    decoder.expect_start();
    REQUIRE(decoder.feed(acknowledgement_frame(), received));
    CHECK(events == 2);
    SECTION("successful terminal") {
        REQUIRE(decoder.feed(completion_frame(), received));
        CHECK(events == 3);
        REQUIRE(decoder.finish(0));
        CHECK_FALSE(decoder.finish(7));
        CHECK_FALSE(decoder.feed(handshake, received));
    }
    SECTION("duplicate handshake") { CHECK_FALSE(decoder.feed(handshake, received)); }
    SECTION("sequence gap") { CHECK_FALSE(decoder.feed(completion_frame(3), received)); }
    SECTION("truncated terminal") {
        auto completion = completion_frame();
        completion.pop_back();
        REQUIRE(decoder.feed(completion, received));
        CHECK_FALSE(decoder.finish(0));
    }
}
TEST_CASE("worker wire rejects untrusted lengths offsets versions and UTF-8 without publication") {
    worker_stream_decoder decoder{std::string(SESSION)};
    auto bytes = handshake_frame();
    SECTION("over budget length") { std::fill_n(bytes.begin(), 4, std::byte{0xff}); }
    SECTION("invalid root offset") { std::fill_n(bytes.begin() + 4, 4, std::byte{0xff}); }
    SECTION("wrong identifier") { bytes[8] = std::byte{'X'}; }
    SECTION("unsupported version") { bytes = handshake_frame(3); }
    SECTION("invalid UTF-8") { bytes = handshake_frame(2, std::string_view("\xff", 1)); }
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
    bool verified_completion{};
    const auto started = std::chrono::steady_clock::now();
    const auto result = execute_worker_protocol(
        WE_TEST_PYTHON, script, {mode, artifact.string()}, {},
        [&](const std::string& text) { log += text; },
        [&](const worker_event& event) {
            if (std::holds_alternative<worker_completed>(event))
                verified_completion = true;
        });
    INFO(log);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(4));
    REQUIRE(log.find("stderr diagnostic survives") != std::string::npos);
    CHECK(verified_completion == (std::string_view(mode) == "valid"));
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

TEST_CASE("worker START and CANCEL encoding enforce bounded structured arguments") {
    const auto started = encode_worker_start(std::string(SESSION),
                                             {"run", "--request", "path with $ ; spaces.json"});
    REQUIRE(started);
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(started->data()),
                                   started->size());
    REQUIRE(control::VerifySizePrefixedcommand_envelopeBuffer(verifier));
    const auto* root = control::GetSizePrefixedcommand_envelope(started->data());
    CHECK(root->operation() == 1);
    CHECK(root->sequence() == 0);
    CHECK(root->body_as_start_command()->arguments()->Get(2)->str() == "path with $ ; spaces.json");
    const auto cancelled = encode_worker_cancel(std::string(SESSION));
    REQUIRE(cancelled);
    const auto* cancel = control::GetSizePrefixedcommand_envelope(cancelled->data());
    CHECK(cancel->operation() == 2);
    CHECK(cancel->body_as_cancel_command()->target_operation() == 1);
    CHECK_FALSE(encode_worker_start("wrong", {"run"}));
    CHECK_FALSE(encode_worker_start(std::string(SESSION), {}));
    CHECK_FALSE(encode_worker_start(std::string(SESSION), {std::string("NUL\0tail", 8)}));
    CHECK_FALSE(encode_worker_start(std::string(SESSION), {std::string(1, '\xff')}));
    CHECK_FALSE(encode_worker_start(std::string(SESSION), {std::string(4097, 'x')}));
    CHECK_FALSE(encode_worker_start(std::string(SESSION), std::vector<std::string>(65, "x")));
    CHECK_FALSE(encode_worker_start(std::string(SESSION),
                                    std::vector<std::string>(9, std::string(4096, 'x'))));
}

TEST_CASE("worker completion observers require actual regular-file size and checksum validation",
          "[worker_validation][worker_manifest]") {
    temporary_worker worker;
    const auto script = worker.directory / "synthetic-manifest.py";
    const auto artifact = worker.directory / "manifest.json";
    const auto mode = GENERATE("valid", "missing", "directory", "size", "hash", "growing");
    std::ofstream file(script);
    file
        << "import sys, hashlib\nfrom pathlib import Path\nsys.path.insert(0, "
        << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump()
        << ")\nimport we_protocol as p\n"
           "p.configure(sys.argv,backend='synthetic_manifest',backend_version='1',"
           "sensors=['diagnostic'],devices=['cpu'],checkpoint_semantics='none',"
           "checkpoint_rights='synthetic',limits={'max_frames':120,'max_source_bytes':1024,"
           "'max_output_bytes':1024})\n"
           "mode=sys.argv[1]; path=Path(sys.argv[2]); data=b'{}'\n"
           "if mode == 'directory': path.mkdir()\n"
           "elif mode != 'missing': path.write_bytes(data + (b'x' if mode == 'growing' else b''))\n"
           "writer=p.WRITER; builder=writer.runtime.Builder(512)\n"
           "reference=writer.table(builder,'artifact_reference',{'Path':builder.CreateString(str("
           "path)),"
           "'Sha256':builder.CreateString('0'*64 if mode == 'hash' else "
           "hashlib.sha256(data).hexdigest()),"
           "'Bytes':3 if mode == 'size' else 2})\n"
           "body=writer.table(builder,'completed',{'Stage':builder.CreateString('synthetic'),"
           "'Artifact':reference})\nwriter.write(builder,'completed',body)\n";
    file.close();
    std::string log;
    std::size_t completions{};
    const auto result = execute_worker_protocol(
        WE_TEST_PYTHON, script, {mode, artifact.string()}, {},
        [&](const std::string& message) { log += message; },
        [&](const worker_event& event) {
            completions += std::holds_alternative<worker_completed>(event) ? 1U : 0U;
        });
    INFO(log);
    if (std::string_view(mode) == "valid") {
        REQUIRE(result);
        CHECK(*result == 0);
        CHECK(completions == 1);
    } else {
        REQUIRE(!result);
        CHECK(result.error().code == error_code::INVALID_DATA);
        CHECK(completions == 0);
        const bool invalid_file =
            std::string_view(mode) == "missing" || std::string_view(mode) == "directory";
        CHECK(result.error().message ==
              (invalid_file ? "Worker completion manifest must be a regular file"
                            : "Worker completion manifest checksum/size mismatch"));
    }
    CHECK(log.find("Worker accepted START.") != std::string::npos);
}

TEST_CASE("worker invocation rejects invalid START before launching a process",
          "[worker_validation]") {
    temporary_worker worker;
    const auto result = execute_worker_protocol(
        WE_TEST_PYTHON, worker.directory / "never-created.py", {""}, {}, {});
    REQUIRE(!result);
    CHECK(result.error().code == error_code::INVALID_ARGUMENT);
}

TEST_CASE("worker event sessions and command acknowledgements cannot be spoofed or reordered") {
    worker_stream_decoder decoder{std::string(SESSION)};
    const auto start = [&] {
        REQUIRE(decoder.feed(handshake_frame(), {}));
        decoder.expect_start();
        REQUIRE(decoder.feed(acknowledgement_frame(), {}));
    };
    SECTION("wrong session") {
        worker_stream_decoder other{std::string(64, 'b')};
        CHECK_FALSE(other.feed(handshake_frame(), {}));
    }
    SECTION("unsolicited START acknowledgement") {
        REQUIRE(decoder.feed(handshake_frame(), {}));
        CHECK_FALSE(decoder.feed(acknowledgement_frame(), {}));
    }
    SECTION("completion before START") {
        REQUIRE(decoder.feed(handshake_frame(), {}));
        CHECK_FALSE(decoder.feed(completion_frame(1), {}));
    }
    SECTION("unsolicited CANCEL acknowledgement") {
        start();
        CHECK_FALSE(decoder.feed(acknowledgement_frame(2, wire::command_kind::CANCEL), {}));
    }
    SECTION("acknowledged cancellation and duplicate replies") {
        start();
        decoder.expect_cancel();
        REQUIRE(decoder.feed(acknowledgement_frame(2, wire::command_kind::CANCEL), {}));
        REQUIRE(decoder.feed(acknowledgement_frame(3, wire::command_kind::CANCEL), {}));
        REQUIRE(decoder.feed(completion_frame(4), {}));
        CHECK(decoder.finish(0));
    }
}

namespace {
void control_fixture(const std::filesystem::path& script) {
    std::ofstream file(script);
    file << "import sys,time,signal\nfrom pathlib import Path\nsys.path.insert(0, "
         << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump()
         << ")\nimport we_protocol as p\n"
            "p.configure(sys.argv,backend='synthetic_control_fixture',backend_version='1',"
            "sensors=['diagnostic'],devices=['cpu'],checkpoint_semantics='diagnostic marker only',"
            "checkpoint_rights='synthetic',limits={'max_frames':120,'max_source_bytes':1024,'max_"
            "output_bytes':1024})\n"
            "mode=sys.argv[1]; marker=Path(sys.argv[2])\n"
            "with marker.open('a') as f: f.write('one invocation\\n')\n"
            "if mode == 'ignore':\n"
            " import worker as w, os, threading\n w.WIRE=p\n"
            " signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
            " child_marker=marker.with_suffix('.child')\n"
            " group=os.getpgrp() if os.name=='posix' else 0\n"
            " code='import os,time; from pathlib import Path; "
            "Path('+repr(str(child_marker))+').write_text(str(os.getpid())+\" \"+str(os.getpgrp() "
            "if os.name==\"posix\" else 0)+\" '+str(group)+'\"); time.sleep(30)'\n"
            " def ready():\n  while not child_marker.exists(): time.sleep(.01)\n  "
            "p.progress('diagnostic','ready')\n"
            " threading.Thread(target=ready,daemon=True).start()\n"
            " w.run_command([sys.executable,'-c',code],timeout=30)\n"
            "if mode == 'slow_ready': time.sleep(6.25)\n"
            "p.progress('diagnostic','ready')\n"
            "while not p.cancel_requested() or mode == 'ignore': time.sleep(.01)\n"
            "if mode == 'retry':\n while p.WRITER.sequence < 6: time.sleep(.01)\n"
            "marker.with_suffix('.checkpoint').write_text('synthetic checkpoint marker; no "
            "optimizer')\n"
            "p.failure('CANCELLED','diagnostic','cooperative stop')\nsys.exit(130)\n";
}
} // namespace
TEST_CASE("Python command verification rejects hostile START before executing an invocation") {
    temporary_worker worker;
    const auto script = worker.directory / "control.py";
    const auto marker = worker.directory / "launches";
    control_fixture(script);
    auto start = encode_worker_start(std::string(SESSION), {"wait", marker.string()}).value();
    const auto mode = GENERATE("length", "identifier", "root", "vtable", "version", "session",
                               "vector", "nul", "utf8", "cancel_first");
    const auto* root = control::GetSizePrefixedcommand_envelope(start.data());
    if (std::string_view(mode) == "length")
        std::fill_n(start.begin(), 4, std::byte{0xff});
    if (std::string_view(mode) == "identifier")
        start[8] = std::byte{'X'};
    if (std::string_view(mode) == "root")
        std::fill_n(start.begin() + 4, 4, std::byte{0xff});
    // Locate pointers only before mutations that make root access invalid.
    if (std::string_view(mode) == "vtable") {
        const auto offset = reinterpret_cast<const std::byte*>(root) - start.data();
        std::fill_n(start.begin() + offset, 4, std::byte{0xff});
    }
    if (std::string_view(mode) == "version") {
        flatbuffers::FlatBufferBuilder builder;
        const auto arg = builder.CreateVector(
            std::vector{builder.CreateString("wait"), builder.CreateString(marker.string())});
        const auto body = control::Createstart_command(builder, arg);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder, 3, 0, builder.CreateString(SESSION), 0, 1,
                                            control::command_payload::start_command, body.Union()));
        const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        start.assign(bytes.begin(), bytes.end());
    }
    if (std::string_view(mode) == "session") {
        const auto offset =
            reinterpret_cast<const std::byte*>(root->session()->data()) - start.data();
        start[static_cast<std::size_t>(offset)] = std::byte{'b'};
    }
    if (std::string_view(mode) == "vector") {
        const auto offset =
            reinterpret_cast<const std::byte*>(root->body_as_start_command()->arguments()) -
            start.data();
        std::fill_n(start.begin() + offset, 4, std::byte{0xff});
    }
    if (std::string_view(mode) == "nul" || std::string_view(mode) == "utf8") {
        const auto offset = reinterpret_cast<const std::byte*>(
                                root->body_as_start_command()->arguments()->Get(0)->data()) -
                            start.data();
        start[static_cast<std::size_t>(offset)] =
            std::string_view(mode) == "nul" ? std::byte{0} : std::byte{0xff};
    }
    if (std::string_view(mode) == "cancel_first")
        start = encode_worker_cancel(std::string(SESSION)).value();
    worker_stream_decoder decoder{std::string(SESSION)};
    bool sent{}, rejected{};
    std::stop_source cancellation;
    const auto executed =
        execute_process({WE_TEST_PYTHON, "-u", script.string(), "--wire-bindings",
                         WE_TEST_WIRE_BINDINGS, "--wire-session", std::string(SESSION)},
                        cancellation.get_token(),
                        [&](const std::string& bytes) {
                            const auto decoded = decoder.feed(
                                std::as_bytes(std::span(bytes.data(), bytes.size())),
                                [&](const worker_event& event) {
                                    if (const auto* failure = std::get_if<worker_failure>(&event))
                                        rejected = failure->code == "PROTOCOL_INVALID";
                                });
                            if (!decoded)
                                cancellation.request_stop();
                        },
                        {},
                        {[&](bool) -> result<std::vector<std::byte>> {
                             if (sent)
                                 return std::vector<std::byte>{};
                             sent = true;
                             return start;
                         },
                         {}});
    REQUIRE(executed);
    CHECK(*executed == 2);
    CHECK(decoder.finish(*executed));
    CHECK(rejected);
    CHECK_FALSE(std::filesystem::exists(marker));
}

TEST_CASE("typed worker cancellation acknowledges cooperative stop and bounds an ignored request") {
    temporary_worker worker;
    const auto script = worker.directory / "control.py";
    const auto marker = worker.directory / "launches";
    control_fixture(script);
    const auto mode = GENERATE("wait", "ignore", "before_start", "slow_ready");
    std::stop_source cancellation;
    bool start_ack{}, cancel_ack{};
    std::string log;
    const auto began = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> cancel_began;
    const auto result = execute_worker_protocol(
        WE_TEST_PYTHON, script, {mode, marker.string()}, cancellation.get_token(),
        [&](const std::string& text) { log += text; },
        [&](const worker_event& event) {
            if (const auto* ack = std::get_if<worker_command_acknowledged>(&event)) {
                start_ack |= ack->kind == worker_command_kind::START;
                cancel_ack |= ack->kind == worker_command_kind::CANCEL;
            }
            if ((std::string_view(mode) == "before_start" &&
                 std::holds_alternative<worker_handshake>(event)) ||
                std::holds_alternative<worker_progress>(event)) {
                if (!cancel_began)
                    cancel_began = std::chrono::steady_clock::now();
                cancellation.request_stop();
            }
        });
    const auto ended = std::chrono::steady_clock::now();
    CAPTURE(mode);
    INFO(log);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == error_code::CANCELLED);
    if (cancel_began) {
        INFO(
            "pre-cancel seconds: " << std::chrono::duration<double>(*cancel_began - began).count());
        INFO("cancellation seconds: "
             << std::chrono::duration<double>(ended - *cancel_began).count());
        // Startup and work before the request do not consume the cancellation bound.
        CHECK(ended - *cancel_began < std::chrono::seconds(6));
        if (std::string_view(mode) == "slow_ready")
            CHECK(*cancel_began - began >= std::chrono::seconds(6));
    } else {
        FAIL("Worker fixture never requested cancellation");
    }
    if (std::string_view(mode) == "before_start") {
        CHECK_FALSE(start_ack);
        CHECK_FALSE(std::filesystem::exists(marker));
    } else {
        CHECK(start_ack);
        CHECK(cancel_ack);
        CHECK(std::filesystem::exists(marker));
        auto checkpoint = marker;
        checkpoint.replace_extension(".checkpoint");
        CHECK(std::filesystem::exists(checkpoint) == (std::string_view(mode) != "ignore"));
#ifdef __linux__
        if (std::string_view(mode) == "ignore") {
            auto child_marker = marker;
            child_marker.replace_extension(".child");
            std::ifstream child_info(child_marker);
            int child_pid{}, group{}, owner_group{};
            REQUIRE(static_cast<bool>(child_info >> child_pid >> group >> owner_group));
            REQUIRE(child_pid > 0);
            CHECK(group == owner_group);
            std::ifstream status("/proc/" + std::to_string(child_pid) + "/stat");
            std::string line;
            if (std::getline(status, line)) {
                const auto state = line.at(line.rfind(')') + 2);
                CHECK((state == 'Z' || state == 'X'));
                if (state != 'Z' && state != 'X')
                    kill(child_pid, SIGKILL); // Only this fixture's recorded child.
            }
        }
#endif
    }
}

TEST_CASE("worker command retries execute once and reject changed operation content") {
    temporary_worker worker;
    const auto script = worker.directory / "control.py";
    const auto marker = worker.directory / "launches";
    control_fixture(script);
    bool changed{}, fragmented{}, alternate_encoding{};
    SECTION("identical START and CANCEL retries") {}
    SECTION("fragmented START prefix and body") { fragmented = true; }
    SECTION("equivalent START with explicit default fields") { alternate_encoding = true; }
    SECTION("different content under START operation 1") { changed = true; }
    const auto start =
        encode_worker_start(std::string(SESSION), {changed ? "wait" : "retry", marker.string()})
            .value();
    auto retry =
        changed ? encode_worker_start(std::string(SESSION), {"different", marker.string()}).value()
                : start;
    if (alternate_encoding) {
        flatbuffers::FlatBufferBuilder builder;
        builder.ForceDefaults(true);
        const auto args = builder.CreateVector(
            std::vector{builder.CreateString("retry"), builder.CreateString(marker.string())});
        const auto body = control::Createstart_command(builder, args);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder, 2, 0, builder.CreateString(SESSION), 0, 1,
                                            control::command_payload::start_command, body.Union()));
        const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        retry.assign(bytes.begin(), bytes.end());
        REQUIRE(retry != start);
    }
    const auto cancel = encode_worker_cancel(std::string(SESSION)).value();
    worker_stream_decoder decoder{std::string(SESSION)};
    decoder.expect_start();
    std::size_t start_acks{}, cancel_acks{}, sent{}, fragment_offset{};
    bool invalid_operation{};
    const auto executed = execute_process(
        {WE_TEST_PYTHON, "-u", script.string(), "--wire-bindings", WE_TEST_WIRE_BINDINGS,
         "--wire-session", std::string(SESSION)},
        {},
        [&](const std::string& bytes) {
            REQUIRE(decoder.feed(
                std::as_bytes(std::span(bytes.data(), bytes.size())),
                [&](const worker_event& event) {
                    if (const auto* ack = std::get_if<worker_command_acknowledged>(&event)) {
                        start_acks += ack->kind == worker_command_kind::START ? 1 : 0;
                        cancel_acks += ack->kind == worker_command_kind::CANCEL ? 1 : 0;
                    }
                    if (const auto* failure = std::get_if<worker_failure>(&event))
                        invalid_operation = failure->code == "PROTOCOL_INVALID";
                }));
        },
        {},
        {[&](bool) -> result<std::vector<std::byte>> {
             if (sent == 0) {
                 if (fragmented && fragment_offset == 0) {
                     fragment_offset = 3;
                     return std::vector<std::byte>(start.begin(), start.begin() + 3);
                 }
                 ++sent;
                 return std::vector<std::byte>(
                     start.begin() + static_cast<std::ptrdiff_t>(fragment_offset), start.end());
             }
             if (sent == 1 && start_acks == 1) {
                 ++sent;
                 return retry;
             }
             if (!changed && sent == 2 && start_acks == 2) {
                 ++sent;
                 decoder.expect_cancel();
                 return cancel;
             }
             if (!changed && sent == 3 && cancel_acks == 1) {
                 ++sent;
                 return cancel;
             }
             return std::vector<std::byte>{};
         },
         {}});
    REQUIRE(executed);
    CHECK(*executed == 130);
    CHECK(decoder.finish(*executed));
    CHECK(invalid_operation == changed);
    CHECK(start_acks == (changed ? 1 : 2));
    CHECK(cancel_acks == (changed ? 0 : 2));
    std::ifstream launches(marker);
    std::string line;
    REQUIRE(std::getline(launches, line));
    CHECK(line == "one invocation");
    CHECK_FALSE(std::getline(launches, line));
}

#ifndef _WIN32
TEST_CASE("worker launch reserves descriptors when the desktop host has no stdin") {
    temporary_worker worker;
    const auto script = worker.directory / "capabilities.py";
    std::ofstream file(script);
    file << "import sys\nsys.path.insert(0," << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump()
         << ")\nimport we_protocol as p\n"
            "p.configure(sys.argv,backend='synthetic_closed_stdin',backend_version='1',"
            "sensors=['diagnostic'],devices=['cpu'],checkpoint_semantics='none',checkpoint_rights='"
            "synthetic',"
            "limits={'max_frames':120,'max_source_bytes':1024,'max_output_bytes':1024})\n"
            "p.completed('capabilities')\n";
    file.close();
    struct stdin_guard {
        int original{dup(STDIN_FILENO)};
        stdin_guard() { close(STDIN_FILENO); }
        ~stdin_guard() {
            if (original >= 0) {
                dup2(original, STDIN_FILENO);
                close(original);
            }
        }
    } restore;
    const auto result = execute_worker_protocol(WE_TEST_PYTHON, script, {"capabilities"}, {}, {});
    INFO((result ? "completed" : result.error().message));
    REQUIRE(result);
    CHECK(*result == 0);
}
#endif
