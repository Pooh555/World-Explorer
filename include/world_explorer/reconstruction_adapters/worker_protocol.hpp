#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_PROTOCOL_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_PROTOCOL_HPP
#include "world_explorer/core/result.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
namespace world_explorer {
inline constexpr std::size_t WORKER_MESSAGE_LIMIT = std::size_t{1024} * 1024;
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
using worker_event =
    std::variant<worker_handshake, worker_progress, worker_completed, worker_failure>;
using worker_event_callback = std::function<void(const worker_event&)>;
// Single owner. Bounded framing, structural verification and ordered v1 session
// validation precede every callback. A failed stream is permanently invalid.
class worker_stream_decoder {
  public:
    result<void> feed(std::span<const std::byte> bytes, const worker_event_callback& callback);
    result<void> finish(int exit_code) const;

  private:
    result<worker_event> decode();
    std::vector<std::byte> pending_;
    std::size_t target_{4};
    std::uint64_t sequence_{};
    bool handshake_{}, terminal_{}, successful_{}, invalid_{};
};
// Local one-shot batch invocation. The worker advertises its capabilities before
// events; argv selects the command/explicit JSON request. No remote fallback.
result<int> execute_worker_protocol(const std::string& python, const std::filesystem::path& script,
                                    const std::vector<std::string>& arguments,
                                    const std::stop_token& stop,
                                    const process_output_callback& log);
} // namespace world_explorer
#endif
