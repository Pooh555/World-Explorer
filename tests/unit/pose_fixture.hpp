#pragma once

#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include <array>
#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

namespace world_explorer::test {

inline std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return { bytes.begin(), bytes.end() };
}

struct pose_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_pose_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string decode_key = std::string(64, 'a'), pose_key = std::string(64, 'c');
    std::filesystem::path decoded = root / "stages" / ("decode-" + decode_key);
    std::filesystem::path posed = root / "stages" / ("poses-" + pose_key);
    std::string source_digest;
    nlohmann::json canonical;

    explicit pose_fixture(std::filesystem::path directory = {}) {
        if (!directory.empty()) {
            root = std::move(directory);
            decoded = root / "stages" / ("decode-" + decode_key);
            posed = root / "stages" / ("poses-" + pose_key);
        }
        std::filesystem::create_directories(decoded / "images");
        std::filesystem::create_directories(posed);
        std::ofstream(root / "source.mp4") << "synthetic diagnostic; not a video";
        source_digest = *sha256_file(root / "source.mp4", 4096);
        constexpr std::array<unsigned char, 72> PNG {
            137, 80,  78,  71,  13,  10,  26,  10, 0,  0,  0,  13,  73, 72,  68,  82, 0,  0,
            0,   2,   0,   0,   0,   1,   8,   2,  0,  0,  0,  123, 64, 232, 221, 0,  0,  0,
            15,  73,  68,  65,  84,  120, 156, 99, 16, 84, 50, 118, 9,  77,  3,   0,  3,  191,
            1,   102, 125, 140, 226, 99,  0,   0,  0,  0,  73, 69,  78, 68,  174, 66, 96, 130};
        nlohmann::json frames = nlohmann::json::array(), poses = nlohmann::json::array();
        for (int index { 0 }; index < 3; ++index) {
            const auto name = "images/" + std::to_string(index) + ".png";
            std::ofstream image(decoded / name, std::ios::binary);
            image.write(reinterpret_cast<const char*>(PNG.data()),
                        static_cast<std::streamsize>(PNG.size()));
            image.close();
            const auto external = source_digest.substr(0, 16) + "-" + std::to_string(index * 10);
            const auto calibration = source_digest.substr(0, 16) + "-lens-0";
            frames.push_back(
                {{"file", name},
                 {"sha256", *sha256_file(decoded / name, 4096)},
                 {"frame_id", external},
                 {"calibration_id", calibration},
                 {"source_index", index * 10},
                 {"pts", index * 10},
                 {"time_base", "1/30"},
                 {"capture_time_ns", static_cast<std::int64_t>(index) * 1000000000 / 3}});
            poses.push_back({{"image_id", index + 1},
                             {"backend_rig_frame_id", index + 1},
                             {"frame_id", external},
                             {"calibration_id", calibration},
                             {"camera_id", 1},
                             {"pose_revision", 1},
                             {"state", "final"},
                             {"pts", index * 10},
                             {"time_base", "1/30"},
                             {"world_from_camera",
                              {{"translation", {1. + index, 2. - index, 3. + index}},
                               {"rotation_wxyz", {0.5, 0.5, 0.5, 0.5}}}}});
        }
        std::ofstream(decoded / "result.json") << nlohmann::json {
            {"selection", "timestamp_sampling_v1"},
            {"crop", "none"},
            {"resize", "none"},
            {"source_frame_count", 21},
            {"frames", frames}}.dump();
        const auto camera = nlohmann::json {
            {"camera_id", 1},
            {"revision", 1},
            {"model", "OPENCV"},
            {"width", 2},
            {"height", 1},
            {"backend_params_centers", "half_pixel"},
            {"canonical_pixel_centers", "integer"},
            {"calibration_matrix", {{711., 0., .25}, {0., 529., .75}, {0., 0., 1.}}},
            {"backend_params", {711., 529., .75, 1.25, .1, -.2, .003, -.004}}};
        canonical = { { "schema_version", 1 },
                     { "epoch", 0 },
                     { "revision", 1 },
                     { "scale_status", "ARBITRARY" },
                     { "gravity", "unknown" },
                     { "world_up", "+y" },
                     { "backend_world_rotation_wxyz", { 0, 1, 0, 0 } },
                     { "registered_frames", 3 },
                     { "points", 20 },
                     { "mean_reprojection_error", .6 },
                     { "cameras", nlohmann::json::array({ camera }) },
                     { "poses", poses } };
        std::ofstream(posed / "result.json")
            << nlohmann::json {{"canonical_poses", "canonical_poses.json"},
                               {"registered_frames", 3},
                               {"scale_status", "ARBITRARY"},
                               {"geometry", "UNAVAILABLE"}}
                   .dump();
        std::ofstream(root / "decode.json")
            << nlohmann::json {{"key", decode_key}, {"output", path_utf8(decoded)}}.dump();
        std::ofstream(root / "poses.json")
            << nlohmann::json {{"key", pose_key}, {"output", path_utf8(posed)}}.dump();
        std::ofstream(root / "result.json") << nlohmann::json {
            {"schema_version", 1},
            {"status", "complete"},
            {"stage", "poses"},
            {"source_sha256", source_digest},
            {"output", path_utf8(posed)}}.dump();
        publish();
    }

    ~pose_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void inventory(const std::filesystem::path& directory) const {
        auto files = nlohmann::json::array();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().filename() == "completion.json") continue;
            files.push_back({{"path", path_utf8(entry.path().lexically_relative(directory))},
                             {"bytes", entry.file_size()},
                             {"sha256", *sha256_file(entry.path(), 1024uz * 1024)}});
        }
        std::ofstream(directory / "completion.json") << nlohmann::json {
            {"schema_version", 1},
            {"status", "complete"},
            {"key", directory == decoded ? decode_key : pose_key},
            {"files", files}}.dump();
    }

    void publish() const {
        std::ofstream(posed / "canonical_poses.json") << canonical.dump();
        inventory(decoded);
        inventory(posed);
    }

    Result<std::shared_ptr<const CompletedPoseDataset>> load() const {
        return load_completed_poses({root, source_digest});
    }
};

}  // namespace world_explorer::test
