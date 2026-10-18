#include "commands_generated.h"
#include "events_generated.h"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <chrono>
#include <fstream>
#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#ifdef __linux__
#include <csignal>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
using namespace world_explorer;

namespace {

constexpr std::string_view K_SESSION {
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};

std::vector<std::byte> handshake_frame(std::uint16_t major = 2,
                                       std::string_view backend = "diagnostic") {
    flatbuffers::FlatBufferBuilder builder;
    const auto K_NAME = builder.CreateString(backend);
    const auto K_VERSION = builder.CreateString("test-1");
    const auto K_SENSORS = builder.CreateVector(std::vector { builder.CreateString("video") });
    const auto K_DEVICES = builder.CreateVector(std::vector { builder.CreateString("cpu") });
    const auto K_CHECKPOINT = builder.CreateString("completed stages");
    const auto K_RIGHTS = builder.CreateString("local diagnostic data");
    const auto K_BODY = wire::Createhandshake(builder,
                                              K_NAME,
                                              K_VERSION,
                                              K_SENSORS,
                                              true,
                                              false,
                                              K_DEVICES,
                                              K_CHECKPOINT,
                                              K_RIGHTS,
                                              K_WORKER_MESSAGE_LIMIT,
                                              120,
                                              16ULL * 1024 * 1024 * 1024,
                                              32ULL * 1024 * 1024 * 1024);
    wire::FinishSizePrefixedenvelopeBuffer(builder,
                                           wire::Createenvelope(builder,
                                                                major,
                                                                0,
                                                                0,
                                                                wire::payload::handshake,
                                                                K_BODY.Union(),
                                                                builder.CreateString(K_SESSION),
                                                                0));
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return { K_BYTES.begin(), K_BYTES.end() };
}

std::vector<std::byte> completion_frame(std::uint64_t sequence = 2) {
    flatbuffers::FlatBufferBuilder builder;
    const auto K_BODY = wire::Createcompleted(builder, builder.CreateString("capabilities"));
    wire::FinishSizePrefixedenvelopeBuffer(builder,
                                           wire::Createenvelope(builder,
                                                                2,
                                                                0,
                                                                sequence,
                                                                wire::payload::completed,
                                                                K_BODY.Union(),
                                                                builder.CreateString(K_SESSION),
                                                                1));
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return { K_BYTES.begin(), K_BYTES.end() };
}

std::vector<std::byte> acknowledgement_frame(std::uint64_t sequence = 1,
                                             wire::command_kind kind = wire::command_kind::START) {
    flatbuffers::FlatBufferBuilder builder;
    const bool K_CANCEL = kind == wire::command_kind::CANCEL;
    const auto K_BODY =
        wire::Createcommand_acknowledged(builder, K_CANCEL ? 1 : 0, K_CANCEL ? 2 : 1, kind);
    wire::FinishSizePrefixedenvelopeBuffer(builder,
                                           wire::Createenvelope(builder,
                                                                2,
                                                                0,
                                                                sequence,
                                                                wire::payload::command_acknowledged,
                                                                K_BODY.Union(),
                                                                builder.CreateString(K_SESSION),
                                                                1));
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return { K_BYTES.begin(), K_BYTES.end() };
}

struct TemporaryWorker {
    std::filesystem::path m_directory =
        std::filesystem::temp_directory_path() /
        ("we_wire_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    TemporaryWorker() { std::filesystem::create_directories(m_directory); }

    ~TemporaryWorker() {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }
};

}  // namespace

TEST_CASE(
    "worker wire verifies fragmented frames and enforces handshake sequence and terminal state") {
    WorkerStreamDecoder decoder { std::string(K_SESSION) };
    const auto K_HANDSHAKE = handshake_frame();
    std::size_t events {};
    const auto K_RECEIVED = [&](const WorkerEvent& event) {
        ++events;
        if (const auto* capabilities = std::get_if<WorkerHandshake>(&event)) {
            CHECK(capabilities->m_backend == "diagnostic");
            CHECK(capabilities->m_batch);
            CHECK_FALSE(capabilities->m_streaming_tracking);
        }
    };
    for (const auto& byte : K_HANDSHAKE) REQUIRE(decoder.feed(std::span(&byte, 1), K_RECEIVED));
    CHECK(events == 1);
    CHECK_FALSE(decoder.finish(0));
    decoder.expect_start();
    REQUIRE(decoder.feed(acknowledgement_frame(), K_RECEIVED));
    CHECK(events == 2);
    SECTION("successful terminal") {
        REQUIRE(decoder.feed(completion_frame(), K_RECEIVED));
        CHECK(events == 3);
        REQUIRE(decoder.finish(0));
        CHECK_FALSE(decoder.finish(7));
        CHECK_FALSE(decoder.feed(K_HANDSHAKE, K_RECEIVED));
    }
    SECTION("duplicate handshake") {
        CHECK_FALSE(decoder.feed(K_HANDSHAKE, K_RECEIVED));
    }
    SECTION("sequence gap") {
        CHECK_FALSE(decoder.feed(completion_frame(3), K_RECEIVED));
    }
    SECTION("truncated terminal") {
        auto completion = completion_frame();
        completion.pop_back();
        REQUIRE(decoder.feed(completion, K_RECEIVED));
        CHECK_FALSE(decoder.finish(0));
    }
}

TEST_CASE("worker wire rejects untrusted lengths offsets versions and UTF-8 without publication") {
    WorkerStreamDecoder decoder { std::string(K_SESSION) };
    auto bytes = handshake_frame();
    SECTION("over budget length") {
        std::fill_n(bytes.begin(), 4, std::byte { 0xff });
    }
    SECTION("invalid root offset") {
        std::fill_n(bytes.begin() + 4, 4, std::byte { 0xff });
    }
    SECTION("wrong identifier") {
        bytes[8] = std::byte { 'X' };
    }
    SECTION("unsupported version") {
        bytes = handshake_frame(3);
    }
    SECTION("invalid UTF-8") {
        bytes = handshake_frame(2, std::string_view("\xff", 1));
    }
    SECTION("terminal before handshake") {
        bytes = completion_frame(0);
    }
    std::size_t events {};
    CHECK_FALSE(decoder.feed(bytes, [&](const WorkerEvent&) { ++events; }));
    CHECK(events == 0);
    CHECK_FALSE(decoder.finish(0));
    CHECK_FALSE(decoder.feed(handshake_frame(), {}));
}

TEST_CASE("native framed worker separates diagnostics and verifies completion manifests") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "worker with spaces.py";
    const auto K_ARTIFACT = worker.m_directory / "manifest.json";
    const auto K_MODE = GENERATE(
        "valid", "truncated", "corrupt", "corrupt_hang", "silent", "failure", "missing", "changed");
    std::ofstream file(K_SCRIPT);
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
    bool verified_completion {};
    const auto K_STARTED = std::chrono::steady_clock::now();
    const auto K_RESULT = execute_worker_protocol(
        WE_TEST_PYTHON,
        K_SCRIPT,
        {K_MODE, K_ARTIFACT.string()},
        {},
        [&](const std::string& text) { log += text; },
        [&](const WorkerEvent& event) {
            if (std::holds_alternative<WorkerCompleted>(event)) verified_completion = true;
        });
    INFO(log);
    CHECK(std::chrono::steady_clock::now() - K_STARTED < std::chrono::seconds(4));
    REQUIRE(log.find("stderr diagnostic survives") != std::string::npos);
    CHECK(verified_completion == (std::string_view(K_MODE) == "valid"));
    if (std::string_view(K_MODE) == "valid") {
        REQUIRE(K_RESULT);
        CHECK(*K_RESULT == 0);
    } else if (std::string_view(K_MODE) == "failure") {
        REQUIRE(K_RESULT);
        CHECK(*K_RESULT == 2);
        CHECK(log.find("explicit failure") != std::string::npos);
    } else CHECK_FALSE(K_RESULT);
}

