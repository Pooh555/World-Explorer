#include "scale_fixture.hpp"

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
    require(argc == 2, "usage: we_scale_fixture NEW_DIRECTORY (synthetic; no capture)");
    const std::filesystem::path K_OUTPUT(argv[1]);
    require(std::filesystem::create_directory(K_OUTPUT), "output directory must not exist");
    const auto K_SCENE = make_scale_fixture();
    const auto K_HASH = scene_content_hash(*K_SCENE);
    require(K_HASH == K_SCALE_FIXTURE_SHA256, "generator differs from frozen fixture: " + K_HASH);
    require(write_bundle(K_OUTPUT / "scene.wexplore", K_SCENE).has_value(), "bundle write failed");
    BundleLimits limits;
    limits.m_scene.m_max_splats = K_SCALE_FIXTURE_SPLATS;
    limits.m_scene.m_max_chunks = 8;
    limits.m_scene.m_max_payload_bytes = 512ULL * 1024 * 1024;
    const auto K_LOADED = load_bundle(K_OUTPUT / "scene.wexplore", limits);
    require(K_LOADED.has_value(), K_LOADED ? "" : K_LOADED.error().m_message);
    require(scene_content_hash(**K_LOADED) == K_HASH, "published fixture differs");
    std::filesystem::copy_file(WE_FIXTURE_LICENSE, K_OUTPUT / "LICENSE");
    auto assets = nlohmann::json::array();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(K_OUTPUT)) {
        require(!entry.is_symlink(), "unexpected generated link");
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto K_DIGEST = sha256_file(entry.path(), 128ULL * 1024 * 1024);
        require(K_DIGEST.has_value(), "cannot hash generated asset");
        assets.push_back({ { "path", entry.path().lexically_relative(K_OUTPUT).generic_string() },
                          { "bytes", entry.file_size() },
                          { "sha256", *K_DIGEST } });
    }
    std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) {
        return a.at("path").template get<std::string>() < b.at("path").template get<std::string>();
    });
    const nlohmann::json K_PROFILE {
        { "schema_version", 1 },
        { "scope", "synthetic maximum-scale admission/memory fixture; no capture or FPS gate" },
        { "fixture", K_SCENE->m_provenance.at("fixture") },
        { "asset_license", "MIT" },
        { "scene_content_sha256", K_HASH },
        { "resident_splats", K_SCALE_FIXTURE_SPLATS },
        { "projectable_splats_per_view", K_SCALE_FIXTURE_CONTRIBUTORS },
        { "expected_tile_references_per_view", K_SCALE_FIXTURE_REFERENCES },
        { "sh_degree", 3 },
        { "chunks", 8 },
        { "width", K_SCALE_FIXTURE_WIDTH },
        { "height", K_SCALE_FIXTURE_HEIGHT },
        { "camera_position", { 0, 0, 8 } },
        { "intrinsics", { 1536, 1536, 959.5, 539.5 } },
        { "clip", { 1, 32 } },
        { "views", { "forward -Z/right +X/down -Y", "forward +Z/right -X/down -Y" } },
        { "scale", "ARBITRARY" },
        { "collision", "UNAVAILABLE" },
        { "assets", std::move(assets) } };
    std::ofstream stream(K_OUTPUT / "fixture.json.partial", std::ios::binary | std::ios::noreplace);
    stream << K_PROFILE.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot publish fixture profile");
    std::filesystem::rename(K_OUTPUT / "fixture.json.partial", K_OUTPUT / "fixture.json");
    std::cout << "Synthetic 2M fixture: " << K_HASH << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Synthetic scale fixture failed (new partial output retained): " << error.what()
              << '\n';
    return 2;
}
