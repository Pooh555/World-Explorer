#include "commands_generated.h"
#include "events_generated.h"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

using namespace world_explorer;
namespace {
constexpr char SESSION[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
struct envelope_fields {
    std::uint16_t major{2}, minor{};
    std::uint64_t sequence{}, operation{};
    std::string session{SESSION};
};
struct handshake_fields {
    std::string backend{"synthetic_validation"}, version{"1"}, checkpoint{"none"},
        rights{"synthetic"};
    std::vector<std::string> sensors{"video"}, devices{"cpu"};
    std::uint32_t message_bytes{WORKER_MESSAGE_LIMIT}, frames{120};
    std::uint64_t source_bytes{16ULL * 1024 * 1024 * 1024},
        output_bytes{32ULL * 1024 * 1024 * 1024};
};
std::vector<std::byte> envelope(flatbuffers::FlatBufferBuilder& builder, wire::payload kind,
                                flatbuffers::Offset<void> body, const envelope_fields& fields) {
    builder.ForceDefaults(true);
    const auto session = builder.CreateString(fields.session);
    wire::FinishSizePrefixedenvelopeBuffer(
        builder, wire::Createenvelope(builder, fields.major, fields.minor, fields.sequence, kind,
                                      body, session, fields.operation));
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
std::vector<std::byte> handshake(const handshake_fields& fields = {},
                                 const envelope_fields& header = {}) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<flatbuffers::String>> sensors, devices;
    for (const auto& sensor : fields.sensors)
        sensors.push_back(builder.CreateString(sensor));
    for (const auto& device : fields.devices)
        devices.push_back(builder.CreateString(device));
    const auto body = wire::Createhandshake(
        builder, builder.CreateString(fields.backend), builder.CreateString(fields.version),
        builder.CreateVector(sensors), true, false, builder.CreateVector(devices),
        builder.CreateString(fields.checkpoint), builder.CreateString(fields.rights),
        fields.message_bytes, fields.frames, fields.source_bytes, fields.output_bytes);
    return envelope(builder, wire::payload::handshake, body.Union(), header);
}
struct acknowledgement_fields {
    wire::command_kind kind{wire::command_kind::START};
    std::uint64_t sequence{}, operation{1};
};
std::vector<std::byte> acknowledgement(const acknowledgement_fields& fields = {},
                                       const envelope_fields& header = {2, 0, 1, 1, SESSION}) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    const auto body =
        wire::Createcommand_acknowledged(builder, fields.sequence, fields.operation, fields.kind);
    return envelope(builder, wire::payload::command_acknowledged, body.Union(), header);
}
std::vector<std::byte> progress(double elapsed = 0, std::string_view stage = "synthetic",
                                std::string_view message = "diagnostic only",
                                const envelope_fields& header = {2, 0, 2, 1, SESSION}) {
    flatbuffers::FlatBufferBuilder builder;
    const auto body = wire::Createprogress(builder, builder.CreateString(stage),
                                           builder.CreateString(message), elapsed);
    return envelope(builder, wire::payload::progress, body.Union(), header);
}
std::string path_text(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
struct artifact_fields {
    std::string path{path_text(std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                                         "synthetic-manifest.json")
                                   .lexically_normal())};
    std::string digest{std::string(64, 'a')};
    std::uint64_t bytes{2};
};
std::vector<std::byte> completed(std::string_view stage = "capabilities",
                                 const std::optional<artifact_fields>& artifact = {},
                                 const envelope_fields& header = {2, 0, 2, 1, SESSION}) {
    flatbuffers::FlatBufferBuilder builder;
    flatbuffers::Offset<wire::artifact_reference> reference;
    if (artifact)
        reference =
            wire::Createartifact_reference(builder, builder.CreateString(artifact->path),
                                           builder.CreateString(artifact->digest), artifact->bytes);
    const auto body = wire::Createcompleted(builder, builder.CreateString(stage), reference);
    return envelope(builder, wire::payload::completed, body.Union(), header);
}
std::vector<std::byte> failure(std::string_view code = "SYNTHETIC_FAILURE",
                               std::string_view stage = "diagnostic",
                               std::string_view message = "injected failure",
                               std::string_view recovery = "retry diagnostic",
                               const envelope_fields& header = {2, 0, 1, 1, SESSION}) {
    flatbuffers::FlatBufferBuilder builder;
    const auto body =
        wire::Createfailure(builder, builder.CreateString(code), builder.CreateString(stage),
                            builder.CreateString(message), builder.CreateString(recovery));
    return envelope(builder, wire::payload::failure, body.Union(), header);
}
void start(worker_stream_decoder& decoder) {
    REQUIRE(decoder.feed(handshake(), {}));
    decoder.expect_start();
    REQUIRE(decoder.feed(acknowledgement(), {}));
}
void reject(worker_stream_decoder& decoder, const std::vector<std::byte>& frame,
            error_code expected = error_code::INVALID_DATA) {
    std::size_t callbacks{};
    const auto result = decoder.feed(frame, [&](const worker_event&) { ++callbacks; });
    REQUIRE(!result);
    CHECK(result.error().code == expected);
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
    const auto offset = reinterpret_cast<const std::byte*>(address) - frame.data();
    REQUIRE(offset >= 0);
    REQUIRE(static_cast<std::size_t>(offset) < frame.size());
    frame[static_cast<std::size_t>(offset)] = std::byte{0xff};
}
} // namespace

