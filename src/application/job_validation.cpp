#include "job_validation.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"
#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>

namespace world_explorer::job_detail {
result<nlohmann::json> read_json(const std::filesystem::path& path, std::size_t maximum,
                                 const std::string& expected_sha256) {
    try {
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maximum)
            return fail(error_code::RESOURCE_EXHAUSTED,
                        "JSON input missing or exceeds budget: " + path.string());
        std::ifstream input(path, std::ios::binary);
        std::string bytes(maximum + 1, '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > maximum || !input.eof())
            return fail(error_code::RESOURCE_EXHAUSTED, "JSON input changed or exceeds budget");
        bytes.resize(count);
        if (!expected_sha256.empty() && sha256(std::as_bytes(std::span(bytes))) != expected_sha256)
            return fail(error_code::INVALID_DATA, "Job JSON checksum differs from selection");
        return nlohmann::json::parse(
            bytes, [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 64)
                    throw std::invalid_argument("JSON nesting exceeds 64 levels");
                return true;
            });
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Invalid job input: " + std::string(exception.what()));
    }
}

bool valid_digest(const std::string& digest) {
    return digest.size() == 64 && std::ranges::all_of(digest, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

result<std::filesystem::path> contained_file(const std::filesystem::path& root,
                                             const std::string& relative) {
    const std::filesystem::path path(relative);
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        relative.find('\0') != std::string::npos)
        return fail(error_code::INVALID_DATA, "Asset path must be relative");
    for (const auto& component : path)
        if (component == ".." || component == ".")
            return fail(error_code::INVALID_DATA, "Unsafe asset path");
    auto canonical = std::filesystem::canonical(root / path);
    if (canonical.lexically_relative(root) != path || !std::filesystem::is_regular_file(canonical))
        return fail(error_code::INVALID_DATA, "Asset path escapes its directory or uses a symlink");
    return canonical;
}

result<pose_input> verify_poses(const reconstruction_job& job, const std::string& source_hash,
                                const std::stop_token& stop) {
    const auto root = std::filesystem::canonical(job.pose_job);
    const auto pointer = read_json(root / "poses.json", std::size_t{1024} * 1024);
    const auto completed = read_json(root / "result.json", std::size_t{1024} * 1024);
    if (!pointer)
        return std::unexpected(pointer.error());
    if (!completed)
        return std::unexpected(completed.error());
    const auto key = pointer->at("key").get<std::string>();
    if (!valid_digest(key) || completed->at("schema_version") != 1 ||
        completed->at("status") != "complete" || completed->at("stage") != "poses" ||
        completed->at("source_sha256") != source_hash)
        return fail(error_code::INVALID_DATA, "Pose completion does not match the source video");
    const auto directory = std::filesystem::canonical(pointer->at("output").get<std::string>());
    if (directory != root / "stages" / ("poses-" + key) ||
        std::filesystem::canonical(completed->at("output").get<std::string>()) != directory)
        return fail(error_code::INVALID_DATA, "Pose pointer escapes its committed stage");
    const auto stage = verify_committed_stage(directory, key, stop);
    if (!stage)
        return std::unexpected(stage.error());
    if (!stage->assets.contains("training/sparse/cameras.bin") ||
        !stage->assets.contains("training/sparse/images.bin") ||
        !stage->assets.contains("training/sparse/points3D.bin"))
        return fail(error_code::INVALID_DATA, "Pose stage lacks the undistorted model");
    // Reject unsupported/malformed calibration before map admission or optimizer launch.
    const auto camera =
        read_colmap_training_camera(directory / "training/sparse/cameras.bin",
                                    stage->assets.at("training/sparse/cameras.bin").sha256, stop);
    if (!camera)
        return std::unexpected(camera.error());
    pose_input poses{directory, directory / "training", stage->completion_sha256, {}};
    for (const auto& [name, asset] : stage->assets)
        poses.file_hashes.emplace(name, asset.sha256);
    return poses;
}

result<std::string> verify_fit(const reconstruction_job& job, const pose_input& poses,
                               const std::string& source_hash,
                               const completed_pose_dataset& dataset) {
    const auto camera = read_colmap_training_camera(
        poses.training / "sparse/cameras.bin", poses.file_hashes.at("training/sparse/cameras.bin"));
    if (!camera)
        return std::unexpected(camera.error());
    const auto ratio = std::min(1., static_cast<double>(job.max_image_dimension) /
                                        std::max(camera->width, camera->height));
    // Match Python's nearest-even integer resize instead of ties-away std::round.
    const auto rounded = [](double value) {
        auto whole = static_cast<std::uint32_t>(std::floor(value));
        if (value - whole > .5 || (value - whole == .5 && whole % 2 != 0))
            ++whole;
        return whole;
    };
    const auto width = rounded(camera->width * ratio);
    const auto height = rounded(camera->height * ratio);
    const auto scale_x = static_cast<double>(width) / camera->width;
    const auto scale_y = static_cast<double>(height) / camera->height;
    const std::array<std::array<double, 3>, 3> intrinsic{
        {{camera->camera.fx * scale_x, 0, (camera->camera.cx + .5) * scale_x},
         {0, camera->camera.fy * scale_y, (camera->camera.cy + .5) * scale_y},
         {0, 0, 1}}};
    const auto result_file = read_json(job.fit_directory / "result.json", std::size_t{1024} * 1024);
    const auto inputs =
        read_json(job.fit_directory / "input_manifest.json", std::size_t{1024} * 1024);
    if (!result_file)
        return std::unexpected(result_file.error());
    if (!inputs)
        return std::unexpected(inputs.error());
    if (result_file->at("recipe") != "local_gsplat_v1" ||
        inputs->at("recipe") != "local_gsplat_v1" ||
        result_file->at("source_sha256") != source_hash ||
        inputs->at("source_sha256") != source_hash || result_file->at("steps") != job.steps ||
        inputs->at("steps") != job.steps ||
        inputs->at("max_image_dimension") != job.max_image_dimension ||
        inputs->at("max_splats") != job.max_splats)
        return fail(error_code::INVALID_DATA,
                    "Completed fit differs from the selected source or configuration");
    if (job.action == fit_action::REUSE_COMPLETED &&
        result_file->at("identity") != job.completed_fit_identity)
        return fail(error_code::INVALID_DATA,
                    "Completed fit identity differs from the explicitly selected fit");
    const auto& frames = inputs->at("dataset").at("frames");
    if (!frames.is_array() || frames.empty() || frames.size() > 120 ||
        frames.size() != dataset.poses.size())
        return fail(error_code::INVALID_DATA, "Invalid fit frame inventory");
    std::map<frame_id, const pose_estimate*> registered;
    for (const auto& pose : dataset.poses)
        registered.emplace(pose.frame, &pose);
    std::set<std::string> names;
    for (const auto& frame : frames) {
        if (frame.at("original_width") != camera->width ||
            frame.at("original_height") != camera->height || frame.at("width") != width ||
            frame.at("height") != height)
            return fail(error_code::INVALID_DATA,
                        "Fit dimensions differ from undistorted camera/resize");
        const auto recorded = frame.at("intrinsic").get<std::array<std::array<double, 3>, 3>>();
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column) {
                const auto expected = intrinsic[row][column];
                // The fitting dataset stores float32 intrinsics and per-axis resizes.
                const auto tolerance =
                    8 * std::numeric_limits<float>::epsilon() * std::max(1., std::abs(expected));
                if (!std::isfinite(recorded[row][column]) || !std::isfinite(expected) ||
                    std::abs(recorded[row][column] - expected) > tolerance)
                    return fail(error_code::INVALID_DATA,
                                "Fit intrinsics differ from undistorted camera/resize");
            }
        const auto name = frame.at("name").get<std::string>();
        const auto found = poses.file_hashes.find("training/images/" + name);
        if (!names.insert(name).second || found == poses.file_hashes.end() ||
            found->second != frame.at("sha256").get<std::string>())
            return fail(error_code::INVALID_DATA,
                        "Fit images differ from the committed pose stage");
        const auto source_frame = dataset.training_frames.find(name);
        if (source_frame == dataset.training_frames.end() ||
            !registered.contains(source_frame->second))
            return fail(error_code::INVALID_DATA, "Fit image has no registered canonical pose");
        const auto& transform = registered.at(source_frame->second)->submap_from_camera;
        const auto q = transform.rotation;
        const quatd inverse{q.w, -q.x, -q.y, -q.z};
        // Native COLMAP cam_from_world = canonical cam_from_world * Rx(pi).
        const auto x = rotate(inverse, {1, 0, 0});
        const auto y = rotate(inverse, {0, -1, 0});
        const auto z = rotate(inverse, {0, 0, -1});
        const auto t = rotate(inverse, transform.translation) * -1;
        const std::array<std::array<double, 4>, 4> expected{
            {{x.x, y.x, z.x, t.x}, {x.y, y.y, z.y, t.y}, {x.z, y.z, z.z, t.z}, {0, 0, 0, 1}}};
        const auto view = frame.at("view").get<std::array<std::array<double, 4>, 4>>();
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                if (!std::isfinite(view[row][column]) ||
                    std::abs(view[row][column] - expected[row][column]) >
                        5e-6 * std::max(1., std::abs(expected[row][column])))
                    return fail(error_code::INVALID_DATA,
                                "Fit cameras differ from the canonical pose dependency");
    }
    auto identity = result_file->at("identity").get<std::string>();
    if (!valid_digest(identity))
        return fail(error_code::INVALID_DATA, "Invalid fit identity");
    return identity;
}
} // namespace world_explorer::job_detail
