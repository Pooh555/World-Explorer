#include "world_explorer/application/capsule_session.hpp"
#include "world_explorer/application/collision_inspection.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/corpus_inspection.hpp"
#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/depth_space.hpp"
#include "world_explorer/application/depth_visibility.hpp"
#include "world_explorer/application/external_model_inspection.hpp"
#include "world_explorer/application/fusion_assets.hpp"
#include "world_explorer/application/fusion_cleanup.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/application/geometry_job.hpp"
#include "world_explorer/application/gltf.hpp"
#include "world_explorer/application/manual_actions.hpp"
#include "world_explorer/application/map_commands.hpp"
#include "world_explorer/application/mesh_color_audit.hpp"
#include "world_explorer/application/mesh_texture.hpp"
#include "world_explorer/application/native_collider_diagnostic.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/application/pose_solve.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

volatile std::sig_atomic_t interrupted = 0;

void handle_interrupt(int) {
    interrupted = 1;
}

struct SignalGuard {
    using Handler = void (*)(int);
    Handler m_interrupt { std::signal(SIGINT, handle_interrupt) };
    Handler m_terminate { std::signal(SIGTERM, handle_interrupt) };

    ~SignalGuard() {
        std::signal(SIGINT, m_interrupt);
        std::signal(SIGTERM, m_terminate);
    }
};

}  // namespace

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
                   "  we_process inspect-depth INPUT.wedepth SHA256 [--max-dimension N] (format "
                   "inspection only)\n"
                   "  we_process inspect-fusion-scene REQUEST.json SHA256\n"
                   "  we_process compile-fusion REQUEST.json SHA256 NEW_SCENE.wexplore\n"
                   "  we_process adopt-geometry REQUEST.json SHA256 NEW_NATIVE_JOB\n"
                   "  we_process run-geometry REQUEST.json SHA256 NEW_PIPELINE_JOB\n"
                   "  we_process audit-mesh-colors REQUEST.json SHA256 NEW_DIRECTORY\n"
                   "  we_process bake-mesh-colors REQUEST.json SHA256 NEW_SCENE.wexplore\n"
                   "  we_process align-world SOURCE.wexplore MEASUREMENT.json SHA256 "
                   "NEW_SCENE.wexplore\n"
                   "  we_process inspect-collision REQUEST.json SHA256 NEW_DIRECTORY\n"
                   "  we_process capsule-session REQUEST.json SHA256 FORWARD RIGHT FRAME_NS "
                   "FRAMES\n"
                   "  we_process inspect-depth-space REQUEST.json SHA256 NEW_DIRECTORY\n"
                   "  we_process check-depth-space SOURCE SOURCE_SHA DIRECTORY REPORT_SHA "
                   "REQUEST_SHA\n"
                   "  we_process inspect-baked SCENE.wexplore\n"
                   "  we_process export-glb SCENE.wexplore NEW_DIRECTORY\n"
                   "  we_process import-glb FILE.glb SHA256 NEW_SCENE.wexplore\n"
                   "  we_process inspect-fusion INPUT.wefusion SHA MANIFEST_SHA RECEIPT_SHA "
                   "RECIPE_SHA POSES POSE_SHA\n"
                   "  we_process analyze-fusion INPUT.wefusion SHA MANIFEST_SHA RECEIPT_SHA "
                   "RECIPE_SHA POSES POSE_SHA (read-only component/support diagnostic)\n"
                   "  we_process cleanup-fusion INPUT.wefusion SHA MANIFEST_SHA RECEIPT_SHA "
                   "RECIPE_SHA POSES POSE_SHA NEW_DIRECTORY\n"
                   "  we_process export-depth-visibility REQUEST.json SHA256 NEW_DIRECTORY\n"
                   "  we_process prepare-dense POSE_JOB SOURCE_SHA POSES.weposes POSE_SHA "
                   "RECIPE.json RECIPE_SHA OUTPUT.json [--decode-stage DIR]\n"
                   "  we_process check-resized-dense POSE_JOB SOURCE_SHA POSES.weposes POSE_SHA "
                   "NATIVE_RECIPE NATIVE_RECIPE_SHA RESIZED_RECIPE RESIZED_RECIPE_SHA "
                   "MANIFEST MANIFEST_SHA [--decode-stage DIR]\n"
                   "  we_process init-map MAP_DIRECTORY\n"
                   "  we_process import-map INPUT.weposes SHA256 MAP_DIRECTORY\n"
                   "  we_process inspect-map MAP_DIRECTORY (checked restart/recovery)\n"
                   "  we_process inspect-actions JOURNAL GENERATION MANIFEST_SHA (declared "
                   "actions)\n"
                   "  we_process inspect-corpus REQUEST.json SHA256 (read-only frozen metadata)\n"
                   "  we_process inspect-external-models REQUEST.json SHA256 (read-only "
                   "observations)\n"
                   "  we_process inspect-colliders REQUEST.json SHA256 (synthetic static ray "
                   "queries)\n"
                   "  we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE\n"
                   "  we_process fixture OUTPUT.wexplore  (synthetic diagnostic)\n"
                   "Worker overrides: --worker-script PATH --worker-python EXECUTABLE\n";
            return argc < 2 ? 2 : 0;
        }
        const std::string K_COMMAND = argv[1];
        if (K_COMMAND == "inspect-actions") {
            if (argc != 5) {
                std::cerr << "Usage: we_process inspect-actions JOURNAL GENERATION MANIFEST_SHA\n";
                return 2;
            }
            RecordCommit selected;
            const std::string_view K_TEXT(argv[3uz]);
            const auto K_PARSED = std::from_chars(
                K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), selected.m_generation);
            if (K_PARSED.ec != std::errc {} || K_PARSED.ptr != K_TEXT.data() + K_TEXT.size()) {
                std::cerr << "Invalid journal generation\n";
                return 2;
            }
            selected.m_manifest_sha256 = argv[4uz];
            const auto K_JOURNAL = read_manual_action_journal(argv[2uz], selected);
            if (!K_JOURNAL) {
                std::cerr << K_JOURNAL.error().m_message << '\n';
                return 2;
            }
            const auto& accounting = K_JOURNAL->accounting();
            std::cout << "Declared manual actions; actor/time/action completion unverified\n"
                      << "Generation " << K_JOURNAL->commit().m_generation << '\n'
                      << "Setup: " << accounting.m_setup_count << " actions, "
                      << accounting.m_setup_active_ms << " active ms\n"
                      << "Evaluation only: " << accounting.m_evaluation_count << " actions, "
                      << accounting.m_evaluation_active_ms << " active ms\n"
                      << "Output assistance: " << accounting.m_assistance_count << " actions, "
                      << accounting.m_assistance_active_ms << " active ms\n";
            return 0;
        }
        if (K_COMMAND == "compile-fit" && argc == 4) {
            const auto K_RESULT = compile_fit_scene({ .m_input = argv[2], .m_destination = argv[3] });
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << "Published validated captured appearance: " << argv[3] << '\n';
            return 0;
        }
        if (K_COMMAND == "compile" && argc == 4) {
            const auto K_RESULT =
                compile_splat_scene({ .m_input = argv[2], .m_destination = argv[3] });
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << "Published validated Gaussian scene: " << argv[3] << '\n';
            return 0;
        }
        if (K_COMMAND == "fixture" && argc == 3) {
            const auto K_RESULT = write_bundle(argv[2], make_diagnostic_scene());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << "Synthetic diagnostic scene (not reconstructed): " << argv[2] << '\n';
            return 0;
        }
        if (K_COMMAND == "validate" && argc == 3) {
            const auto K_RESULT = load_bundle(argv[2]);
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << "Scene " << (*K_RESULT)->m_id.m_value << ", revision "
                      << (*K_RESULT)->m_revision.m_value << ", chunks "
                      << (*K_RESULT)->m_chunks.size() << '\n'
                      << "Scene SHA-256: " << scene_content_hash(**K_RESULT) << '\n';
            return 0;
        }
        WorkerConfiguration configuration { WE_WORKER_SCRIPT };
        if (K_COMMAND == "fit") {
            configuration.m_script = configuration.m_script.parent_path() / "gsplat" / "fit.py";
        }
        std::vector<std::string> arguments;
        for (int index { 1 }; index < argc; ++index) {
            if (index == 1 && K_COMMAND == "fit") {
                continue;
            }
            const std::string K_ARGUMENT = argv[index];
            if ((K_ARGUMENT == "--worker-script" || K_ARGUMENT == "--worker-python") &&
                index + 1 < argc) {
                if (K_ARGUMENT == "--worker-script") {
                    configuration.m_script = argv[++index];
                } else {
                    configuration.m_python = argv[++index];
                }
            } else {
                arguments.push_back(K_ARGUMENT);
            }
        }
        SignalGuard signals;
        std::stop_source cancellation;
        std::jthread monitor([&](const std::stop_token& stop) {
            while (!stop.stop_requested() && !interrupted) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (interrupted) {
                cancellation.request_stop();
            }
        });
        if (K_COMMAND == "inspect-external-models") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-external-models REQUEST.json SHA256\n";
                return 2;
            }
            const auto K_REPORT = inspect_asset_external_models_request(
                argv[2uz], argv[3uz], cancellation.get_token());
            if (!K_REPORT) {
                std::cerr << K_REPORT.error().m_message << '\n';
                return 2;
            }
            std::cout << *K_REPORT << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-colliders") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-colliders REQUEST.json SHA256\n";
                return 2;
            }
            const auto K_REPORT =
                inspect_native_colliders_request(argv[2uz], argv[3uz], cancellation.get_token());
            if (!K_REPORT) {
                std::cerr << K_REPORT.error().m_message << '\n';
                return 2;
            }
            std::cout << *K_REPORT << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-corpus") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-corpus REQUEST.json SHA256\n";
                return 2;
            }
            const auto K_CORPUS =
                inspect_asset_corpus_request(argv[2uz], argv[3uz], cancellation.get_token());
            if (!K_CORPUS) {
                std::cerr << K_CORPUS.error().m_message << '\n';
                return 2;
            }
            const auto& definition = K_CORPUS->definition();
            const std::array K_TRACKS { "SYNTHETIC", "PILOT", "RELEASE" };
            std::cout << "Frozen asset corpus metadata; no execution or AP qualification\n"
                      << "Track: " << K_TRACKS.at(static_cast<std::size_t>(definition.m_track))
                      << '\n'
                      << "Corpus: " << definition.m_corpus << ", revision " << definition.m_revision
                      << ", captures " << definition.m_captures.size() << ", recipes "
                      << definition.m_recipes.size() << '\n'
                      << "SHA-256: " << K_CORPUS->reference().m_sha256 << '\n';
            for (const auto& blocker : K_CORPUS->blockers()) {
                std::cout << blocker.m_scope << ": " << blocker.m_code << '\n';
            }
            return 0;
        }
        if (K_COMMAND == "init-map" || K_COMMAND == "import-map" || K_COMMAND == "inspect-map") {
            if (argc != (K_COMMAND == "import-map" ? 5 : 3)) {
                std::cerr << "Usage: init-map DIR | import-map INPUT.weposes SHA256 DIR | "
                             "inspect-map DIR\n";
                return 2;
            }
            const auto K_SNAPSHOT =
                K_COMMAND == "init-map"
                    ? initialize_persistent_map(argv[2], cancellation.get_token())
                : K_COMMAND == "import-map"
                    ? import_canonical_pose_map(argv[2], argv[3], argv[4], cancellation.get_token())
                    : inspect_persistent_map(argv[2], cancellation.get_token());
            if (!K_SNAPSHOT) {
                std::cerr << K_SNAPSHOT.error().m_message << '\n';
                return K_SNAPSHOT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Persistent map: epoch " << (*K_SNAPSHOT)->m_epoch.m_value << ", revision "
                      << (*K_SNAPSHOT)->m_revision.m_value << ", "
                      << (*K_SNAPSHOT)->m_observations.size() << " observations, "
                      << (*K_SNAPSHOT)->m_poses.size() << " poses, "
                      << (*K_SNAPSHOT)->m_calibrations.size() << " calibrations; scale arbitrary\n";
            if ((*K_SNAPSHOT)->m_initial_pose_asset_sha256) {
                std::cout << "Initial canonical pose asset SHA-256 "
                          << *(*K_SNAPSHOT)->m_initial_pose_asset_sha256 << '\n';
            }
            return 0;
        }
        if (K_COMMAND == "check-depth-space") {
            if (argc != 7) {
                std::cerr << "Usage: check-depth-space SOURCE SOURCE_SHA DIRECTORY REPORT_SHA "
                             "REQUEST_SHA\n";
                return 2;
            }
            const auto K_EVIDENCE = open_depth_space({ argv[2], argv[4], argv[3], argv[5], argv[6] },
                                                     cancellation.get_token());
            if (!K_EVIDENCE) {
                std::cerr << K_EVIDENCE.error().m_message << '\n';
                return K_EVIDENCE.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            const auto K_COUNTS = (*K_EVIDENCE)->categories();
            std::cout << "Admitted " << (*K_EVIDENCE)->frames().size() << " views, "
                      << (*K_EVIDENCE)->cells().size() << " cells: unknown " << K_COUNTS[0uz]
                      << ", front-only " << K_COUNTS[1uz] << ", surface/behind " << K_COUNTS[2uz]
                      << ", conflict " << K_COUNTS[3uz] << "; walking unavailable\n";
            return 0;
        }
        if (K_COMMAND == "inspect-depth-space") {
            if (argc != 5) {
                std::cerr << "Usage: inspect-depth-space REQUEST.json SHA256 NEW_DIRECTORY\n";
                return 2;
            }
            const auto K_RESULT =
                inspect_depth_space(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published observed-depth space diagnostic; scale arbitrary, walking "
                         "unavailable\n";
            return 0;
        }
        if (K_COMMAND == "capsule-session") {
            if (argc != 8) {
                std::cerr
                    << "Usage: capsule-session REQUEST.json SHA256 FORWARD RIGHT FRAME_NS FRAMES\n";
                return 2;
            }
            const auto K_NUMBER = []<class Number>(const char* text, Number& value) {
                const auto K_END = text + std::char_traits<char>::length(text);
                const auto K_PARSED = std::from_chars(text, K_END, value);
                return K_PARSED.ec == std::errc {} && K_PARSED.ptr == K_END;
            };
            FlyInput input;
            input.m_captured = true;
            std::int64_t frame_ns {};
            std::size_t frames {};
            if (!K_NUMBER(argv[4], input.m_forward) || !K_NUMBER(argv[5], input.m_right) ||
                !K_NUMBER(argv[6], frame_ns) || !K_NUMBER(argv[7], frames)) {
                std::cerr << "Invalid capsule replay numeric argument\n";
                return 2;
            }
            const auto K_RESULT = replay_capsule_session(argv[2],
                                                         argv[3],
                                                         input,
                                                         std::chrono::nanoseconds(frame_ns),
                                                         frames,
                                                         cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << *K_RESULT << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-collision") {
            if (argc != 5) {
                std::cerr << "Usage: inspect-collision REQUEST.json SHA256 NEW_DIRECTORY\n";
                return 2;
            }
            const auto K_RESULT =
                inspect_collision_support(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout << "Published synthetic support diagnostic; measured collision and walking "
                         "unavailable\n";
            return 0;
        }
        if (K_COMMAND == "align-world") {
            if (argc != 6) {
                std::cerr << "Usage: align-world SOURCE MEASUREMENT.json SHA256 NEW_BUNDLE\n";
                return 2;
            }
            const auto K_RESULT =
                align_world_scene({ argv[2], argv[3], argv[4], argv[5] }, cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return 2;
            }
            std::cout
                << "Published declared world alignment; geometry unverified; walking disabled\n";
            return 0;
        }
        if (K_COMMAND == "export-glb" || K_COMMAND == "import-glb") {
            const bool K_EXPORT = K_COMMAND == "export-glb";
            if (argc != (K_EXPORT ? 4 : 5)) {
                std::cerr << "Usage: export-glb BUNDLE NEW_DIRECTORY | import-glb FILE SHA256 "
                             "NEW_BUNDLE\n";
                return 2;
            }
            const auto K_RESULT =
                K_EXPORT ? export_baked_glb_bundle({ argv[2], argv[3] }, cancellation.get_token())
                         : import_baked_glb(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout
                << "Captured unlit GLB conversion complete; scale arbitrary; walking unavailable\n";
            return 0;
        }
        if (K_COMMAND == "inspect-baked" && argc == 3) {
            const auto K_SCENE = load_bundle(argv[2]);
            if (!K_SCENE) {
                std::cerr << K_SCENE.error().m_message << '\n';
                return 2;
            }
            for (const auto& [K_ID, K_CHUNK] : (*K_SCENE)->m_chunks) {
                static_cast<void>(K_ID);
                if (K_CHUNK->m_mesh && K_CHUNK->m_mesh->m_baked) {
                    std::cout << K_CHUNK->m_mesh->m_baked->m_manifest_json << '\n';
                    return 0;
                }
            }
            std::cerr << "Scene has no baked appearance\n";
            return 2;
        }
        if (K_COMMAND == "bake-mesh-colors") {
            if (argc != 5) {
                std::cerr << "Usage: we_process bake-mesh-colors REQUEST.json SHA256 "
                             "NEW_SCENE.wexplore\n";
                return 2;
            }
            const auto K_RESULT =
                bake_mesh_texture(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published baked captured color; arbitrary scale; unknown texels masked; "
                         "walking unavailable\n";
            return 0;
        }
        if (K_COMMAND == "audit-mesh-colors") {
            if (argc != 5) {
                std::cerr
                    << "Usage: we_process audit-mesh-colors REQUEST.json SHA256 NEW_DIRECTORY\n";
                return 2;
            }
            const auto K_RESULT =
                audit_mesh_colors(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Color projection diagnostic published: " << *K_RESULT
                      << "; texture export and walking unavailable\n";
            return 0;
        }
        if (K_COMMAND == "adopt-geometry" || K_COMMAND == "run-geometry") {
            if (argc != 5) {
                std::cerr << "Usage: we_process " << K_COMMAND << " REQUEST.json SHA256 NEW_JOB\n";
                return 2;
            }
            const auto K_RUN = K_COMMAND == "run-geometry" ? run_geometry_job : adopt_geometry_job;
            const auto K_RESULT = K_RUN(
                argv[2], argv[3], argv[4], cancellation.get_token(), [](const std::string& output) {
                    std::cerr << output << std::flush;
                });
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published partial observed geometry; walking disabled: " << *K_RESULT
                      << '\n';
            return 0;
        }
        if (K_COMMAND == "compile-fusion") {
            if (argc != 5) {
                std::cerr << "Usage: we_process compile-fusion REQUEST.json SHA256 "
                             "NEW_SCENE.wexplore\n";
                return 2;
            }
            const auto K_RESULT =
                compile_fusion_scene(argv[2], argv[3], argv[4], cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published partial observed geometry; arbitrary units; walking disabled: "
                      << argv[4] << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-fusion-scene") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-fusion-scene REQUEST.json SHA256\n";
                return 2;
            }
            const auto K_SCENE =
                load_fusion_scene_request(argv[2], argv[3], cancellation.get_token());
            if (!K_SCENE) {
                std::cerr << K_SCENE.error().m_message << '\n';
                return 2;
            }
            const auto& chunk = (*K_SCENE)->m_chunks.begin()->second;
            if (!chunk->m_mesh) {
                throw std::runtime_error("Admitted mesh scene is missing its payload");
            }
            const auto& mesh = *chunk->m_mesh;
            std::cout << "Partial observed geometry; arbitrary units; walking disabled\n"
                      << "Scene SHA-256: " << scene_content_hash(**K_SCENE) << '\n'
                      << "Vertices: " << mesh.m_vertices.size()
                      << " triangles: " << mesh.m_triangles.size() << '\n';
            return 0;
        }
        if (K_COMMAND == "cleanup-fusion") {
            if (argc != 10) {
                std::cerr << "Usage: we_process cleanup-fusion INPUT.wefusion SHA MANIFEST_SHA "
                             "RECEIPT_SHA RECIPE_SHA POSES POSE_SHA NEW_DIRECTORY\n";
                return 2;
            }
            const auto K_RESULT = export_fusion_cleanup(
                { argv[2], argv[7], argv[3], argv[8], argv[4], argv[5], argv[6] },
                argv[9],
                cancellation.get_token());
            if (!K_RESULT) {
                std::cerr << K_RESULT.error().m_message << '\n';
                return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published partial observed cleanup; manifest SHA-256 " << *K_RESULT
                      << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-fusion" || K_COMMAND == "analyze-fusion") {
            if (argc != 9) {
                std::cerr << "Usage: we_process " << K_COMMAND
                          << " INPUT.wefusion SHA MANIFEST_SHA "
                             "RECEIPT_SHA RECIPE_SHA POSES POSE_SHA\n";
                return 2;
            }
            const auto K_ASSET = load_pose_bound_fusion(
                { argv[2], argv[7], argv[3], argv[8], argv[4], argv[5], argv[6] },
                {},
                cancellation.get_token());
            if (!K_ASSET) {
                std::cerr << K_ASSET.error().m_message << '\n';
                return 2;
            }
            const auto K_SUMMARY =
                K_COMMAND == "analyze-fusion"
                    ? inspect_fusion_components(*K_ASSET, cancellation.get_token())
                    : inspect_fusion_asset(*K_ASSET);
            if (!K_SUMMARY) {
                std::cerr << K_SUMMARY.error().m_message << '\n';
                return 2;
            }
            std::cout << *K_SUMMARY << '\n';
            return 0;
        }
        if (K_COMMAND == "export-depth-visibility") {
            if (argc != 5) {
                std::cerr << "Usage: we_process export-depth-visibility REQUEST.json SHA256 "
                             "NEW_DIRECTORY\n";
                return 2;
            }
            const auto K_EXPORTED = export_depth_visibility_cache(
                argv[2uz], argv[3uz], argv[4uz], cancellation.get_token());
            if (!K_EXPORTED) {
                std::cerr << K_EXPORTED.error().m_message << '\n';
                return K_EXPORTED.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Pose-admitted depth visibility cache; scale arbitrary; no "
                         "fusion/walking\nSHA-256 "
                      << *K_EXPORTED << '\n';
            return 0;
        }
        if (K_COMMAND == "check-resized-dense") {
            if (argc != 12 && !(argc == 14 && std::string_view(argv[12uz]) == "--decode-stage")) {
                std::cerr
                    << "Usage: we_process check-resized-dense POSE_JOB SOURCE_SHA POSES.weposes "
                       "POSE_SHA NATIVE_RECIPE NATIVE_RECIPE_SHA RESIZED_RECIPE RESIZED_RECIPE_SHA "
                       "MANIFEST MANIFEST_SHA [--decode-stage DIR]\n";
                return 2;
            }
            const ResizedDenseInputRequest K_REQUEST {
                { { argv[2uz], argv[3uz], argc == 14 ? argv[13uz] : std::filesystem::path {} },
                 argv[4uz],
                 argv[5uz],
                 argv[6uz],
                 argv[7uz] },
                argv[8uz],
                argv[9uz] };
            const auto K_PREPARED = load_resized_dense_inputs(
                K_REQUEST, argv[10uz], argv[11uz], cancellation.get_token());
            if (!K_PREPARED) {
                std::cerr << K_PREPARED.error().m_message << '\n';
                return K_PREPARED.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Checked resized dense inputs: " << K_PREPARED->m_frames.size()
                      << " frames; arbitrary scale; no depth produced\n";
            return 0;
        }
        if (K_COMMAND == "prepare-dense") {
            if (argc != 9 && !(argc == 11 && std::string_view(argv[9]) == "--decode-stage")) {
                std::cerr << "Usage: we_process prepare-dense POSE_JOB SOURCE_SHA POSES.weposes "
                             "POSE_SHA RECIPE.json RECIPE_SHA OUTPUT.json [--decode-stage DIR]\n";
                return 2;
            }
            const DenseInputRequest K_REQUEST {
                { argv[2], argv[3], argc == 11 ? argv[10] : std::filesystem::path {} },
                argv[4],
                argv[5],
                argv[6],
                argv[7] };
            const auto K_PUBLISHED =
                write_dense_inputs(K_REQUEST, argv[8], cancellation.get_token());
            if (!K_PUBLISHED) {
                std::cerr << K_PUBLISHED.error().m_message << '\n';
                return K_PUBLISHED.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Dense input manifest (native grid; arbitrary scale; no depth produced): "
                      << argv[8] << "\nSHA-256 " << *K_PUBLISHED << '\n';
            return 0;
        }
        if (K_COMMAND == "prepare-poses") {
            if (argc != 5 && !(argc == 7 && std::string_view(argv[5]) == "--decode-stage")) {
                std::cerr << "Usage: we_process prepare-poses POSE_JOB VIDEO MAP_DIRECTORY "
                             "[--decode-stage DIR]\n";
                return 2;
            }
            const auto K_PREPARED = prepare_completed_pose_map(
                { { argv[2], argv[3], argc == 7 ? argv[6] : std::filesystem::path {} }, argv[4] },
                cancellation.get_token(),
                [](const std::string& text) {
                    std::cout << text << std::flush;
                });
            if (!K_PREPARED) {
                std::cerr << K_PREPARED.error().m_message << '\n';
                return K_PREPARED.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            const auto K_SNAPSHOT = K_PREPARED->m_store->acquire_snapshot();
            std::cout << "Durable pose map: " << K_SNAPSHOT->m_observations.size()
                      << " observations, " << K_SNAPSHOT->m_poses.size() << " poses; epoch "
                      << K_SNAPSHOT->m_epoch.m_value << ", revision "
                      << K_SNAPSHOT->m_revision.m_value << "; canonical SHA256 "
                      << K_PREPARED->m_asset_sha256 << '\n';
            return 0;
        }
        if (K_COMMAND == "export-poses") {
            if (argc != 5 && !(argc == 7 && std::string_view(argv[5]) == "--decode-stage")) {
                std::cerr << "Usage: we_process export-poses POSE_JOB VIDEO OUTPUT.weposes "
                             "[--decode-stage DIR]\n";
                return 2;
            }
            const auto K_HASH = export_completed_pose_asset(
                { { argv[2], argv[3], argc == 7 ? argv[6] : std::filesystem::path {} }, argv[4] },
                cancellation.get_token());
            if (!K_HASH) {
                std::cerr << K_HASH.error().m_message << '\n';
                return K_HASH.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Canonical poses (arbitrary scale, unknown gravity): " << argv[4]
                      << "\nSHA-256 " << *K_HASH << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-depth") {
            if (argc != 4 && !(argc == 6 && std::string_view(argv[4]) == "--max-dimension")) {
                std::cerr << "Usage: we_process inspect-depth INPUT.wedepth SHA256 "
                             "[--max-dimension N]\n";
                return 2;
            }
            DepthAssetLimits limits;
            if (argc == 6) {
                const std::string_view K_SELECTED(argv[5]);
                const auto K_PARSED = std::from_chars(K_SELECTED.data(),
                                                      K_SELECTED.data() + K_SELECTED.size(),
                                                      limits.m_max_dimension);
                if (K_PARSED.ec != std::errc {} ||
                    K_PARSED.ptr != K_SELECTED.data() + K_SELECTED.size() ||
                    !limits.m_max_dimension || limits.m_max_dimension > 16384) {
                    std::cerr << "Depth dimension limit must be an integer in [1,16384]\n";
                    return 2;
                }
            }
            const auto K_SUMMARY =
                inspect_depth_file(argv[2], argv[3], limits, cancellation.get_token());
            if (!K_SUMMARY) {
                std::cerr << K_SUMMARY.error().m_message << '\n';
                return K_SUMMARY.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << *K_SUMMARY << '\n';
            return 0;
        }
        if (K_COMMAND == "inspect-poses") {
            if (argc != 4) {
                std::cerr << "Usage: we_process inspect-poses INPUT.weposes SHA256\n";
                return 2;
            }
            const auto K_ASSET = read_pose_asset(argv[2], argv[3], {}, cancellation.get_token());
            if (!K_ASSET) {
                std::cerr << K_ASSET.error().m_message << '\n';
                return K_ASSET.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            const auto K_JSON = inspect_pose_asset(*K_ASSET);
            if (!K_JSON) {
                std::cerr << K_JSON.error().m_message << '\n';
                return 2;
            }
            std::cout << *K_JSON << '\n';
            return 0;
        }
        if (K_COMMAND == "solve-poses") {
            if (arguments.size() < 6 || arguments.size() % 2 != 0) {
                std::cerr << "Usage: we_process solve-poses DECODE_STAGE VIDEO NEW_JOB "
                             "--pose-python EXECUTABLE [--recipe sequential|exhaustive_guided] "
                             "[--selection-sha256 HASH]\n";
                return 2;
            }
            PoseSolveRequest request { arguments[2],
                                      { .m_decode_stage = arguments[1],
                                       .m_job_directory = arguments[3],
                                       .m_worker_script = configuration.m_script,
                                       .m_worker_python = configuration.m_python } };
            bool pose_python {};
            for (std::size_t index { 4uz }; index < arguments.size(); index += 2uz) {
                if (arguments[index] == "--pose-python" && !pose_python) {
                    request.m_batch.m_pose_python = arguments[index + 1];
                    pose_python = true;
                } else if (arguments[index] == "--selection-sha256" &&
                           request.m_batch.m_frozen_selection_sha256.empty()) {
                    request.m_batch.m_frozen_selection_sha256 = arguments[index + 1];
                } else if (arguments[index] == "--recipe" &&
                           arguments[index + 1] == "exhaustive_guided") {
                    request.m_batch.m_recipe = PoseRecipe::EXHAUSTIVE_GUIDED;
                } else if (arguments[index] != "--recipe" || arguments[index + 1] != "sequential") {
                    std::cerr << "Invalid batch pose option\n";
                    return 2;
                }
            }
            if (!pose_python) {
                std::cerr << "An explicit --pose-python executable is required\n";
                return 2;
            }
            const auto K_MAPS = solve_selected_pose_map(
                std::move(request), cancellation.get_token(), [](const std::string& text) {
                    std::cout << text << std::flush;
                });
            if (!K_MAPS) {
                std::cerr << K_MAPS.error().m_message << '\n';
                return K_MAPS.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Fresh batch map: " << (*K_MAPS)->m_observations.size()
                      << " observations, " << (*K_MAPS)->m_poses.size() << " poses, "
                      << (*K_MAPS)->m_calibrations.size() << " calibrations\n";
            return 0;
        }
        if (K_COMMAND == "replay-poses") {
            if (argc != 4 && !(argc == 6 && std::string_view(argv[4]) == "--decode-stage")) {
                std::cerr << "Usage: we_process replay-poses POSE_JOB VIDEO [--decode-stage DIR]\n";
                return 2;
            }
            const auto K_MAPS = replay_completed_pose_map(
                { .m_pose_job = argv[2],
                 .m_video = argv[3],
                 .m_decode_stage = argc == 6 ? argv[5] : std::filesystem::path {} },
                cancellation.get_token(),
                [](const std::string& text) {
                    std::cout << text << std::flush;
                });
            if (!K_MAPS) {
                std::cerr << K_MAPS.error().m_message << '\n';
                return K_MAPS.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Completed-pose replay: " << (*K_MAPS)->m_observations.size()
                      << " observations, " << (*K_MAPS)->m_poses.size() << " poses, "
                      << (*K_MAPS)->m_calibrations.size() << " calibrations; epoch "
                      << (*K_MAPS)->m_epoch.m_value << ", revision "
                      << (*K_MAPS)->m_revision.m_value << '\n';
            return 0;
        }
        if (K_COMMAND == "replay-frames") {
            if (argc != 4) {
                std::cerr << "Usage: we_process replay-frames DECODE_STAGE VIDEO\n";
                return 2;
            }
            const auto K_DIGEST =
                sha256_file(argv[3], 16ULL * 1024 * 1024 * 1024, cancellation.get_token());
            if (!K_DIGEST) {
                std::cerr << K_DIGEST.error().m_message << '\n';
                return K_DIGEST.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            const auto K_SOURCE = open_decoded_frame_source(
                { .m_stage_directory = argv[2], .m_source_sha256 = *K_DIGEST },
                cancellation.get_token());
            if (!K_SOURCE) {
                std::cerr << K_SOURCE.error().m_message << '\n';
                return K_SOURCE.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::size_t count {};
            const auto K_START = std::chrono::steady_clock::now();
            while (true) {
                const auto K_EVENT = (*K_SOURCE)->read_next(cancellation.get_token());
                if (!K_EVENT) {
                    std::cerr << K_EVENT.error().m_message << '\n';
                    return K_EVENT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
                }
                if (const auto* frame = std::get_if<FramePacket>(&*K_EVENT)) {
                    ++count;
                    std::cout << "Frame " << frame->m_id.m_value << ", capture_ns "
                              << frame->m_capture_time.count() << ", PTS "
                              << frame->m_original_timestamp.m_pts << " @ "
                              << frame->m_original_timestamp.m_time_base_numerator << '/'
                              << frame->m_original_timestamp.m_time_base_denominator << ", "
                              << frame->m_pixels.m_width << 'x' << frame->m_pixels.m_height
                              << " RGB8 sha256 " << sha256(*frame->m_pixels.m_bytes) << '\n';
                }
                if (std::holds_alternative<SourceEnd>(*K_EVENT)) {
                    break;
                }
            }
            std::cout
                << "Replayed " << count << " committed frames in "
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - K_START).count()
                << " s; no reconstruction rerun.\n";
            return 0;
        }
        if (K_COMMAND == "reconstruct") {
            if (argc != 4 || std::string(argv[2]) != "--request") {
                std::cerr << "Usage: we_process reconstruct --request JOB.json\n";
                return 2;
            }
            const auto K_JOB = read_reconstruction_job(argv[3]);
            if (!K_JOB) {
                std::cerr << K_JOB.error().m_message << '\n';
                return 2;
            }
            const auto K_COMPLETED =
                reconstruct(*K_JOB, cancellation.get_token(), [](const std::string& output) {
                    std::cout << output << std::flush;
                });
            if (!K_COMPLETED) {
                std::cerr << K_COMPLETED.error().m_message << '\n';
                return K_COMPLETED.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
            }
            std::cout << "Published validated captured appearance: " << K_COMPLETED->string()
                      << '\n';
            return 0;
        }
        const auto K_RESULT = run_worker(
            configuration, arguments, cancellation.get_token(), [](const std::string& output) {
                std::cout << output << std::flush;
            });
        if (!K_RESULT) {
            std::cerr << K_RESULT.error().m_message << '\n';
            return K_RESULT.error().m_code == ErrorCode::CANCELLED ? 130 : 2;
        }
        return *K_RESULT;
    } catch (const std::exception& error) {
        std::cerr << "Operation failed: " << error.what() << '\n';
        return 2;
    }
}
