#include "color_inputs.hpp"

#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/image_decoder.hpp"

#include <set>

namespace world_explorer::color_inputs {
namespace {

using Json = nlohmann::json;

Json parse_document(std::span<const std::byte> bytes) {
    const std::string K_TEXT { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    if (K_TEXT.find('\0') != K_TEXT.npos) throw std::runtime_error("Raw NUL in color audit JSON");
    std::vector<std::set<std::string>> keys;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 8) throw std::runtime_error("Color audit JSON nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key &&
            !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate color audit JSON key");
        return true;
    });
}

std::uint64_t unsigned_field(const Json& value, const char* name) {
    const auto& field = value.at(name);
    if (!field.is_number_unsigned()) throw std::runtime_error("Invalid unsigned color audit field");
    return field.get<std::uint64_t>();
}

std::filesystem::path
request_path(const std::filesystem::path& base, const Json& value, const char* key) {
    const auto K_TEXT = value.at(key).get<std::string>();
    if (K_TEXT.empty() || K_TEXT.find('\0') != K_TEXT.npos)
        throw std::runtime_error("Invalid color audit input path");
    return std::filesystem::absolute(base / K_TEXT);
}

std::filesystem::path
cache_path(const std::filesystem::path& base, const std::string& name, const std::string& prefix) {
    if (!name.starts_with(prefix) || name.size() <= prefix.size() || name.size() > 128uz ||
        name.substr(prefix.size())
                .find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            name.npos)
        throw std::runtime_error("Unsafe color cache path");
    const auto K_PATH = base / name;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(K_PATH.parent_path())) ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(K_PATH)))
        throw std::runtime_error("Linked color cache input");
    return K_PATH;
}

SimilarityTransform camera_transform(const Json& frame) {
    const auto& transform = frame.at("world_from_camera");
    const auto K_Q = transform.at("rotation_wxyz").get<std::array<double, 4>>();
    const auto K_T = transform.at("translation").get<std::array<double, 3>>();
    return {{ K_Q[0], K_Q[1], K_Q[2], K_Q[3] },
            { K_T[0], K_T[1], K_T[2] },
            transform.at("scale").get<double>()};
}

}  // namespace

