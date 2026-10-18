#include "commands_generated.h"
#include "events_generated.h"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <array>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

constexpr char K_SESSION[] {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};

struct EnvelopeFields {
    std::uint16_t m_major { 2 }, m_minor {};
    std::uint64_t m_sequence {}, m_operation {};
    std::string m_session { K_SESSION };
};

struct HandshakeFields {
    std::string m_backend { "synthetic_validation" }, m_version { "1" }, m_checkpoint { "none" },
        m_rights { "synthetic" };
    std::vector<std::string> m_sensors { "video" }, m_devices { "cpu" };
    std::uint32_t m_message_bytes { K_WORKER_MESSAGE_LIMIT }, m_frames { 120 };
    std::uint64_t m_source_bytes { 16ULL * 1024 * 1024 * 1024 },
        m_output_bytes { 32ULL * 1024 * 1024 * 1024 };
};

std::vector<std::byte> envelope(flatbuffers::FlatBufferBuilder& builder,
                                wire::payload kind,
                                flatbuffers::Offset<void> body,
                                const EnvelopeFields& fields) {
    builder.ForceDefaults(true);
    const auto K_ENVELOPE_SESSION = builder.CreateString(fields.m_session);
    wire::FinishSizePrefixedenvelopeBuffer(builder,
                                           wire::Createenvelope(builder,
                                                                fields.m_major,
                                                                fields.m_minor,
                                                                fields.m_sequence,
                                                                kind,
                                                                body,
                                                                K_ENVELOPE_SESSION,
                                                                fields.m_operation));
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return { K_BYTES.begin(), K_BYTES.end() };
}

std::vector<std::byte> handshake(const HandshakeFields& fields = {},
                                 const EnvelopeFields& header = {}) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<flatbuffers::String>> sensors, devices;
    for (const auto& sensor : fields.m_sensors) sensors.push_back(builder.CreateString(sensor));
    for (const auto& device : fields.m_devices) devices.push_back(builder.CreateString(device));
    const auto K_BODY = wire::Createhandshake(builder,
                                              builder.CreateString(fields.m_backend),
                                              builder.CreateString(fields.m_version),
                                              builder.CreateVector(sensors),
                                              true,
                                              false,
                                              builder.CreateVector(devices),
                                              builder.CreateString(fields.m_checkpoint),
                                              builder.CreateString(fields.m_rights),
                                              fields.m_message_bytes,
                                              fields.m_frames,
                                              fields.m_source_bytes,
                                              fields.m_output_bytes);
    return envelope(builder, wire::payload::handshake, K_BODY.Union(), header);
}

struct AcknowledgementFields {
    wire::command_kind m_kind { wire::command_kind::START };
    std::uint64_t m_sequence {}, m_operation { 1 };
};

std::vector<std::byte> acknowledgement(const AcknowledgementFields& fields = {},
                                       const EnvelopeFields& header = { 2, 0, 1, 1, K_SESSION }) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    const auto K_BODY = wire::Createcommand_acknowledged(
        builder, fields.m_sequence, fields.m_operation, fields.m_kind);
    return envelope(builder, wire::payload::command_acknowledged, K_BODY.Union(), header);
}

std::vector<std::byte> progress(double elapsed = 0,
                                std::string_view stage = "synthetic",
                                std::string_view message = "diagnostic only",
                                const EnvelopeFields& header = { 2, 0, 2, 1, K_SESSION }) {
    flatbuffers::FlatBufferBuilder builder;
    const auto K_BODY = wire::Createprogress(
        builder, builder.CreateString(stage), builder.CreateString(message), elapsed);
    return envelope(builder, wire::payload::progress, K_BODY.Union(), header);
}

std::string path_text(const std::filesystem::path& path) {
    const auto K_BYTES = path.generic_u8string();
    return { K_BYTES.begin(), K_BYTES.end() };
}

struct ArtifactFields {
    std::string m_path {path_text(std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                                            "synthetic-manifest.json")
                                      .lexically_normal())};
    std::string m_digest { std::string(64, 'a') };
    std::uint64_t m_bytes { 2 };
};

