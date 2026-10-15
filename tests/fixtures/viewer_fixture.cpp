#include "viewer_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace world_explorer;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_viewer_fixture NEW_DIRECTORY (synthetic; no capture)");
    const std::filesystem::path output(argv[1]);
    require(std::filesystem::create_directory(output), "output directory must not exist");
    const auto scene = make_portable_viewer_fixture();
    const auto content_hash = scene_content_hash(*scene);
    require(content_hash == VIEWER_FIXTURE_SHA256, "generator differs from frozen v1 fixture");
    const auto written = write_bundle(output / "scene.wexplore", scene);
    require(written.has_value(), written ? "" : written.error().message);
    auto loaded = load_bundle(output / "scene.wexplore");
    require(loaded.has_value(), loaded ? "" : loaded.error().message);
    require(scene_content_hash(**loaded) == content_hash, "published fixture differs");
    std::filesystem::copy_file(WE_FIXTURE_LICENSE, output / "LICENSE");
    nlohmann::json assets = nlohmann::json::array();
    // Include the license and every normal bundle asset; no machine-local paths.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) {
        require(!entry.is_symlink(), "unexpected link in generated fixture");
        if (!entry.is_regular_file())
            continue;
        const auto hash = sha256_file(entry.path(), std::size_t{128} * 1024 * 1024);
        require(hash.has_value(), hash ? "" : hash.error().message);
        assets.push_back({{"path", entry.path().lexically_relative(output).generic_string()},
                          {"bytes", entry.file_size()},
                          {"sha256", *hash}});
    }
    std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) {
        return a.at("path").template get<std::string>() < b.at("path").template get<std::string>();
    });
    const auto camera = portable_viewer_camera();
    nlohmann::json profile{{"schema", 1},
                           {"label", "synthetic portable viewer fixture; not a reconstruction"},
                           {"fixture", scene->provenance.at("fixture")},
                           {"asset_license", "MIT"},
                           {"scene_content_sha256", content_hash},
                           {"splat_count", VIEWER_FIXTURE_SPLATS},
                           {"chunks", 4},
                           {"sh_degree", 3},
                           {"width", VIEWER_FIXTURE_WIDTH},
                           {"height", VIEWER_FIXTURE_HEIGHT},
                           {"camera",
                            {{"position", {0, 0, 12}},
                             {"right", {1, 0, 0}},
                             {"down", {0, -1, 0}},
                             {"forward", {0, 0, -1}},
                             {"intrinsics", {900, 900, 639.5, 359.5}},
                             {"near", camera.near_plane},
                             {"far", camera.far_plane}}},
                           {"scale", "ARBITRARY"},
                           {"collision", "UNAVAILABLE"},
                           {"assets", assets}};
    std::ofstream stream(output / "fixture.json.partial", std::ios::binary);
    stream << profile.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot publish fixture profile");
    std::filesystem::rename(output / "fixture.json.partial", output / "fixture.json");
    std::cout << "Synthetic portable viewer fixture: " << VIEWER_FIXTURE_SPLATS
              << " splats, scene_content_sha256=" << content_hash << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Synthetic fixture failed (partial new output retained): " << error.what() << '\n';
    return 2;
}
