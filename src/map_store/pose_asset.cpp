#include "world_explorer/map_store/pose_asset.hpp"
#include "map_codec.hpp"
#include "map_files.hpp"
#include "poses_generated.h"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <new>
#include <nlohmann/json.hpp>

namespace world_explorer {
namespace {
namespace wire = pose_wire;
using map_codec::digest_valid;
using map_codec::limits_valid;
constexpr std::size_t IO_BLOCK_BYTES = std::size_t{256} * 1024;
result<void> validate(const canonical_pose_asset& asset, const pose_asset_limits& limits,
                      const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Pose asset cancelled");
    if (!limits_valid(limits))
        return fail(error_code::INVALID_ARGUMENT, "Invalid pose asset limits");
    if (!asset.snapshot)
        return fail(error_code::INVALID_ARGUMENT, "Missing pose snapshot");
    const auto& map = *asset.snapshot;
    const auto& provenance = asset.provenance;
    if (map.scale != scale_status::ARBITRARY)
        return fail(
            error_code::UNSUPPORTED_OPERATION,
            "Pose asset v1 requires arbitrary scale; measured provenance is not implemented");
    if (!digest_valid(provenance.source_sha256) ||
        !digest_valid(provenance.decode_completion_sha256) ||
        !digest_valid(provenance.pose_completion_sha256) ||
        (provenance.journal_sha256 && !digest_valid(*provenance.journal_sha256)))
        return fail(error_code::INVALID_DATA, "Invalid pose asset provenance");
    if (!map.revision.value || map.calibrations.empty() || map.observations.empty() ||
        map.poses.empty() || map.world_from_submap.empty())
        return fail(error_code::INVALID_DATA, "Pose asset requires a nonempty revisioned map");
    return map_codec::validate_state(map, limits, stop);
}
nlohmann::json transform_json(const similarity_transform& t) {
    return {{"rotation_wxyz", {t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z}},
            {"translation", {t.translation.x, t.translation.y, t.translation.z}},
            {"scale", t.scale}};
}
} // namespace

result<std::vector<std::byte>> encode_pose_asset(const canonical_pose_asset& asset,
                                                 pose_asset_limits limits,
                                                 const std::stop_token& stop) {
    try {
        const auto checked = validate(asset, limits, stop);
        if (!checked)
            return std::unexpected(checked.error());
        flatbuffers::FlatBufferBuilder builder;
        const auto& map = *asset.snapshot;
        const auto encoded = map_codec::encode_records(builder, map, limits, stop);
        if (!encoded)
            return std::unexpected(encoded.error());
        const auto& p = asset.provenance;
        const auto source = builder.CreateString(p.source_sha256);
        const auto decode = builder.CreateString(p.decode_completion_sha256);
        const auto pose = builder.CreateString(p.pose_completion_sha256);
        const auto journal = p.journal_sha256 ? builder.CreateString(*p.journal_sha256) : 0;
        const auto provenance = wire::Createprovenance_record(
            builder, source, decode, pose, journal, wire::scale_provenance::ARBITRARY_MONOCULAR,
            wire::gravity_provenance::UNKNOWN);
        const auto cameras = builder.CreateVector(encoded->calibrations);
        const auto frames = builder.CreateVector(encoded->observations);
        const auto anchors = builder.CreateVector(encoded->alignments);
        const auto estimates = builder.CreateVector(encoded->poses);
        const auto invalidations = builder.CreateVector(encoded->invalidated);
        const auto root = wire::Createpose_asset(
            builder, 1, wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER, map.epoch.value,
            map.revision.value, provenance, cameras, frames, anchors, estimates, invalidations);
        wire::Finishpose_assetBuffer(builder, root);
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose encoding cancelled");
        if (builder.GetSize() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Pose encoding failed: ") + e.what());
    }
}

result<canonical_pose_asset> decode_pose_asset(std::span<const std::byte> bytes,
                                               const std::string& expected_sha256,
                                               pose_asset_limits limits,
                                               const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (!limits_valid(limits))
            return fail(error_code::INVALID_ARGUMENT, "Invalid pose asset limits");
        if (!digest_valid(expected_sha256))
            return fail(error_code::INVALID_ARGUMENT, "Expected pose SHA-256 is required");
        if (bytes.empty() || bytes.size() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        sha256_builder digest;
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled");
            const auto count = std::min(IO_BLOCK_BYTES, bytes.size() - offset);
            digest.update(bytes.subspan(offset, count));
            offset += count;
        }
        if (digest.finish() != expected_sha256)
            return fail(error_code::INVALID_DATA, "Pose asset checksum mismatch");
        // FlatBuffers structs require double alignment. Accept borrowed byte
        // slices safely even when their start address is not aligned.
        std::vector<std::byte> aligned;
        if (reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(wire::transform) != 0) {
            aligned.assign(bytes.begin(), bytes.end());
            bytes = aligned;
        }
        // No pointers escape this verified buffer; native ownership is reconstructed
        // only after bounded structural and scalar checks.
        flatbuffers::Verifier verifier(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), 16,
            static_cast<flatbuffers::uoffset_t>(limits.records.max_frames * 2 +
                                                limits.records.max_calibrations +
                                                limits.records.max_submaps + 2));
        if (!wire::Verifypose_assetBuffer(verifier))
            return fail(error_code::INVALID_DATA, "Malformed FlatBuffers pose asset");
        const auto* root = wire::Getpose_asset(bytes.data());
        const auto* provenance = root->provenance();
        if (root->schema_version() != 1 ||
            root->coordinates() != wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER ||
            provenance->scale() != wire::scale_provenance::ARBITRARY_MONOCULAR ||
            provenance->gravity() != wire::gravity_provenance::UNKNOWN)
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Unsupported pose schema or coordinate/scale/gravity profile");
        if (root->calibrations()->size() > limits.records.max_calibrations ||
            root->observations()->size() > limits.records.max_frames ||
            root->poses()->size() > limits.records.max_frames ||
            root->alignments()->size() > limits.records.max_submaps ||
            root->invalidated_frames()->size() > limits.records.max_frames)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset record budget exceeded");
        if (provenance->source_sha256()->size() != 64 ||
            provenance->decode_completion_sha256()->size() != 64 ||
            provenance->pose_completion_sha256()->size() != 64 ||
            (provenance->journal_sha256() && provenance->journal_sha256()->size() != 64))
            return fail(error_code::INVALID_DATA, "Invalid pose asset hash length");
        const auto map = map_codec::decode_records(
            {root->epoch(), root->revision(), root->calibrations(), root->observations(),
             root->alignments(), root->poses(), root->invalidated_frames()},
            limits, stop);
        if (!map)
            return std::unexpected(map.error());
        canonical_pose_asset asset{*map,
                                   {provenance->source_sha256()->str(),
                                    provenance->decode_completion_sha256()->str(),
                                    provenance->pose_completion_sha256()->str(),
                                    {}}};
        if (provenance->journal_sha256())
            asset.provenance.journal_sha256 = provenance->journal_sha256()->str();
        const auto checked = validate(asset, limits, stop);
        if (!checked)
            return std::unexpected(checked.error());
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Pose decoding failed: ") + e.what());
    }
}