std::vector<std::byte> completed(std::string_view stage = "capabilities",
                                 const std::optional<ArtifactFields>& artifact = {},
                                 const EnvelopeFields& header = { 2, 0, 2, 1, K_SESSION }) {
    flatbuffers::FlatBufferBuilder builder;
    flatbuffers::Offset<wire::artifact_reference> reference;
    if (artifact)
        reference = wire::Createartifact_reference(builder,
                                                   builder.CreateString(artifact->m_path),
                                                   builder.CreateString(artifact->m_digest),
                                                   artifact->m_bytes);
    const auto K_BODY = wire::Createcompleted(builder, builder.CreateString(stage), reference);
    return envelope(builder, wire::payload::completed, K_BODY.Union(), header);
}

std::vector<std::byte> failure(std::string_view code = "SYNTHETIC_FAILURE",
                               std::string_view stage = "diagnostic",
                               std::string_view message = "injected failure",
                               std::string_view recovery = "retry diagnostic",
                               const EnvelopeFields& header = { 2, 0, 1, 1, K_SESSION }) {
    flatbuffers::FlatBufferBuilder builder;
    const auto K_BODY = wire::Createfailure(builder,
                                            builder.CreateString(code),
                                            builder.CreateString(stage),
                                            builder.CreateString(message),
                                            builder.CreateString(recovery));
    return envelope(builder, wire::payload::failure, K_BODY.Union(), header);
}

void start(WorkerStreamDecoder& decoder) {
    REQUIRE(decoder.feed(handshake(), {}));
    decoder.expect_start();
    REQUIRE(decoder.feed(acknowledgement(), {}));
}

void reject(WorkerStreamDecoder& decoder,
            const std::vector<std::byte>& frame,
            ErrorCode expected = ErrorCode::INVALID_DATA) {
    std::size_t callbacks {};
    const auto K_RESULT = decoder.feed(frame, [&](const WorkerEvent&) { ++callbacks; });
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == expected);
    CHECK(callbacks == 0);
    CHECK(!decoder.finish(0));
    CHECK(!decoder.finish(2));
    CHECK(!decoder.feed(handshake(), {}));
    CHECK(!decoder.feed({}, {}));
}

// Model a foreign enum tag in serialized bytes, without constructing an invalid C++ enum.
void poison_tag(std::vector<std::byte>& frame, const void* table, flatbuffers::voffset_t field) {
    const auto* address = reinterpret_cast<const flatbuffers::Table*>(table)->GetAddressOf(field);
    REQUIRE(address);
    const auto K_OFFSET = reinterpret_cast<const std::byte*>(address) - frame.data();
    REQUIRE(K_OFFSET >= 0);
    REQUIRE(static_cast<std::size_t>(K_OFFSET) < frame.size());
    frame[static_cast<std::size_t>(K_OFFSET)] = std::byte { 0xff };
}

}  // namespace

TEST_CASE("worker envelope metadata rejection is permanent and publishes no event",
          "[worker_validation]") {
    EnvelopeFields header;
    std::string local_session = K_SESSION;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        header.m_major = 3;
        break;
    case 1:
        header.m_minor = 1;
        break;
    case 2:
        local_session.pop_back();
        break;
    case 3:
        local_session[0] = 'g';
        break;
    case 4:
        header.m_session[0] = 'b';
        break;
    case 5:
        header.m_session.clear();
        break;
    case 6:
        header.m_operation = 1;
        break;
    case 7:
        header.m_sequence = 1;
        break;
    default:
        FAIL("unknown envelope mutation");
    }
    WorkerStreamDecoder decoder(local_session);
    reject(decoder, handshake({}, header));
}

TEST_CASE("worker handshakes enforce all resource and capability bounds", "[worker_validation]") {
    HandshakeFields fields;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        fields.m_message_bytes = K_WORKER_MESSAGE_LIMIT - 1;
        break;
    case 1:
        fields.m_frames = 0;
        break;
    case 2:
        fields.m_frames = 121;
        break;
    case 3:
        fields.m_source_bytes = 0;
        break;
    case 4:
        ++fields.m_source_bytes;
        break;
    case 5:
        fields.m_output_bytes = 0;
        break;
    case 6:
        ++fields.m_output_bytes;
        break;
    case 7:
        fields.m_sensors.clear();
        break;
    case 8:
        fields.m_sensors.resize(17, "video");
        break;
    case 9:
        fields.m_devices.clear();
        break;
    case 10:
        fields.m_devices.resize(17, "cpu");
        break;
    case 11:
        fields.m_sensors = { "" };
        break;
    case 12:
        fields.m_devices = { std::string(129, 'x') };
        break;
    case 13:
        fields.m_version.clear();
        break;
    case 14:
        fields.m_checkpoint = std::string(4097, 'x');
        break;
    case 15:
        fields.m_rights.clear();
        break;
    default:
        FAIL("unknown handshake mutation");
    }
    WorkerStreamDecoder decoder(K_SESSION);
    reject(decoder, handshake(fields));
}

