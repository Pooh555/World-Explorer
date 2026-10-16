#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/map_commands.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/application/pose_solve.hpp"
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
                   "  we_process replay-poses POSE_JOB VIDEO (completed-output replay; no solve)\n"
                   "  we_process export-poses POSE_JOB VIDEO OUTPUT.weposes [--decode-stage DIR]\n"
                   "  we_process prepare-poses POSE_JOB VIDEO MAP_DIRECTORY [--decode-stage DIR]\n"
                   "  we_process inspect-poses INPUT.weposes SHA256 (checked JSON inspection)\n"
                   "  we_process inspect-depth INPUT.wedepth SHA256 (format inspection only)\n"
                   "  we_process prepare-dense POSE_JOB SOURCE_SHA POSES.weposes POSE_SHA "
                   "RECIPE.json RECIPE_SHA OUTPUT.json [--decode-stage DIR]\n"
                   "  we_process init-map MAP_DIRECTORY\n"
                   "  we_process import-map INPUT.weposes SHA256 MAP_DIRECTORY\n"
                   "  we_process inspect-map MAP_DIRECTORY (checked restart/recovery)\n"
                   "  we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE\n"
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
        if (command == "init-map" || command == "import-map" || command == "inspect-map") {
            if (argc != (command == "import-map" ? 5 : 3)) {
                std::cerr << "Usage: init-map DIR | import-map INPUT.weposes SHA256 DIR | "
                             "inspect-map DIR\n";
                return 2;
            }
            const auto snapshot =
                command == "init-map" ? initialize_persistent_map(argv[2], cancellation.get_token())
                : command == "import-map"
                    ? import_canonical_pose_map(argv[2], argv[3], argv[4], cancellation.get_token())
                    : inspect_persistent_map(argv[2], cancellation.get_token());
            if (!snapshot) {
                std::cerr << snapshot.error().message << '\n';
                return snapshot.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Persistent map: epoch " << (*snapshot)->epoch.value << ", revision "
                      << (*snapshot)->revision.value << ", " << (*snapshot)->observations.size()
                      << " observations, " << (*snapshot)->poses.size() << " poses, "
                      << (*snapshot)->calibrations.size() << " calibrations; scale arbitrary\n";
            if ((*snapshot)->initial_pose_asset_sha256)
                std::cout << "Initial canonical pose asset SHA-256 "
                          << *(*snapshot)->initial_pose_asset_sha256 << '\n';
            return 0;
        }
        if (command == "prepare-dense") {
            if (argc != 9 && !(argc == 11 && std::string_view(argv[9]) == "--decode-stage")) {
                std::cerr << "Usage: we_process prepare-dense POSE_JOB SOURCE_SHA POSES.weposes "
                             "POSE_SHA RECIPE.json RECIPE_SHA OUTPUT.json [--decode-stage DIR]\n";
                return 2;
            }
            const dense_input_request request{
                {argv[2], argv[3], argc == 11 ? argv[10] : std::filesystem::path{}},
                argv[4],
                argv[5],
                argv[6],
                argv[7]};
            const auto published = write_dense_inputs(request, argv[8], cancellation.get_token());
            if (!published) {
                std::cerr << published.error().message << '\n';
                return published.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Dense input manifest (native grid; arbitrary scale; no depth produced): "
                      << argv[8] << "\nSHA-256 " << *published << '\n';
            return 0;
        }
        if (command == "prepare-poses") {
            if (argc != 5 && !(argc == 7 && std::string_view(argv[5]) == "--decode-stage")) {
                std::cerr << "Usage: we_process prepare-poses POSE_JOB VIDEO MAP_DIRECTORY "
                             "[--decode-stage DIR]\n";
                return 2;
            }
            const auto prepared = prepare_completed_pose_map(
                {{argv[2], argv[3], argc == 7 ? argv[6] : std::filesystem::path{}}, argv[4]},
                cancellation.get_token(),
                [](const std::string& text) { std::cout << text << std::flush; });
            if (!prepared) {
                std::cerr << prepared.error().message << '\n';
                return prepared.error().code == error_code::CANCELLED ? 130 : 2;
            }
            const auto snapshot = prepared->store->acquire_snapshot();
            std::cout << "Durable pose map: " << snapshot->observations.size() << " observations, "
                      << snapshot->poses.size() << " poses; epoch " << snapshot->epoch.value
                      << ", revision " << snapshot->revision.value << "; canonical SHA256 "
                      << prepared->asset_sha256 << '\n';
            return 0;
        }
        if (command == "export-poses") {
            if (argc != 5 && !(argc == 7 && std::string_view(argv[5]) == "--decode-stage")) {
                std::cerr << "Usage: we_process export-poses POSE_JOB VIDEO OUTPUT.weposes "
                             "[--decode-stage DIR]\n";
                return 2;
            }
            const auto hash = export_completed_pose_asset(
                {{argv[2], argv[3], argc == 7 ? argv[6] : std::filesystem::path{}}, argv[4]},
                cancellation.get_token());
            if (!hash) {
                std::cerr << hash.error().message << '\n';
                return hash.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Canonical poses (arbitrary scale, unknown gravity): " << argv[4]
                      << "\nSHA-256 " << *hash << '\n';
            return 0;
        }
        if (command == "inspect-depth") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-depth INPUT.wedepth SHA256\n";
                return 2;
            }
            const auto summary = inspect_depth_file(argv[2], argv[3], {}, cancellation.get_token());
            if (!summary) {
                std::cerr << summary.error().message << '\n';
                return summary.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << *summary << '\n';
            return 0;
        }
        if (command == "inspect-poses") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-poses INPUT.weposes SHA256\n";
                return 2;
            }
            const auto asset = read_pose_asset(argv[2], argv[3], {}, cancellation.get_token());
            if (!asset) {
                std::cerr << asset.error().message << '\n';
                return asset.error().code == error_code::CANCELLED ? 130 : 2;
            }
            const auto json = inspect_pose_asset(*asset);
            if (!json) {
                std::cerr << json.error().message << '\n';
                return 2;
            }
            std::cout << *json << '\n';
            return 0;
        }
        if (command == "solve-poses") {
            if (arguments.size() < 6 || arguments.size() % 2 != 0) {
                std::cerr << "Usage: we_process solve-poses DECODE_STAGE VIDEO NEW_JOB "
                             "--pose-python EXECUTABLE [--recipe sequential|exhaustive_guided]\n";
                return 2;
            }
            pose_solve_request request{arguments[2],
                                       {.decode_stage = arguments[1],
                                        .job_directory = arguments[3],
                                        .worker_script = configuration.script,
                                        .worker_python = configuration.python}};
            bool pose_python{};
            for (std::size_t index = 4; index < arguments.size(); index += 2) {
                if (arguments[index] == "--pose-python" && !pose_python) {
                    request.batch.pose_python = arguments[index + 1];
                    pose_python = true;
                } else if (arguments[index] == "--recipe" &&
                           arguments[index + 1] == "exhaustive_guided")
                    request.batch.recipe = pose_recipe::EXHAUSTIVE_GUIDED;
                else if (arguments[index] != "--recipe" || arguments[index + 1] != "sequential") {
                    std::cerr << "Invalid batch pose option\n";
                    return 2;
                }
            }
            if (!pose_python) {
                std::cerr << "An explicit --pose-python executable is required\n";
                return 2;
            }
            const auto maps = solve_selected_pose_map(
                std::move(request), cancellation.get_token(),
                [](const std::string& text) { std::cout << text << std::flush; });
            if (!maps) {
                std::cerr << maps.error().message << '\n';
                return maps.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Fresh batch map: " << (*maps)->observations.size() << " observations, "
                      << (*maps)->poses.size() << " poses, " << (*maps)->calibrations.size()
                      << " calibrations\n";
            return 0;
        }
        if (command == "replay-poses") {
            if (argc != 4 && !(argc == 6 && std::string_view(argv[4]) == "--decode-stage")) {
                std::cerr << "Usage: we_process replay-poses POSE_JOB VIDEO [--decode-stage DIR]\n";
                return 2;
            }
            const auto maps = replay_completed_pose_map(
                {.pose_job = argv[2],
                 .video = argv[3],
                 .decode_stage = argc == 6 ? argv[5] : std::filesystem::path{}},
                cancellation.get_token(),
                [](const std::string& text) { std::cout << text << std::flush; });
            if (!maps) {
                std::cerr << maps.error().message << '\n';
                return maps.error().code == error_code::CANCELLED ? 130 : 2;
            }
            std::cout << "Completed-pose replay: " << (*maps)->observations.size()
                      << " observations, " << (*maps)->poses.size() << " poses, "
                      << (*maps)->calibrations.size() << " calibrations; epoch "
                      << (*maps)->epoch.value << ", revision " << (*maps)->revision.value << '\n';
            return 0;
        }
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
