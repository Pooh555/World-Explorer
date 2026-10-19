#include "world_explorer/map_store/pose_asset.hpp"

#include "map_codec.hpp"
#include "map_files.hpp"
#include "poses_generated.h"
#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <new>

namespace world_explorer {
namespace {
namespace wire = pose_wire;
using map_codec::digest_valid;
using map_codec::limits_valid;
constexpr std::size_t K_IO_BLOCK_BYTES { 256uz * 1024uz };

Result<void> validate(const CanonicalPoseAsset& asset,
                      const PoseAssetLimits& limits,
                      const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
    }
    if (!limits_valid(limits)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid pose asset limits");
    }
    if (!asset.m_snapshot) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Missing pose snapshot");
    }
    const auto& map = *asset.m_snapshot;
    const auto& provenance = asset.m_provenance;
    if (map.m_scale != ScaleStatus::ARBITRARY) {
        return fail(
            ErrorCode::UNSUPPORTED_OPERATION,
            "Pose asset v1 requires arbitrary scale; measured provenance is not implemented");
    }
    if (!digest_valid(provenance.m_source_sha256) ||
        !digest_valid(provenance.m_decode_completion_sha256) ||
        !digest_valid(provenance.m_pose_completion_sha256) ||
        (provenance.m_journal_sha256 && !digest_valid(*provenance.m_journal_sha256))) {
        return fail(ErrorCode::INVALID_DATA, "Invalid pose asset provenance");
    }
    if (!map.m_revision.m_value || map.m_calibrations.empty() || map.m_observations.empty() ||
        map.m_poses.empty() || map.m_world_from_submap.empty()) {
        return fail(ErrorCode::INVALID_DATA, "Pose asset requires a nonempty revisioned map");
    }
    return map_codec::validate_state(map, limits, stop);
}

nlohmann::json transform_json(const SimilarityTransform& t) {
    return {
        { "rotation_wxyz", { t.m_rotation.m_w, t.m_rotation.m_x, t.m_rotation.m_y, t.m_rotation.m_z } },
        { "translation", { t.m_translation.m_x, t.m_translation.m_y, t.m_translation.m_z } },
        { "scale", t.m_scale } };
}

}  // namespace

Result<std::vector<std::byte>> encode_pose_asset(const CanonicalPoseAsset& asset,
                                                 PoseAssetLimits limits,
                                                 const std::stop_token& stop) {
    try {
        const auto K_CHECKED = validate(asset, limits, stop);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        flatbuffers::FlatBufferBuilder builder;
        const auto& map = *asset.m_snapshot;
        const auto K_ENCODED = map_codec::encode_records(builder, map, limits, stop);
        if (!K_ENCODED) {
            return std::unexpected(K_ENCODED.error());
        }
        const auto& p = asset.m_provenance;
        const auto K_SOURCE = builder.CreateString(p.m_source_sha256);
        const auto K_DECODE = builder.CreateString(p.m_decode_completion_sha256);
        const auto K_POSE = builder.CreateString(p.m_pose_completion_sha256);
        const auto K_JOURNAL = p.m_journal_sha256 ? builder.CreateString(*p.m_journal_sha256) : 0;
        const auto K_PROVENANCE =
            wire::Createprovenance_record(builder,
                                          K_SOURCE,
                                          K_DECODE,
                                          K_POSE,
                                          K_JOURNAL,
                                          wire::scale_provenance::ARBITRARY_MONOCULAR,
                                          wire::gravity_provenance::UNKNOWN);
        const auto K_CAMERAS = builder.CreateVector(K_ENCODED->calibrations);
        const auto K_FRAMES = builder.CreateVector(K_ENCODED->observations);
        const auto K_ANCHORS = builder.CreateVector(K_ENCODED->alignments);
        const auto K_ESTIMATES = builder.CreateVector(K_ENCODED->poses);
        const auto K_INVALIDATIONS = builder.CreateVector(K_ENCODED->invalidated);
        const auto K_ROOT =
            wire::Createpose_asset(builder,
                                   1,
                                   wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER,
                                   map.m_epoch.m_value,
                                   map.m_revision.m_value,
                                   K_PROVENANCE,
                                   K_CAMERAS,
                                   K_FRAMES,
                                   K_ANCHORS,
                                   K_ESTIMATES,
                                   K_INVALIDATIONS);
        wire::Finishpose_assetBuffer(builder, K_ROOT);
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Pose encoding cancelled");
        }
        if (builder.GetSize() > limits.m_encoded_bytes.m_bytes) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        }
        const auto K_BYTES =
            std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
        return std::vector<std::byte>(K_BYTES.begin(), K_BYTES.end());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Pose encoding failed: ") + e.what());
    }
}