TEST_CASE("worker START and CANCEL encoding enforce bounded structured arguments") {
    const auto K_STARTED = encode_worker_start(std::string(K_SESSION),
                                               {"run", "--request", "path with $ ; spaces.json"});
    REQUIRE(K_STARTED);
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(K_STARTED->data()),
                                   K_STARTED->size());
    REQUIRE(control::VerifySizePrefixedcommand_envelopeBuffer(verifier));
    const auto* root = control::GetSizePrefixedcommand_envelope(K_STARTED->data());
    CHECK(root->operation() == 1);
    CHECK(root->sequence() == 0);
    CHECK(root->body_as_start_command()->arguments()->Get(2)->str() == "path with $ ; spaces.json");
    const auto K_CANCELLED = encode_worker_cancel(std::string(K_SESSION));
    REQUIRE(K_CANCELLED);
    const auto* cancel = control::GetSizePrefixedcommand_envelope(K_CANCELLED->data());
    CHECK(cancel->operation() == 2);
    CHECK(cancel->body_as_cancel_command()->target_operation() == 1);
    CHECK_FALSE(encode_worker_start("wrong", {"run"}));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION), {}));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION), {std::string("NUL\0tail", 8)}));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION), {std::string(1, '\xff')}));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION), {std::string(4097, 'x')}));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION), std::vector<std::string>(65, "x")));
    CHECK_FALSE(encode_worker_start(std::string(K_SESSION),
                                    std::vector<std::string>(9, std::string(4096, 'x'))));
}

