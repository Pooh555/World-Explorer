#include "job_state.hpp"
#include "world_explorer/application/scenes.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;
TEST_CASE("viewer completion waits for published snapshots after the loader clears busy") {
    viewer::application_state state;
    CHECK_FALSE(state.has_pending_work());
    state.busy = true;
    CHECK(state.has_pending_work());
    const auto scene = make_diagnostic_scene();
    {
        std::lock_guard lock(state.mutex);
        state.loaded = scene;
    }
    // Reproduce the handoff window: loading is complete, UI has not taken the snapshot.
    state.busy = false;
    CHECK(state.has_pending_work());
    scene_snapshot admitted;
    {
        std::lock_guard lock(state.mutex);
        admitted = std::move(state.loaded);
    }
    CHECK(admitted == scene);
    CHECK_FALSE(state.has_pending_work());
}