Result<CanonicalPoseAsset> decode_pose_asset(std::span<const std::byte> bytes,
                                             const std::string& expected_sha256,
                                             PoseAssetLimits limits,
                                             const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        }
        if (!limits_valid(limits)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid pose asset limits");
        }
        if (!digest_valid(expected_sha256)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Expected pose SHA-256 is required");
        }
        if (bytes.empty() || bytes.size() > limits.m_encoded_bytes.m_bytes) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        }
        Sha256Builder digest;
        for (std::size_t offset { 0uz }; offset < bytes.size();) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
            }
            const auto K_COUNT = std::min(K_IO_BLOCK_BYTES, bytes.size() - offset);
            digest.update(bytes.subspan(offset, K_COUNT));
            offset += K_COUNT;
        }
        if (digest.finish() != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "Pose asset checksum mismatch");
        }
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
            reinterpret_cast<const std::uint8_t*>(bytes.data()),
            bytes.size(),
            16,
            static_cast<flatbuffers::uoffset_t>(limits.m_records.m_max_frames * 2 +
                                                limits.m_records.m_max_calibrations +
                                                limits.m_records.m_max_submaps + 2));
        if (!wire::Verifypose_assetBuffer(verifier)) {
            return fail(ErrorCode::INVALID_DATA, "Malformed FlatBuffers pose asset");
        }
        const auto* root = wire::Getpose_asset(bytes.data());
        const auto* provenance = root->provenance();
        if (root->schema_version() != 1 ||
            root->coordinates() != wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER ||
            provenance->scale() != wire::scale_provenance::ARBITRARY_MONOCULAR ||
            provenance->gravity() != wire::gravity_provenance::UNKNOWN) {
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Unsupported pose schema or coordinate/scale/gravity profile");
        }
        if (root->calibrations()->size() > limits.m_records.m_max_calibrations ||
            root->observations()->size() > limits.m_records.m_max_frames ||
            root->poses()->size() > limits.m_records.m_max_frames ||
            root->alignments()->size() > limits.m_records.m_max_submaps ||
            root->invalidated_frames()->size() > limits.m_records.m_max_frames) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose asset record budget exceeded");
        }
        if (provenance->source_sha256()->size() != 64 ||
            provenance->decode_completion_sha256()->size() != 64 ||
            provenance->pose_completion_sha256()->size() != 64 ||
            (provenance->journal_sha256() && provenance->journal_sha256()->size() != 64)) {
            return fail(ErrorCode::INVALID_DATA, "Invalid pose asset hash length");
        }
        const auto K_MAP = map_codec::decode_records({ root->epoch(),
                                                      root->revision(),
                                                      root->calibrations(),
                                                      root->observations(),
                                                      root->alignments(),
                                                      root->poses(),
                                                      root->invalidated_frames() },
                                                     limits,
                                                     stop);
        if (!K_MAP) {
            return std::unexpected(K_MAP.error());
        }
        CanonicalPoseAsset asset { *K_MAP,
                                  { provenance->source_sha256()->str(),
                                   provenance->decode_completion_sha256()->str(),
                                   provenance->pose_completion_sha256()->str(),
                                   {} } };
        if (provenance->journal_sha256()) {
            asset.m_provenance.m_journal_sha256 = provenance->journal_sha256()->str();
        }
        const auto K_CHECKED = validate(asset, limits, stop);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Pose decoding failed: ") + e.what());
    }
}

Result<std::string> write_pose_asset(const std::filesystem::path& destination,
                                     const CanonicalPoseAsset& asset,
                                     PoseAssetLimits limits,
                                     const std::stop_token& stop) {
    const auto K_BYTES = encode_pose_asset(asset, limits, stop);
    if (!K_BYTES) {
        return std::unexpected(K_BYTES.error());
    }
    return map_files::publish_asset(destination, *K_BYTES, stop);
}

Result<CanonicalPoseAsset> read_pose_asset(const std::filesystem::path& path,
                                           const std::string& expected_sha256,
                                           PoseAssetLimits limits,
                                           const std::stop_token& stop) {
    if (!limits_valid(limits)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid pose asset limits");
    }
    const auto K_BYTES = map_files::read_bytes(path, limits.m_encoded_bytes.m_bytes, stop);
    if (!K_BYTES) {
        return std::unexpected(K_BYTES.error());
    }
    return decode_pose_asset(*K_BYTES, expected_sha256, limits, stop);
}

