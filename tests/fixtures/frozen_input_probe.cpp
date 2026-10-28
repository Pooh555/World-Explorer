#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <iostream>
#include <variant>

int main(int argc, char** argv) {
    using namespace world_explorer;
    if (argc != 6) {
        return 64;
    }
    const BatchPoseConfig K_CONFIG { .m_decode_stage = argv[1],
                                    .m_job_directory = argv[4],
                                    .m_worker_script = argv[5],
                                    .m_source_sha256 = argv[2],
                                    .m_frozen_selection_sha256 = argv[3] };
    auto estimator = make_batch_pose_estimator(K_CONFIG);
    auto source =
        open_decoded_frame_source({ .m_stage_directory = argv[1], .m_source_sha256 = argv[2] });
    if (!estimator || !source) {
        std::cerr << (!estimator ? estimator.error().m_message : source.error().m_message) << '\n';
        return 2;
    }
    const auto K_CAPACITY = (*estimator)->capabilities().m_observation_capacity;
    const auto K_STARTED = (*estimator)->start({ { 64uz * 1024uz * 1024uz }, K_CAPACITY });
    if (!K_STARTED) {
        std::cerr << K_STARTED.error().m_message << '\n';
        return 2;
    }
    std::uint64_t operation {};
    bool reset {};
    while (true) {
        auto event = (*source)->read_next({});
        if (!event) {
            std::cerr << event.error().m_message << '\n';
            return 2;
        }
        if (std::holds_alternative<SourceEnd>(*event)) {
            break;
        }
        if (std::holds_alternative<SourceReset>(*event)) {
            if (reset || operation != 0) {
                return 2;
            }
            reset = true;
            continue;
        }
        auto* frame = std::get_if<FramePacket>(&*event);
        if (!frame || !reset || operation >= K_CAPACITY) {
            return 2;
        }
        FrameObservation observation { { ++operation }, std::move(*frame) };
        const auto K_ADMITTED = (*estimator)->submit(observation);
        if (!K_ADMITTED || observation.m_frame.m_pixels.m_bytes) {
            std::cerr << (!K_ADMITTED ? K_ADMITTED.error().m_message : "Unconsumed RGB lease")
                      << '\n';
            return 2;
        }
    }
    if (operation != K_CAPACITY) {
        return 2;
    }
    // Deliberately do not call finish(): this verifies persisted input admission,
    // without sealing a journal or launching a solver/optimizer.
    std::cout << "Persisted " << operation << " frozen inference RGB frames; no solver launched\n";
    return 0;
}
