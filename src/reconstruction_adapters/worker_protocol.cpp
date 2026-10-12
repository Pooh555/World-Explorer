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
    for (std::size_t offset = 0; offset < text.size();) {
        const auto head = static_cast<unsigned char>(text[offset++]);
        if (head == 0)
            return false;
        if (head < 128)
            continue;
        const std::size_t count = head >= 0xc2 && head <= 0xdf   ? 1
                                  : head >= 0xe0 && head <= 0xef ? 2
                                  : head >= 0xf0 && head <= 0xf4 ? 3
                                                                 : 0;
        if (!count || count > text.size() - offset)
            return false;
        auto codepoint = static_cast<std::uint32_t>(head & (0x7fU >> count));
        for (std::size_t index = 0; index < count; ++index) {
            const auto byte = static_cast<unsigned char>(text[offset++]);
            if ((byte & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (byte & 0x3fU);
        }
        if (codepoint < (count == 1   ? 0x80U
                         : count == 2 ? 0x800U
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
    for (const auto* value : *values)
        result.push_back(text(value, 128));
    return result;
}
bool session_valid(const std::string& session) {
    return session.size() == 64 &&
           session.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::vector<std::byte> copy_command(const flatbuffers::FlatBufferBuilder& builder) {
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return {bytes.begin(), bytes.end()};
}
} // namespace
result<worker_event> worker_stream_decoder::decode() {
    try {
        flatbuffers::Verifier::Options options;
        options.max_depth = 16;
        options.max_tables = 128;
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(pending_.data()),
                                       pending_.size(), options);
        if (!wire::VerifySizePrefixedenvelopeBuffer(verifier))
            return fail(error_code::INVALID_DATA, "Invalid FlatBuffers worker envelope");
        const auto* envelope = wire::GetSizePrefixedenvelope(pending_.data());
        if (envelope->api_major() != 2 || envelope->api_minor() != 0 || !session_valid(session_) ||
            text(envelope->session(), 64) != session_ ||
            envelope->operation() != (handshake_ ? 1U : 0U) || envelope->sequence() != sequence_ ||
            sequence_ >= 10000 || terminal_)
            return fail(error_code::INVALID_DATA, "Worker version, sequence or lifecycle mismatch");
        const auto kind = envelope->body_type();
        if (!handshake_ && kind != wire::payload::handshake)
            return fail(error_code::INVALID_DATA, "Worker handshake must precede all events");
        worker_event event;
        switch (kind) {
        case wire::payload::handshake: {
            const auto* value = envelope->body_as_handshake();
            if (handshake_ || !value || value->max_message_bytes() != WORKER_MESSAGE_LIMIT ||
                value->max_frames() == 0 || value->max_frames() > 120 ||
                value->max_source_bytes() == 0 ||
                value->max_source_bytes() > 16ULL * 1024 * 1024 * 1024 ||
                value->max_output_bytes() == 0 ||
                value->max_output_bytes() > 32ULL * 1024 * 1024 * 1024)
                return fail(error_code::INVALID_DATA, "Invalid worker handshake limits");
            event = worker_handshake{text(value->backend(), 128),
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
            handshake_ = true;
            break;
        }
        case wire::payload::command_acknowledged: {
            const auto* value = envelope->body_as_command_acknowledged();
            if (!value)
                return fail(error_code::INVALID_DATA, "Missing worker command acknowledgement");
            if (start_sent_ && value->kind() == wire::command_kind::START &&
                value->command_sequence() == 0 && value->command_operation() == 1) {
                started_ = true;
                event = worker_command_acknowledged{worker_command_kind::START, {1}};
            } else if (value->kind() == wire::command_kind::CANCEL &&
                       value->command_sequence() == 1 && value->command_operation() == 2 &&
                       started_ && cancel_sent_) {
                event = worker_command_acknowledged{worker_command_kind::CANCEL, {2}};
            } else
                return fail(error_code::INVALID_DATA, "Unexpected worker command acknowledgement");
            break;
        }
        case wire::payload::progress: {
            if (!started_)
                return fail(error_code::INVALID_DATA,
                            "Worker progress precedes START acknowledgement");
            const auto* value = envelope->body_as_progress();
            if (!value || !std::isfinite(value->elapsed_seconds()) || value->elapsed_seconds() < 0)
                return fail(error_code::INVALID_DATA, "Invalid worker progress duration");
            event = worker_progress{text(value->stage(), 128), text(value->message(), 65536),
                                    value->elapsed_seconds()};
            break;
        }
        case wire::payload::completed: {
            if (!started_)
                return fail(error_code::INVALID_DATA,
                            "Worker completion precedes START acknowledgement");
            const auto* value = envelope->body_as_completed();
            if (!value)
                return fail(error_code::INVALID_DATA, "Missing worker completion");
            worker_completed completed{text(value->stage(), 128), {}};
            if (const auto* artifact = value->artifact()) {
                const auto path = text(artifact->path());
                const auto digest = text(artifact->sha256(), 64);
                const std::filesystem::path native_path(std::u8string(path.begin(), path.end()));
                if (digest.size() != 64 ||
                    digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
                    !native_path.is_absolute() || native_path.lexically_normal() != native_path ||
                    artifact->bytes() == 0 || artifact->bytes() > WORKER_MESSAGE_LIMIT)
                    return fail(error_code::INVALID_DATA,
                                "Invalid worker completion manifest reference");
                completed.artifact = worker_artifact{native_path, digest, artifact->bytes()};
            } else if (completed.stage != "capabilities")
                return fail(error_code::INVALID_DATA,
                            "Worker completion requires a manifest reference");
            event = std::move(completed);
            terminal_ = successful_ = true;
            break;
        }
        case wire::payload::failure: {
            const auto* value = envelope->body_as_failure();
            if (!value)
                return fail(error_code::INVALID_DATA, "Missing worker failure");
            event = worker_failure{text(value->code(), 128), text(value->stage(), 128),
                                   text(value->message(), 65536), text(value->recovery())};
            terminal_ = true;
            break;
        }
        default:
            return fail(error_code::INVALID_DATA, "Unknown worker payload");
        }
        ++sequence_;
        return event;
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
result<void> worker_stream_decoder::feed(std::span<const std::byte> bytes,
                                         const worker_event_callback& callback) {
    if (invalid_)
        return fail(error_code::INVALID_DATA, "Worker stream already invalid");
    while (!bytes.empty()) {
        const auto count = std::min(target_ - pending_.size(), bytes.size());
        pending_.insert(pending_.end(), bytes.begin(),
                        bytes.begin() + static_cast<std::ptrdiff_t>(count));
        bytes = bytes.subspan(count);
        if (pending_.size() != target_)
            continue;
        if (target_ == 4) {
            const auto size = flatbuffers::ReadScalar<std::uint32_t>(pending_.data());
            if (size < 12 || size > WORKER_MESSAGE_LIMIT) {
                invalid_ = true;
                return fail(error_code::RESOURCE_EXHAUSTED, "Worker frame length outside bounds");
            }
            target_ = static_cast<std::size_t>(size) + 4;
            continue;
        }
        const auto event = decode();
        if (!event) {
            invalid_ = true;
            return std::unexpected(event.error());
        }
        pending_.clear();
        target_ = 4;
        if (callback)
            callback(*event);
    }
    return {};
}
result<void> worker_stream_decoder::finish(int exit_code) const {
    if (invalid_ || !pending_.empty() || !handshake_ || !terminal_ ||
        (exit_code == 0) != successful_)
        return fail(error_code::INVALID_DATA,
                    "Incomplete worker stream or inconsistent process exit");
    return {};
}
result<std::vector<std::byte>> encode_worker_start(const std::string& session,
                                                   const std::vector<std::string>& arguments) {
    try {
        if (!session_valid(session) || arguments.empty() || arguments.size() > 64)
            return fail(error_code::INVALID_ARGUMENT,
                        "Invalid worker START session/argument count");
        std::size_t total{};
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<flatbuffers::String>> offsets;
        for (const auto& argument : arguments) {
            if (argument.empty() || argument.size() > 4096 || !utf8(argument))
                return fail(error_code::INVALID_ARGUMENT, "Invalid worker START argument");
            total += argument.size();
            if (total > 32768)
                return fail(error_code::RESOURCE_EXHAUSTED, "Worker START arguments exceed budget");
            offsets.push_back(builder.CreateString(argument));
        }
        const auto args = builder.CreateVector(offsets);
        const auto body = control::Createstart_command(builder, args);
        const auto token = builder.CreateString(session);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder,
            control::Createcommand_envelope(builder, 2, 0, token, 0, 1,
                                            control::command_payload::start_command, body.Union()));
        if (builder.GetSize() > PROCESS_INPUT_LIMIT)
            return fail(error_code::RESOURCE_EXHAUSTED, "Worker START frame exceeds budget");
        return copy_command(builder);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Worker START allocation failed");
    }
}
result<std::vector<std::byte>> encode_worker_cancel(const std::string& session) {
    try {
        if (!session_valid(session))
            return fail(error_code::INVALID_ARGUMENT, "Invalid worker CANCEL session");
        flatbuffers::FlatBufferBuilder builder;
        const auto body = control::Createcancel_command(builder, 1);
        const auto token = builder.CreateString(session);
        control::FinishSizePrefixedcommand_envelopeBuffer(
            builder, control::Createcommand_envelope(builder, 2, 0, token, 1, 2,
                                                     control::command_payload::cancel_command,
                                                     body.Union()));
        return copy_command(builder);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Worker CANCEL allocation failed");
    }
}
result<int> execute_worker_protocol(const std::string& python, const std::filesystem::path& script,
                                    const std::vector<std::string>& arguments,
                                    const std::stop_token& stop, const process_output_callback& log,
                                    const worker_event_callback& observed) {
    try {
        std::random_device random;
        std::array<std::uint32_t, 8> entropy{};
        for (auto& word : entropy)
            word = random();
        const auto session = sha256(std::as_bytes(std::span(entropy)));
        auto start_command = encode_worker_start(session, arguments);
        auto cancel_command = encode_worker_cancel(session);
        if (!start_command)
            return std::unexpected(start_command.error());
        if (!cancel_command)
            return std::unexpected(cancel_command.error());
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        std::vector<std::string> command{
            python,           "-u",   script.string(), "--wire-bindings", WE_WIRE_BINDINGS,
            "--wire-session", session};
        worker_stream_decoder decoder(session);
        bool ready{}, start_sent{}, cancel_sent{}, start_acknowledged{};
        const auto start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::optional<error> failure;
        const auto event = [&](const worker_event& value) {
            if (std::holds_alternative<worker_handshake>(value))
                ready = true;
            if (const auto* ack = std::get_if<worker_command_acknowledged>(&value)) {
                if (ack->kind == worker_command_kind::START)
                    start_acknowledged = true;
                if (log)
                    log(ack->kind == worker_command_kind::START ? "Worker accepted START.\n"
                                                                : "Worker accepted CANCEL.\n");
            }
            if (const auto* progress = std::get_if<worker_progress>(&value); progress && log)
                log(progress->stage + ": " + progress->message + "\n");
            if (const auto* completed = std::get_if<worker_completed>(&value);
                completed && completed->artifact) {
                const auto& asset = *completed->artifact;
                std::error_code filesystem_error;
                const auto status = std::filesystem::symlink_status(asset.path, filesystem_error);
                if (filesystem_error || !std::filesystem::is_regular_file(status)) {
                    failure = error{error_code::INVALID_DATA,
                                    "Worker completion manifest must be a regular file"};
                    cancellation.request_stop();
                    return;
                }
                const auto digest = sha256_file(asset.path, static_cast<std::size_t>(asset.bytes),
                                                cancellation.get_token());
                const auto bytes = std::filesystem::file_size(asset.path, filesystem_error);
                if (!digest && digest.error().code == error_code::CANCELLED) {
                    failure = digest.error();
                    return;
                }
                if (!digest || filesystem_error || bytes != asset.bytes ||
                    *digest != asset.sha256) {
                    failure = error{error_code::INVALID_DATA,
                                    "Worker completion manifest checksum/size mismatch"};
                    cancellation.request_stop();
                } else if (log)
                    log("Worker completed: " + asset.path.string() + "\n");
            }
            if (const auto* error = std::get_if<worker_failure>(&value); error && log)
                log(error->stage + ": " + error->code + ": " + error->message + "\n" +
                    error->recovery + "\n");
            if (observed && !failure)
                observed(value);
        };
        const auto executed = execute_process(
            command, cancellation.get_token(),
            [&](const std::string& output) {
                if (failure)
                    return;
                const auto decoded =
                    decoder.feed(std::as_bytes(std::span(output.data(), output.size())), event);
                if (!decoded) {
                    failure = decoded.error();
                    cancellation.request_stop();
                }
            },
            log,
            {[&](bool cancelling) -> result<std::vector<std::byte>> {
                 if (failure)
                     return std::unexpected(*failure);
                 if (!start_acknowledged && std::chrono::steady_clock::now() > start_deadline)
                     return fail(error_code::BACKEND_FAILURE,
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
                 return std::vector<std::byte>{};
             },
             std::chrono::seconds(2)});
        if (failure)
            return std::unexpected(*failure);
        if (!executed)
            return std::unexpected(executed.error());
        const auto complete = decoder.finish(*executed);
        if (!complete)
            return std::unexpected(complete.error());
        return executed;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Worker protocol allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::BACKEND_FAILURE,
                    std::string("Worker protocol failed: ") + e.what());
    }
}
} // namespace world_explorer
