#ifndef WORLD_EXPLORER_VIEWER_INPUT_LATENCY_HPP
#define WORLD_EXPLORER_VIEWER_INPUT_LATENCY_HPP

#include "world_explorer/navigation/fly_camera.hpp"
#include <SDL3/SDL_events.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace world_explorer::viewer {
struct input_latency_context {
    const char* video_driver;
    const char* device;
    std::size_t resident_splats;
    std::size_t validation_errors;
};
// Viewer-only diagnostic. All storage is reserved before observing input, and
// serialization happens after the interactive loop has finished.
class input_latency_recorder {
  public:
    static constexpr std::size_t MAX_EVENTS = 4096;
    explicit input_latency_recorder(const std::filesystem::path& path);
    void observe(const SDL_Event& event, std::uint64_t polled_ns, const std::array<int, 7>& keys);
    void finish_frame(unsigned frame, std::uint64_t state_ns, const fly_camera& before,
                      const fly_camera& after, const fly_input& input, bool scene_visible);
    nlohmann::json report() const;
    void write(const input_latency_context& context);

  private:
    struct event_sample {
        SDL_Event event;
        std::uint64_t polled_ns;
    };
    struct frame_sample {
        unsigned frame;
        std::uint64_t previous_state_ns, state_ns;
        std::size_t begin, end;
        fly_camera before, after;
        fly_input input;
        bool scene_visible;
    };
    std::ofstream output_;
    std::vector<event_sample> events_;
    std::vector<frame_sample> frames_;
    std::size_t pending_begin_{}, dropped_{};
    std::uint64_t previous_state_ns_{};
};
} // namespace world_explorer::viewer
#endif