TEST_CASE("worker envelope metadata rejection is permanent and publishes no event",
          "[worker_validation]") {
    envelope_fields header;
    std::string local_session = SESSION;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(field);
    switch (field) {
    case 0:
        header.major = 3;
        break;
    case 1:
        header.minor = 1;
        break;
    case 2:
        local_session.pop_back();
        break;
    case 3:
        local_session[0] = 'g';
        break;
    case 4:
        header.session[0] = 'b';
        break;
    case 5:
        header.session.clear();
        break;
    case 6:
        header.operation = 1;
        break;
    case 7:
        header.sequence = 1;
        break;
    default:
        FAIL("unknown envelope mutation");
    }
    worker_stream_decoder decoder(local_session);
    reject(decoder, handshake({}, header));
}

TEST_CASE("worker handshakes enforce all resource and capability bounds", "[worker_validation]") {
    handshake_fields fields;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(field);
    switch (field) {
    case 0:
        fields.message_bytes = WORKER_MESSAGE_LIMIT - 1;
        break;
    case 1:
        fields.frames = 0;
        break;
    case 2:
        fields.frames = 121;
        break;
    case 3:
        fields.source_bytes = 0;
        break;
    case 4:
        ++fields.source_bytes;
        break;
    case 5:
        fields.output_bytes = 0;
        break;
    case 6:
        ++fields.output_bytes;
        break;
    case 7:
        fields.sensors.clear();
        break;
    case 8:
        fields.sensors.resize(17, "video");
        break;
    case 9:
        fields.devices.clear();
        break;
    case 10:
        fields.devices.resize(17, "cpu");
        break;
    case 11:
        fields.sensors = {""};
        break;
    case 12:
        fields.devices = {std::string(129, 'x')};
        break;
    case 13:
        fields.version.clear();
        break;
    case 14:
        fields.checkpoint = std::string(4097, 'x');
        break;
    case 15:
        fields.rights.clear();
        break;
    default:
        FAIL("unknown handshake mutation");
    }
    worker_stream_decoder decoder(SESSION);
    reject(decoder, handshake(fields));
}

TEST_CASE("worker UTF-8 rejects malformed encodings and preserves valid Unicode",
          "[worker_validation]") {
    const auto malformed =
        GENERATE(std::string("a\0b", 3), std::string("\x80", 1), std::string("\xc0\x80", 2),
                 std::string("\xc2", 1), std::string("\xe0\xa0", 2), std::string("\xf0\x90\x80", 3),
                 std::string("\xc2\x41", 2), std::string("\xe0\x80\x80", 3),
                 std::string("\xf0\x80\x80\x80", 4), std::string("\xed\xa0\x80", 3),
                 std::string("\xf4\x90\x80\x80", 4), std::string("\xf5\x80\x80\x80", 4),
                 std::string("\xff", 1));
    worker_stream_decoder invalid(SESSION);
    handshake_fields fields;
    fields.backend = malformed;
    reject(invalid, handshake(fields));
    CHECK(!encode_worker_start(SESSION, {malformed}));

    fields.backend = "synthetic \xc2\xa9 \xe4\xbd\xa0 \xf0\x9f\x8c\x8d";
    fields.devices = std::vector<std::string>(16, "cpu");
    fields.sensors = std::vector<std::string>(16, "video");
    worker_stream_decoder valid(SESSION);
    std::optional<worker_handshake> observed;
    REQUIRE(valid.feed(handshake(fields), [&](const worker_event& event) {
        observed = std::get<worker_handshake>(event);
    }));
    if (observed) {
        CHECK(observed->backend == fields.backend);
        CHECK(observed->device_options.size() == 16);
    } else
        FAIL("expected a verified handshake");
    const auto encoded = encode_worker_start(SESSION, {fields.backend});
    REQUIRE(encoded);
    const auto* command = control::GetSizePrefixedcommand_envelope(encoded->data());
    CHECK(command->body_as_start_command()->arguments()->Get(0)->str() == fields.backend);
}

