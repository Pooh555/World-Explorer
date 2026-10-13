#ifndef WORLD_EXPLORER_TEST_RECONSTRUCTION_FIXTURE_HPP
#define WORLD_EXPLORER_TEST_RECONSTRUCTION_FIXTURE_HPP
#include "pose_fixture.hpp"
#include "stage_chain_fixture.hpp"
#include "training_camera_fixture.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include <stdexcept>

namespace world_explorer::test {
inline void save_reconstruction_json(const std::filesystem::path& path,
                                     const nlohmann::json& value) {
    std::ofstream file(path);
    file << value.dump();
}
// Checked, tiny synthetic pose/fit assets. Exercises admission, not real reconstruction.
struct reconstruction_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_job_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    test::pose_fixture poses{root / "poses"};
    reconstruction_job job;
    std::filesystem::path stage;
    std::string source_hash;
    std::string image_hash;
    std::string identity = std::string(64, 'b');
    reconstruction_fixture() {
        job.video = root / "capture.mp4";
        job.pose_job = root / "poses";
        job.fit_directory = root / "fit";
        job.scene_output = root / "scene.wexplore";
        job.action = fit_action::REUSE_COMPLETED;
        job.completed_fit_identity = identity;
        stage = poses.posed;
        std::filesystem::create_directories(stage / "training/images");
        std::filesystem::create_directories(stage / "training/sparse");
        std::filesystem::create_directories(job.fit_directory / "assets");
        // Diagnostic byte fixtures exercise admission, not capture reconstruction.
        std::filesystem::copy_file(poses.root / "source.mp4", job.video);
        source_hash = sha256_file(job.video, 1024).value();
        std::ofstream(stage / "training/images/frame.png") << "diagnostic image bytes";
        image_hash = sha256_file(stage / "training/images/frame.png", 1024).value();
        for (const auto* name : {"images.bin", "points3D.bin"})
            std::ofstream(stage / "training/sparse" / name) << "diagnostic model bytes";
        test::write_training_camera(stage / "training/sparse/cameras.bin",
                                    test::training_camera_bytes());
        poses.inventory(stage);
        std::ofstream(job.fit_directory / "assets/fit.ply")
            << "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\n"
               "element vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
               "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
               "property float opacity\nproperty float scale_0\nproperty float scale_1\n"
               "property float scale_2\nproperty float rot_0\nproperty float rot_1\n"
               "property float rot_2\nproperty float rot_3\nend_header\n"
               "0 0 2 0 0 0 0 -2 -2 -2 1 0 0 0\n";
        save_reconstruction_json(
            job.fit_directory / "result.json",
            {{"schema_version", 1},
             {"status", "complete"},
             {"recipe", "local_gsplat_v1"},
             {"identity", identity},
             {"source_sha256", source_hash},
             {"steps", 7000},
             {"splats", 1},
             {"ply", "assets/fit.ply"},
             {"ply_sha256", sha256_file(job.fit_directory / "assets/fit.ply", 65536).value()}});
        const auto dataset = poses.load().value();
        nlohmann::json frames = nlohmann::json::array();
        std::filesystem::remove(stage / "training/images/frame.png");
        for (const auto& [name, frame] : dataset->training_frames) {
            std::ofstream(stage / "training/images" / name) << "diagnostic image bytes";
            const auto pose = std::ranges::find(dataset->poses, frame, &pose_estimate::frame);
            const auto& transform = pose->submap_from_camera;
            const auto q = transform.rotation;
            const quatd inverse{q.w, -q.x, -q.y, -q.z};
            const auto x = rotate(inverse, {1, 0, 0});
            const auto y = rotate(inverse, {0, -1, 0});
            const auto z = rotate(inverse, {0, 0, -1});
            const auto t = rotate(inverse, transform.translation) * -1;
            frames.push_back(
                {{"name", name},
                 {"sha256", image_hash},
                 {"height", 128},
                 {"width", 128},
                 {"original_height", 128},
                 {"original_width", 128},
                 {"view",
                  {{x.x, y.x, z.x, t.x}, {x.y, y.y, z.y, t.y}, {x.z, y.z, z.z, t.z}, {0, 0, 0, 1}}},
                 {"intrinsic", {{200, 0, 64}, {0, 200, 64}, {0, 0, 1}}}});
        }
        poses.inventory(stage);
        save_reconstruction_json(job.fit_directory / "input_manifest.json",
                                 {{"recipe", "local_gsplat_v1"},
                                  {"source_sha256", source_hash},
                                  {"steps", 7000},
                                  {"max_image_dimension", 1280},
                                  {"max_splats", 2000000},
                                  {"dataset", {{"scene_scale", 2}, {"frames", frames}}}});
    }
    ~reconstruction_fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void adopt_inspection() {
        const auto directory = poses.root / "stages" / ("inspect-" + poses.pose_key);
        std::filesystem::create_directory(directory);
        save_reconstruction_json(
            directory / "result.json",
            {{"schema_version", 1},
             {"source",
              {{"sha256", source_hash}, {"bytes", std::filesystem::file_size(job.video)}}},
             {"stream",
              {{"codec_type", "video"},
               {"codec_name", "synthetic_diagnostic"},
               {"width", 2},
               {"height", 1},
               {"time_base", "1/30"},
               {"color_transfer", "bt709"}}},
             {"duration_seconds", 1.},
             {"rotation_degrees", -90},
             {"audio", "ignored"},
             {"scale_status", "ARBITRARY"},
             {"color_domain", "srgb"}});
        poses.inventory(directory);
        job.inspection = retained_video_inspection{
            directory, sha256_file(directory / "completion.json", 65536).value(), 1};
    }
    retained_video_inspection& inspection() {
        if (!job.inspection)
            throw std::logic_error("Fixture inspection has not been prepared");
        return *job.inspection;
    }
    std::filesystem::path write_retained_request() {
        const auto request = root / "request.json";
        const auto& selected = inspection();
        save_reconstruction_json(request,
                                 {{"schema_version", 2},
                                  {"recipe", "local_gsplat_v1"},
                                  {"action", "reuse_completed"},
                                  {"pose_action", "adopt_completed"},
                                  {"video", path_utf8(job.video)},
                                  {"pose_job", path_utf8(job.pose_job)},
                                  {"fit_directory", path_utf8(job.fit_directory)},
                                  {"scene_output", path_utf8(job.scene_output)},
                                  {"steps", job.steps},
                                  {"max_image_dimension", job.max_image_dimension},
                                  {"max_splats", job.max_splats},
                                  {"completed_fit_identity", job.completed_fit_identity},
                                  {"inspection",
                                   {{"action", "adopt_completed"},
                                    {"stage", path_utf8(selected.stage)},
                                    {"completion_sha256", selected.completion_sha256},
                                    {"minimum_free_bytes", selected.minimum_free_bytes}}}});
        return request;
    }
    void fresh(const std::string& mode = "valid", bool automatic = false) {
        adopt_inspection();
        auto metadata = nlohmann::json::parse(std::ifstream(inspection().stage / "result.json"));
        metadata["rotation_degrees"] = 0;
        save_reconstruction_json(inspection().stage / "result.json", metadata);
        poses.inventory(inspection().stage);
        inspection().completion_sha256 =
            sha256_file(inspection().stage / "completion.json", 65536).value();
        job.pose_job = root / "fresh poses";
        job.fresh_poses =
            fresh_pose_configuration{{root / "pose worker.py", WE_TEST_PYTHON}, WE_TEST_PYTHON};
        test::stage_worker(root / "pose worker.py", poses, mode);
        if (automatic) {
            test::stage_worker(root / "decode worker.py", poses);
            job.automatic_decode = automatic_video_decode{
                root / "decoded video", {root / "decode worker.py", WE_TEST_PYTHON}};
        } else
            job.selected_decode = retained_video_decode{
                poses.decoded, sha256_file(poses.decoded / "completion.json", 65536).value()};
    }
};
} // namespace world_explorer::test
#endif
