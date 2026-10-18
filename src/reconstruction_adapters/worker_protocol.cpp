#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include "commands_generated.h"
#include "events_generated.h"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace world_explorer {
namespace {

bool utf8(std::string_view text) {
    for (std::size_t offset { 0uz }; offset < text.size();) {
        const auto K_HEAD = static_cast<unsigned char>(text[offset++]);
        if (K_HEAD == 0) return false;
        if (K_HEAD < 128) continue;
        const std::size_t K_COUNT = K_HEAD >= 0xc2 && K_HEAD <= 0xdf   ? 1
                                    : K_HEAD >= 0xe0 && K_HEAD <= 0xef ? 2
                                    : K_HEAD >= 0xf0 && K_HEAD <= 0xf4 ? 3
                                                                       : 0;
        if (!K_COUNT || K_COUNT > text.size() - offset) return false;
        auto codepoint = static_cast<std::uint32_t>(K_HEAD & (0x7fU >> K_COUNT));
        for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
            const auto K_BYTE = static_cast<unsigned char>(text[offset++]);
            if ((K_BYTE & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (K_BYTE & 0x3fU);
        }
        if (codepoint < (K_COUNT == 1   ? 0x80U
                         : K_COUNT == 2 ? 0x800U
                                        : 0x10000U) ||
            codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU))
            return false;
    }
    return true;
}

std::string text(const flatbuffers::String* value, std::size_t maximum = 4096) {
    if (!value || value->size() == 0 || value->size() > maximum || !utf8(value->string_view()))
        throw std::invalid_argument("Invalid worker UTF-8 string or length");
    return value->str();
}

std::vector<std::string>
strings(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>* values) {
    if (!values || values->size() == 0 || values->size() > 16)
        throw std::invalid_argument("Invalid worker capability vector");
    std::vector<std::string> result;
    for (const auto* value : *values) result.push_back(text(value, 128));
    return result;
}

bool session_valid(const std::string& session) {
    return session.size() == 64 &&
           session.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::vector<std::byte> copy_command(const flatbuffers::FlatBufferBuilder& builder) {
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return { K_BYTES.begin(), K_BYTES.end() };
}

}  // namespace

Result<WorkerEvent> WorkerStreamDecoder::decode() {
    try {
        flatbuffers::Verifier::Options options;
        options.max_depth = 16;
        options.max_tables = 128;
        flatbuffers::Verifier verifier(
            reinterpret_cast<const std::uint8_t*>(m_pending.data()), m_pending.size(), options);
        if (!wire::VerifySizePrefixedenvelopeBuffer(verifier))
            return fail(ErrorCode::INVALID_DATA, "Invalid FlatBuffers worker envelope");
        const auto* envelope = wire::GetSizePrefixedenvelope(m_pending.data());
        if (envelope->api_major() != 2 || envelope->api_minor() != 0 || !session_valid(m_session) ||
            text(envelope->session(), 64) != m_session ||
            envelope->operation() != (m_handshake ? 1U : 0U) ||
            envelope->sequence() != m_sequence || m_sequence >= 10000 || m_terminal)
            return fail(ErrorCode::INVALID_DATA, "Worker version, sequence or lifecycle mismatch");
        const auto K_KIND = envelope->body_type();
        if (!m_handshake && K_KIND != wire::payload::handshake)
            return fail(ErrorCode::INVALID_DATA, "Worker handshake must precede all events");
        WorkerEvent event;
        switch (K_KIND) {
        case wire::payload::handshake: {
            const auto* value = envelope->body_as_handshake();
            if (m_handshake || !value || value->max_message_bytes() != K_WORKER_MESSAGE_LIMIT ||
                value->max_frames() == 0 || value->max_frames() > 120 ||
                value->max_source_bytes() == 0 ||
                value->max_source_bytes() > 16ULL * 1024 * 1024 * 1024 ||
                value->max_output_bytes() == 0 ||
                value->max_output_bytes() > 32ULL * 1024 * 1024 * 1024)
                return fail(ErrorCode::INVALID_DATA, "Invalid worker handshake limits");
            event = WorkerHandshake {text(value->backend(), 128),
                                     text(value->backend_version(), 128),
                                     text(value->checkpoint_semantics()),
                                     text(value->checkpoint_rights()),
                                     strings(value->sensors()),
                                     strings(value->device_options()),
                                     value->batch(),
                                     value->streaming_tracking(),
                                     value->max_frames(),
                                     value->max_source_bytes(),
                                     value->max_output_bytes()};
            m_handshake = true;
            break;
        }
        case wire::payload::command_acknowledged: {
            const auto* value = envelope->body_as_command_acknowledged();
            if (!value)
                return fail(ErrorCode::INVALID_DATA, "Missing worker command acknowledgement");
            if (m_start_sent && value->kind() == wire::command_kind::START &&
                value->command_sequence() == 0 && value->command_operation() == 1) {
                m_started = true;
                event = WorkerCommandAcknowledged {WorkerCommandKind::START, {1}};
            } else if (value->kind() == wire::command_kind::CANCEL &&
                       value->command_sequence() == 1 && value->command_operation() == 2 &&
                       m_started && m_cancel_sent) {
                event = WorkerCommandAcknowledged {WorkerCommandKind::CANCEL, {2}};
            } else
                return fail(ErrorCode::INVALID_DATA, "Unexpected worker command acknowledgement");
            break;
        }
        case wire::payload::progress: {
            if (!m_started)
                return fail(ErrorCode::INVALID_DATA,
                            "Worker progress precedes START acknowledgement");
            const auto* value = envelope->body_as_progress();
            if (!value || !std::isfinite(value->elapsed_seconds()) || value->elapsed_seconds() < 0)
                return fail(ErrorCode::INVALID_DATA, "Invalid worker progress duration");
            event = WorkerProgress {
                text(value->stage(), 128), text(value->message(), 65536), value->elapsed_seconds()};
            break;
        }
        case wire::payload::completed: {
            if (!m_started)
                return fail(ErrorCode::INVALID_DATA,
                            "Worker completion precedes START acknowledgement");
            const auto* value = envelope->body_as_completed();
            if (!value) return fail(ErrorCode::INVALID_DATA, "Missing worker completion");
            WorkerCompleted completed {text(value->stage(), 128), {}};
            if (const auto* artifact = value->artifact()) {
                const auto K_PATH = text(artifact->path());
                const auto K_DIGEST = text(artifact->sha256(), 64);
                const std::filesystem::path K_NATIVE_PATH(
                    std::u8string(K_PATH.begin(), K_PATH.end()));
                if (K_DIGEST.size() != 64 ||
                    K_DIGEST.find_first_not_of("0123456789abcdef") != std::string::npos ||
                    !K_NATIVE_PATH.is_absolute() ||
                    K_NATIVE_PATH.lexically_normal() != K_NATIVE_PATH || artifact->bytes() == 0 ||
                    artifact->bytes() > K_WORKER_MESSAGE_LIMIT)
                    return fail(ErrorCode::INVALID_DATA,
                                "Invalid worker completion manifest reference");
                completed.m_artifact = WorkerArtifact { K_NATIVE_PATH, K_DIGEST, artifact->bytes() };
            } else if (completed.m_stage != "capabilities")
                return fail(ErrorCode::INVALID_DATA,
                            "Worker completion requires a manifest reference");
            event = std::move(completed);
            m_terminal = m_successful = true;
            break;
        }
        case wire::payload::failure: {
            const auto* value = envelope->body_as_failure();
            if (!value) return fail(ErrorCode::INVALID_DATA, "Missing worker failure");
            event = WorkerFailure {text(value->code(), 128),
                                   text(value->stage(), 128),
                                   text(value->message(), 65536),
                                   text(value->recovery())};
            m_terminal = true;
            break;
        }
        default:
            return fail(ErrorCode::INVALID_DATA, "Unknown worker payload");
        }
        ++m_sequence;
        return event;
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<void> WorkerStreamDecoder::feed(std::span<const std::byte> bytes,
                                       const WorkerEventCallback& callback) {
    if (m_invalid) return fail(ErrorCode::INVALID_DATA, "Worker stream already invalid");
    while (!bytes.empty()) {
        const auto K_COUNT = std::min(m_target - m_pending.size(), bytes.size());
        m_pending.insert(
            m_pending.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(K_COUNT));
        bytes = bytes.subspan(K_COUNT);
        if (m_pending.size() != m_target) continue;
        if (m_target == 4) {
            const auto K_SIZE = flatbuffers::ReadScalar<std::uint32_t>(m_pending.data());
            if (K_SIZE < 12 || K_SIZE > K_WORKER_MESSAGE_LIMIT) {
                m_invalid = true;
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker frame length outside bounds");
            }
            m_target = static_cast<std::size_t>(K_SIZE) + 4;
            continue;
        }
        const auto K_EVENT = decode();
        if (!K_EVENT) {
            m_invalid = true;
            return std::unexpected(K_EVENT.error());
        }
        m_pending.clear();
        m_target = 4;
        if (callback) callback(*K_EVENT);
    }
    return {};
}

Result<void> WorkerStreamDecoder::finish(int exit_code) const {
    if (m_invalid || !m_pending.empty() || !m_handshake || !m_terminal ||
        (exit_code == 0) != m_successful)
        return fail(ErrorCode::INVALID_DATA,
                    "Incomplete worker stream or inconsistent process exit");
    return {};
}

Result<std::vector<std::byte>> encode_worker_start(const std::string& session,
                                                   const std::vector<std::string>& arguments) {
    try {
        if (!session_valid(session) || arguments.empty() || arguments.size() > 64)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid worker START session/argument count");
        std::size_t total {};
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<flatbuffers::String>> offsets;
        for (const auto& argument : arguments) {
            if (argument.empty() || argument.size() > 4096 || !utf8(argument))
                return fail(ErrorCode::INVALID_ARGUMENT, "Invalid worker START argument");
            total += argument.size();
            if (total > 32768)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker START arguments exceed budget");
            offsets.push_back(builder.CreateString(argument));
        }
        const auto K_ARGS = builder.CreateVector(offsets);
        const auto K_BODY = control::Createstart_command(builder, K_ARGS);
        const auto K_TOKEN = builder.CreateString(session);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder,
                                            2,
                                            0,
                                            K_TOKEN,
                                            0,
                                            1,
                                            control::command_payload::start_command,
                                            K_BODY.Union()));
        if (builder.GetSize() > K_PROCESS_INPUT_LIMIT)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker START frame exceeds budget");
        return copy_command(builder);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker START allocation failed");
    }
}

Result<std::vector<std::byte>> encode_worker_cancel(const std::string& session) {
    try {
        if (!session_valid(session))
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid worker CANCEL session");
        flatbuffers::FlatBufferBuilder builder;
        const auto K_BODY = control::Createcancel_command(builder, 1);
        const auto K_TOKEN = builder.CreateString(session);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder,
                                            2,
                                            0,
                                            K_TOKEN,
                                            1,
                                            2,
                                            control::command_payload::cancel_command,
                                            K_BODY.Union()));
        return copy_command(builder);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker CANCEL allocation failed");
    }
}

