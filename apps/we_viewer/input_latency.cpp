#include "input_latency.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace world_explorer::viewer {
namespace {
nlohmann::json camera_state(const fly_camera& camera) {
    return {{"position", {camera.position.x, camera.position.y, camera.position.z}},
            {"yaw", camera.yaw},
            {"pitch", camera.pitch},
            {"speed", camera.speed}};
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
} // namespace
input_latency_recorder::input_latency_recorder(const std::filesystem::path& path)
    : output_(path, std::ios::out | std::ios::binary | std::ios::noreplace) {
    if (!output_)
        throw std::runtime_error("Cannot create input latency report; choose a new file");
    events_.reserve(MAX_EVENTS);
    frames_.reserve(MAX_EVENTS);
}
void input_latency_recorder::observe(const SDL_Event& event, std::uint64_t polled_ns,
                                     const std::array<int, 7>& keys) {
    if (!relevant(event, keys))
        return;
    if (events_.size() == MAX_EVENTS) {
        ++dropped_;
        return;
    }
    events_.push_back({event, polled_ns});
}
void input_latency_recorder::finish_frame(unsigned frame, std::uint64_t state_ns,
                                          const fly_camera& before, const fly_camera& after,
                                          const fly_input& input, bool scene_visible) {
    if (pending_begin_ != events_.size()) {
        frames_.push_back({frame, previous_state_ns_, state_ns, pending_begin_, events_.size(),
                           before, after, input, scene_visible});
        pending_begin_ = events_.size();
    }
    previous_state_ns_ = state_ns;
}
nlohmann::json input_latency_recorder::report() const {
    auto rows = nlohmann::json::array();
    std::size_t invalid_timestamps{}, invalid_values{};
    for (const auto& frame : frames_) {
        auto events = nlohmann::json::array();
        for (auto index = frame.begin; index < frame.end; ++index) {
            const auto& sample = events_[index];
            const auto& event = sample.event;
            const bool valid = event.common.timestamp != 0 &&
                               event.common.timestamp <= sample.polled_ns &&
                               sample.polled_ns <= frame.state_ns;
            invalid_timestamps += !valid;
            const bool values_valid =
                (event.type != SDL_EVENT_MOUSE_MOTION ||
                 (std::isfinite(event.motion.xrel) && std::isfinite(event.motion.yrel))) &&
                (event.type != SDL_EVENT_MOUSE_WHEEL || std::isfinite(event.wheel.y));
            invalid_values += !values_valid;
            nlohmann::json item{{"type", event.type},
                                {"event_ns", event.common.timestamp},
                                {"polled_ns", sample.polled_ns},
                                {"timestamp_valid", valid},
                                {"values_valid", values_valid},
                                {"event_to_state_ns", nullptr},
                                {"poll_to_state_ns", nullptr},
                                {"since_previous_state", nullptr}};
            if (valid) {
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
                item["relative"] = {event.motion.xrel, event.motion.yrel};
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                item["wheel_y"] = event.wheel.y;
            }
            events.push_back(std::move(item));
        }
        rows.push_back({{"frame", frame.frame},
                        {"previous_state_ns", frame.previous_state_ns},
                        {"state_ns", frame.state_ns},
                        {"before", camera_state(frame.before)},
                        {"after", camera_state(frame.after)},
                        {"scene_visible", frame.scene_visible},
                        {"captured", frame.input.captured},
                        {"focused", frame.input.focused},
                        {"text_input", frame.input.text_input},
                        {"movement", {frame.input.forward, frame.input.right, frame.input.up}},
                        {"mouse", {frame.input.mouse_x, frame.input.mouse_y}},
                        {"wheel", frame.input.wheel},
                        {"accelerate", frame.input.accelerate},
                        {"events", std::move(events)}});
    }
    return {{"schema_version", 1},
            {"scope", "SDL event timestamp to CPU camera state; no display measurement"},
            {"clock", "SDL_GetTicksNS"},
            {"max_events", MAX_EVENTS},
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
    if (!output_)
        throw std::runtime_error("Input latency report write failed");
}
} // namespace world_explorer::viewer