TEST_CASE("worker completion observers require actual regular-file size and checksum validation",
          "[worker_validation][worker_manifest]") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "synthetic-manifest.py";
    const auto K_ARTIFACT = worker.m_directory / "manifest.json";
    const auto K_MODE = GENERATE("valid", "missing", "directory", "size", "hash", "growing");
    std::ofstream file(K_SCRIPT);
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
    std::size_t completions {};
    const auto K_RESULT = execute_worker_protocol(
        WE_TEST_PYTHON,
        K_SCRIPT,
        {K_MODE, K_ARTIFACT.string()},
        {},
        [&](const std::string& message) { log += message; },
        [&](const WorkerEvent& event) {
            completions += std::holds_alternative<WorkerCompleted>(event) ? 1U : 0U;
        });
    INFO(log);
    if (std::string_view(K_MODE) == "valid") {
        REQUIRE(K_RESULT);
        CHECK(*K_RESULT == 0);
        CHECK(completions == 1);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
        CHECK(completions == 0);
        const bool K_INVALID_FILE =
            std::string_view(K_MODE) == "missing" || std::string_view(K_MODE) == "directory";
        CHECK(K_RESULT.error().m_message ==
              (K_INVALID_FILE ? "Worker completion manifest must be a regular file"
                              : "Worker completion manifest checksum/size mismatch"));
    }
    CHECK(log.find("Worker accepted START.") != std::string::npos);
}

TEST_CASE("worker invocation rejects invalid START before launching a process",
          "[worker_validation]") {
    TemporaryWorker worker;
    const auto K_RESULT = execute_worker_protocol(
        WE_TEST_PYTHON, worker.m_directory / "never-created.py", {""}, {}, {});
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT);
}