Result<int> execute_worker_protocol(const std::string& python,
                                    const std::filesystem::path& script,
                                    const std::vector<std::string>& arguments,
                                    const std::stop_token& stop,
                                    const ProcessOutputCallback& log,
                                    const WorkerEventCallback& observed) {
    try {
        std::random_device random;
        std::array<std::uint32_t, 8> entropy {};
        for (auto& word : entropy) word = random();
        const auto K_SESSION = sha256(std::as_bytes(std::span(entropy)));
        auto start_command = encode_worker_start(K_SESSION, arguments);
        auto cancel_command = encode_worker_cancel(K_SESSION);
        if (!start_command) return std::unexpected(start_command.error());
        if (!cancel_command) return std::unexpected(cancel_command.error());
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        std::vector<std::string> command {python,
                                          "-u",
                                          script.string(),
                                          "--wire-bindings",
                                          WE_WIRE_BINDINGS,
                                          "--wire-session",
                                          K_SESSION};
        WorkerStreamDecoder decoder(K_SESSION);
        bool ready {}, start_sent {}, cancel_sent {}, start_acknowledged {};
        const auto K_START_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::optional<Error> failure;
        const auto K_EVENT = [&](const WorkerEvent& value) {
            if (std::holds_alternative<WorkerHandshake>(value)) ready = true;
            if (const auto* ack = std::get_if<WorkerCommandAcknowledged>(&value)) {
                if (ack->m_kind == WorkerCommandKind::START) start_acknowledged = true;
                if (log)
                    log(ack->m_kind == WorkerCommandKind::START ? "Worker accepted START.\n"
                                                                : "Worker accepted CANCEL.\n");
            }
            if (const auto* progress = std::get_if<WorkerProgress>(&value); progress && log)
                log(progress->m_stage + ": " + progress->m_message + "\n");
            if (const auto* completed = std::get_if<WorkerCompleted>(&value);
                completed && completed->m_artifact) {
                const auto& asset = *completed->m_artifact;
                std::error_code filesystem_error;
                const auto K_STATUS =
                    std::filesystem::symlink_status(asset.m_path, filesystem_error);
                if (filesystem_error || !std::filesystem::is_regular_file(K_STATUS)) {
                    failure = Error {ErrorCode::INVALID_DATA,
                                     "Worker completion manifest must be a regular file"};
                    cancellation.request_stop();
                    return;
                }
                const auto K_DIGEST = sha256_file(asset.m_path,
                                                  static_cast<std::size_t>(asset.m_bytes),
                                                  cancellation.get_token());
                const auto K_BYTES = std::filesystem::file_size(asset.m_path, filesystem_error);
                if (!K_DIGEST && K_DIGEST.error().m_code == ErrorCode::CANCELLED) {
                    failure = K_DIGEST.error();
                    return;
                }
                if (!K_DIGEST || filesystem_error || K_BYTES != asset.m_bytes ||
                    *K_DIGEST != asset.m_sha256) {
                    failure = Error {ErrorCode::INVALID_DATA,
                                     "Worker completion manifest checksum/size mismatch"};
                    cancellation.request_stop();
                } else if (log) log("Worker completed: " + asset.m_path.string() + "\n");
            }
            if (const auto* error = std::get_if<WorkerFailure>(&value); error && log)
                log(error->m_stage + ": " + error->m_code + ": " + error->m_message + "\n" +
                    error->m_recovery + "\n");
            if (observed && !failure) observed(value);
        };
        const auto K_EXECUTED = execute_process(
            command,
            cancellation.get_token(),
            [&](const std::string& output) {
                if (failure) return;
                const auto K_DECODED =
                    decoder.feed(std::as_bytes(std::span(output.data(), output.size())), K_EVENT);
                if (!K_DECODED) {
                    failure = K_DECODED.error();
                    cancellation.request_stop();
                }
            },
            log,
            {[&](bool cancelling) -> Result<std::vector<std::byte>> {
                 if (failure) return std::unexpected(*failure);
                 if (!start_acknowledged && std::chrono::steady_clock::now() > K_START_DEADLINE)
                     return fail(ErrorCode::BACKEND_FAILURE,
                                 "Worker START acknowledgement deadline exceeded");
                 if (ready && !start_sent && !cancelling) {
                     start_sent = true;
                     decoder.expect_start();
                     return std::move(*start_command);
                 }
                 if (start_sent && cancelling && !cancel_sent) {
                     cancel_sent = true;
                     decoder.expect_cancel();
                     return std::move(*cancel_command);
                 }
                 return std::vector<std::byte> {};
             },
             std::chrono::seconds(2)});
        if (failure) return std::unexpected(*failure);
        if (!K_EXECUTED) return std::unexpected(K_EXECUTED.error());
        const auto K_COMPLETE = decoder.finish(*K_EXECUTED);
        if (!K_COMPLETE) return std::unexpected(K_COMPLETE.error());
        return K_EXECUTED;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Worker protocol allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::BACKEND_FAILURE, std::string("Worker protocol failed: ") + e.what());
    }
}

}  // namespace world_explorer
