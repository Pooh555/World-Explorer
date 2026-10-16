#include "scale_fixture.hpp"
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
    require(argc == 2, "usage: we_scale_fixture NEW_DIRECTORY (synthetic; no capture)");
    const std::filesystem::path output(argv[1]);
    require(std::filesystem::create_directory(output), "output directory must not exist");
    const auto scene = make_scale_fixture();
    const auto hash = scene_content_hash(*scene);
    require(hash == SCALE_FIXTURE_SHA256, "generator differs from frozen fixture: " + hash);
    require(write_bundle(output / "scene.wexplore", scene).has_value(), "bundle write failed");
    bundle_limits limits;
    limits.scene.max_splats = SCALE_FIXTURE_SPLATS;
    limits.scene.max_chunks = 8;
    limits.scene.max_payload_bytes = 512ULL * 1024 * 1024;
    const auto loaded = load_bundle(output / "scene.wexplore", limits);
    require(loaded.has_value(), loaded ? "" : loaded.error().message);
    require(scene_content_hash(**loaded) == hash, "published fixture differs");
    std::filesystem::copy_file(WE_FIXTURE_LICENSE, output / "LICENSE");
    auto assets = nlohmann::json::array();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) {
        require(!entry.is_symlink(), "unexpected generated link");
        if (!entry.is_regular_file())
            continue;
        const auto digest = sha256_file(entry.path(), 128ULL * 1024 * 1024);
        require(digest.has_value(), "cannot hash generated asset");
        assets.push_back({{"path", entry.path().lexically_relative(output).generic_string()},
                          {"bytes", entry.file_size()},
                          {"sha256", *digest}});
    }
    std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) {
        return a.at("path").template get<std::string>() < b.at("path").template get<std::string>();
    });
    const nlohmann::json profile{
        {"schema_version", 1},
        {"scope", "synthetic maximum-scale admission/memory fixture; no capture or FPS gate"},
        {"fixture", scene->provenance.at("fixture")},
        {"asset_license", "MIT"},
        {"scene_content_sha256", hash},
        {"resident_splats", SCALE_FIXTURE_SPLATS},
        {"projectable_splats_per_view", SCALE_FIXTURE_CONTRIBUTORS},
        {"expected_tile_references_per_view", SCALE_FIXTURE_REFERENCES},
        {"sh_degree", 3},
        {"chunks", 8},
        {"width", SCALE_FIXTURE_WIDTH},
        {"height", SCALE_FIXTURE_HEIGHT},
        {"camera_position", {0, 0, 8}},
        {"intrinsics", {1536, 1536, 959.5, 539.5}},
        {"clip", {1, 32}},
        {"views", {"forward -Z/right +X/down -Y", "forward +Z/right -X/down -Y"}},
        {"scale", "ARBITRARY"},
        {"collision", "UNAVAILABLE"},
        {"assets", std::move(assets)}};
    std::ofstream stream(output / "fixture.json.partial", std::ios::binary | std::ios::noreplace);
    stream << profile.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot publish fixture profile");
    std::filesystem::rename(output / "fixture.json.partial", output / "fixture.json");
    std::cout << "Synthetic 2M fixture: " << hash << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Synthetic scale fixture failed (new partial output retained): " << error.what()
              << '\n';
    return 2;
}