TEST_CASE("worker event sessions and command acknowledgements cannot be spoofed or reordered") {
    WorkerStreamDecoder decoder { std::string(K_SESSION) };
    const auto K_START = [&] {
        REQUIRE(decoder.feed(handshake_frame(), {}));
        decoder.expect_start();
        REQUIRE(decoder.feed(acknowledgement_frame(), {}));
    };
    SECTION("wrong session") {
        WorkerStreamDecoder other { std::string(64, 'b') };
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
        K_START();
        CHECK_FALSE(decoder.feed(acknowledgement_frame(2, wire::command_kind::CANCEL), {}));
    }
    SECTION("acknowledged cancellation and duplicate replies") {
        K_START();
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

}  // namespace

TEST_CASE("Python command verification rejects hostile START before executing an invocation") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "control.py";
    const auto K_MARKER = worker.m_directory / "launches";
    control_fixture(K_SCRIPT);
    auto start = encode_worker_start(std::string(K_SESSION), {"wait", K_MARKER.string()}).value();
    const auto K_MODE = GENERATE("length",
                                 "identifier",
                                 "root",
                                 "vtable",
                                 "version",
                                 "session",
                                 "vector",
                                 "nul",
                                 "utf8",
                                 "cancel_first");
    const auto* root = control::GetSizePrefixedcommand_envelope(start.data());
    if (std::string_view(K_MODE) == "length") std::fill_n(start.begin(), 4, std::byte { 0xff });
    if (std::string_view(K_MODE) == "identifier") start[8] = std::byte { 'X' };
    if (std::string_view(K_MODE) == "root") std::fill_n(start.begin() + 4, 4, std::byte { 0xff });
    // Locate pointers only before mutations that make root access invalid.
    if (std::string_view(K_MODE) == "vtable") {
        const auto K_OFFSET = reinterpret_cast<const std::byte*>(root) - start.data();
        std::fill_n(start.begin() + K_OFFSET, 4, std::byte { 0xff });
    }
    if (std::string_view(K_MODE) == "version") {
        flatbuffers::FlatBufferBuilder builder;
        const auto K_ARG = builder.CreateVector(
            std::vector { builder.CreateString("wait"), builder.CreateString(K_MARKER.string()) });
        const auto K_BODY = control::Createstart_command(builder, K_ARG);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder,
                                            3,
                                            0,
                                            builder.CreateString(K_SESSION),
                                            0,
                                            1,
                                            control::command_payload::start_command,
                                            K_BODY.Union()));
        const auto K_BYTES =
            std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        start.assign(K_BYTES.begin(), K_BYTES.end());
    }
    if (std::string_view(K_MODE) == "session") {
        const auto K_OFFSET =
            reinterpret_cast<const std::byte*>(root->session()->data()) - start.data();
        start[static_cast<std::size_t>(K_OFFSET)] = std::byte { 'b' };
    }
    if (std::string_view(K_MODE) == "vector") {
        const auto K_OFFSET =
            reinterpret_cast<const std::byte*>(root->body_as_start_command()->arguments()) -
            start.data();
        std::fill_n(start.begin() + K_OFFSET, 4, std::byte { 0xff });
    }
    if (std::string_view(K_MODE) == "nul" || std::string_view(K_MODE) == "utf8") {
        const auto K_OFFSET = reinterpret_cast<const std::byte*>(
                                  root->body_as_start_command()->arguments()->Get(0)->data()) -
                              start.data();
        start[static_cast<std::size_t>(K_OFFSET)] =
            std::string_view(K_MODE) == "nul" ? std::byte { 0 } : std::byte { 0xff };
    }
    if (std::string_view(K_MODE) == "cancel_first")
        start = encode_worker_cancel(std::string(K_SESSION)).value();
    WorkerStreamDecoder decoder { std::string(K_SESSION) };
    bool sent {}, rejected {};
    std::stop_source cancellation;
    const auto K_EXECUTED =
        execute_process({WE_TEST_PYTHON,
                         "-u",
                         K_SCRIPT.string(),
                         "--wire-bindings",
                         WE_TEST_WIRE_BINDINGS,
                         "--wire-session",
                         std::string(K_SESSION)},
                        cancellation.get_token(),
                        [&](const std::string& bytes) {
                            const auto K_DECODED = decoder.feed(
                                std::as_bytes(std::span(bytes.data(), bytes.size())),
                                [&](const WorkerEvent& event) {
                                    if (const auto* failure = std::get_if<WorkerFailure>(&event))
                                        rejected = failure->m_code == "PROTOCOL_INVALID";
                                });
                            if (!K_DECODED) cancellation.request_stop();
                        },
                        {},
                        {[&](bool) -> Result<std::vector<std::byte>> {
                             if (sent) return std::vector<std::byte> {};
                             sent = true;
                             return start;
                         },
                         {}});
    REQUIRE(K_EXECUTED);
    CHECK(*K_EXECUTED == 2);
    CHECK(decoder.finish(*K_EXECUTED));
    CHECK(rejected);
    CHECK_FALSE(std::filesystem::exists(K_MARKER));
}

