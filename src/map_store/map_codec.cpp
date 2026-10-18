#include "map_codec.hpp"

#include <algorithm>
#include <cmath>

namespace world_explorer::map_codec {
namespace wire = pose_wire;
constexpr std::size_t K_MAX_ASSET_BYTES { 256uz * 1024uz * 1024uz };
constexpr std::size_t K_MAX_FRAMES { 1000000uz };
constexpr std::size_t K_MAX_GROUPS { 4096uz };

bool digest_valid(std::string_view value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}

bool limits_valid(const PoseAssetLimits& limits) {
    return limits.m_encoded_bytes.m_bytes > 0 &&
           limits.m_encoded_bytes.m_bytes <= K_MAX_ASSET_BYTES &&
           limits.m_records.m_max_frames > 0 && limits.m_records.m_max_frames <= K_MAX_FRAMES &&
           limits.m_records.m_max_calibrations > 0 &&
           limits.m_records.m_max_calibrations <= K_MAX_GROUPS &&
           limits.m_records.m_max_submaps > 0 && limits.m_records.m_max_submaps <= K_MAX_GROUPS;
}

SimilarityTransform compose(const SimilarityTransform& a, const SimilarityTransform& b) {
    const auto K_P = a.m_rotation;
    const auto K_Q = b.m_rotation;
    return {{K_P.m_w * K_Q.m_w - K_P.m_x * K_Q.m_x - K_P.m_y * K_Q.m_y - K_P.m_z * K_Q.m_z,
             K_P.m_w * K_Q.m_x + K_P.m_x * K_Q.m_w + K_P.m_y * K_Q.m_z - K_P.m_z * K_Q.m_y,
             K_P.m_w * K_Q.m_y - K_P.m_x * K_Q.m_z + K_P.m_y * K_Q.m_w + K_P.m_z * K_Q.m_x,
             K_P.m_w * K_Q.m_z + K_P.m_x * K_Q.m_y - K_P.m_y * K_Q.m_x + K_P.m_z * K_Q.m_w},
            transform_point(a, b.m_translation),
            a.m_scale * b.m_scale};
}

wire::transform pack(const SimilarityTransform& t) {
    return {t.m_rotation.m_w,
            t.m_rotation.m_x,
            t.m_rotation.m_y,
            t.m_rotation.m_z,
            t.m_translation.m_x,
            t.m_translation.m_y,
            t.m_translation.m_z,
            t.m_scale};
}

SimilarityTransform unpack(const wire::transform& t) {
    return {{t.qw(), t.qx(), t.qy(), t.qz()}, {t.tx(), t.ty(), t.tz()}, t.scale()};
}

bool same_transform(const SimilarityTransform& a, const SimilarityTransform& b) {
    // Compare rotation separately so a large translation cannot hide an
    // inconsistent orientation. q and -q represent the same rotation.
    const double K_SIGN =
        a.m_rotation.m_w * b.m_rotation.m_w + a.m_rotation.m_x * b.m_rotation.m_x +
                    a.m_rotation.m_y * b.m_rotation.m_y + a.m_rotation.m_z * b.m_rotation.m_z <
                0
            ? -1
            : 1;
    for (const auto& pair : {std::pair { a.m_rotation.m_w, K_SIGN * b.m_rotation.m_w },
                             std::pair { a.m_rotation.m_x, K_SIGN * b.m_rotation.m_x },
                             std::pair { a.m_rotation.m_y, K_SIGN * b.m_rotation.m_y },
                             std::pair { a.m_rotation.m_z, K_SIGN * b.m_rotation.m_z }})
        if (std::abs(pair.first - pair.second) > 1e-12) return false;
    for (const auto& pair : {std::pair { a.m_translation.m_x, b.m_translation.m_x },
                             std::pair { a.m_translation.m_y, b.m_translation.m_y },
                             std::pair { a.m_translation.m_z, b.m_translation.m_z },
                             std::pair { a.m_scale, b.m_scale }})
        if (std::abs(pair.first - pair.second) >
            1e-12 * std::max({1.0, std::abs(pair.first), std::abs(pair.second)}))
            return false;
    return true;
}

bool covariance_valid(const std::array<double, 36>& values) {
    double magnitude { 1 };
    for (double value : values) {
        if (!std::isfinite(value)) return false;
        magnitude = std::max(magnitude, std::abs(value));
    }
    const double K_TOLERANCE = magnitude * 1e-12;
    std::array<double, 36> residual = values;
    for (std::size_t i { 0uz }; i < 6uz; ++i)
        for (std::size_t j { 0uz }; j < 6uz; ++j)
            if (std::abs(values[i * 6 + j] - values[j * 6 + i]) > K_TOLERANCE) return false;
    // Symmetric pivoted Schur complements accept positive semidefinite matrices,
    // including singular covariances, without inventing backend uncertainty.
    for (std::size_t k { 0uz }; k < 6uz; ++k) {
        std::size_t pivot = k;
        for (std::size_t i { k + 1uz }; i < 6uz; ++i)
            if (residual[i * 6 + i] > residual[pivot * 6 + pivot]) pivot = i;
        if (pivot != k) {
            for (std::size_t i { 0uz }; i < 6uz; ++i)
                std::swap(residual[k * 6 + i], residual[pivot * 6 + i]);
            for (std::size_t i { 0uz }; i < 6uz; ++i)
                std::swap(residual[i * 6 + k], residual[i * 6 + pivot]);
        }
        const double K_DIAGONAL = residual[k * 6 + k];
        if (K_DIAGONAL < -K_TOLERANCE) return false;
        if (K_DIAGONAL <= K_TOLERANCE) {
            for (std::size_t i { k }; i < 6uz; ++i)
                for (std::size_t j { k }; j < 6uz; ++j)
                    if (std::abs(residual[i * 6 + j]) > K_TOLERANCE) return false;
            return true;
        }
        for (std::size_t i { k + 1uz }; i < 6uz; ++i)
            for (std::size_t j { k + 1uz }; j < 6uz; ++j)
                residual[i * 6 + j] -= (residual[i * 6 + k] / K_DIAGONAL) * residual[k * 6 + j];
    }
    return true;
}

Result<void>
validate_state(const MapState& map, const PoseAssetLimits& limits, const std::stop_token& stop) {
    if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map validation cancelled");
    if (!limits_valid(limits)) return fail(ErrorCode::INVALID_ARGUMENT, "Invalid map codec limits");
    if (map.m_scale != ScaleStatus::ARBITRARY)
        return fail(ErrorCode::UNSUPPORTED_OPERATION, "Measured map scale is not implemented");
    if (map.m_initial_pose_asset_sha256 && !digest_valid(*map.m_initial_pose_asset_sha256))
        return fail(ErrorCode::INVALID_DATA, "Invalid initial pose asset dependency");
    if (!map.m_revision.m_value &&
        (map.m_initial_pose_asset_sha256 || !map.m_calibrations.empty() ||
         !map.m_observations.empty() || !map.m_poses.empty() || !map.m_world_from_submap.empty() ||
         !map.m_invalidated_frames.empty()))
        return fail(ErrorCode::INVALID_DATA, "Revision zero map must be empty");
    if (map.m_observations.size() > limits.m_records.m_max_frames ||
        map.m_poses.size() > limits.m_records.m_max_frames ||
        map.m_invalidated_frames.size() > limits.m_records.m_max_frames ||
        map.m_calibrations.size() > limits.m_records.m_max_calibrations ||
        map.m_world_from_submap.size() > limits.m_records.m_max_submaps)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose asset record budget exceeded");
    for (const auto& [K_ID, K_CALIBRATION] : map.m_calibrations)
        if (K_ID != K_CALIBRATION.m_id || !valid(K_CALIBRATION))
            return fail(ErrorCode::INVALID_DATA, "Invalid pose asset calibration");
    for (const auto& [K_ID, K_OBSERVATION] : map.m_observations) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        if (!K_ID.m_value || K_ID != K_OBSERVATION.m_id || !K_OBSERVATION.m_capture.m_value ||
            !map.m_calibrations.contains(K_OBSERVATION.m_calibration) ||
            K_OBSERVATION.m_timestamp.m_time_base_numerator <= 0 ||
            K_OBSERVATION.m_timestamp.m_time_base_denominator <= 0 ||
            K_OBSERVATION.m_capture_time.count() < 0 || !digest_valid(K_OBSERVATION.m_image_sha256))
            return fail(ErrorCode::INVALID_DATA, "Invalid pose asset observation");
    }
    for (const auto& [K_ID, K_ALIGNMENT] : map.m_world_from_submap)
        if (!K_ID.m_value || !valid(K_ALIGNMENT))
            return fail(ErrorCode::INVALID_DATA, "Invalid pose asset alignment");
    for (const auto& [K_ID, K_POSE] : map.m_poses) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        if (K_ID != K_POSE.m_frame || !map.m_observations.contains(K_ID) ||
            !map.m_world_from_submap.contains(K_POSE.m_submap) ||
            K_POSE.m_capture_time != map.m_observations.at(K_ID).m_capture_time ||
            !K_POSE.m_revision.m_value || K_POSE.m_revision.m_value > map.m_revision.m_value ||
            !valid(K_POSE.m_submap_from_camera) ||
            std::abs(K_POSE.m_submap_from_camera.m_scale - 1) > 1e-12 ||
            static_cast<std::uint8_t>(K_POSE.m_tracking) >
                static_cast<std::uint8_t>(TrackingState::FINISHED) ||
            (K_POSE.m_covariance && !covariance_valid(*K_POSE.m_covariance)))
            return fail(ErrorCode::INVALID_DATA, "Invalid pose asset pose or covariance");
        if (!valid(
                compose(map.m_world_from_submap.at(K_POSE.m_submap), K_POSE.m_submap_from_camera)))
            return fail(ErrorCode::INVALID_DATA, "Pose asset world transform overflow");
    }
    for (auto id : map.m_invalidated_frames)
        if (!id.m_value) return fail(ErrorCode::INVALID_DATA, "Invalid pose invalidation ID");
    return {};
}