TEST_CASE("worker UTF-8 rejects malformed encodings and preserves valid Unicode",
          "[worker_validation]") {
    const auto K_MALFORMED = GENERATE(std::string("a\0b", 3),
                                      std::string("\x80", 1),
                                      std::string("\xc0\x80", 2),
                                      std::string("\xc2", 1),
                                      std::string("\xe0\xa0", 2),
                                      std::string("\xf0\x90\x80", 3),
                                      std::string("\xc2\x41", 2),
                                      std::string("\xe0\x80\x80", 3),
                                      std::string("\xf0\x80\x80\x80", 4),
                                      std::string("\xed\xa0\x80", 3),
                                      std::string("\xf4\x90\x80\x80", 4),
                                      std::string("\xf5\x80\x80\x80", 4),
                                      std::string("\xff", 1));
    WorkerStreamDecoder invalid(K_SESSION);
    HandshakeFields fields;
    fields.m_backend = K_MALFORMED;
    reject(invalid, handshake(fields));
    CHECK(!encode_worker_start(K_SESSION, {K_MALFORMED}));

    fields.m_backend = "synthetic \xc2\xa9 \xe4\xbd\xa0 \xf0\x9f\x8c\x8d";
    fields.m_devices = std::vector<std::string>(16, "cpu");
    fields.m_sensors = std::vector<std::string>(16, "video");
    WorkerStreamDecoder valid(K_SESSION);
    std::optional<WorkerHandshake> observed;
    REQUIRE(valid.feed(handshake(fields), [&](const WorkerEvent& event) {
        observed = std::get<WorkerHandshake>(event);
    }));
    if (observed) {
        CHECK(observed->m_backend == fields.m_backend);
        CHECK(observed->m_device_options.size() == 16);
    } else FAIL("expected a verified handshake");
    const auto K_ENCODED = encode_worker_start(K_SESSION, {fields.m_backend});
    REQUIRE(K_ENCODED);
    const auto* command = control::GetSizePrefixedcommand_envelope(K_ENCODED->data());
    CHECK(command->body_as_start_command()->arguments()->Get(0)->str() == fields.m_backend);
}

TEST_CASE("worker acknowledgements bind kind sequence operation and expected lifecycle",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    decoder.expect_start();
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    AcknowledgementFields fields;
    EnvelopeFields header { 2, 0, 1, 1, K_SESSION };
    switch (K_FIELD) {
    case 0:
        fields.m_sequence = 1;
        break;
    case 1:
        fields.m_operation = 2;
        break;
    case 2:
        break;
    case 3:
        fields = { wire::command_kind::CANCEL, 1, 2 };
        decoder.expect_cancel();
        break;
    case 4:
        REQUIRE(decoder.feed(acknowledgement(), {}));
        header.m_sequence = 2;
        fields = { wire::command_kind::CANCEL, 0, 2 };
        decoder.expect_cancel();
        break;
    case 5:
        REQUIRE(decoder.feed(acknowledgement(), {}));
        header.m_sequence = 2;
        fields = { wire::command_kind::CANCEL, 1, 1 };
        decoder.expect_cancel();
        break;
    default:
        FAIL("unknown acknowledgement mutation");
    }
    auto frame = acknowledgement(fields, header);
    if (K_FIELD == 2)
        poison_tag(frame,
                   wire::GetSizePrefixedenvelope(frame.data())->body_as_command_acknowledged(),
                   wire::command_acknowledged::VT_KIND);
    reject(decoder, frame);
}

TEST_CASE("worker progress rejects pre-START events nonfinite durations and excessive text",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    if (K_FIELD == 0) {
        REQUIRE(decoder.feed(handshake(), {}));
        reject(decoder, progress(0, "synthetic", "early", {2, 0, 1, 1, K_SESSION}));
        return;
    }
    start(decoder);
    auto elapsed = 0.0;
    std::string stage = "synthetic", message = "diagnostic";
    switch (K_FIELD) {
    case 1:
        elapsed = std::numeric_limits<double>::infinity();
        break;
    case 2:
        elapsed = std::numeric_limits<double>::quiet_NaN();
        break;
    case 3:
        elapsed = -1;
        break;
    case 4:
        stage.clear();
        break;
    case 5:
        stage = std::string(129, 'x');
        break;
    case 6:
        message = std::string(65537, 'x');
        break;
    default:
        FAIL("unknown progress mutation");
    }
    reject(decoder, progress(elapsed, stage, message));
}