TEST_CASE("typed worker cancellation acknowledges cooperative stop and bounds an ignored request") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "control.py";
    const auto K_MARKER = worker.m_directory / "launches";
    control_fixture(K_SCRIPT);
    const auto K_MODE = GENERATE("wait", "ignore", "before_start", "slow_ready");
    std::stop_source cancellation;
    bool start_ack {}, cancel_ack {};
    std::string log;
    const auto K_BEGAN = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> cancel_began;
    const auto K_RESULT = execute_worker_protocol(
        WE_TEST_PYTHON,
        K_SCRIPT,
        {K_MODE, K_MARKER.string()},
        cancellation.get_token(),
        [&](const std::string& text) { log += text; },
        [&](const WorkerEvent& event) {
            if (const auto* ack = std::get_if<WorkerCommandAcknowledged>(&event)) {
                start_ack |= ack->m_kind == WorkerCommandKind::START;
                cancel_ack |= ack->m_kind == WorkerCommandKind::CANCEL;
            }
            if ((std::string_view(K_MODE) == "before_start" &&
                 std::holds_alternative<WorkerHandshake>(event)) ||
                std::holds_alternative<WorkerProgress>(event)) {
                if (!cancel_began) cancel_began = std::chrono::steady_clock::now();
                cancellation.request_stop();
            }
        });
    const auto K_ENDED = std::chrono::steady_clock::now();
    CAPTURE(K_MODE);
    INFO(log);
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    if (cancel_began) {
        INFO("pre-cancel seconds: "
             << std::chrono::duration<double>(*cancel_began - K_BEGAN).count());
        INFO("cancellation seconds: "
             << std::chrono::duration<double>(K_ENDED - *cancel_began).count());
        // Startup and work before the request do not consume the cancellation bound.
        CHECK(K_ENDED - *cancel_began < std::chrono::seconds(6));
        if (std::string_view(K_MODE) == "slow_ready")
            CHECK(*cancel_began - K_BEGAN >= std::chrono::seconds(6));
    } else {
        FAIL("Worker fixture never requested cancellation");
    }
    if (std::string_view(K_MODE) == "before_start") {
        CHECK_FALSE(start_ack);
        CHECK_FALSE(std::filesystem::exists(K_MARKER));
    } else {
        CHECK(start_ack);
        CHECK(cancel_ack);
        CHECK(std::filesystem::exists(K_MARKER));
        auto checkpoint = K_MARKER;
        checkpoint.replace_extension(".checkpoint");
        CHECK(std::filesystem::exists(checkpoint) == (std::string_view(K_MODE) != "ignore"));
#ifdef __linux__
        if (std::string_view(K_MODE) == "ignore") {
            auto child_marker = K_MARKER;
            child_marker.replace_extension(".child");
            std::ifstream child_info(child_marker);
            int child_pid {}, group {}, owner_group {};
            REQUIRE(static_cast<bool>(child_info >> child_pid >> group >> owner_group));
            REQUIRE(child_pid > 0);
            CHECK(group == owner_group);
            std::ifstream status("/proc/" + std::to_string(child_pid) + "/stat");
            std::string line;
            if (std::getline(status, line)) {
                const auto K_STATE = line.at(line.rfind(')') + 2);
                CHECK((K_STATE == 'Z' || K_STATE == 'X'));
                if (K_STATE != 'Z' && K_STATE != 'X')
                    kill(child_pid, SIGKILL);  // Only this fixture's recorded child.
            }
        }
#endif
    }
}

