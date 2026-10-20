#include "color_inputs.hpp"

#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/image_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace world_explorer::color_inputs {
namespace {

using Json = nlohmann::json;

Json parse_document(std::span<const std::byte> bytes) {
    const std::string K_TEXT { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    if (K_TEXT.find('\0') != K_TEXT.npos) {
        throw std::runtime_error("Raw NUL in color audit JSON");
    }
    std::vector<std::set<std::string>> keys;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 8) {
            throw std::runtime_error("Color audit JSON nesting exceeds budget");
        }
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key &&
            !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second) {
            throw std::runtime_error("Duplicate color audit JSON key");
        }
        return true;
    });
}

std::uint64_t unsigned_field(const Json& value, const char* name) {
    const auto& field = value.at(name);
    if (!field.is_number_unsigned()) {
        throw std::runtime_error("Invalid unsigned color audit field");
    }
    return field.get<std::uint64_t>();
}

std::filesystem::path
request_path(const std::filesystem::path& base, const Json& value, const char* key) {
    const auto K_TEXT = value.at(key).get<std::string>();
    if (K_TEXT.empty() || K_TEXT.find('\0') != K_TEXT.npos) {
        throw std::runtime_error("Invalid color audit input path");
    }
    return std::filesystem::absolute(base / K_TEXT);
}

std::filesystem::path
cache_path(const std::filesystem::path& base, const std::string& name, const std::string& prefix) {
    if (!name.starts_with(prefix) || name.size() <= prefix.size() || name.size() > 128uz ||
        name.substr(prefix.size())
                .find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            name.npos) {
        throw std::runtime_error("Unsafe color cache path");
    }
    const auto K_PATH = base / name;
    if (std::filesystem::canonical(K_PATH) != K_PATH || !std::filesystem::is_regular_file(K_PATH)) {
        throw std::runtime_error("Linked color cache input");
    }
    return K_PATH;
}

SimilarityTransform camera_transform(const Json& frame) {
    const auto& transform = frame.at("world_from_camera");
    const auto K_Q = transform.at("rotation_wxyz").get<std::array<double, 4>>();
    const auto K_T = transform.at("translation").get<std::array<double, 3>>();
    return { { K_Q[0], K_Q[1], K_Q[2], K_Q[3] },
            { K_T[0], K_T[1], K_T[2] },
            transform.at("scale").get<double>() };
}

}  // namespace

