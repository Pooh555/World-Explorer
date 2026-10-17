#include "world_explorer/application/dense_inputs.hpp"
#include "job_validation.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/colmap_images.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace world_explorer {
namespace {
bool close(double first, double second) {
    return std::isfinite(first) && std::isfinite(second) &&
           std::abs(first - second) <= 1e-8 * std::max({1., std::abs(first), std::abs(second)});
}
bool same_pose(const similarity_transform& first, const similarity_transform& second) {
    if (!close(first.scale, second.scale))
        return false;
    for (const auto point : {vec3d{}, vec3d{1, 0, 0}, vec3d{0, 1, 0}, vec3d{0, 0, 1}}) {
        const auto a = transform_point(first, point);
        const auto b = transform_point(second, point);
        if (!close(a.x, b.x) || !close(a.y, b.y) || !close(a.z, b.z))
            return false;
    }
    return true;
}
result<void> check_png(const std::filesystem::path& file, const std::string& hash,
                       const training_camera& camera, const std::stop_token& stop) {
    const auto bytes = asset_files::read_bytes(file, std::size_t{64} * 1024 * 1024, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    constexpr std::array<unsigned char, 16> HEADER{137, 80, 78, 71, 13, 10, 26, 10,
                                                   0,   0,  0,  13, 73, 72, 68, 82};
    if (sha256(*bytes) != hash || bytes->size() < 33 ||
        !std::equal(HEADER.begin(), HEADER.end(), bytes->begin(),
                    [](auto a, auto b) { return a == std::to_integer<unsigned char>(b); }))
        return fail(error_code::INVALID_DATA, "Dense image checksum/PNG header differs");
    const auto number = [&](std::size_t offset) {
        std::uint32_t value{};
        for (std::size_t i = offset; i < offset + 4; ++i)
            value = (value << 8U) | std::to_integer<unsigned char>((*bytes)[i]);
        return value;
    };
    if (number(16) != camera.width || number(20) != camera.height || (*bytes)[24] != std::byte{8} ||
        (*bytes)[25] != std::byte{2} || (*bytes)[26] != std::byte{0} ||
        (*bytes)[27] != std::byte{0} || (*bytes)[28] != std::byte{0})
        return fail(error_code::INVALID_DATA, "Dense PNG must match native RGB8 calibrated grid");
    // Full pixel decoding belongs to the consuming isolated worker. This checks
    // encoded identity/header, not PNG decompression or photometric correctness.
    return {};
}
bool within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto relative = child.lexically_relative(parent);
    return child == parent || (!relative.empty() && *relative.begin() != "..");
}
} // namespace
result<prepared_dense_inputs> prepare_dense_inputs(const dense_input_request& request,
                                                   const std::stop_token& stop) {
    try {
        const auto selected_recipe =
            asset_files::read_bytes(request.depth_recipe, std::size_t{1024} * 1024, stop);
        if (!selected_recipe)
            return std::unexpected(selected_recipe.error());
        if (!job_detail::valid_digest(request.depth_recipe_sha256) ||
            sha256(*selected_recipe) != request.depth_recipe_sha256)
            return fail(error_code::INVALID_DATA, "Dense depth recipe checksum differs");
        // libc++ JSON adapters require character iterators; preserve the hashed byte extent.
        const auto depth_recipe = nlohmann::json::parse(
            reinterpret_cast<const char*>(selected_recipe->data()),
            reinterpret_cast<const char*>(selected_recipe->data() + selected_recipe->size()),
            [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 32)
                    throw std::invalid_argument("Dense recipe nesting exceeds profile");
                return true;
            });
        if (depth_recipe.at("schema_version") != 1 ||
            depth_recipe.at("input_profile") != "colmap_native_dense_input_v1" ||
            depth_recipe.at("depth_producer") != "MULTIVIEW_STEREO")
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Unsupported dense depth recipe input profile");
        const auto canonical =
            read_pose_asset(request.canonical_poses, request.pose_sha256, {}, stop);
        if (!canonical)
            return std::unexpected(canonical.error());
        const auto dataset = load_completed_poses(request.completed, stop);
        if (!dataset)
            return std::unexpected(dataset.error());
        const auto reconstructed = pose_pipeline::canonical(**dataset, stop);
        if (!reconstructed)
            return std::unexpected(reconstructed.error());
        const auto encoded = encode_pose_asset(*reconstructed, {}, stop);
        if (!encoded)
            return std::unexpected(encoded.error());
        if (sha256(*encoded) != request.pose_sha256)
            return fail(error_code::RESYNC_REQUIRED,
                        "Canonical poses differ from retained pose/decode inputs");
        reconstruction_job job;
        job.pose_job = request.completed.pose_job;
        const auto poses = job_detail::verify_poses(job, request.completed.source_sha256, stop);
        if (!poses)
            return std::unexpected(poses.error());
        if (poses->completion_hash != (*dataset)->pose_completion_sha256)
            return fail(error_code::RESYNC_REQUIRED, "Dense pose stage changed during preparation");
        const auto camera =
            read_colmap_training_camera(poses->training / "sparse/cameras.bin",
                                        poses->file_hashes.at("training/sparse/cameras.bin"), stop);
        if (!camera)
            return std::unexpected(camera.error());
        const auto images =
            read_colmap_image_poses(poses->training / "sparse/images.bin",
                                    poses->file_hashes.at("training/sparse/images.bin"), stop);
        if (!images)
            return std::unexpected(images.error());
        if (images->size() != (*dataset)->training_frames.size())
            return fail(error_code::INVALID_DATA, "Dense model image inventory differs");
        const nlohmann::json recipe{{"recipe", "colmap_native_dense_input_v1"},
                                    {"crop", "none"},
                                    {"resize", "none"},
                                    {"pixel_centers", "integer"},
                                    {"image_encoding", "PNG_RGB8"}};
        const auto recipe_bytes = recipe.dump();
        const auto recipe_hash = sha256(std::as_bytes(std::span(recipe_bytes)));
        const auto& source_camera = (*dataset)->calibrations.front();
        const nlohmann::json source_calibration{
            {"calibration", source_camera.id.value},
            {"width", source_camera.width},
            {"height", source_camera.height},
            {"model", source_camera.model == calibration_model::OPENCV ? "OPENCV" : "PINHOLE"},
            {"intrinsics",
             {source_camera.camera.fx, source_camera.camera.fy, source_camera.camera.cx,
              source_camera.camera.cy}},
            {"distortion_k1_k2_p1_p2", source_camera.distortion},
            {"image_transform", source_camera.image_transform}};
        prepared_dense_inputs prepared;
        auto frames = nlohmann::json::array();
        const auto& map = *canonical->snapshot;
        auto ordered = *images;
        std::ranges::sort(ordered, {}, &colmap_image_pose::name);
        for (const auto& image : ordered) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Dense preparation cancelled");
            const auto frame = (*dataset)->training_frames.find(image.name);
            if (frame == (*dataset)->training_frames.end() ||
                image.camera_id != camera->backend_camera_id)
                return fail(error_code::INVALID_DATA, "Dense image/calibration identity differs");
            const auto& observation = map.observations.at(frame->second);
            const auto& pose = map.poses.at(frame->second);
            if (map.invalidated_frames.contains(frame->second) ||
                (pose.tracking != tracking_state::TRACKING &&
                 pose.tracking != tracking_state::FINISHED) ||
                !same_pose(image.world_from_camera, pose.submap_from_camera))
                return fail(error_code::RESYNC_REQUIRED,
                            "Dense model pose differs from canonical pose");
            const auto name = "training/images/" + image.name;
            const auto hash = poses->file_hashes.at(name);
            const auto path = job_detail::contained_file(poses->directory, name);
            if (!path)
                return std::unexpected(path.error());
            const auto png = check_png(*path, hash, *camera, stop);
            if (!png)
                return std::unexpected(png.error());
            depth_asset_metadata metadata;
            metadata.dependency = {map.epoch,           map.revision,
                                   frame->second,       observation.calibration,
                                   request.pose_sha256, observation.image_sha256};
            metadata.width = camera->width;
            metadata.height = camera->height;
            metadata.camera = camera->camera;
            metadata.calibrated_image_sha256 = hash;
            metadata.recipe_sha256 = request.depth_recipe_sha256;
            metadata.producer = depth_producer_kind::MULTIVIEW_STEREO;
            prepared.frames.push_back({image.name, *path, metadata});
            const auto& transform = pose.submap_from_camera;
            const auto& q = transform.rotation;
            const auto& t = transform.translation;
            frames.push_back(
                {{"frame", frame->second.value},
                 {"calibration", observation.calibration.value},
                 {"backend_image_id", image.image_id},
                 {"backend_camera_id", image.camera_id},
                 {"image", name},
                 {"calibrated_image_sha256", hash},
                 {"observation_image_sha256", observation.image_sha256},
                 {"width", metadata.width},
                 {"height", metadata.height},
                 {"intrinsics",
                  {metadata.camera.fx, metadata.camera.fy, metadata.camera.cx, metadata.camera.cy}},
                 {"world_from_camera",
                  {{"rotation_wxyz", {q.w, q.x, q.y, q.z}},
                   {"translation", {t.x, t.y, t.z}},
                   {"scale", transform.scale}}}});
        }
        // Input identities are checked again after assembling the manifest. The
        // caller must recheck before consumption; paths are not immutable leases.
        const auto final_pose =
            sha256_file(request.canonical_poses, std::size_t{256} * 1024 * 1024, stop);
        const auto final_completion =
            sha256_file(poses->directory / "completion.json", std::size_t{16} * 1024 * 1024, stop);
        if (!final_pose)
            return std::unexpected(final_pose.error());
        if (!final_completion)
            return std::unexpected(final_completion.error());
        if (*final_pose != request.pose_sha256 || *final_completion != poses->completion_hash)
            return fail(error_code::RESYNC_REQUIRED,
                        "Dense dependencies changed during preparation");
        prepared.manifest_json =
            nlohmann::json{
                {"schema_version", 1},
                {"kind", "DENSE_INPUTS_NATIVE_GRID"},
                {"scope",
                 "checked input identities and calibration; no depth or geometry produced"},
                {"world_epoch", map.epoch.value},
                {"pose_revision", map.revision.value},
                {"source_sha256", request.completed.source_sha256},
                {"source_calibration", source_calibration},
                {"pose_asset_sha256", request.pose_sha256},
                {"pose_completion_sha256", poses->completion_hash},
                {"decode_completion_sha256", (*dataset)->decode_completion_sha256},
                {"cameras_sha256", poses->file_hashes.at("training/sparse/cameras.bin")},
                {"images_sha256", poses->file_hashes.at("training/sparse/images.bin")},
                {"units", "ARBITRARY_POSE_CAMERA_UNITS"},
                {"camera_axes", "+x_right,+y_down,+z_forward"},
                {"depth_convention", "AXIAL_CAMERA_Z"},
                {"preparation_recipe", recipe},
                {"preparation_recipe_sha256", recipe_hash},
                {"depth_recipe_sha256", request.depth_recipe_sha256},
                {"frames", frames}}
                .dump(2) +
            "\n";
        return prepared;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Dense preparation allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Dense preparation failed: ") + exception.what());
    }
}
result<std::string> write_dense_inputs(const dense_input_request& request,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop) {
    try {
        if (destination.empty())
            return fail(error_code::INVALID_ARGUMENT, "Dense manifest destination is empty");
        const auto output = std::filesystem::weakly_canonical(destination);
        for (const auto& input : {request.completed.pose_job, request.completed.decode_stage,
                                  request.canonical_poses, request.depth_recipe}) {
            if (!input.empty()) {
                const auto selected = std::filesystem::canonical(input);
                if (within(output, selected) || within(selected, output))
                    return fail(error_code::INVALID_ARGUMENT, "Dense manifest overlaps an input");
            }
        }
        // Refuse existing destinations before expensive verification.
        if (std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return fail(error_code::INVALID_ARGUMENT, "Dense manifest destination already exists");
        const auto prepared = prepare_dense_inputs(request, stop);
        if (!prepared)
            return std::unexpected(prepared.error());
        return asset_files::publish_asset(destination,
                                          std::as_bytes(std::span(prepared->manifest_json)), stop);
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
result<prepared_dense_inputs> load_dense_inputs(const dense_input_request& request,
                                                const std::filesystem::path& manifest,
                                                const std::string& manifest_sha256,
                                                const std::stop_token& stop) {
    const auto bytes = asset_files::read_bytes(manifest, std::size_t{1024} * 1024, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (sha256(*bytes) != manifest_sha256)
        return fail(error_code::INVALID_DATA, "Dense manifest checksum differs");
    auto prepared = prepare_dense_inputs(request, stop);
    if (!prepared)
        return std::unexpected(prepared.error());
    const auto expected = std::as_bytes(std::span(prepared->manifest_json));
    if (!std::ranges::equal(*bytes, expected))
        return fail(error_code::RESYNC_REQUIRED, "Dense manifest differs from checked inputs");
    return prepared;
}
} // namespace world_explorer
