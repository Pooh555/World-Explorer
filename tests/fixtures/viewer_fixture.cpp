#include "viewer_fixture.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace world_explorer;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_viewer_fixture NEW_DIRECTORY (synthetic; no capture)");
    const std::filesystem::path K_OUTPUT(argv[1]);
    require(std::filesystem::create_directory(K_OUTPUT), "output directory must not exist");
    const auto K_SCENE = make_portable_viewer_fixture();
    const auto K_CONTENT_HASH = scene_content_hash(*K_SCENE);
    require(K_CONTENT_HASH == K_VIEWER_FIXTURE_SHA256, "generator differs from frozen v1 fixture");
    const auto K_WRITTEN = write_bundle(K_OUTPUT / "scene.wexplore", K_SCENE);
    require(K_WRITTEN.has_value(), K_WRITTEN ? "" : K_WRITTEN.error().m_message);
    auto loaded = load_bundle(K_OUTPUT / "scene.wexplore");
    require(loaded.has_value(), loaded ? "" : loaded.error().m_message);
    require(scene_content_hash(**loaded) == K_CONTENT_HASH, "published fixture differs");
    std::filesystem::copy_file(WE_FIXTURE_LICENSE, K_OUTPUT / "LICENSE");
    nlohmann::json assets = nlohmann::json::array();
    // Include the license and every normal bundle asset; no machine-local paths.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(K_OUTPUT)) {
        require(!entry.is_symlink(), "unexpected link in generated fixture");
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto K_HASH = sha256_file(entry.path(), 128uz * 1024 * 1024);
        require(K_HASH.has_value(), K_HASH ? "" : K_HASH.error().m_message);
        assets.push_back({ { "path", entry.path().lexically_relative(K_OUTPUT).generic_string() },
                          { "bytes", entry.file_size() },
                          { "sha256", *K_HASH } });
    }
    std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) {
        return a.at("path").template get<std::string>() < b.at("path").template get<std::string>();
    });
    const auto K_CAMERA = portable_viewer_camera();
    nlohmann::json profile { { "schema", 1 },
                            { "label", "synthetic portable viewer fixture; not a reconstruction" },
                            { "fixture", K_SCENE->m_provenance.at("fixture") },
                            { "asset_license", "MIT" },
                            { "scene_content_sha256", K_CONTENT_HASH },
                            { "splat_count", K_VIEWER_FIXTURE_SPLATS },
                            { "chunks", 4 },
                            { "sh_degree", 3 },
                            { "width", K_VIEWER_FIXTURE_WIDTH },
                            { "height", K_VIEWER_FIXTURE_HEIGHT },
                            { "camera",
                             { { "position", { 0, 0, 12 } },
                              { "right", { 1, 0, 0 } },
                              { "down", { 0, -1, 0 } },
                              { "forward", { 0, 0, -1 } },
                              { "intrinsics", { 900, 900, 639.5, 359.5 } },
                              { "near", K_CAMERA.m_near_plane },
                              { "far", K_CAMERA.m_far_plane } } },
                            { "scale", "ARBITRARY" },
                            { "collision", "UNAVAILABLE" },
                            { "assets", assets } };
    std::ofstream stream(K_OUTPUT / "fixture.json.partial", std::ios::binary);
    stream << profile.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot publish fixture profile");
    std::filesystem::rename(K_OUTPUT / "fixture.json.partial", K_OUTPUT / "fixture.json");
    std::cout << "Synthetic portable viewer fixture: " << K_VIEWER_FIXTURE_SPLATS
              << " splats, scene_content_sha256=" << K_CONTENT_HASH << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Synthetic fixture failed (partial new output retained): " << error.what() << '\n';
    return 2;
}