TEST_CASE("worker acknowledgements bind kind sequence operation and expected lifecycle",
          "[worker_validation]") {
    worker_stream_decoder decoder(SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    decoder.expect_start();
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    acknowledgement_fields fields;
    envelope_fields header{2, 0, 1, 1, SESSION};
    switch (field) {
    case 0:
        fields.sequence = 1;
        break;
    case 1:
        fields.operation = 2;
        break;
    case 2:
        break;
    case 3:
        fields = {wire::command_kind::CANCEL, 1, 2};
        decoder.expect_cancel();
        break;
    case 4:
        REQUIRE(decoder.feed(acknowledgement(), {}));
        header.sequence = 2;
        fields = {wire::command_kind::CANCEL, 0, 2};
        decoder.expect_cancel();
        break;
    case 5:
        REQUIRE(decoder.feed(acknowledgement(), {}));
        header.sequence = 2;
        fields = {wire::command_kind::CANCEL, 1, 1};
        decoder.expect_cancel();
        break;
    default:
        FAIL("unknown acknowledgement mutation");
    }
    auto frame = acknowledgement(fields, header);
    if (field == 2)
        poison_tag(frame,
                   wire::GetSizePrefixedenvelope(frame.data())->body_as_command_acknowledged(),
                   wire::command_acknowledged::VT_KIND);
    reject(decoder, frame);
}

TEST_CASE("worker progress rejects pre-START events nonfinite durations and excessive text",
          "[worker_validation]") {
    worker_stream_decoder decoder(SESSION);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    if (field == 0) {
        REQUIRE(decoder.feed(handshake(), {}));
        reject(decoder, progress(0, "synthetic", "early", {2, 0, 1, 1, SESSION}));
        return;
    }
    start(decoder);
    auto elapsed = 0.0;
    std::string stage = "synthetic", message = "diagnostic";
    switch (field) {
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
    worker_stream_decoder decoder(SESSION);
    start(decoder);
    artifact_fields fields;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    switch (field) {
    case 0:
        fields.digest = std::string(63, 'a');
        break;
    case 1:
        fields.digest[0] = 'g';
        break;
    case 2:
        fields.path = "relative-manifest.json";
        break;
    case 3:
        fields.path =
            path_text(std::filesystem::temp_directory_path() / "a" / ".." / "manifest.json");
        break;
    case 4:
        fields.bytes = 0;
        break;
    case 5:
        fields.bytes = WORKER_MESSAGE_LIMIT + 1;
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
    worker_stream_decoder decoder(SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    const auto field = GENERATE(0, 1, 2, 3, 4);
    std::string code = "SYNTHETIC_FAILURE", stage = "diagnostic", message = "failure",
                recovery = "retry";
    switch (field) {
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
        std::optional<worker_failure> observed;
        REQUIRE(decoder.feed(failure(), [&](const worker_event& event) {
            observed = std::get<worker_failure>(event);
        }));
        if (observed)
            CHECK(observed->code == "SYNTHETIC_FAILURE");
        else
            FAIL("expected the pre-START failure");
        CHECK(decoder.finish(2));
        CHECK(!decoder.finish(0));
        reject(decoder, failure(code, stage, message, recovery, {2, 0, 2, 1, SESSION}));
        return;
    }
    default:
        FAIL("unknown failure mutation");
    }
    reject(decoder, failure(code, stage, message, recovery));
}

TEST_CASE("worker verification rejects absent and unknown payloads without callbacks",
          "[worker_validation]") {
    const auto field = GENERATE(std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3},
                                std::size_t{4}, std::size_t{5}, std::size_t{6});
    constexpr std::array KINDS{wire::payload::handshake,
                               wire::payload::progress,
                               wire::payload::completed,
                               wire::payload::failure,
                               wire::payload::command_acknowledged,
                               wire::payload::NONE,
                               wire::payload::NONE};
    const auto kind = KINDS[field];
    worker_stream_decoder decoder(SESSION);
    envelope_fields header;
    if (kind != wire::payload::handshake) {
        start(decoder);
        header.sequence = 2;
        header.operation = 1;
    }
    flatbuffers::FlatBufferBuilder builder;
    auto frame = envelope(builder, kind, {}, header);
    if (field == 6)
        poison_tag(frame, wire::GetSizePrefixedenvelope(frame.data()),
                   wire::envelope::VT_BODY_TYPE);
    reject(decoder, frame);
}

TEST_CASE("worker duplicate handshake and premature progress fail at the current sequence",
          "[worker_validation]") {
    worker_stream_decoder decoder(SESSION);
    REQUIRE(decoder.feed(handshake(), {}));
    SECTION("second handshake") { reject(decoder, handshake({}, {2, 0, 1, 1, SESSION})); }
    SECTION("progress is not a handshake") {
        worker_stream_decoder empty(SESSION);
        reject(empty, progress(0, "synthetic", "early", {}));
    }
}

TEST_CASE("worker framing rejects short declarations and diagnoses incomplete EOF",
          "[worker_validation]") {
    const auto length = GENERATE(std::uint32_t{0}, std::uint32_t{11},
                                 static_cast<std::uint32_t>(WORKER_MESSAGE_LIMIT + 1));
    std::vector<std::byte> prefix(4);
    flatbuffers::WriteScalar(prefix.data(), length);
    worker_stream_decoder decoder(SESSION);
    reject(decoder, prefix, error_code::RESOURCE_EXHAUSTED);
    worker_stream_decoder empty(SESSION);
    CHECK(!empty.finish(0));
    CHECK(empty.feed({}, {}));
    flatbuffers::WriteScalar(prefix.data(), std::uint32_t{12});
    REQUIRE(empty.feed(std::span(prefix).first(2), {}));
    CHECK(!empty.finish(0));
}

TEST_CASE("worker coalesced frames preserve ordering and exact progress text bounds",
          "[worker_validation]") {
    worker_stream_decoder decoder(SESSION);
    decoder.expect_start();
    auto stream = handshake();
    for (const auto& frame :
         {acknowledgement(), progress(1.5, std::string(128, 'x'), std::string(65536, 'm')),
          completed("capabilities", {}, {2, 0, 3, 1, SESSION})})
        stream.insert(stream.end(), frame.begin(), frame.end());
    std::vector<worker_event> events;
    REQUIRE(decoder.feed(stream, [&](const worker_event& event) { events.push_back(event); }));
    REQUIRE(events.size() == 4);
    CHECK(std::holds_alternative<worker_handshake>(events[0]));
    CHECK(std::holds_alternative<worker_command_acknowledged>(events[1]));
    CHECK(std::get<worker_progress>(events[2]).elapsed_seconds == 1.5);
    CHECK(std::get<worker_progress>(events[2]).message.size() == 65536);
    CHECK(std::holds_alternative<worker_completed>(events[3]));
    REQUIRE(decoder.finish(0));
}

TEST_CASE("worker event cap admits the last valid terminal and rejects one more event",
          "[worker_validation]") {
    const bool terminal_at_limit = GENERATE(true, false);
    worker_stream_decoder decoder(SESSION);
    start(decoder);
    for (std::uint64_t sequence = 2; sequence < 9999; ++sequence)
        REQUIRE(decoder.feed(
            progress(0, "synthetic", "bounded replay", {2, 0, sequence, 1, SESSION}), {}));
    if (terminal_at_limit) {
        REQUIRE(decoder.feed(completed("capabilities", {}, {2, 0, 9999, 1, SESSION}), {}));
        CHECK(decoder.finish(0));
    } else {
        REQUIRE(
            decoder.feed(progress(0, "synthetic", "bounded replay", {2, 0, 9999, 1, SESSION}), {}));
        reject(decoder, completed("capabilities", {}, {2, 0, 10000, 1, SESSION}));
    }
}

TEST_CASE("worker command encoding accepts exact budgets and rejects empty or invalid sessions",
          "[worker_validation]") {
    const auto arguments = std::vector<std::string>(64, std::string(512, 'x'));
    const auto encoded = encode_worker_start(SESSION, arguments);
    REQUIRE(encoded);
    const auto* command = control::GetSizePrefixedcommand_envelope(encoded->data());
    CHECK(command->body_as_start_command()->arguments()->size() == 64);
    CHECK(!encode_worker_start(SESSION, {""}));
    CHECK(!encode_worker_cancel(""));
    CHECK(!encode_worker_cancel(std::string(64, 'z')));
    CHECK(!encode_worker_start(std::string(64, 'z'), arguments));
}

TEST_CASE("worker completion preserves Unicode reference metadata at the wire byte limit",
          "[worker_validation]") {
    worker_stream_decoder decoder(SESSION);
    start(decoder);
    artifact_fields fields;
    const auto path =
        std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                  std::filesystem::path(u8"synthetic \u00e9\u623f.json"))
            .lexically_normal();
    fields.path = path_text(path);
    fields.bytes = WORKER_MESSAGE_LIMIT;
    bool received{};
    REQUIRE(decoder.feed(completed("synthetic", fields), [&](const worker_event& event) {
        const auto& result = std::get<worker_completed>(event);
        if (result.artifact) {
            CHECK(result.artifact->path == path);
            CHECK(result.artifact->sha256 == fields.digest);
            CHECK(result.artifact->bytes == WORKER_MESSAGE_LIMIT);
            received = true;
        } else
            FAIL("expected a manifest reference");
    }));
    CHECK(received);
    CHECK(decoder.finish(0));
}
