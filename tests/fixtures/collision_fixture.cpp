#include "collision_support_fixture.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <fstream>
#include <iostream>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

void write(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary);
    output << value.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("Could not write synthetic collision fixture");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: we_collision_fixture NEW_DIRECTORY (synthetic only)\n";
            return 2;
        }
        const auto K_OUTPUT = std::filesystem::absolute(argv[1]);
        if (std::filesystem::exists(std::filesystem::symlink_status(K_OUTPUT)) ||
            !std::filesystem::create_directory(K_OUTPUT)) {
            throw std::runtime_error("Choose a new synthetic fixture directory");
        }
        const auto K_SOURCE = std::make_shared<SceneState>(collision_source());
        const auto K_WRITTEN = write_bundle(K_OUTPUT / "source.wexplore", K_SOURCE);
        if (!K_WRITTEN) {
            throw std::runtime_error(K_WRITTEN.error().m_message);
        }
        auto document = collision_document(*K_SOURCE);
        auto& cells = document["grid"]["states"].get_ref<std::string&>();
        // Full-height thin wall; explicit unknown column; neither is inferred from TSDF allocation.
        for (std::size_t y { 0uz }; y < 4uz; ++y) {
            for (std::size_t z { 0uz }; z < 3uz; ++z) {
                cells[3uz + 8uz * (y + 4uz * z)] = '#';
            }
            cells[1uz + 8uz * (y + 4uz * 4uz)] = '?';
        }
        write(K_OUTPUT / "support.json", document);
        const auto K_SUPPORT_HASH = sha256_file(K_OUTPUT / "support.json", 1024uz * 1024uz);
        const auto K_SOURCE_HASH =
            sha256_file(K_OUTPUT / "source.wexplore/manifest.json", 1024uz * 1024uz);
        if (!K_SUPPORT_HASH || !K_SOURCE_HASH) {
            throw std::runtime_error("Could not hash synthetic inputs");
        }
        const auto K_PROBE = [](std::array<double, 3uz> start, std::array<double, 3uz> end) {
            return nlohmann::json {
                { "start_feet", start }, { "end_feet", end }, { "radius", .2 }, { "height", 1.7 } };
        };
        write(K_OUTPUT / "request.json",
              { { "kind", "COLLISION_SUPPORT_INSPECTION_V1" },
               { "source_bundle", "source.wexplore" },
               { "source_manifest_sha256", *K_SOURCE_HASH },
               { "support", "support.json" },
               { "support_sha256", *K_SUPPORT_HASH },
               { "probes",
                nlohmann::json::array({ K_PROBE({ 1.5, 0, 1.5 }, { 1.5, 0, 1.5 }),
                                       K_PROBE({ 1.5, 0, 1.5 }, { 4.5, 0, 1.5 }),
                                       K_PROBE({ 1.5, 0, 4.5 }, { 1.5, 0, 4.5 }),
                                       K_PROBE({ 3.5, 0, 4.5 }, { 3.5, 0, 4.5 }) }) } });
        std::cout << "Synthetic collision source/support/request saved; no measured collision or "
                     "walking\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
