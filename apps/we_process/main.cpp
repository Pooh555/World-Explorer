#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void handle_interrupt(int) { interrupted = 1; }
struct signal_guard {
    using handler = void (*)(int);
    handler interrupt{std::signal(SIGINT, handle_interrupt)};
    handler terminate{std::signal(SIGTERM, handle_interrupt)};
    ~signal_guard() {
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, terminate);
    }
};
} // namespace

int main(int argc, char** argv) {
    using namespace world_explorer;
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout
                << "World Explorer (development)\n"
                   "  we_process capabilities | inspect --source VIDEO --job-dir DIR\n"
                   "  we_process run --request JOB.json\n"
                   "  we_process reconstruct --request JOB.json (explicit development recipe)\n"
                   "  we_process fit --source GSPLAT --data COLMAP --video VIDEO --output DIR\n"
                   "                 --steps 7000 --max-image-dimension N --worker-python EXE\n"
                   "  we_process compile PLY OUTPUT.wexplore\n"
                   "  we_process compile-fit RESULT.json OUTPUT.wexplore\n"
                   "  we_process validate SCENE.wexplore\n"
                   "  we_process replay-frames DECODE_STAGE VIDEO (local decoded-image replay)\n"
                   "  we_process fixture OUTPUT.wexplore  (synthetic diagnostic)\n"
                   "Worker overrides: --worker-script PATH --worker-python EXECUTABLE\n";
            return argc < 2 ? 2 : 0;
        }
        const std::string command = argv[1];
        if (command == "compile-fit" && argc == 4) {
            const auto result = compile_fit_scene({.input = argv[2], .destination = argv[3]});
            if (!result) {
                std::cerr << result.error().message << '\n';
                return 2;
            }
            std::cout << "Published validated captured appearance: " << argv[3] << '\n';
            return 0;
        }
        if (command == "compile" && argc == 4) {
            const auto result = compile_splat_scene({.input = argv[2], .destination = argv[3]});
            if (!result) {
                std::cerr << result.error().message << '\n';
                return 2;
            }
            std::cout << "Published validated Gaussian scene: " << argv[3] << '\n';
            return 0;
        }
        if (command == "fixture" && argc == 3) {
            const auto result = write_bundle(argv[2], make_diagnostic_scene());
            if (!result) {
                std::cerr << result.error().message << '\n';
                return 2;
            }
            std::cout << "Synthetic diagnostic scene (not reconstructed): " << argv[2] << '\n';
            return 0;
        }
        if (command == "validate" && argc == 3) {
            const auto result = load_bundle(argv[2]);
            if (!result) {
                std::cerr << result.error().message << '\n';
                return 2;
            }
            std::cout << "Scene " << (*result)->id.value << ", revision "
                      << (*result)->revision.value << ", chunks " << (*result)->chunks.size()
                      << '\n';
            return 0;
        }
        worker_configuration configuration{WE_WORKER_SCRIPT};
        if (command == "fit")
            configuration.script = configuration.script.parent_path() / "gsplat" / "fit.py";
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            if (index == 1 && command == "fit")
                continue;
            const std::string argument = argv[index];
            if ((argument == "--worker-script" || argument == "--worker-python") &&
                index + 1 < argc) {
                if (argument == "--worker-script")
                    configuration.script = argv[++index];
                else
                    configuration.python = argv[++index];
            } else
                arguments.push_back(argument);
        }
        signal_guard signals;
        std::stop_source cancellation;
        std::jthread monitor([&](const std::stop_token& stop) {
            while (!stop.stop_requested() && !interrupted)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (interrupted)
                cancellation.request_stop();
        });
        if (command == "replay-frames") {
            if (argc != 4) {
                std::cerr << "Usage: we_process replay-frames DECODE_STAGE VIDEO\n";
                return 2;
            }
            const auto digest =
                sha256_file(argv[3], 16ULL * 1024 * 1024 * 1024, cancellation.get_token());
            if (!digest) {
                std::cerr << digest.error().message << '\n';
                return digest.error().code == error_code::CANCELLED ? 130 : 2;
            }
            const auto source = open_decoded_frame_source(
                {.stage_directory = argv[2], .source_sha256 = *digest}, cancellation.get_token());
            if (!source) {
                std::cerr << source.error().message << '\n';
                return source.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::size_t count{};
            const auto start = std::chrono::steady_clock::now();
            while (true) {
                const auto event = (*source)->read_next(cancellation.get_token());
                if (!event) {
                    std::cerr << event.error().message << '\n';
                    return event.error().code == error_code::CANCELLED ? 130 : 2;
                }
                if (const auto* frame = std::get_if<frame_packet>(&*event)) {
                    ++count;
                    std::cout << "Frame " << frame->id.value << ", capture_ns "
                              << frame->capture_time.count() << ", PTS "
                              << frame->original_timestamp.pts << " @ "
                              << frame->original_timestamp.time_base_numerator << '/'
                              << frame->original_timestamp.time_base_denominator << ", "
                              << frame->pixels.width << 'x' << frame->pixels.height
                              << " RGB8 sha256 " << sha256(*frame->pixels.bytes) << '\n';
                }
                if (std::holds_alternative<source_end>(*event))
                    break;
            }
            std::cout
                << "Replayed " << count << " committed frames in "
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                << " s; no reconstruction rerun.\n";
            return 0;
        }
        if (command == "reconstruct") {
            if (argc != 4 || std::string(argv[2]) != "--request") {
                std::cerr << "Usage: we_process reconstruct --request JOB.json\n";
                return 2;
            }
            const auto job = read_reconstruction_job(argv[3]);
            if (!job) {
                std::cerr << job.error().message << '\n';
                return 2;
            }
            const auto completed =
                reconstruct(*job, cancellation.get_token(),
                            [](const std::string& output) { std::cout << output << std::flush; });
            if (!completed) {
                std::cerr << completed.error().message << '\n';
                return completed.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Published validated captured appearance: " << completed->string() << '\n';
            return 0;
        }
        const auto result =
            run_worker(configuration, arguments, cancellation.get_token(),
                       [](const std::string& output) { std::cout << output << std::flush; });
        if (!result) {
            std::cerr << result.error().message << '\n';
            return result.error().code == error_code::CANCELLED ? 130 : 2;
        }
        return *result;
    } catch (const std::exception& error) {
        std::cerr << "Operation failed: " << error.what() << '\n';
        return 2;
    }
}