Result<std::string> inspect_pose_asset(const CanonicalPoseAsset& asset, PoseAssetLimits limits) {
    try {
        const auto K_CHECKED = validate(asset, limits, {});
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        const auto& map = *asset.m_snapshot;
        nlohmann::json value {
            { "schema_version", 1 },
            { "epoch", map.m_epoch.m_value },
            { "revision", map.m_revision.m_value },
            { "coordinates",
             { { "handedness", "right" },
              { "world_up", "+y" },
              { "camera_axes", "+x_right,+y_down,+z_forward" },
              { "pose_convention", "world_from_camera" },
              { "quaternion_order", "wxyz" },
              { "pixel_centers", "integer" } } },
            { "provenance",
             { { "source_sha256", asset.m_provenance.m_source_sha256 },
              { "decode_completion_sha256", asset.m_provenance.m_decode_completion_sha256 },
              { "pose_completion_sha256", asset.m_provenance.m_pose_completion_sha256 },
              { "scale", "ARBITRARY_MONOCULAR" },
              { "gravity", "UNKNOWN" } } },
            { "calibrations", nlohmann::json::array() },
            { "observations", nlohmann::json::array() },
            { "alignments", nlohmann::json::array() },
            { "poses", nlohmann::json::array() },
            { "invalidated_frames", nlohmann::json::array() } };
        value["provenance"]["journal_sha256"] =
            asset.m_provenance.m_journal_sha256
                ? nlohmann::json(*asset.m_provenance.m_journal_sha256)
                : nlohmann::json(nullptr);
        for (const auto& [K_ID, K_C] : map.m_calibrations) {
            value["calibrations"].push_back(
                { { "id", K_ID.m_value },
                 { "width", K_C.m_width },
                 { "height", K_C.m_height },
                 { "model", K_C.m_model == CalibrationModel::PINHOLE ? "PINHOLE" : "OPENCV" },
                 { "intrinsics",
                  { K_C.m_camera.m_fx, K_C.m_camera.m_fy, K_C.m_camera.m_cx, K_C.m_camera.m_cy } },
                 { "distortion_k1_k2_p1_p2", K_C.m_distortion },
                 { "image_transform_sx_sy_ox_oy", K_C.m_image_transform } });
        }
        for (const auto& [K_ID, K_O] : map.m_observations) {
            value["observations"].push_back(
                { { "frame", K_ID.m_value },
                 { "capture", K_O.m_capture.m_value },
                 { "calibration", K_O.m_calibration.m_value },
                 { "original_pts", K_O.m_timestamp.m_pts },
                 { "time_base",
                  { K_O.m_timestamp.m_time_base_numerator, K_O.m_timestamp.m_time_base_denominator } },
                 { "capture_nanoseconds", K_O.m_capture_time.count() },
                 { "image_sha256", K_O.m_image_sha256 } });
        }
        for (const auto& [K_ID, K_T] : map.m_world_from_submap) {
            value["alignments"].push_back(
                { { "submap", K_ID.m_value }, { "world_from_submap", transform_json(K_T) } });
        }
        for (const auto& [K_ID, K_P] : map.m_poses) {
            value["poses"].push_back(
                { { "frame", K_ID.m_value },
                 { "submap", K_P.m_submap.m_value },
                 { "revision", K_P.m_revision.m_value },
                 { "capture_nanoseconds", K_P.m_capture_time.count() },
                 { "tracking", static_cast<std::uint8_t>(K_P.m_tracking) },
                 { "submap_from_camera", transform_json(K_P.m_submap_from_camera) },
                 { "world_from_camera",
                  transform_json(map_codec::compose(map.m_world_from_submap.at(K_P.m_submap),
                                                    K_P.m_submap_from_camera)) },
                 { "covariance",
                  K_P.m_covariance ? nlohmann::json(*K_P.m_covariance) : nlohmann::json(nullptr) } });
        }
        for (auto id : map.m_invalidated_frames) {
            value["invalidated_frames"].push_back(id.m_value);
        }
        auto text = value.dump(2);
        if (text.size() > limits.m_encoded_bytes.m_bytes) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose inspection exceeds budget");
        }
        return text;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose inspection allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Pose inspection failed: ") + e.what());
    }
}

}  // namespace world_explorer