std::array<double, 4> unpack(const wire::coefficients& v) {
    return { v.a(), v.b(), v.c(), v.d() };
}

Result<record_offsets> encode_records(flatbuffers::FlatBufferBuilder& builder,
                                      const MapState& map,
                                      const PoseAssetLimits& limits,
                                      const std::stop_token& stop) {
    record_offsets encoded;
    const auto K_BUDGET_CHECK = [&]() -> Result<void> {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        if (builder.GetSize() > limits.m_encoded_bytes.m_bytes)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        return {};
    };
    auto& calibrations = encoded.calibrations;
    calibrations.reserve(map.m_calibrations.size());
    for (const auto& [K_ID, K_C] : map.m_calibrations) {
        const wire::coefficients K_INTRINSICS(
            K_C.m_camera.m_fx, K_C.m_camera.m_fy, K_C.m_camera.m_cx, K_C.m_camera.m_cy);
        const wire::coefficients K_DISTORTION(
            K_C.m_distortion[0], K_C.m_distortion[1], K_C.m_distortion[2], K_C.m_distortion[3]);
        const wire::coefficients K_IMAGE(K_C.m_image_transform[0],
                                         K_C.m_image_transform[1],
                                         K_C.m_image_transform[2],
                                         K_C.m_image_transform[3]);
        calibrations.push_back(wire::Createcalibration(builder,
                                                       K_ID.m_value,
                                                       K_C.m_width,
                                                       K_C.m_height,
                                                       static_cast<wire::camera_model>(K_C.m_model),
                                                       &K_INTRINSICS,
                                                       &K_DISTORTION,
                                                       &K_IMAGE));
        const auto K_ADMITTED = K_BUDGET_CHECK();
        if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
    }
    auto& observations = encoded.observations;
    observations.reserve(map.m_observations.size());
    for (const auto& [K_ID, K_O] : map.m_observations) {
        const auto K_IMAGE_HASH = builder.CreateString(K_O.m_image_sha256);
        observations.push_back(wire::Createobservation(builder,
                                                       K_ID.m_value,
                                                       K_O.m_capture.m_value,
                                                       K_O.m_calibration.m_value,
                                                       K_O.m_timestamp.m_pts,
                                                       K_O.m_timestamp.m_time_base_numerator,
                                                       K_O.m_timestamp.m_time_base_denominator,
                                                       K_O.m_capture_time.count(),
                                                       K_IMAGE_HASH));
        const auto K_ADMITTED = K_BUDGET_CHECK();
        if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
    }
    auto& alignments = encoded.alignments;
    alignments.reserve(map.m_world_from_submap.size());
    for (const auto& [K_ID, K_T] : map.m_world_from_submap) {
        const auto K_TRANSFORM = pack(K_T);
        alignments.push_back(wire::Createalignment(builder, K_ID.m_value, &K_TRANSFORM));
        const auto K_ADMITTED = K_BUDGET_CHECK();
        if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
    }
    auto& poses = encoded.poses;
    poses.reserve(map.m_poses.size());
    for (const auto& [K_ID, K_P] : map.m_poses) {
        const auto K_LOCAL = pack(K_P.m_submap_from_camera);
        const auto K_WORLD =
            pack(compose(map.m_world_from_submap.at(K_P.m_submap), K_P.m_submap_from_camera));
        const auto K_COVARIANCE = K_P.m_covariance ? builder.CreateVector(K_P.m_covariance->data(),
                                                                          K_P.m_covariance->size())
                                                   : 0;
        poses.push_back(wire::Createpose(builder,
                                         K_ID.m_value,
                                         K_P.m_submap.m_value,
                                         K_P.m_revision.m_value,
                                         K_P.m_capture_time.count(),
                                         static_cast<wire::tracking_state>(K_P.m_tracking),
                                         &K_LOCAL,
                                         &K_WORLD,
                                         K_COVARIANCE));
        const auto K_ADMITTED = K_BUDGET_CHECK();
        if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
    }
    encoded.invalidated.reserve(map.m_invalidated_frames.size());
    for (auto id : map.m_invalidated_frames) encoded.invalidated.push_back(id.m_value);
    return encoded;
}

