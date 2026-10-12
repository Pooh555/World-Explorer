#include "map_codec.hpp"
#include <algorithm>
#include <cmath>
namespace world_explorer::map_codec {
namespace wire = pose_wire;
constexpr std::size_t MAX_ASSET_BYTES = std::size_t{256} * 1024 * 1024;
constexpr std::size_t MAX_FRAMES = 1000000;
constexpr std::size_t MAX_GROUPS = 4096;
bool digest_valid(std::string_view value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}
bool limits_valid(const pose_asset_limits& limits) {
    return limits.encoded_bytes.bytes > 0 && limits.encoded_bytes.bytes <= MAX_ASSET_BYTES &&
           limits.records.max_frames > 0 && limits.records.max_frames <= MAX_FRAMES &&
           limits.records.max_calibrations > 0 && limits.records.max_calibrations <= MAX_GROUPS &&
           limits.records.max_submaps > 0 && limits.records.max_submaps <= MAX_GROUPS;
}
similarity_transform compose(const similarity_transform& a, const similarity_transform& b) {
    const auto p = a.rotation;
    const auto q = b.rotation;
    return {{p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z,
             p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y,
             p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x,
             p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w},
            transform_point(a, b.translation),
            a.scale * b.scale};
}
wire::transform pack(const similarity_transform& t) {
    return {t.rotation.w,    t.rotation.x,    t.rotation.y,    t.rotation.z,
            t.translation.x, t.translation.y, t.translation.z, t.scale};
}
similarity_transform unpack(const wire::transform& t) {
    return {{t.qw(), t.qx(), t.qy(), t.qz()}, {t.tx(), t.ty(), t.tz()}, t.scale()};
}
bool same_transform(const similarity_transform& a, const similarity_transform& b) {
    // Compare rotation separately so a large translation cannot hide an
    // inconsistent orientation. q and -q represent the same rotation.
    const double sign = a.rotation.w * b.rotation.w + a.rotation.x * b.rotation.x +
                                    a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z <
                                0
                            ? -1
                            : 1;
    for (const auto& pair : {std::pair{a.rotation.w, sign * b.rotation.w},
                             std::pair{a.rotation.x, sign * b.rotation.x},
                             std::pair{a.rotation.y, sign * b.rotation.y},
                             std::pair{a.rotation.z, sign * b.rotation.z}})
        if (std::abs(pair.first - pair.second) > 1e-12)
            return false;
    for (const auto& pair :
         {std::pair{a.translation.x, b.translation.x}, std::pair{a.translation.y, b.translation.y},
          std::pair{a.translation.z, b.translation.z}, std::pair{a.scale, b.scale}})
        if (std::abs(pair.first - pair.second) >
            1e-12 * std::max({1.0, std::abs(pair.first), std::abs(pair.second)}))
            return false;
    return true;
}
bool covariance_valid(const std::array<double, 36>& values) {
    double magnitude{1};
    for (double value : values) {
        if (!std::isfinite(value))
            return false;
        magnitude = std::max(magnitude, std::abs(value));
    }
    const double tolerance = magnitude * 1e-12;
    std::array<double, 36> residual = values;
    for (std::size_t i = 0; i < 6; ++i)
        for (std::size_t j = 0; j < 6; ++j)
            if (std::abs(values[i * 6 + j] - values[j * 6 + i]) > tolerance)
                return false;
    // Symmetric pivoted Schur complements accept positive semidefinite matrices,
    // including singular covariances, without inventing backend uncertainty.
    for (std::size_t k = 0; k < 6; ++k) {
        std::size_t pivot = k;
        for (std::size_t i = k + 1; i < 6; ++i)
            if (residual[i * 6 + i] > residual[pivot * 6 + pivot])
                pivot = i;
        if (pivot != k) {
            for (std::size_t i = 0; i < 6; ++i)
                std::swap(residual[k * 6 + i], residual[pivot * 6 + i]);
            for (std::size_t i = 0; i < 6; ++i)
                std::swap(residual[i * 6 + k], residual[i * 6 + pivot]);
        }
        const double diagonal = residual[k * 6 + k];
        if (diagonal < -tolerance)
            return false;
        if (diagonal <= tolerance) {
            for (std::size_t i = k; i < 6; ++i)
                for (std::size_t j = k; j < 6; ++j)
                    if (std::abs(residual[i * 6 + j]) > tolerance)
                        return false;
            return true;
        }
        for (std::size_t i = k + 1; i < 6; ++i)
            for (std::size_t j = k + 1; j < 6; ++j)
                residual[i * 6 + j] -= (residual[i * 6 + k] / diagonal) * residual[k * 6 + j];
    }
    return true;
}
result<void> validate_state(const map_state& map, const pose_asset_limits& limits,
                            const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Map validation cancelled");
    if (!limits_valid(limits))
        return fail(error_code::INVALID_ARGUMENT, "Invalid map codec limits");
    if (map.scale != scale_status::ARBITRARY)
        return fail(error_code::UNSUPPORTED_OPERATION, "Measured map scale is not implemented");
    if (map.initial_pose_asset_sha256 && !digest_valid(*map.initial_pose_asset_sha256))
        return fail(error_code::INVALID_DATA, "Invalid initial pose asset dependency");
    if (!map.revision.value &&
        (map.initial_pose_asset_sha256 || !map.calibrations.empty() || !map.observations.empty() ||
         !map.poses.empty() || !map.world_from_submap.empty() || !map.invalidated_frames.empty()))
        return fail(error_code::INVALID_DATA, "Revision zero map must be empty");
    if (map.observations.size() > limits.records.max_frames ||
        map.poses.size() > limits.records.max_frames ||
        map.invalidated_frames.size() > limits.records.max_frames ||
        map.calibrations.size() > limits.records.max_calibrations ||
        map.world_from_submap.size() > limits.records.max_submaps)
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset record budget exceeded");
    for (const auto& [id, calibration] : map.calibrations)
        if (id != calibration.id || !valid(calibration))
            return fail(error_code::INVALID_DATA, "Invalid pose asset calibration");
    for (const auto& [id, observation] : map.observations) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (!id.value || id != observation.id || !observation.capture.value ||
            !map.calibrations.contains(observation.calibration) ||
            observation.timestamp.time_base_numerator <= 0 ||
            observation.timestamp.time_base_denominator <= 0 ||
            observation.capture_time.count() < 0 || !digest_valid(observation.image_sha256))
            return fail(error_code::INVALID_DATA, "Invalid pose asset observation");
    }
    for (const auto& [id, alignment] : map.world_from_submap)
        if (!id.value || !valid(alignment))
            return fail(error_code::INVALID_DATA, "Invalid pose asset alignment");
    for (const auto& [id, pose] : map.poses) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (id != pose.frame || !map.observations.contains(id) ||
            !map.world_from_submap.contains(pose.submap) ||
            pose.capture_time != map.observations.at(id).capture_time || !pose.revision.value ||
            pose.revision.value > map.revision.value || !valid(pose.submap_from_camera) ||
            std::abs(pose.submap_from_camera.scale - 1) > 1e-12 ||
            static_cast<std::uint8_t>(pose.tracking) >
                static_cast<std::uint8_t>(tracking_state::FINISHED) ||
            (pose.covariance && !covariance_valid(*pose.covariance)))
            return fail(error_code::INVALID_DATA, "Invalid pose asset pose or covariance");
        if (!valid(compose(map.world_from_submap.at(pose.submap), pose.submap_from_camera)))
            return fail(error_code::INVALID_DATA, "Pose asset world transform overflow");
    }
    for (auto id : map.invalidated_frames)
        if (!id.value)
            return fail(error_code::INVALID_DATA, "Invalid pose invalidation ID");
    return {};
}
std::array<double, 4> unpack(const wire::coefficients& v) { return {v.a(), v.b(), v.c(), v.d()}; }
result<record_offsets> encode_records(flatbuffers::FlatBufferBuilder& builder, const map_state& map,
                                      const pose_asset_limits& limits,
                                      const std::stop_token& stop) {
    record_offsets encoded;
    const auto budget_check = [&]() -> result<void> {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (builder.GetSize() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
        return {};
    };
    auto& calibrations = encoded.calibrations;
    calibrations.reserve(map.calibrations.size());
    for (const auto& [id, c] : map.calibrations) {
        const wire::coefficients intrinsics(c.camera.fx, c.camera.fy, c.camera.cx, c.camera.cy);
        const wire::coefficients distortion(c.distortion[0], c.distortion[1], c.distortion[2],
                                            c.distortion[3]);
        const wire::coefficients image(c.image_transform[0], c.image_transform[1],
                                       c.image_transform[2], c.image_transform[3]);
        calibrations.push_back(wire::Createcalibration(builder, id.value, c.width, c.height,
                                                       static_cast<wire::camera_model>(c.model),
                                                       &intrinsics, &distortion, &image));
        const auto admitted = budget_check();
        if (!admitted)
            return std::unexpected(admitted.error());
    }
    auto& observations = encoded.observations;
    observations.reserve(map.observations.size());
    for (const auto& [id, o] : map.observations) {
        const auto image_hash = builder.CreateString(o.image_sha256);
        observations.push_back(wire::Createobservation(
            builder, id.value, o.capture.value, o.calibration.value, o.timestamp.pts,
            o.timestamp.time_base_numerator, o.timestamp.time_base_denominator,
            o.capture_time.count(), image_hash));
        const auto admitted = budget_check();
        if (!admitted)
            return std::unexpected(admitted.error());
    }
    auto& alignments = encoded.alignments;
    alignments.reserve(map.world_from_submap.size());
    for (const auto& [id, t] : map.world_from_submap) {
        const auto transform = pack(t);
        alignments.push_back(wire::Createalignment(builder, id.value, &transform));
        const auto admitted = budget_check();
        if (!admitted)
            return std::unexpected(admitted.error());
    }
    auto& poses = encoded.poses;
    poses.reserve(map.poses.size());
    for (const auto& [id, p] : map.poses) {
        const auto local = pack(p.submap_from_camera);
        const auto world = pack(compose(map.world_from_submap.at(p.submap), p.submap_from_camera));
        const auto covariance =
            p.covariance ? builder.CreateVector(p.covariance->data(), p.covariance->size()) : 0;
        poses.push_back(wire::Createpose(
            builder, id.value, p.submap.value, p.revision.value, p.capture_time.count(),
            static_cast<wire::tracking_state>(p.tracking), &local, &world, covariance));
        const auto admitted = budget_check();
        if (!admitted)
            return std::unexpected(admitted.error());
    }
    encoded.invalidated.reserve(map.invalidated_frames.size());
    for (auto id : map.invalidated_frames)
        encoded.invalidated.push_back(id.value);
    return encoded;
}
result<map_snapshot> decode_records(record_view records, const pose_asset_limits& limits,
                                    const std::stop_token& stop) {
    if (records.calibrations->size() > limits.records.max_calibrations ||
        records.observations->size() > limits.records.max_frames ||
        records.poses->size() > limits.records.max_frames ||
        records.alignments->size() > limits.records.max_submaps ||
        records.invalidated_frames->size() > limits.records.max_frames)
        return fail(error_code::RESOURCE_EXHAUSTED, "Map record budget exceeded");
    auto map = std::make_shared<map_state>();
    map->epoch = {records.epoch};
    map->revision = {records.revision};
    for (const auto* c : *records.calibrations) {
        const auto intrinsic = unpack(*c->intrinsics());
        calibration_record calibration{{c->id()},
                                       {intrinsic[0], intrinsic[1], intrinsic[2], intrinsic[3]},
                                       c->width(),
                                       c->height(),
                                       static_cast<calibration_model>(c->model()),
                                       unpack(*c->distortion()),
                                       unpack(*c->image_transform())};
        if (!map->calibrations.emplace(calibration.id, calibration).second)
            return fail(error_code::INVALID_DATA, "Duplicate pose calibration ID");
    }
    for (const auto* o : *records.observations) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (o->image_sha256()->size() != 64)
            return fail(error_code::INVALID_DATA, "Invalid image hash length");
        observation_record observation{
            {o->frame()},
            {o->capture()},
            {o->calibration_id()},
            {o->original_pts(), o->time_base_numerator(), o->time_base_denominator()},
            std::chrono::nanoseconds(o->capture_nanoseconds()),
            o->image_sha256()->str()};
        if (!map->observations.emplace(observation.id, std::move(observation)).second)
            return fail(error_code::INVALID_DATA, "Duplicate pose observation ID");
    }
    for (const auto* a : *records.alignments)
        if (!map->world_from_submap.emplace(submap_id{a->submap()}, unpack(*a->world_from_submap()))
                 .second)
            return fail(error_code::INVALID_DATA, "Duplicate pose submap ID");
    for (const auto* p : *records.poses) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        pose_estimate pose{{p->frame()},
                           {p->submap()},
                           unpack(*p->submap_from_camera()),
                           std::chrono::nanoseconds(p->capture_nanoseconds()),
                           {p->revision()},
                           static_cast<tracking_state>(p->tracking()),
                           {}};
        if (p->covariance()) {
            if (p->covariance()->size() != 36)
                return fail(error_code::INVALID_DATA, "Pose covariance must be 6x6");
            pose.covariance.emplace();
            std::copy(p->covariance()->begin(), p->covariance()->end(), pose.covariance->begin());
        }
        const auto alignment = map->world_from_submap.find(pose.submap);
        const auto world = unpack(*p->world_from_camera());
        if (alignment == map->world_from_submap.end() || !valid(world) ||
            !same_transform(compose(alignment->second, pose.submap_from_camera), world))
            return fail(error_code::INVALID_DATA,
                        "Pose world transform disagrees with submap dependencies");
        if (!map->poses.emplace(pose.frame, pose).second)
            return fail(error_code::INVALID_DATA, "Duplicate pose frame ID");
    }
    for (auto id : *records.invalidated_frames)
        if (!map->invalidated_frames.emplace(frame_id{id}).second)
            return fail(error_code::INVALID_DATA, "Duplicate pose invalidation ID");
    const auto valid = validate_state(*map, limits, stop);
    if (!valid)
        return std::unexpected(valid.error());
    return map_snapshot(std::move(map));
}
} // namespace world_explorer::map_codec
