#pragma once

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

inline constexpr std::size_t K_WORKER_MESSAGE_LIMIT { 1024uz * 1024uz };
enum class WorkerCommandKind : std::uint8_t { START, CANCEL };

struct WorkerCommandAcknowledged {
    WorkerCommandKind m_kind;
    OperationId m_operation;
};

struct WorkerHandshake {
    std::string m_backend, m_backend_version, m_checkpoint_semantics, m_checkpoint_rights;
    std::vector<std::string> m_sensors, m_device_options;
    bool m_batch {}, m_streaming_tracking {};
    std::uint32_t m_max_frames {};
    std::uint64_t m_max_source_bytes {}, m_max_output_bytes {};
};

struct WorkerProgress {
    std::string m_stage, m_message;
    double m_elapsed_seconds {};
};

struct WorkerArtifact {
    std::filesystem::path m_path;
    std::string m_sha256;
    std::uint64_t m_bytes {};
};

struct WorkerCompleted {
    std::string m_stage;
    std::optional<WorkerArtifact> m_artifact;
};

struct WorkerFailure {
    std::string m_code, m_stage, m_message, m_recovery;
};

using WorkerEvent = std::variant<WorkerHandshake,
                                 WorkerProgress,
                                 WorkerCompleted,
                                 WorkerFailure,
                                 WorkerCommandAcknowledged>;
using WorkerEventCallback = std::function<void(const WorkerEvent&)>;

// Single owner. Bounded framing, structural verification and ordered v2 session
// validation precede every callback. A failed stream is permanently invalid.
class WorkerStreamDecoder {
  public:
    explicit WorkerStreamDecoder(std::string session) : m_session(std::move(session)) { }

    void expect_start() noexcept { m_start_sent = true; }

    void expect_cancel() noexcept { m_cancel_sent = true; }

    Result<void> feed(std::span<const std::byte> bytes, const WorkerEventCallback& callback);
    Result<void> finish(int exit_code) const;

  private:
    Result<WorkerEvent> decode();
    std::vector<std::byte> m_pending;
    std::size_t m_target { 4uz };
    std::uint64_t m_sequence {};
    bool m_handshake {}, m_terminal {}, m_successful {}, m_invalid {};
    std::string m_session;
    bool m_start_sent {}, m_started {}, m_cancel_sent {};
};

Result<std::vector<std::byte>> encode_worker_start(const std::string& session,
                                                   const std::vector<std::string>& arguments);
Result<std::vector<std::byte>> encode_worker_cancel(const std::string& session);
// Local one-shot batch invocation. START follows verified capabilities; bounded
// stdin commands and stdout events bind the session. Cancellation first requests
// a checkpoint-preserving stop, then enforces process-tree termination.
Result<int> execute_worker_protocol(const std::string& python,
                                    const std::filesystem::path& script,
                                    const std::vector<std::string>& arguments,
                                    const std::stop_token& stop,
                                    const ProcessOutputCallback& log,
                                    const WorkerEventCallback& observed = {});

}  // namespace world_explorer