Result<MapSnapshot>
decode_records(record_view records, const PoseAssetLimits& limits, const std::stop_token& stop) {
    if (records.calibrations->size() > limits.m_records.m_max_calibrations ||
        records.observations->size() > limits.m_records.m_max_frames ||
        records.poses->size() > limits.m_records.m_max_frames ||
        records.alignments->size() > limits.m_records.m_max_submaps ||
        records.invalidated_frames->size() > limits.m_records.m_max_frames)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map record budget exceeded");
    auto map = std::make_shared<MapState>();
    map->m_epoch = { records.epoch };
    map->m_revision = { records.revision };
    for (const auto* c : *records.calibrations) {
        const auto K_INTRINSIC = unpack(*c->intrinsics());
        CalibrationRecord calibration {
            {c->id()},
            {K_INTRINSIC[0], K_INTRINSIC[1], K_INTRINSIC[2], K_INTRINSIC[3]},
            c->width(),
            c->height(),
            static_cast<CalibrationModel>(c->model()),
            unpack(*c->distortion()),
            unpack(*c->image_transform())};
        if (!map->m_calibrations.emplace(calibration.m_id, calibration).second)
            return fail(ErrorCode::INVALID_DATA, "Duplicate pose calibration ID");
    }
    for (const auto* o : *records.observations) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        if (o->image_sha256()->size() != 64)
            return fail(ErrorCode::INVALID_DATA, "Invalid image hash length");
        ObservationRecord observation {
            {o->frame()},
            {o->capture()},
            {o->calibration_id()},
            {o->original_pts(), o->time_base_numerator(), o->time_base_denominator()},
            std::chrono::nanoseconds(o->capture_nanoseconds()),
            o->image_sha256()->str()};
        if (!map->m_observations.emplace(observation.m_id, std::move(observation)).second)
            return fail(ErrorCode::INVALID_DATA, "Duplicate pose observation ID");
    }
    for (const auto* a : *records.alignments)
        if (!map->m_world_from_submap
                 .emplace(SubmapId { a->submap() }, unpack(*a->world_from_submap()))
                 .second)
            return fail(ErrorCode::INVALID_DATA, "Duplicate pose submap ID");
    for (const auto* p : *records.poses) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Pose asset cancelled");
        PoseEstimate pose {{p->frame()},
                           {p->submap()},
                           unpack(*p->submap_from_camera()),
                           std::chrono::nanoseconds(p->capture_nanoseconds()),
                           {p->revision()},
                           static_cast<TrackingState>(p->tracking()),
                           {}};
        if (p->covariance()) {
            if (p->covariance()->size() != 36)
                return fail(ErrorCode::INVALID_DATA, "Pose covariance must be 6x6");
            pose.m_covariance.emplace();
            std::copy(p->covariance()->begin(), p->covariance()->end(), pose.m_covariance->begin());
        }
        const auto K_ALIGNMENT = map->m_world_from_submap.find(pose.m_submap);
        const auto K_WORLD = unpack(*p->world_from_camera());
        if (K_ALIGNMENT == map->m_world_from_submap.end() || !valid(K_WORLD) ||
            !same_transform(compose(K_ALIGNMENT->second, pose.m_submap_from_camera), K_WORLD))
            return fail(ErrorCode::INVALID_DATA,
                        "Pose world transform disagrees with submap dependencies");
        if (!map->m_poses.emplace(pose.m_frame, pose).second)
            return fail(ErrorCode::INVALID_DATA, "Duplicate pose frame ID");
    }
    for (auto id : *records.invalidated_frames)
        if (!map->m_invalidated_frames.emplace(FrameId { id }).second)
            return fail(ErrorCode::INVALID_DATA, "Duplicate pose invalidation ID");
    const auto K_VALID = validate_state(*map, limits, stop);
    if (!K_VALID) return std::unexpected(K_VALID.error());
    return MapSnapshot(std::move(map));
}

}  // namespace world_explorer::map_codec
