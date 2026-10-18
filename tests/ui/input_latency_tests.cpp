#include "input_latency.hpp"
#include "preferences.hpp"

#include <chrono>
#include <fstream>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;
using namespace world_explorer::viewer;

namespace {

struct LatencyFixture {
    std::filesystem::path m_path =
        std::filesystem::temp_directory_path() /
        ("we_latency_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");

    ~LatencyFixture() {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }
};

SDL_Event key_event(std::uint64_t timestamp) {
    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.timestamp = timestamp;
    event.key.scancode = SDL_SCANCODE_W;
    event.key.down = true;
    return event;
}

}  // namespace

TEST_CASE("input timing rejects missing reversed and future timestamps without unsigned wrap",
          "[input_latency]") {
    LatencyFixture fixture;
    input_latency_recorder recorder(fixture.m_path);
    const FlyCamera K_CAMERA;
    for (const auto K_EVENT_NS : {0ULL, 301ULL, 99ULL})
        recorder.observe(key_event(K_EVENT_NS), 200, preferences {}.keys);
    recorder.finish_frame(1, 100, K_CAMERA, K_CAMERA, {}, false);
    const auto K_REPORT = recorder.report();
    CHECK(K_REPORT.at("invalid_timestamps") == 3);
    for (const auto& event : K_REPORT.at("frames").at(0).at("events")) {
        CHECK(event.at("timestamp_valid") == false);
        CHECK(event.at("event_to_state_ns").is_null());
        CHECK(event.at("poll_to_state_ns").is_null());
    }
}

TEST_CASE("input timing retains old backlog and blocked camera state", "[input_latency]") {
    LatencyFixture fixture;
    input_latency_recorder recorder(fixture.m_path);
    FlyCamera camera;
    recorder.finish_frame(10, 1000, camera, camera, {}, false);
    recorder.observe(key_event(900), 1100, preferences {}.keys);
    recorder.observe(key_event(1050), 1101, preferences {}.keys);
    FlyInput blocked;
    blocked.m_captured = true;
    blocked.m_text_input = true;
    blocked.m_forward = 1;
    const auto K_BEFORE = camera;
    camera.update(blocked, 0.016);
    recorder.finish_frame(11, 1200, K_BEFORE, camera, blocked, true);
    const auto K_REPORT = recorder.report();
    const auto& row = K_REPORT.at("frames").at(0);
    CHECK(row.at("frame") == 11);
    CHECK(row.at("before") == row.at("after"));
    CHECK(row.at("text_input") == true);
    CHECK(row.at("events").at(0).at("since_previous_state") == false);
    CHECK(row.at("events").at(1).at("since_previous_state") == true);
    CHECK(row.at("events").at(0).at("event_to_state_ns") == 300);
    CHECK(K_REPORT.at("invalid_timestamps") == 0);
}

TEST_CASE("input timing bounds event storage and exposes truncation in the saved report",
          "[input_latency]") {
    LatencyFixture fixture;
    input_latency_recorder recorder(fixture.m_path);
    const FlyCamera K_CAMERA;
    for (std::size_t index { 0uz }; index < input_latency_recorder::K_MAX_EVENTS + 7uz; ++index) {
        recorder.observe(key_event(10), 20, preferences {}.keys);
        recorder.finish_frame(static_cast<unsigned>(index + 1), 30, K_CAMERA, K_CAMERA, {}, false);
    }
    recorder.write({.video_driver = "synthetic",
                    .device = "synthetic",
                    .resident_splats = 0,
                    .validation_errors = 0});
    std::ifstream saved(fixture.m_path);
    const auto K_REPORT = nlohmann::json::parse(saved);
    CHECK(K_REPORT.at("recorded_events") == input_latency_recorder::K_MAX_EVENTS);
    CHECK(K_REPORT.at("frames").size() == input_latency_recorder::K_MAX_EVENTS);
    CHECK(K_REPORT.at("dropped_events") == 7);
    CHECK(K_REPORT.at("video_driver") == "synthetic");
}

TEST_CASE("input timing output cannot overwrite an existing file", "[input_latency]") {
    LatencyFixture fixture;
    {
        std::ofstream sentinel(fixture.m_path);
        sentinel << "retain caller bytes";
    }
    CHECK_THROWS_AS(input_latency_recorder { fixture.m_path }, std::runtime_error);
    std::ifstream saved(fixture.m_path);
    CHECK(std::string(std::istreambuf_iterator<char>(saved), {}) == "retain caller bytes");
}

TEST_CASE("input timing exposes nonfinite motion instead of certifying null values",
          "[input_latency]") {
    LatencyFixture fixture;
    input_latency_recorder recorder(fixture.m_path);
    SDL_Event motion {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.timestamp = 10;
    motion.motion.xrel = std::numeric_limits<float>::quiet_NaN();
    recorder.observe(motion, 20, preferences {}.keys);
    const FlyCamera K_CAMERA;
    recorder.finish_frame(1, 30, K_CAMERA, K_CAMERA, {}, false);
    const auto K_REPORT = recorder.report();
    CHECK(K_REPORT.at("invalid_values") == 1);
    CHECK(K_REPORT.at("invalid_timestamps") == 0);
    CHECK(K_REPORT.at("frames").at(0).at("events").at(0).at("values_valid") == false);
}
