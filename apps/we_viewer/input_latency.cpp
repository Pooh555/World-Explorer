#include "input_latency.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace world_explorer::viewer {
namespace {

nlohmann::json camera_state(const FlyCamera& camera) {
    return {{"position", {camera.m_position.m_x, camera.m_position.m_y, camera.m_position.m_z}},
            {"yaw", camera.m_yaw},
            {"pitch", camera.m_pitch},
            {"speed", camera.m_speed}};
}

bool relevant(const SDL_Event& event, const std::array<int, 7>& keys) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.scancode == SDL_SCANCODE_ESCAPE ||
               std::ranges::find(keys, static_cast<int>(event.key.scancode)) != keys.end();
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return event.button.button == SDL_BUTTON_LEFT;
    default:
        return false;
    }
}

}  // namespace

input_latency_recorder::input_latency_recorder(const std::filesystem::path& path)
    : output_(path, std::ios::out | std::ios::binary | std::ios::noreplace) {
    if (!output_) throw std::runtime_error("Cannot create input latency report; choose a new file");
    events_.reserve(K_MAX_EVENTS);
    frames_.reserve(K_MAX_EVENTS);
}

void input_latency_recorder::observe(const SDL_Event& event,
                                     std::uint64_t polled_ns,
                                     const std::array<int, 7>& keys) {
    if (!relevant(event, keys)) return;
    if (events_.size() == K_MAX_EVENTS) {
        ++dropped_;
        return;
    }
    events_.push_back({event, polled_ns});
}

void input_latency_recorder::finish_frame(unsigned frame,
                                          std::uint64_t state_ns,
                                          const FlyCamera& before,
                                          const FlyCamera& after,
                                          const FlyInput& input,
                                          bool scene_visible) {
    if (pending_begin_ != events_.size()) {
        frames_.push_back({frame,
                           previous_state_ns_,
                           state_ns,
                           pending_begin_,
                           events_.size(),
                           before,
                           after,
                           input,
                           scene_visible});
        pending_begin_ = events_.size();
    }
    previous_state_ns_ = state_ns;
}

nlohmann::json input_latency_recorder::report() const {
    auto rows = nlohmann::json::array();
    std::size_t invalid_timestamps {}, invalid_values {};
    for (const auto& frame : frames_) {
        auto events = nlohmann::json::array();
        for (auto index = frame.begin; index < frame.end; ++index) {
            const auto& sample = events_[index];
            const auto& event = sample.event;
            const bool K_VALID = event.common.timestamp != 0 &&
                                 event.common.timestamp <= sample.polled_ns &&
                                 sample.polled_ns <= frame.state_ns;
            invalid_timestamps += !K_VALID;
            const bool K_VALUES_VALID =
                (event.type != SDL_EVENT_MOUSE_MOTION ||
                 (std::isfinite(event.motion.xrel) && std::isfinite(event.motion.yrel))) &&
                (event.type != SDL_EVENT_MOUSE_WHEEL || std::isfinite(event.wheel.y));
            invalid_values += !K_VALUES_VALID;
            nlohmann::json item {{"type", event.type},
                                 {"event_ns", event.common.timestamp},
                                 {"polled_ns", sample.polled_ns},
                                 {"timestamp_valid", K_VALID},
                                 {"values_valid", K_VALUES_VALID},
                                 {"event_to_state_ns", nullptr},
                                 {"poll_to_state_ns", nullptr},
                                 {"since_previous_state", nullptr}};
            if (K_VALID) {
                item["event_to_state_ns"] = frame.state_ns - event.common.timestamp;
                item["poll_to_state_ns"] = frame.state_ns - sample.polled_ns;
                if (frame.previous_state_ns != 0)
                    item["since_previous_state"] =
                        event.common.timestamp >= frame.previous_state_ns;
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                item["scancode"] = event.key.scancode;
                item["down"] = event.key.down;
                item["repeat"] = event.key.repeat;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                item["relative"] = { event.motion.xrel, event.motion.yrel };
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                item["wheel_y"] = event.wheel.y;
            }
            events.push_back(std::move(item));
        }
        rows.push_back(
            {{"frame", frame.frame},
             {"previous_state_ns", frame.previous_state_ns},
             {"state_ns", frame.state_ns},
             {"before", camera_state(frame.before)},
             {"after", camera_state(frame.after)},
             {"scene_visible", frame.scene_visible},
             {"captured", frame.input.m_captured},
             {"focused", frame.input.m_focused},
             {"text_input", frame.input.m_text_input},
             {"movement", {frame.input.m_forward, frame.input.m_right, frame.input.m_up}},
             {"mouse", {frame.input.m_mouse_x, frame.input.m_mouse_y}},
             {"wheel", frame.input.m_wheel},
             {"accelerate", frame.input.m_accelerate},
             {"events", std::move(events)}});
    }
    return {{"schema_version", 1},
            {"scope", "SDL event timestamp to CPU camera state; no display measurement"},
            {"clock", "SDL_GetTicksNS"},
            {"max_events", K_MAX_EVENTS},
            {"recorded_events", pending_begin_},
            {"dropped_events", dropped_},
            {"invalid_timestamps", invalid_timestamps},
            {"invalid_values", invalid_values},
            {"frames", std::move(rows)}};
}

void input_latency_recorder::write(const input_latency_context& context) {
    auto document = report();
    document["video_driver"] = context.video_driver ? context.video_driver : "unknown";
    document["device"] = context.device ? context.device : "unknown";
    document["resident_splats"] = context.resident_splats;
    document["validation_errors"] = context.validation_errors;
    output_ << document.dump() << '\n';
    output_.flush();
    if (!output_) throw std::runtime_error("Input latency report write failed");
}

}  // namespace world_explorer::viewer
