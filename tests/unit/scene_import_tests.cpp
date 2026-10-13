#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace world_explorer;
namespace {
struct import_directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("we_import_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    import_directory() { std::filesystem::create_directories(path / "assets"); }
    ~import_directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void save_manifest(const std::filesystem::path& path, const nlohmann::json& manifest) {
    std::ofstream output(path);
    output << manifest.dump();
}
} // namespace

TEST_CASE("fit compilation binds provenance and rejects changed assets without replacing output") {
    import_directory directory;
    const auto ply = directory.path / "assets/fit.ply";
    {
        std::ofstream output(ply);
        output
            << "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\n"
               "element vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
               "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
               "property float opacity\nproperty float scale_0\nproperty float scale_1\n"
               "property float scale_2\nproperty float rot_0\nproperty float rot_1\n"
               "property float rot_2\nproperty float rot_3\nend_header\n"
               "0 0 2 0 0 0 0 -2 -2 -2 1 0 0 0\n";
    }
    const auto digest = sha256_file(ply, 65536).value();
    const std::string source_hash(64, 'a');
    nlohmann::json manifest{{"schema_version", 1},
                            {"status", "complete"},
                            {"recipe", "local_gsplat_v1"},
                            {"identity", std::string(64, 'b')},
                            {"source_sha256", source_hash},
                            {"ply_sha256", digest},
                            {"ply", "assets/fit.ply"},
                            {"steps", 7000},
                            {"splats", 1}};
    const auto input = directory.path / "result.json";
    const auto bundle = directory.path / "output.wexplore";
    const nlohmann::json input_manifest{
        {"source_sha256", source_hash},
        {"dataset",
         {{"scene_scale", 2},
          {"frames", nlohmann::json::array(
                         {{{"name", "000001.png"},
                           {"height", 128},
                           {"view", {{1, 0, 0, 1}, {0, 1, 0, 2}, {0, 0, 1, 3}, {0, 0, 0, 1}}},
                           {"intrinsic", {{200, 0, 64}, {0, 200, 64}, {0, 0, 1}}}}})}}}};
    save_manifest(directory.path / "input_manifest.json", input_manifest);
    save_manifest(input, manifest);
    REQUIRE(compile_fit_scene({.input = input, .destination = bundle}));
    auto loaded = load_bundle(bundle).value();
    CHECK(loaded->provenance.at("capture_sha256") == source_hash);
    CHECK(loaded->provenance.at("quality_gate") == "UNVERIFIED");
    CHECK(loaded->scale == scale_status::ARBITRARY);
    CHECK_FALSE(loaded->chunks.begin()->second->geometry_valid);
    const auto inspection = source_view(*loaded).value();
    CHECK(inspection.camera.position == vec3d{-1, 2, 3});
    CHECK(inspection.camera.forward().z == Catch::Approx(-1));
    CHECK(inspection.camera.speed == Catch::Approx(0.4));
    CHECK(inspection.vertical_fov_radians == Catch::Approx(2 * std::atan(128. / 400)));
    CHECK(inspection.frame_name == "000001.png");
    const auto committed = sha256_file(bundle / "manifest.json", 65536).value();

    manifest["ply_sha256"] = std::string(64, 'c');
    save_manifest(input, manifest);
    CHECK_FALSE(compile_fit_scene({.input = input, .destination = bundle}));
    manifest["ply_sha256"] = digest;
    manifest["splats"] = 2;
    save_manifest(input, manifest);
    CHECK_FALSE(compile_fit_scene({.input = input, .destination = bundle}));
    manifest["splats"] = 1;
    manifest["ply"] = "../fit.ply";
    save_manifest(input, manifest);
    CHECK_FALSE(compile_fit_scene({.input = input, .destination = bundle}));
    CHECK(sha256_file(bundle / "manifest.json", 65536).value() == committed);
}

TEST_CASE("source view reset rejects missing or invalid camera metadata") {
    scene_state scene;
    CHECK_FALSE(source_view(scene));
    scene.provenance["source_inspection_view"] =
        nlohmann::json{{"schema_version", 1},       {"position", {0, 0, 0}},
                       {"forward", {0, 0, -2}},     {"speed", 1},
                       {"vertical_fov_radians", 1}, {"frame_name", "000001.png"}}
            .dump();
    CHECK_FALSE(source_view(scene));
    scene.provenance["source_inspection_view"] = std::string(4097, ' ');
    CHECK_FALSE(source_view(scene));
}

TEST_CASE("source view preserves recorded roll without claiming gravity") {
    scene_state scene;
    nlohmann::json metadata{{"schema_version", 2},
                            {"position", {1, 2, 3}},
                            {"forward", {0, 0, -1}},
                            {"up", {-1, 0, 0}},
                            {"up_reference", "CAPTURE_ORIENTATION"},
                            {"speed", 2},
                            {"vertical_fov_radians", 1},
                            {"frame_name", "000001.png"}};
    scene.provenance["source_inspection_view"] = metadata.dump();
    const auto view = source_view(scene);
    REQUIRE(view);
    CHECK(view->camera.up_axis == vec3d{-1, 0, 0});
    CHECK(view->camera.forward().z == Catch::Approx(-1));
    CHECK(view->camera.right().y == Catch::Approx(1));
    CHECK(view->camera.down().x == Catch::Approx(1));
    metadata["up"] = {0, 0, -1};
    scene.provenance["source_inspection_view"] = metadata.dump();
    CHECK_FALSE(source_view(scene));
    metadata["up"] = {0, 0, 0};
    scene.provenance["source_inspection_view"] = metadata.dump();
    CHECK_FALSE(source_view(scene));
    metadata["schema_version"] = 1;
    scene.provenance["source_inspection_view"] = metadata.dump();
    REQUIRE(source_view(scene));
    CHECK(source_view(scene)->camera.up_axis == vec3d{0, 1, 0});
}
