#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_PROTOCOL_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_PROTOCOL_HPP
#include "world_explorer/core/result.hpp"
#include "world_explorer/core/types.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
namespace world_explorer {
inline constexpr std::size_t WORKER_MESSAGE_LIMIT = std::size_t{1024} * 1024;
enum class worker_command_kind : std::uint8_t { START, CANCEL };
struct worker_command_acknowledged {
    worker_command_kind kind;
    operation_id operation;
};
struct worker_handshake {
    std::string backend, backend_version, checkpoint_semantics, checkpoint_rights;
    std::vector<std::string> sensors, device_options;
    bool batch{}, streaming_tracking{};
    std::uint32_t max_frames{};
    std::uint64_t max_source_bytes{}, max_output_bytes{};
};
struct worker_progress {
    std::string stage, message;
    double elapsed_seconds{};
};
struct worker_artifact {
    std::filesystem::path path;
    std::string sha256;
    std::uint64_t bytes{};
};
struct worker_completed {
    std::string stage;
    std::optional<worker_artifact> artifact;
};
struct worker_failure {
    std::string code, stage, message, recovery;
};
using worker_event = std::variant<worker_handshake, worker_progress, worker_completed,
                                  worker_failure, worker_command_acknowledged>;
using worker_event_callback = std::function<void(const worker_event&)>;
// Single owner. Bounded framing, structural verification and ordered v2 session
// validation precede every callback. A failed stream is permanently invalid.
class worker_stream_decoder {
  public:
    explicit worker_stream_decoder(std::string session) : session_(std::move(session)) {}
    void expect_start() noexcept { start_sent_ = true; }
    void expect_cancel() noexcept { cancel_sent_ = true; }
    result<void> feed(std::span<const std::byte> bytes, const worker_event_callback& callback);
    result<void> finish(int exit_code) const;

  private:
    result<worker_event> decode();
    std::vector<std::byte> pending_;
    std::size_t target_{4};
    std::uint64_t sequence_{};
    bool handshake_{}, terminal_{}, successful_{}, invalid_{};
    std::string session_;
    bool start_sent_{}, started_{}, cancel_sent_{};
};
result<std::vector<std::byte>> encode_worker_start(const std::string& session,
                                                   const std::vector<std::string>& arguments);
result<std::vector<std::byte>> encode_worker_cancel(const std::string& session);
// Local one-shot batch invocation. START follows verified capabilities; bounded
// stdin commands and stdout events bind the session. Cancellation first requests
// a checkpoint-preserving stop, then enforces process-tree termination.
result<int> execute_worker_protocol(const std::string& python, const std::filesystem::path& script,
                                    const std::vector<std::string>& arguments,
                                    const std::stop_token& stop, const process_output_callback& log,
                                    const worker_event_callback& observed = {});
} // namespace world_explorer
#endif