TEST_CASE("worker completion references reject invalid paths hashes sizes and missing manifests",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    start(decoder);
    ArtifactFields fields;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        fields.m_digest = std::string(63, 'a');
        break;
    case 1:
        fields.m_digest[0] = 'g';
        break;
    case 2:
        fields.m_path = "relative-manifest.json";
        break;
    case 3:
        fields.m_path =
            path_text(std::filesystem::temp_directory_path() / "a" / ".." / "manifest.json");
        break;
    case 4:
        fields.m_bytes = 0;
        break;
    case 5:
        fields.m_bytes = K_WORKER_MESSAGE_LIMIT + 1;
        break;
    case 6:
        reject(decoder, completed("appearance"));
        return;
    default:
        FAIL("unknown completion mutation");
    }
    reject(decoder, completed("appearance", fields));
}

TEST_CASE("worker failure may precede START but must have valid typed fields and exit",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    std::string code = "SYNTHETIC_FAILURE", stage = "diagnostic", message = "failure",
                recovery = "retry";
    switch (K_FIELD) {
    case 0:
        code.clear();
        break;
    case 1:
        stage = std::string(129, 'x');
        break;
    case 2:
        message = std::string(65537, 'x');
        break;
    case 3:
        recovery.clear();
        break;
    case 4: {
        std::optional<WorkerFailure> observed;
        REQUIRE(decoder.feed(failure(), [&](const WorkerEvent& event) {
            observed = std::get<WorkerFailure>(event);
        }));
        if (observed) CHECK(observed->m_code == "SYNTHETIC_FAILURE");
        else FAIL("expected the pre-START failure");
        CHECK(decoder.finish(2));
        CHECK(!decoder.finish(0));
        reject(decoder, failure(code, stage, message, recovery, {2, 0, 2, 1, K_SESSION}));
        return;
    }
    default:
        FAIL("unknown failure mutation");
    }
    reject(decoder, failure(code, stage, message, recovery));
}

TEST_CASE("worker verification rejects absent and unknown payloads without callbacks",
          "[worker_validation]") {
    const auto K_FIELD = GENERATE(0uz, 1uz, 2uz, 3uz, 4uz, 5uz, 6uz);
    constexpr std::array K_KINDS {wire::payload::handshake,
                                  wire::payload::progress,
                                  wire::payload::completed,
                                  wire::payload::failure,
                                  wire::payload::command_acknowledged,
                                  wire::payload::NONE,
                                  wire::payload::NONE};
    const auto K_KIND = K_KINDS[K_FIELD];
    WorkerStreamDecoder decoder(K_SESSION);
    EnvelopeFields header;
    if (K_KIND != wire::payload::handshake) {
        start(decoder);
        header.m_sequence = 2;
        header.m_operation = 1;
    }
    flatbuffers::FlatBufferBuilder builder;
    auto frame = envelope(builder, K_KIND, {}, header);
    if (K_FIELD == 6)
        poison_tag(
            frame, wire::GetSizePrefixedenvelope(frame.data()), wire::envelope::VT_BODY_TYPE);
    reject(decoder, frame);
}

TEST_CASE("worker duplicate handshake and premature progress fail at the current sequence",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    SECTION("second handshake") {
        reject(decoder, handshake({}, {2, 0, 1, 1, K_SESSION}));
    }
    SECTION("progress is not a handshake") {
        WorkerStreamDecoder empty(K_SESSION);
        reject(empty, progress(0, "synthetic", "early", {}));
    }
}

TEST_CASE("worker framing rejects short declarations and diagnoses incomplete EOF",
          "[worker_validation]") {
    const auto K_LENGTH = GENERATE(std::uint32_t { 0 },
                                   std::uint32_t { 11 },
                                   static_cast<std::uint32_t>(K_WORKER_MESSAGE_LIMIT + 1));
    std::vector<std::byte> prefix(4);
    flatbuffers::WriteScalar(prefix.data(), K_LENGTH);
    WorkerStreamDecoder decoder(K_SESSION);
    reject(decoder, prefix, ErrorCode::RESOURCE_EXHAUSTED);
    WorkerStreamDecoder empty(K_SESSION);
    CHECK(!empty.finish(0));
    CHECK(empty.feed({}, {}));
    flatbuffers::WriteScalar(prefix.data(), std::uint32_t { 12 });
    REQUIRE(empty.feed(std::span(prefix).first(2), {}));
    CHECK(!empty.finish(0));
}

