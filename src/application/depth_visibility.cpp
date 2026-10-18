#include "world_explorer/application/depth_visibility.hpp"

#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <set>
#include <string_view>

#include <nlohmann/json.hpp>

namespace world_explorer {
namespace {

constexpr std::size_t K_MAX_CACHE_BYTES { 768uz * 1024uz * 1024uz };
constexpr std::size_t K_MAX_PIXELS { 48000000uz };

bool within(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto K_RELATIVE = path.lexically_relative(root);
    return !K_RELATIVE.empty() && *K_RELATIVE.begin() != "..";
}

Result<std::vector<std::byte>> pinned(const std::filesystem::path& path,
                                      const std::string& expected,
                                      std::size_t maximum,
                                      const std::stop_token& stop) {
    auto bytes = asset_files::read_bytes(path, maximum, stop);
    if (!bytes) return std::unexpected(bytes.error());
    if (sha256(*bytes) != expected)
        return fail(ErrorCode::INVALID_DATA, "Visibility input checksum differs");
    return bytes;
}

}  // namespace

Result<std::string> export_depth_visibility_cache(const std::filesystem::path& request,
                                                  const std::string& request_sha256,
                                                  const std::filesystem::path& destination,
                                                  const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Visibility export cancelled");
        const auto K_CONFIG_BYTES = pinned(request, request_sha256, 1024uz * 1024uz, stop);
        if (!K_CONFIG_BYTES) return std::unexpected(K_CONFIG_BYTES.error());
        if (std::ranges::find(*K_CONFIG_BYTES, std::byte {}) != K_CONFIG_BYTES->end())
            return fail(ErrorCode::INVALID_DATA, "Visibility request contains a raw NUL byte");
        const auto K_CONFIG_TEXT =
            K_CONFIG_BYTES->empty()
                ? std::string_view {}
                : std::string_view { reinterpret_cast<const char*>(K_CONFIG_BYTES->data()),
                                    K_CONFIG_BYTES->size() };
        const auto K_CONFIG = nlohmann::json::parse(K_CONFIG_TEXT);
        const std::set<std::string> K_FIELDS { "schema_version",
                                              "pose_job",
                                              "source_sha256",
                                              "poses",
                                              "pose_sha256",
                                              "native_recipe",
                                              "native_recipe_sha256",
                                              "recipe",
                                              "recipe_sha256",
                                              "manifest",
                                              "manifest_sha256",
                                              "decode_stage",
                                              "outputs" };
        if (!K_CONFIG.is_object() || K_CONFIG.size() != K_FIELDS.size())
            return fail(ErrorCode::INVALID_DATA, "Invalid visibility request schema");
        for (const auto& key : K_FIELDS) {
            if (!K_CONFIG.contains(key))
                return fail(ErrorCode::INVALID_DATA, "Missing visibility request field");
        }
        if (!K_CONFIG.at("schema_version").is_number_unsigned() ||
            K_CONFIG.at("schema_version") != 1 || !K_CONFIG.at("outputs").is_array() ||
            K_CONFIG.at("outputs").empty() || K_CONFIG.at("outputs").size() > 18uz)
            return fail(ErrorCode::INVALID_DATA, "Invalid visibility inventory/version");
        const auto K_BASE = std::filesystem::canonical(request).parent_path();
        const auto K_PATH = [&](const std::string& key) {
            const std::filesystem::path K_VALUE { K_CONFIG.at(key).get<std::string>() };
            return K_VALUE.empty() ? K_VALUE : std::filesystem::absolute(K_BASE / K_VALUE);
        };
        const DenseInputRequest K_NATIVE { { K_PATH("pose_job"),
                                           K_CONFIG.at("source_sha256").get<std::string>(),
                                           K_PATH("decode_stage") },
                                          K_PATH("poses"),
                                          K_CONFIG.at("pose_sha256").get<std::string>(),
                                          K_PATH("native_recipe"),
                                          K_CONFIG.at("native_recipe_sha256").get<std::string>() };
        const ResizedDenseInputRequest K_INPUT {
            K_NATIVE, K_PATH("recipe"), K_CONFIG.at("recipe_sha256").get<std::string>() };
        const auto K_MANIFEST = K_PATH("manifest");
        if (destination.empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return fail(ErrorCode::INVALID_ARGUMENT, "Visibility destination must be new");
        const auto K_OUTPUT = std::filesystem::weakly_canonical(destination);
        std::vector<std::filesystem::path> inputs { request,
                                                   K_NATIVE.m_completed.m_pose_job,
                                                   K_NATIVE.m_completed.m_decode_stage,
                                                   K_NATIVE.m_canonical_poses,
                                                   K_NATIVE.m_depth_recipe,
                                                   K_INPUT.m_depth_recipe,
                                                   K_MANIFEST.parent_path() };
        std::vector<std::filesystem::path> depth_paths;
        for (const auto& record : K_CONFIG.at("outputs")) {
            if (!record.is_object() || record.size() != 3uz || !record.contains("frame") ||
                !record.at("frame").is_number_unsigned() || !record.contains("path") ||
                !record.contains("sha256"))
                return fail(ErrorCode::INVALID_DATA, "Invalid selected depth record");
            const std::filesystem::path K_PATH_VALUE { record.at("path").get<std::string>() };
            if (K_PATH_VALUE.empty()) return fail(ErrorCode::INVALID_DATA, "Empty depth path");
            const auto K_DEPTH = std::filesystem::absolute(K_BASE / K_PATH_VALUE);
            depth_paths.push_back(K_DEPTH);
            inputs.push_back(K_DEPTH.parent_path());
        }
        for (const auto& input : inputs) {
            if (input.empty()) continue;
            const auto K_SELECTED = std::filesystem::canonical(input);
            if (within(K_OUTPUT, K_SELECTED) || within(K_SELECTED, K_OUTPUT))
                return fail(ErrorCode::INVALID_ARGUMENT, "Visibility output overlaps an input");
        }
        const auto K_PREPARED = load_resized_dense_inputs(
            K_INPUT, K_MANIFEST, K_CONFIG.at("manifest_sha256").get<std::string>(), stop);
        if (!K_PREPARED) return std::unexpected(K_PREPARED.error());
        const auto K_DOCUMENT = nlohmann::json::parse(K_PREPARED->m_manifest_json);
        std::vector<std::size_t> indices;
        std::size_t pixels {};
        for (const auto& record : K_CONFIG.at("outputs")) {
            const auto K_FRAME = record.at("frame").get<std::uint64_t>();
            const auto K_FOUND = std::ranges::find_if(K_PREPARED->m_frames, [&](const auto& frame) {
                return frame.m_expected_depth.m_dependency.m_frame.m_value == K_FRAME;
            });
            if (K_FOUND == K_PREPARED->m_frames.end())
                return fail(ErrorCode::INVALID_DATA,
                            "Selected depth frame is not in checked inputs");
            const auto K_INDEX = static_cast<std::size_t>(K_FOUND - K_PREPARED->m_frames.begin());
            if (!indices.empty() && K_INDEX <= indices.back())
                return fail(ErrorCode::INVALID_DATA,
                            "Depth inventory must follow unique manifest order");
            indices.push_back(K_INDEX);
            const auto& metadata = K_FOUND->m_expected_depth;
            const auto K_PIXELS = static_cast<std::size_t>(metadata.m_width) * metadata.m_height;
            if (K_PIXELS > K_MAX_PIXELS - pixels)
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Visibility decoded pixel budget exceeded");
            pixels += K_PIXELS;
        }
        if (!std::filesystem::create_directory(K_OUTPUT))
            return fail(ErrorCode::IO_ERROR, "Cannot reserve visibility directory");
        const auto K_DIRECTORY_FLUSH = asset_files::flush_directory_entry(K_OUTPUT.parent_path());
        if (!K_DIRECTORY_FLUSH) return std::unexpected(K_DIRECTORY_FLUSH.error());
        std::filesystem::create_directory(K_OUTPUT / "assets");
        std::filesystem::create_directory(K_OUTPUT / "images");
        auto files = nlohmann::json::object();
        std::size_t total_bytes {};
        const auto K_PUBLISH = [&](const std::string& name,
                                   std::span<const std::byte> bytes) -> Result<std::string> {
            if (bytes.size() > K_MAX_CACHE_BYTES - total_bytes)
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Visibility encoded byte budget exceeded");
            const auto K_HASH = asset_files::publish_asset(K_OUTPUT / name, bytes, stop);
            if (!K_HASH) return std::unexpected(K_HASH.error());
            total_bytes += bytes.size();
            files[name] = { { "sha256", *K_HASH }, { "bytes", bytes.size() } };
            return K_HASH;
        };
        for (const auto& entry :
             std::vector<std::pair<std::string, std::pair<std::filesystem::path, std::string>>> {
                 { "poses.weposes", { K_NATIVE.m_canonical_poses, K_NATIVE.m_pose_sha256 } },
                 { "dense-inputs.json",
                  { K_MANIFEST, K_CONFIG.at("manifest_sha256").get<std::string>() } },
                 { "native-recipe.json", { K_NATIVE.m_depth_recipe, K_NATIVE.m_depth_recipe_sha256 } },
                 { "depth-recipe.json", { K_INPUT.m_depth_recipe, K_INPUT.m_depth_recipe_sha256 } } }) {
            const auto K_BYTES =
                pinned(entry.second.first,
                       entry.second.second,
                       entry.first == "poses.weposes" ? 256uz * 1024uz * 1024uz : 1024uz * 1024uz,
                       stop);
            if (!K_BYTES) return std::unexpected(K_BYTES.error());
            const auto K_WRITTEN = K_PUBLISH(entry.first, *K_BYTES);
            if (!K_WRITTEN) return std::unexpected(K_WRITTEN.error());
        }
        auto frames = nlohmann::json::array();
        for (std::size_t i { 0uz }; i < indices.size(); ++i) {
            const auto& selected = K_PREPARED->m_frames[indices[i]];
            const auto& record = K_CONFIG.at("outputs")[i];
            const auto K_DEPTH = load_pose_bound_depth({ depth_paths[i],
                                                        K_NATIVE.m_canonical_poses,
                                                        record.at("sha256").get<std::string>(),
                                                        K_NATIVE.m_pose_sha256,
                                                        selected.m_expected_depth },
                                                       {},
                                                       stop);
            if (!K_DEPTH) return std::unexpected(K_DEPTH.error());
            const auto K_BYTES = encode_depth_asset(*K_DEPTH, {}, stop);
            if (!K_BYTES) return std::unexpected(K_BYTES.error());
            if (sha256(*K_BYTES) != record.at("sha256").get<std::string>())
                return fail(ErrorCode::INVALID_DATA,
                            "Admitted depth re-encoding differs from selected bytes");
            const auto K_NAME = std::filesystem::path(selected.m_image_name).filename().string();
            const auto K_DEPTH_NAME = "assets/" + K_NAME + ".wedepth";
            const auto K_WRITTEN_DEPTH = K_PUBLISH(K_DEPTH_NAME, *K_BYTES);
            if (!K_WRITTEN_DEPTH) return std::unexpected(K_WRITTEN_DEPTH.error());
            const auto K_RGB = pinned(selected.m_calibrated_image,
                                      selected.m_expected_depth.m_calibrated_image_sha256,
                                      64uz * 1024uz * 1024uz,
                                      stop);
            if (!K_RGB) return std::unexpected(K_RGB.error());
            const auto K_IMAGE_NAME = "images/" + K_NAME;
            const auto K_WRITTEN_RGB = K_PUBLISH(K_IMAGE_NAME, *K_RGB);
            if (!K_WRITTEN_RGB) return std::unexpected(K_WRITTEN_RGB.error());
            auto frame = K_DOCUMENT.at("frames")[indices[i]];
            frame["image"] = K_IMAGE_NAME;
            frame["manifest_index"] = indices[i];
            frame["depth_asset"] = K_DEPTH_NAME;
            frame["depth_sha256"] = *K_WRITTEN_DEPTH;
            const auto K_VALID = std::ranges::count(K_DEPTH->m_valid, std::uint8_t { 1 });
            frame["valid_pixels"] = K_VALID;
            frame["unknown_pixels"] = K_DEPTH->m_valid.size() - static_cast<std::size_t>(K_VALID);
            frame["uncertainty"] =
                K_DEPTH->m_standard_deviation ? "CAMERA_UNIT_STANDARD_DEVIATION" : "UNAVAILABLE";
            frames.push_back(std::move(frame));
        }
        const nlohmann::json K_RECEIPT {
            { "schema_version", 1 },
            { "kind", "POSE_ADMITTED_DEPTH_VISIBILITY_V1" },
            { "scope",
             "partial pose-admitted depth/RGB cache; geometry quality, fusion and walking "
             "unverified" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "camera_axes", "+x_right,+y_down,+z_forward" },
            { "pixel_centers", "integer" },
            { "depth_convention", "AXIAL_CAMERA_Z" },
            { "confidence", "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY" },
            { "request_sha256", request_sha256 },
            { "pose_asset_sha256", K_NATIVE.m_pose_sha256 },
            { "input_manifest_sha256", K_CONFIG.at("manifest_sha256") },
            { "depth_recipe_sha256", K_INPUT.m_depth_recipe_sha256 },
            { "world_epoch", K_DOCUMENT.at("world_epoch") },
            { "pose_revision", K_DOCUMENT.at("pose_revision") },
            { "frames", frames },
            { "files", files } };
        const auto K_TEXT = K_RECEIPT.dump(2) + "\n";
        if (K_TEXT.size() > 1024uz * 1024uz)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Visibility receipt exceeds byte budget");
        return asset_files::publish_asset(
            K_OUTPUT / "manifest.json", std::as_bytes(std::span(K_TEXT)), stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Visibility export allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Visibility export failed: ") + exception.what());
    }
}

}  // namespace world_explorer