Result<Inputs> load(const std::filesystem::path& request,
                    const std::string& expected_sha256,
                    const std::filesystem::path& destination,
                    const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Color audit cancelled");
        if (destination.empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new color audit directory");
        const auto K_BYTES = asset_files::read_bytes(request, 1024uz * 1024uz, stop);
        if (!K_BYTES) return std::unexpected(K_BYTES.error());
        if (sha256(*K_BYTES) != expected_sha256)
            return fail(ErrorCode::INVALID_DATA, "Color audit request checksum differs");
        const auto K_REQUEST = parse_document(*K_BYTES);
        if (!K_REQUEST.is_object() || K_REQUEST.size() != 5uz ||
            unsigned_field(K_REQUEST, "schema_version") != 1)
            return fail(ErrorCode::INVALID_DATA, "Unsupported color audit request");
        const auto K_BASE = std::filesystem::absolute(request).parent_path();
        const auto K_FUSION = request_path(K_BASE, K_REQUEST, "fusion_request");
        const auto K_CACHE = request_path(K_BASE, K_REQUEST, "cache");
        const auto K_CACHE_ROOT = std::filesystem::canonical(K_CACHE).parent_path();
        const auto K_OUTPUT =
            std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
        const auto K_RELATIVE_OUTPUT = K_OUTPUT.lexically_relative(K_CACHE_ROOT);
        if (!K_RELATIVE_OUTPUT.empty() && *K_RELATIVE_OUTPUT.begin() != "..")
            return fail(ErrorCode::INVALID_ARGUMENT, "Color output overlaps its immutable cache");
        const auto K_SCENE = load_fusion_scene_request(
            K_FUSION, K_REQUEST.at("fusion_request_sha256").get<std::string>(), stop);
        if (!K_SCENE) return std::unexpected(K_SCENE.error());
        const auto& chunk = *(*K_SCENE)->m_chunks.begin()->second;
        if (!chunk.m_mesh || !chunk.m_mesh->m_observed)
            return fail(ErrorCode::INVALID_DATA, "Missing observed mesh source");
        const auto& mesh = *chunk.m_mesh;
        const auto& source = *mesh.m_observed;
        const auto K_CACHE_BYTES = asset_files::read_bytes(K_CACHE, 1024uz * 1024uz, stop);
        if (!K_CACHE_BYTES) return std::unexpected(K_CACHE_BYTES.error());
        if (sha256(*K_CACHE_BYTES) != source.m_context.m_receipt_sha256 ||
            K_REQUEST.at("cache_sha256").get<std::string>() != source.m_context.m_receipt_sha256)
            return fail(ErrorCode::INVALID_DATA,
                        "Color cache differs from admitted fusion receipt");
        const auto K_RECEIPT = parse_document(*K_CACHE_BYTES);
        const auto K_SOURCE = Json::parse(source.m_manifest_json);
        const double K_VOXEL { K_SOURCE.at("voxel_size").get<double>() };
        const auto& frames = K_RECEIPT.at("frames");
        if (frames.empty() || frames.size() > 18uz)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color view budget exceeded");
        std::size_t total_pixels {};
        for (const auto& frame : frames) {
            const auto K_WIDTH = unsigned_field(frame, "width"),
                       K_HEIGHT = unsigned_field(frame, "height");
            if (K_WIDTH < 2 || K_HEIGHT < 2 || K_WIDTH > 4096 || K_HEIGHT > 4096 ||
                K_WIDTH * K_HEIGHT > 48000000uz - total_pixels)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color pixel budget exceeded");
            total_pixels += static_cast<std::size_t>(K_WIDTH * K_HEIGHT);
        }
        return Inputs { *K_SCENE, K_CACHE_ROOT, K_OUTPUT, K_RECEIPT, K_VOXEL };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color input allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<ColorProjectionView>
view(const Inputs& inputs, std::size_t index, const std::stop_token& stop) {
    try {
        const auto& frame = inputs.m_receipt.at("frames").at(index);
        const auto& chunk = *inputs.m_scene->m_chunks.begin()->second;
        if (!chunk.m_mesh || !chunk.m_mesh->m_observed)
            return fail(ErrorCode::INVALID_DATA, "Missing color source");
        const auto& source = *chunk.m_mesh->m_observed;
        DepthAssetMetadata metadata;
        metadata.m_dependency = {source.m_context.m_epoch,
                                 source.m_context.m_pose_revision,
                                 { unsigned_field(frame, "frame") },
                                 { unsigned_field(frame, "calibration") },
                                 source.m_context.m_pose_sha256,
                                 frame.at("observation_image_sha256").get<std::string>()};
        metadata.m_width = static_cast<std::uint32_t>(unsigned_field(frame, "width"));
        metadata.m_height = static_cast<std::uint32_t>(unsigned_field(frame, "height"));
        const auto K_INTRINSICS = frame.at("intrinsics").get<std::array<double, 4>>();
        metadata.m_camera = { K_INTRINSICS[0], K_INTRINSICS[1], K_INTRINSICS[2], K_INTRINSICS[3] };
        metadata.m_calibrated_image_sha256 = frame.at("calibrated_image_sha256").get<std::string>();
        metadata.m_recipe_sha256 = inputs.m_receipt.at("depth_recipe_sha256").get<std::string>();
        metadata.m_producer = DepthProducerKind::MULTIVIEW_STEREO;
        const auto K_DEPTH_NAME = frame.at("depth_asset").get<std::string>();
        const auto K_IMAGE_NAME = frame.at("image").get<std::string>();
        const auto K_DEPTH_PATH = cache_path(inputs.m_cache_root, K_DEPTH_NAME, "assets/");
        const auto K_IMAGE_PATH = cache_path(inputs.m_cache_root, K_IMAGE_NAME, "images/");
        const auto K_DEPTH_SHA = frame.at("depth_sha256").get<std::string>();
        const auto& files = inputs.m_receipt.at("files");
        if (files.at(K_DEPTH_NAME).at("sha256") != K_DEPTH_SHA ||
            files.at(K_IMAGE_NAME).at("sha256") != metadata.m_calibrated_image_sha256 ||
            std::filesystem::file_size(K_DEPTH_PATH) !=
                unsigned_field(files.at(K_DEPTH_NAME), "bytes") ||
            std::filesystem::file_size(K_IMAGE_PATH) !=
                unsigned_field(files.at(K_IMAGE_NAME), "bytes"))
            return fail(ErrorCode::INVALID_DATA, "Color cache file identity differs");
        auto depth = load_pose_bound_depth({K_DEPTH_PATH,
                                            inputs.m_cache_root / "poses.weposes",
                                            K_DEPTH_SHA,
                                            source.m_context.m_pose_sha256,
                                            metadata},
                                           {},
                                           stop);
        if (!depth) return std::unexpected(depth.error());
        auto rgb = decode_calibrated_rgb(K_IMAGE_PATH,
                                         metadata.m_calibrated_image_sha256,
                                         metadata.m_width,
                                         metadata.m_height,
                                         stop);
        if (!rgb) return std::unexpected(rgb.error());
        auto view = ColorProjectionView::create(
            std::move(*depth), std::move(*rgb), camera_transform(frame), inputs.m_voxel_size, stop);
        return view;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color view allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer::color_inputs
