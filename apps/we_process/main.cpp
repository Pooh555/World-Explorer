#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/scenes.hpp"
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
                   "  we_process fit --source GSPLAT --data COLMAP --video VIDEO --output DIR\n"
                   "                 --steps 7000 --max-image-dimension N --worker-python EXE\n"
                   "  we_process compile PLY OUTPUT.wexplore\n"
                   "  we_process compile-fit RESULT.json OUTPUT.wexplore\n"
                   "  we_process validate SCENE.wexplore\n"
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
        std::jthread monitor([&](std::stop_token stop) {
            while (!stop.stop_requested() && !interrupted)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (interrupted)
                cancellation.request_stop();
        });
        const auto result =
            run_worker(configuration, arguments, cancellation.get_token(),
                       [](std::string output) { std::cout << output << std::flush; });
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