TEST_CASE("worker coalesced frames preserve ordering and exact progress text bounds",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    decoder.expect_start();
    auto stream = handshake();
    for (const auto& frame : {acknowledgement(),
                              progress(1.5, std::string(128, 'x'), std::string(65536, 'm')),
                              completed("capabilities", {}, {2, 0, 3, 1, K_SESSION})})
        stream.insert(stream.end(), frame.begin(), frame.end());
    std::vector<WorkerEvent> events;
    REQUIRE(decoder.feed(stream, [&](const WorkerEvent& event) { events.push_back(event); }));
    REQUIRE(events.size() == 4);
    CHECK(std::holds_alternative<WorkerHandshake>(events[0]));
    CHECK(std::holds_alternative<WorkerCommandAcknowledged>(events[1]));
    CHECK(std::get<WorkerProgress>(events[2]).m_elapsed_seconds == 1.5);
    CHECK(std::get<WorkerProgress>(events[2]).m_message.size() == 65536);
    CHECK(std::holds_alternative<WorkerCompleted>(events[3]));
    REQUIRE(decoder.finish(0));
}

TEST_CASE("worker event cap admits the last valid terminal and rejects one more event",
          "[worker_validation]") {
    const bool K_TERMINAL_AT_LIMIT = GENERATE(true, false);
    WorkerStreamDecoder decoder(K_SESSION);
    start(decoder);
    for (std::uint64_t sequence { 2 }; sequence < 9999; ++sequence)
        REQUIRE(decoder.feed(
            progress(0, "synthetic", "bounded replay", {2, 0, sequence, 1, K_SESSION}), {}));
    if (K_TERMINAL_AT_LIMIT) {
        REQUIRE(decoder.feed(completed("capabilities", {}, {2, 0, 9999, 1, K_SESSION}), {}));
        CHECK(decoder.finish(0));
    } else {
        REQUIRE(decoder.feed(progress(0, "synthetic", "bounded replay", {2, 0, 9999, 1, K_SESSION}),
                             {}));
        reject(decoder, completed("capabilities", {}, {2, 0, 10000, 1, K_SESSION}));
    }
}

TEST_CASE("worker command encoding accepts exact budgets and rejects empty or invalid sessions",
          "[worker_validation]") {
    const auto K_ARGUMENTS = std::vector<std::string>(64, std::string(512, 'x'));
    const auto K_ENCODED = encode_worker_start(K_SESSION, K_ARGUMENTS);
    REQUIRE(K_ENCODED);
    const auto* command = control::GetSizePrefixedcommand_envelope(K_ENCODED->data());
    CHECK(command->body_as_start_command()->arguments()->size() == 64);
    CHECK(!encode_worker_start(K_SESSION, {""}));
    CHECK(!encode_worker_cancel(""));
    CHECK(!encode_worker_cancel(std::string(64, 'z')));
    CHECK(!encode_worker_start(std::string(64, 'z'), K_ARGUMENTS));
}

TEST_CASE("worker completion preserves Unicode reference metadata at the wire byte limit",
          "[worker_validation]") {
    WorkerStreamDecoder decoder(K_SESSION);
    start(decoder);
    ArtifactFields fields;
    const auto K_PATH =
        std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                  std::filesystem::path(u8"synthetic \u00e9\u623f.json"))
            .lexically_normal();
    fields.m_path = path_text(K_PATH);
    fields.m_bytes = K_WORKER_MESSAGE_LIMIT;
    bool received {};
    REQUIRE(decoder.feed(completed("synthetic", fields), [&](const WorkerEvent& event) {
        const auto& result = std::get<WorkerCompleted>(event);
        if (result.m_artifact) {
            CHECK(result.m_artifact->m_path == K_PATH);
            CHECK(result.m_artifact->m_sha256 == fields.m_digest);
            CHECK(result.m_artifact->m_bytes == K_WORKER_MESSAGE_LIMIT);
            received = true;
        } else FAIL("expected a manifest reference");
    }));
    CHECK(received);
    CHECK(decoder.finish(0));
}