result<std::string> write_pose_asset(const std::filesystem::path& destination,
                                     const canonical_pose_asset& asset, pose_asset_limits limits,
                                     const std::stop_token& stop) {
    const auto bytes = encode_pose_asset(asset, limits, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    return map_files::publish_asset(destination, *bytes, stop);
}
result<canonical_pose_asset> read_pose_asset(const std::filesystem::path& path,
                                             const std::string& expected_sha256,
                                             pose_asset_limits limits,
                                             const std::stop_token& stop) {
    if (!limits_valid(limits))
        return fail(error_code::INVALID_ARGUMENT, "Invalid pose asset limits");
    const auto bytes = map_files::read_bytes(path, limits.encoded_bytes.bytes, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    return decode_pose_asset(*bytes, expected_sha256, limits, stop);
}
result<std::string> inspect_pose_asset(const canonical_pose_asset& asset,
                                       pose_asset_limits limits) {
    try {
        const auto checked = validate(asset, limits, {});
        if (!checked)
            return std::unexpected(checked.error());
        const auto& map = *asset.snapshot;
        nlohmann::json value{
            {"schema_version", 1},
            {"epoch", map.epoch.value},
            {"revision", map.revision.value},
            {"coordinates",
             {{"handedness", "right"},
              {"world_up", "+y"},
              {"camera_axes", "+x_right,+y_down,+z_forward"},
              {"pose_convention", "world_from_camera"},
              {"quaternion_order", "wxyz"},
              {"pixel_centers", "integer"}}},
            {"provenance",
             {{"source_sha256", asset.provenance.source_sha256},
              {"decode_completion_sha256", asset.provenance.decode_completion_sha256},
              {"pose_completion_sha256", asset.provenance.pose_completion_sha256},
              {"scale", "ARBITRARY_MONOCULAR"},
              {"gravity", "UNKNOWN"}}},
            {"calibrations", nlohmann::json::array()},
            {"observations", nlohmann::json::array()},
            {"alignments", nlohmann::json::array()},
            {"poses", nlohmann::json::array()},
            {"invalidated_frames", nlohmann::json::array()}};
        value["provenance"]["journal_sha256"] =
            asset.provenance.journal_sha256 ? nlohmann::json(*asset.provenance.journal_sha256)
                                            : nlohmann::json(nullptr);
        for (const auto& [id, c] : map.calibrations)
            value["calibrations"].push_back(
                {{"id", id.value},
                 {"width", c.width},
                 {"height", c.height},
                 {"model", c.model == calibration_model::PINHOLE ? "PINHOLE" : "OPENCV"},
                 {"intrinsics", {c.camera.fx, c.camera.fy, c.camera.cx, c.camera.cy}},
                 {"distortion_k1_k2_p1_p2", c.distortion},
                 {"image_transform_sx_sy_ox_oy", c.image_transform}});
        for (const auto& [id, o] : map.observations)
            value["observations"].push_back(
                {{"frame", id.value},
                 {"capture", o.capture.value},
                 {"calibration", o.calibration.value},
                 {"original_pts", o.timestamp.pts},
                 {"time_base",
                  {o.timestamp.time_base_numerator, o.timestamp.time_base_denominator}},
                 {"capture_nanoseconds", o.capture_time.count()},
                 {"image_sha256", o.image_sha256}});
        for (const auto& [id, t] : map.world_from_submap)
            value["alignments"].push_back(
                {{"submap", id.value}, {"world_from_submap", transform_json(t)}});
        for (const auto& [id, p] : map.poses)
            value["poses"].push_back({{"frame", id.value},
                                      {"submap", p.submap.value},
                                      {"revision", p.revision.value},
                                      {"capture_nanoseconds", p.capture_time.count()},
                                      {"tracking", static_cast<std::uint8_t>(p.tracking)},
                                      {"submap_from_camera", transform_json(p.submap_from_camera)},
                                      {"world_from_camera", transform_json(map_codec::compose(
                                                                map.world_from_submap.at(p.submap),
                                                                p.submap_from_camera))},
                                      {"covariance", p.covariance ? nlohmann::json(*p.covariance)
                                                                  : nlohmann::json(nullptr)}});
        for (auto id : map.invalidated_frames)
            value["invalidated_frames"].push_back(id.value);
        auto text = value.dump(2);
        if (text.size() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose inspection exceeds budget");
        return text;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose inspection allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Pose inspection failed: ") + e.what());
    }
}
} // namespace world_explorer
