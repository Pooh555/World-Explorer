#include "job_validation.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <fstream>
#include <set>

namespace world_explorer::job_detail {
result<nlohmann::json> read_json(const std::filesystem::path& path, std::size_t maximum) {
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
    constexpr std::size_t MAX_STAGE_BYTES = 32ULL * 1024 * 1024 * 1024;
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
    const auto completion_file = directory / "completion.json";
    const auto initial_hash = sha256_file(completion_file, std::size_t{16} * 1024 * 1024, stop);
    if (!initial_hash)
        return std::unexpected(initial_hash.error());
    const auto manifest = read_json(completion_file, std::size_t{16} * 1024 * 1024);
    if (!manifest)
        return std::unexpected(manifest.error());
    const auto& files = manifest->at("files");
    if (manifest->at("schema_version") != 1 || manifest->at("status") != "complete" ||
        manifest->at("key") != key || !files.is_array() || files.empty() || files.size() > 4096)
        return fail(error_code::INVALID_DATA, "Invalid pose stage inventory");
    pose_input poses{directory, directory / "training", *initial_hash, {}};
    std::size_t total{};
    for (const auto& entry : files) {
        const auto relative = entry.at("path").get<std::string>();
        const auto expected_hash = entry.at("sha256").get<std::string>();
        if (!entry.at("bytes").is_number_unsigned() || !valid_digest(expected_hash) ||
            !poses.file_hashes.emplace(relative, expected_hash).second ||
            relative == "completion.json")
            return fail(error_code::INVALID_DATA, "Invalid or duplicate pose inventory entry");
        const auto size = entry.at("bytes").get<std::uint64_t>();
        if (size > MAX_STAGE_BYTES - total)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose stage exceeds 32 GiB");
        total += static_cast<std::size_t>(size);
        const auto path = contained_file(directory, relative);
        if (!path)
            return std::unexpected(path.error());
        if (std::filesystem::file_size(*path) != size)
            return fail(error_code::INVALID_DATA, "Pose asset byte count changed: " + relative);
        const auto digest = sha256_file(*path, static_cast<std::size_t>(size), stop);
        if (!digest)
            return std::unexpected(digest.error());
        if (*digest != expected_hash)
            return fail(error_code::INVALID_DATA, "Pose asset checksum changed: " + relative);
    }
    // Reject extra files as well as missing ones. The worker's completed stage is immutable.
    std::size_t actual_count{};
    std::size_t entry_count{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (++entry_count > 8192)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose directory entry budget exceeded");
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose verification cancelled");
        if (entry.is_symlink())
            return fail(error_code::INVALID_DATA, "Pose stage contains a symlink");
        if (entry.is_regular_file() && entry.path() != completion_file) {
            ++actual_count;
            if (!poses.file_hashes.contains(
                    entry.path().lexically_relative(directory).generic_string()))
                return fail(error_code::INVALID_DATA, "Uncommitted file in pose stage");
        }
    }
    const auto final_hash = sha256_file(completion_file, std::size_t{16} * 1024 * 1024, stop);
    if (!final_hash)
        return std::unexpected(final_hash.error());
    if (actual_count != files.size() || *initial_hash != *final_hash ||
        !poses.file_hashes.contains("training/sparse/cameras.bin") ||
        !poses.file_hashes.contains("training/sparse/images.bin") ||
        !poses.file_hashes.contains("training/sparse/points3D.bin"))
        return fail(error_code::INVALID_DATA, "Pose stage changed or lacks the undistorted model");
    return poses;
}

result<std::string> verify_fit(const reconstruction_job& job, const pose_input& poses,
                               const std::string& source_hash) {
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
    if (!frames.is_array() || frames.empty() || frames.size() > 120)
        return fail(error_code::INVALID_DATA, "Invalid fit frame inventory");
    std::set<std::string> names;
    for (const auto& frame : frames) {
        const auto name = frame.at("name").get<std::string>();
        const auto found = poses.file_hashes.find("training/images/" + name);
        if (!names.insert(name).second || found == poses.file_hashes.end() ||
            found->second != frame.at("sha256").get<std::string>())
            return fail(error_code::INVALID_DATA,
                        "Fit images differ from the committed pose stage");
    }
    auto identity = result_file->at("identity").get<std::string>();
    if (!valid_digest(identity))
        return fail(error_code::INVALID_DATA, "Invalid fit identity");
    return identity;
}
} // namespace world_explorer::job_detail