Result<std::vector<Cache>> admit_caches(const Json& receipt,
                                        const std::filesystem::path& root,
                                        bool sequence,
                                        const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Color cache admission cancelled");
        }
        const auto K_REQUIRE = [](bool condition, const char* message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        };
        K_REQUIRE(std::filesystem::canonical(root) == root &&
                      unsigned_field(receipt, "schema_version") == 1 &&
                      receipt.at("kind") == (sequence ? "POSE_ADMITTED_DEPTH_SEQUENCE_V1"
                                                      : "POSE_ADMITTED_DEPTH_VISIBILITY_V1"),
                  "Color cache policy differs");
        const auto& frames = receipt.at("frames").get_ref<const Json::array_t&>();
        static_cast<void>(unsigned_field(receipt, "world_epoch"));
        static_cast<void>(unsigned_field(receipt, "pose_revision"));
        if (frames.empty() || frames.size() > (sequence ? 120uz : 18uz)) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color view budget exceeded");
        }
        Json batches = sequence ? receipt.at("batches")
                                : Json::array({ { { "cache", "" },
                                                { "first_manifest_index", 0U },
                                                { "frame_count", frames.size() } } });
        K_REQUIRE(batches.is_array() && !batches.empty() && batches.size() <= frames.size(),
                  "Invalid color sequence partition");
        std::vector<Cache> caches;
        std::set<std::uint64_t> seen;
        std::uint64_t valid_pixels {}, unknown_pixels {};
        std::size_t start {};
        for (std::size_t index { 0uz }; index < batches.size(); ++index) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Color cache admission cancelled");
            }
            const auto& batch = batches[index];
            const auto K_COUNT = unsigned_field(batch, "frame_count");
            K_REQUIRE(K_COUNT > 0 && K_COUNT <= 18 &&
                          unsigned_field(batch, "first_manifest_index") == start &&
                          K_COUNT <= frames.size() - start,
                      "Invalid color batch partition");
            auto child = receipt;
            auto child_root = root;
            if (sequence) {
                K_REQUIRE(batch.size() == 4uz &&
                              batch.at("cache") == "batch-" + std::to_string(index),
                          "Unsafe color batch path");
                child_root /= batch.at("cache").get<std::string>();
                const auto K_PATH = child_root / "manifest.json";
                K_REQUIRE(std::filesystem::canonical(K_PATH) == K_PATH,
                          "Linked color batch receipt");
                const auto K_BYTES = asset_files::read_bytes(K_PATH, 1024uz * 1024uz, stop);
                if (!K_BYTES) {
                    return std::unexpected(K_BYTES.error());
                }
                K_REQUIRE(sha256(*K_BYTES) == batch.at("sha256").get<std::string>(),
                          "Color batch checksum differs");
                child = parse_document(*K_BYTES);
            }
            K_REQUIRE(unsigned_field(child, "schema_version") == 1 &&
                          child.at("kind") == "POSE_ADMITTED_DEPTH_VISIBILITY_V1" &&
                          child.at("units") == "ARBITRARY_POSE_CAMERA_UNITS" &&
                          child.at("depth_convention") == "AXIAL_CAMERA_Z" &&
                          child.at("pixel_centers") == "integer" &&
                          child.at("camera_axes") == "+x_right,+y_down,+z_forward" &&
                          child.at("confidence") ==
                              "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY",
                      "Unsupported color cache semantics");
            for (const auto* key : { "input_manifest_sha256",
                                    "depth_recipe_sha256",
                                    "pose_asset_sha256",
                                    "world_epoch",
                                    "pose_revision" }) {
                K_REQUIRE(child.at(key) == receipt.at(key), "Color child dependency differs");
            }
            const auto& child_frames = child.at("frames").get_ref<const Json::array_t&>();
            static_cast<void>(unsigned_field(child, "world_epoch"));
            static_cast<void>(unsigned_field(child, "pose_revision"));
            K_REQUIRE(child_frames.size() == K_COUNT, "Color child frame count differs");
            const auto& files = child.at("files");
            std::set<std::string> required {
                "poses.weposes", "dense-inputs.json", "native-recipe.json", "depth-recipe.json" };
            std::size_t pixels {};
            std::uint64_t previous_index {};
            for (std::size_t local { 0uz }; local < child_frames.size(); ++local) {
                const auto& frame = child_frames[local];
                K_REQUIRE(frame == frames[start + local], "Color child frame order differs");
                const auto K_INDEX = unsigned_field(frame, "manifest_index");
                K_REQUIRE((sequence ? K_INDEX == start + local
                                    : local == 0uz || K_INDEX > previous_index) &&
                              unsigned_field(frame, "frame") > 0 &&
                              seen.insert(unsigned_field(frame, "frame")).second,
                          "Invalid color frame identity/order");
                previous_index = K_INDEX;
                const auto K_WIDTH = unsigned_field(frame, "width"),
                           K_HEIGHT = unsigned_field(frame, "height");
                if (K_WIDTH < 2 || K_HEIGHT < 2 || K_WIDTH > 4096 || K_HEIGHT > 4096 ||
                    K_WIDTH * K_HEIGHT > 48000000uz - pixels) {
                    return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color child pixel budget exceeded");
                }
                pixels += static_cast<std::size_t>(K_WIDTH * K_HEIGHT);
                const auto K_VALID = unsigned_field(frame, "valid_pixels"),
                           K_UNKNOWN = unsigned_field(frame, "unknown_pixels");
                K_REQUIRE(K_VALID <= K_WIDTH * K_HEIGHT &&
                              K_UNKNOWN == K_WIDTH * K_HEIGHT - K_VALID,
                          "Invalid color coverage counts");
                valid_pixels += K_VALID;
                unknown_pixels += K_UNKNOWN;
                for (const auto* key : { "image", "depth_asset" }) {
                    K_REQUIRE(required.insert(frame.at(key).get<std::string>()).second,
                              "Duplicate color cache asset");
                }
                K_REQUIRE(files.at(frame.at("depth_asset").get<std::string>()).at("sha256") ==
                                  frame.at("depth_sha256") &&
                              files.at(frame.at("image").get<std::string>()).at("sha256") ==
                                  frame.at("calibrated_image_sha256"),
                          "Color asset pin differs");
            }
            K_REQUIRE(files.is_object() && files.size() == required.size(),
                      "Color file inventory differs");
            std::uint64_t bytes {};
            for (const auto& name : required) {
                const auto& record = files.at(name);
                const auto K_SIZE = unsigned_field(record, "bytes");
                K_REQUIRE(record.size() == 2uz && K_SIZE <= 256uz * 1024uz * 1024uz,
                          "Color file byte budget exceeded");
                bytes += K_SIZE;
                K_REQUIRE(bytes <= 768uz * 1024uz * 1024uz, "Color cache byte budget exceeded");
                const auto K_PREFIX = name.starts_with("images/")   ? "images/"
                                      : name.starts_with("assets/") ? "assets/"
                                                                    : "";
                const auto K_PATH = cache_path(child_root, name, K_PREFIX);
                K_REQUIRE(std::filesystem::file_size(K_PATH) == K_SIZE, "Color file size differs");
                const auto K_SHA = sha256_file(K_PATH, static_cast<std::size_t>(K_SIZE), stop);
                if (!K_SHA) {
                    return std::unexpected(K_SHA.error());
                }
                K_REQUIRE(*K_SHA == record.at("sha256").get<std::string>(),
                          "Color file checksum differs");
            }
            for (const auto& [K_NAME, K_KEY] :
                 std::array { std::pair { "poses.weposes", "pose_asset_sha256" },
                             std::pair { "dense-inputs.json", "input_manifest_sha256" },
                             std::pair { "depth-recipe.json", "depth_recipe_sha256" } }) {
                K_REQUIRE(files.at(K_NAME).at("sha256") == child.at(K_KEY),
                          "Color metadata pin differs");
            }
            const auto K_DOCUMENT = [&](const char* name) -> Result<Json> {
                const auto K_BYTES =
                    asset_files::read_bytes(child_root / name, 1024uz * 1024uz, stop);
                if (!K_BYTES) {
                    return std::unexpected(K_BYTES.error());
                }
                K_REQUIRE(sha256(*K_BYTES) == files.at(name).at("sha256").get<std::string>(),
                          "Color metadata changed");
                return parse_document(*K_BYTES);
            };
            const auto K_MANIFEST = K_DOCUMENT("dense-inputs.json"),
                       K_NATIVE = K_DOCUMENT("native-recipe.json"),
                       K_RECIPE = K_DOCUMENT("depth-recipe.json");
            if (!K_MANIFEST || !K_NATIVE || !K_RECIPE) {
                return std::unexpected(!K_MANIFEST ? K_MANIFEST.error()
                                       : !K_NATIVE ? K_NATIVE.error()
                                                   : K_RECIPE.error());
            }
            K_REQUIRE(K_MANIFEST->at("kind") == "DENSE_INPUTS_RESIZED_GRID" &&
                          K_NATIVE->at("input_profile") == "colmap_native_dense_input_v1" &&
                          K_RECIPE->at("input_profile") == "colmap_resized_dense_input_v1" &&
                          (!sequence || K_MANIFEST->at("frames").size() == frames.size()),
                      "Color dense input policy differs");
            for (const auto* key :
                 { "pose_asset_sha256", "depth_recipe_sha256", "world_epoch", "pose_revision" }) {
                K_REQUIRE(K_MANIFEST->at(key) == child.at(key), "Color dense dependency differs");
            }
            for (const auto& frame : child_frames) {
                const auto K_INDEX = unsigned_field(frame, "manifest_index");
                auto expected = K_MANIFEST->at("frames").at(static_cast<std::size_t>(K_INDEX));
                const auto K_NAME = std::filesystem::path(expected.at("image").get<std::string>())
                                        .filename()
                                        .string();
                expected["image"] = "images/" + K_NAME;
                expected["depth_asset"] = "assets/" + K_NAME + ".wedepth";
                for (const auto* key : { "manifest_index",
                                        "depth_sha256",
                                        "valid_pixels",
                                        "unknown_pixels",
                                        "uncertainty" }) {
                    expected[key] = frame.at(key);
                }
                K_REQUIRE(expected == frame, "Color frame differs from dense domain");
            }
            start += static_cast<std::size_t>(K_COUNT);
            caches.push_back({ child_root, std::move(child) });
        }
        K_REQUIRE(start == frames.size() &&
                      (!sequence || (unsigned_field(receipt, "valid_pixels") == valid_pixels &&
                                     unsigned_field(receipt, "unknown_pixels") == unknown_pixels)),
                  "Incomplete color sequence coverage");
        return caches;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color cache allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<Inputs> load(const std::filesystem::path& request,
                    const std::string& expected_sha256,
                    const std::filesystem::path& destination,
                    const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Color audit cancelled");
        }
        if (destination.empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(destination))) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new color audit directory");
        }
        const auto K_BYTES = asset_files::read_bytes(request, 1024uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        if (sha256(*K_BYTES) != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "Color audit request checksum differs");
        }
        const auto K_REQUEST = parse_document(*K_BYTES);
        if (!K_REQUEST.is_object() || K_REQUEST.size() != 5uz ||
            (unsigned_field(K_REQUEST, "schema_version") != 1 &&
             unsigned_field(K_REQUEST, "schema_version") != 2)) {
            return fail(ErrorCode::INVALID_DATA, "Unsupported color audit request");
        }
        const auto K_BASE = std::filesystem::absolute(request).parent_path();
        const auto K_FUSION = request_path(K_BASE, K_REQUEST, "fusion_request");
        const auto K_CACHE = request_path(K_BASE, K_REQUEST, "cache");
        if (std::filesystem::canonical(K_CACHE) != K_CACHE.lexically_normal()) {
            return fail(ErrorCode::INVALID_DATA, "Linked color receipt");
        }
        const auto K_CACHE_ROOT = std::filesystem::canonical(K_CACHE).parent_path();
        const auto K_OUTPUT =
            std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
        const auto K_RELATIVE_OUTPUT = K_OUTPUT.lexically_relative(K_CACHE_ROOT);
        if (!K_RELATIVE_OUTPUT.empty() && *K_RELATIVE_OUTPUT.begin() != "..") {
            return fail(ErrorCode::INVALID_ARGUMENT, "Color output overlaps its immutable cache");
        }
        const auto K_SCENE = load_fusion_scene_request(
            K_FUSION, K_REQUEST.at("fusion_request_sha256").get<std::string>(), stop);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        const auto& chunk = *(*K_SCENE)->m_chunks.begin()->second;
        if (!chunk.m_mesh || !chunk.m_mesh->m_observed) {
            return fail(ErrorCode::INVALID_DATA, "Missing observed mesh source");
        }
        const auto& mesh = *chunk.m_mesh;
        const auto& source = *mesh.m_observed;
        const auto K_CACHE_BYTES = asset_files::read_bytes(K_CACHE, 1024uz * 1024uz, stop);
        if (!K_CACHE_BYTES) {
            return std::unexpected(K_CACHE_BYTES.error());
        }
        if (sha256(*K_CACHE_BYTES) != source.m_context.m_receipt_sha256 ||
            K_REQUEST.at("cache_sha256").get<std::string>() != source.m_context.m_receipt_sha256) {
            return fail(ErrorCode::INVALID_DATA,
                        "Color cache differs from admitted fusion receipt");
        }
        const auto K_RECEIPT = parse_document(*K_CACHE_BYTES);
        const auto K_SOURCE = Json::parse(source.m_manifest_json);
        const double K_VOXEL { K_SOURCE.at("voxel_size").get<double>() };
        const bool K_SEQUENCE { unsigned_field(K_REQUEST, "schema_version") == 2 };
        const auto K_CACHES = admit_caches(K_RECEIPT, K_CACHE_ROOT, K_SEQUENCE, stop);
        if (!K_CACHES) {
            return std::unexpected(K_CACHES.error());
        }
        Inputs inputs {
            *K_SCENE, K_CACHE_ROOT, K_OUTPUT, K_RECEIPT, K_VOXEL, K_SEQUENCE, *K_CACHES, {} };
        for (std::size_t index { 0uz }; index < inputs.m_caches.size(); ++index) {
            inputs.m_frame_caches.insert(inputs.m_frame_caches.end(),
                                         inputs.m_caches[index].m_receipt.at("frames").size(),
                                         index);
        }
        return inputs;
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
        const auto& cache = inputs.m_caches.at(inputs.m_frame_caches.at(index));
        const auto& chunk = *inputs.m_scene->m_chunks.begin()->second;
        if (!chunk.m_mesh || !chunk.m_mesh->m_observed) {
            return fail(ErrorCode::INVALID_DATA, "Missing color source");
        }
        const auto& source = *chunk.m_mesh->m_observed;
        DepthAssetMetadata metadata;
        metadata.m_dependency = { source.m_context.m_epoch,
                                 source.m_context.m_pose_revision,
                                 { unsigned_field(frame, "frame") },
                                 { unsigned_field(frame, "calibration") },
                                 source.m_context.m_pose_sha256,
                                 frame.at("observation_image_sha256").get<std::string>() };
        metadata.m_width = static_cast<std::uint32_t>(unsigned_field(frame, "width"));
        metadata.m_height = static_cast<std::uint32_t>(unsigned_field(frame, "height"));
        const auto K_INTRINSICS = frame.at("intrinsics").get<std::array<double, 4>>();
        metadata.m_camera = { K_INTRINSICS[0], K_INTRINSICS[1], K_INTRINSICS[2], K_INTRINSICS[3] };
        metadata.m_calibrated_image_sha256 = frame.at("calibrated_image_sha256").get<std::string>();
        metadata.m_recipe_sha256 = inputs.m_receipt.at("depth_recipe_sha256").get<std::string>();
        metadata.m_producer = DepthProducerKind::MULTIVIEW_STEREO;
        const auto K_DEPTH_NAME = frame.at("depth_asset").get<std::string>();
        const auto K_IMAGE_NAME = frame.at("image").get<std::string>();
        const auto K_DEPTH_PATH = cache_path(cache.m_root, K_DEPTH_NAME, "assets/");
        const auto K_IMAGE_PATH = cache_path(cache.m_root, K_IMAGE_NAME, "images/");
        const auto K_DEPTH_SHA = frame.at("depth_sha256").get<std::string>();
        const auto& files = cache.m_receipt.at("files");
        if (files.at(K_DEPTH_NAME).at("sha256") != K_DEPTH_SHA ||
            files.at(K_IMAGE_NAME).at("sha256") != metadata.m_calibrated_image_sha256 ||
            std::filesystem::file_size(K_DEPTH_PATH) !=
                unsigned_field(files.at(K_DEPTH_NAME), "bytes") ||
            std::filesystem::file_size(K_IMAGE_PATH) !=
                unsigned_field(files.at(K_IMAGE_NAME), "bytes")) {
            return fail(ErrorCode::INVALID_DATA, "Color cache file identity differs");
        }
        auto depth = load_pose_bound_depth({ K_DEPTH_PATH,
                                            cache.m_root / "poses.weposes",
                                            K_DEPTH_SHA,
                                            source.m_context.m_pose_sha256,
                                            metadata },
                                           {},
                                           stop);
        if (!depth) {
            return std::unexpected(depth.error());
        }
        const auto K_VALID_PIXELS = std::ranges::count(depth->m_valid, std::uint8_t { 1 });
        if (static_cast<std::uint64_t>(K_VALID_PIXELS) != unsigned_field(frame, "valid_pixels") ||
            depth->m_valid.size() - static_cast<std::size_t>(K_VALID_PIXELS) !=
                unsigned_field(frame, "unknown_pixels") ||
            frame.at("uncertainty") !=
                (depth->m_standard_deviation ? "CAMERA_UNIT_STANDARD_DEVIATION" : "UNAVAILABLE")) {
            return fail(ErrorCode::INVALID_DATA, "Color depth coverage differs");
        }
        auto rgb = decode_calibrated_rgb(K_IMAGE_PATH,
                                         metadata.m_calibrated_image_sha256,
                                         metadata.m_width,
                                         metadata.m_height,
                                         stop);
        if (!rgb) {
            return std::unexpected(rgb.error());
        }
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