TEST_CASE("worker command retries execute once and reject changed operation content") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "control.py";
    const auto K_MARKER = worker.m_directory / "launches";
    control_fixture(K_SCRIPT);
    bool changed {}, fragmented {}, alternate_encoding {};
    SECTION("identical START and CANCEL retries") {
    }
    SECTION("fragmented START prefix and body") {
        fragmented = true;
    }
    SECTION("equivalent START with explicit default fields") {
        alternate_encoding = true;
    }
    SECTION("different content under START operation 1") {
        changed = true;
    }
    const auto K_START =
        encode_worker_start(std::string(K_SESSION), {changed ? "wait" : "retry", K_MARKER.string()})
            .value();
    auto retry =
        changed
            ? encode_worker_start(std::string(K_SESSION), {"different", K_MARKER.string()}).value()
            : K_START;
    if (alternate_encoding) {
        flatbuffers::FlatBufferBuilder builder;
        builder.ForceDefaults(true);
        const auto K_ARGS = builder.CreateVector(
            std::vector { builder.CreateString("retry"), builder.CreateString(K_MARKER.string()) });
        const auto K_BODY = control::Createstart_command(builder, K_ARGS);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder,
                                            2,
                                            0,
                                            builder.CreateString(K_SESSION),
                                            0,
                                            1,
                                            control::command_payload::start_command,
                                            K_BODY.Union()));
        const auto K_BYTES =
            std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        retry.assign(K_BYTES.begin(), K_BYTES.end());
        REQUIRE(retry != K_START);
    }
    const auto K_CANCEL = encode_worker_cancel(std::string(K_SESSION)).value();
    WorkerStreamDecoder decoder { std::string(K_SESSION) };
    decoder.expect_start();
    std::size_t start_acks {}, cancel_acks {}, sent {}, fragment_offset {};
    bool invalid_operation {};
    const auto K_EXECUTED = execute_process(
        {WE_TEST_PYTHON,
         "-u",
         K_SCRIPT.string(),
         "--wire-bindings",
         WE_TEST_WIRE_BINDINGS,
         "--wire-session",
         std::string(K_SESSION)},
        {},
        [&](const std::string& bytes) {
            REQUIRE(decoder.feed(
                std::as_bytes(std::span(bytes.data(), bytes.size())),
                [&](const WorkerEvent& event) {
                    if (const auto* ack = std::get_if<WorkerCommandAcknowledged>(&event)) {
                        start_acks += ack->m_kind == WorkerCommandKind::START ? 1 : 0;
                        cancel_acks += ack->m_kind == WorkerCommandKind::CANCEL ? 1 : 0;
                    }
                    if (const auto* failure = std::get_if<WorkerFailure>(&event))
                        invalid_operation = failure->m_code == "PROTOCOL_INVALID";
                }));
        },
        {},
        {[&](bool) -> Result<std::vector<std::byte>> {
             if (sent == 0) {
                 if (fragmented && fragment_offset == 0) {
                     fragment_offset = 3;
                     return std::vector<std::byte>(K_START.begin(), K_START.begin() + 3);
                 }
                 ++sent;
                 return std::vector<std::byte>(
                     K_START.begin() + static_cast<std::ptrdiff_t>(fragment_offset), K_START.end());
             }
             if (sent == 1 && start_acks == 1) {
                 ++sent;
                 return retry;
             }
             if (!changed && sent == 2 && start_acks == 2) {
                 ++sent;
                 decoder.expect_cancel();
                 return K_CANCEL;
             }
             if (!changed && sent == 3 && cancel_acks == 1) {
                 ++sent;
                 return K_CANCEL;
             }
             return std::vector<std::byte> {};
         },
         {}});
    REQUIRE(K_EXECUTED);
    CHECK(*K_EXECUTED == 130);
    CHECK(decoder.finish(*K_EXECUTED));
    CHECK(invalid_operation == changed);
    CHECK(start_acks == (changed ? 1 : 2));
    CHECK(cancel_acks == (changed ? 0 : 2));
    std::ifstream launches(K_MARKER);
    std::string line;
    REQUIRE(std::getline(launches, line));
    CHECK(line == "one invocation");
    CHECK_FALSE(std::getline(launches, line));
}

#ifndef _WIN32
TEST_CASE("worker launch reserves descriptors when the desktop host has no stdin") {
    TemporaryWorker worker;
    const auto K_SCRIPT = worker.m_directory / "capabilities.py";
    std::ofstream file(K_SCRIPT);
    file << "import sys\nsys.path.insert(0," << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump()
         << ")\nimport we_protocol as p\n"
            "p.configure(sys.argv,backend='synthetic_closed_stdin',backend_version='1',"
            "sensors=['diagnostic'],devices=['cpu'],checkpoint_semantics='none',checkpoint_rights='"
            "synthetic',"
            "limits={'max_frames':120,'max_source_bytes':1024,'max_output_bytes':1024})\n"
            "p.completed('capabilities')\n";
    file.close();

    struct StdinGuard {
        int m_original { dup(STDIN_FILENO) };

        StdinGuard() { close(STDIN_FILENO); }

        ~StdinGuard() {
            if (m_original >= 0) {
                dup2(m_original, STDIN_FILENO);
                close(m_original);
            }
        }
    } restore;

    const auto K_RESULT =
        execute_worker_protocol(WE_TEST_PYTHON, K_SCRIPT, {"capabilities"}, {}, {});
    INFO((K_RESULT ? "completed" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(*K_RESULT == 0);
}
#endif
