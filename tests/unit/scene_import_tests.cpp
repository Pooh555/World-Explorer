#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"

#include <chrono>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

struct ImportDirectory {
    std::filesystem::path m_path =
        std::filesystem::temp_directory_path() /
        ("we_import_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ImportDirectory() { std::filesystem::create_directories(m_path / "assets"); }

    ~ImportDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
};

void save_manifest(const std::filesystem::path& path, const nlohmann::json& manifest) {
    std::ofstream output(path);
    output << manifest.dump();
}

}  // namespace

TEST_CASE("fit compilation binds provenance and rejects changed assets without replacing output") {
    ImportDirectory directory;
    const auto K_PLY = directory.m_path / "assets/fit.ply";
    {
        std::ofstream output(K_PLY);
        output
            << "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\n"
               "element vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
               "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
               "property float opacity\nproperty float scale_0\nproperty float scale_1\n"
               "property float scale_2\nproperty float rot_0\nproperty float rot_1\n"
               "property float rot_2\nproperty float rot_3\nend_header\n"
               "0 0 2 0 0 0 0 -2 -2 -2 1 0 0 0\n";
    }
    const auto K_DIGEST = sha256_file(K_PLY, 65536).value();
    const std::string K_SOURCE_HASH(64, 'a');
    nlohmann::json manifest {{"schema_version", 1},
                             {"status", "complete"},
                             {"recipe", "local_gsplat_v1"},
                             {"identity", std::string(64, 'b')},
                             {"source_sha256", K_SOURCE_HASH},
                             {"ply_sha256", K_DIGEST},
                             {"ply", "assets/fit.ply"},
                             {"steps", 7000},
                             {"splats", 1}};
    const auto K_INPUT = directory.m_path / "result.json";
    const auto K_BUNDLE = directory.m_path / "output.wexplore";
    const nlohmann::json K_INPUT_MANIFEST {
        {"source_sha256", K_SOURCE_HASH},
        {"dataset",
         {{"scene_scale", 2},
          {"frames",
           nlohmann::json::array(
               {{{"name", "000001.png"},
                 {"height", 128},
                 {"view", {{1, 0, 0, 1}, {0, 1, 0, 2}, {0, 0, 1, 3}, {0, 0, 0, 1}}},
                 {"intrinsic", {{200, 0, 64}, {0, 200, 64}, {0, 0, 1}}}}})}}}};
    save_manifest(directory.m_path / "input_manifest.json", K_INPUT_MANIFEST);
    save_manifest(K_INPUT, manifest);
    REQUIRE(compile_fit_scene({.m_input = K_INPUT, .m_destination = K_BUNDLE}));
    auto loaded = load_bundle(K_BUNDLE).value();
    CHECK(loaded->m_provenance.at("capture_sha256") == K_SOURCE_HASH);
    CHECK(loaded->m_provenance.at("quality_gate") == "UNVERIFIED");
    CHECK(loaded->m_scale == ScaleStatus::ARBITRARY);
    CHECK_FALSE(loaded->m_chunks.begin()->second->m_geometry_valid);
    const auto K_INSPECTION = source_view(*loaded).value();
    CHECK(K_INSPECTION.m_camera.m_position == Vec3d { -1, 2, 3 });
    CHECK(K_INSPECTION.m_camera.forward().m_z == Catch::Approx(-1));
    CHECK(K_INSPECTION.m_camera.m_speed == Catch::Approx(0.4));
    CHECK(K_INSPECTION.m_vertical_fov_radians == Catch::Approx(2 * std::atan(128. / 400)));
    CHECK(K_INSPECTION.m_frame_name == "000001.png");
    const auto K_COMMITTED = sha256_file(K_BUNDLE / "manifest.json", 65536).value();

    manifest["ply_sha256"] = std::string(64, 'c');
    save_manifest(K_INPUT, manifest);
    CHECK_FALSE(compile_fit_scene({.m_input = K_INPUT, .m_destination = K_BUNDLE}));
    manifest["ply_sha256"] = K_DIGEST;
    manifest["splats"] = 2;
    save_manifest(K_INPUT, manifest);
    CHECK_FALSE(compile_fit_scene({.m_input = K_INPUT, .m_destination = K_BUNDLE}));
    manifest["splats"] = 1;
    manifest["ply"] = "../fit.ply";
    save_manifest(K_INPUT, manifest);
    CHECK_FALSE(compile_fit_scene({.m_input = K_INPUT, .m_destination = K_BUNDLE}));
    CHECK(sha256_file(K_BUNDLE / "manifest.json", 65536).value() == K_COMMITTED);
}

TEST_CASE("source view reset rejects missing or invalid camera metadata") {
    SceneState scene;
    CHECK_FALSE(source_view(scene));
    scene.m_provenance["source_inspection_view"] = nlohmann::json {{"schema_version", 1},
                                                                   {"position", {0, 0, 0}},
                                                                   {"forward", {0, 0, -2}},
                                                                   {"speed", 1},
                                                                   {"vertical_fov_radians", 1},
                                                                   {"frame_name", "000001.png"}}
                                                       .dump();
    CHECK_FALSE(source_view(scene));
    scene.m_provenance["source_inspection_view"] = std::string(4097, ' ');
    CHECK_FALSE(source_view(scene));
}

TEST_CASE("source view preserves recorded roll without claiming gravity") {
    SceneState scene;
    nlohmann::json metadata {{"schema_version", 2},
                             {"position", {1, 2, 3}},
                             {"forward", {0, 0, -1}},
                             {"up", {-1, 0, 0}},
                             {"up_reference", "CAPTURE_ORIENTATION"},
                             {"speed", 2},
                             {"vertical_fov_radians", 1},
                             {"frame_name", "000001.png"}};
    scene.m_provenance["source_inspection_view"] = metadata.dump();
    const auto K_VIEW = source_view(scene);
    REQUIRE(K_VIEW);
    CHECK(K_VIEW->m_camera.m_up_axis == Vec3d { -1, 0, 0 });
    CHECK(K_VIEW->m_camera.forward().m_z == Catch::Approx(-1));
    CHECK(K_VIEW->m_camera.right().m_y == Catch::Approx(1));
    CHECK(K_VIEW->m_camera.down().m_x == Catch::Approx(1));
    metadata["up"] = { 0, 0, -1 };
    scene.m_provenance["source_inspection_view"] = metadata.dump();
    CHECK_FALSE(source_view(scene));
    metadata["up"] = { 0, 0, 0 };
    scene.m_provenance["source_inspection_view"] = metadata.dump();
    CHECK_FALSE(source_view(scene));
    metadata["schema_version"] = 1;
    scene.m_provenance["source_inspection_view"] = metadata.dump();
    REQUIRE(source_view(scene));
    CHECK(source_view(scene)->m_camera.m_up_axis == Vec3d { 0, 1, 0 });
}
