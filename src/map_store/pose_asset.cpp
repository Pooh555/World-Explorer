#include "world_explorer/map_store/pose_asset.hpp"
#include "poses_generated.h"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace world_explorer {
namespace {
namespace wire = pose_wire;
constexpr std::size_t MAX_ASSET_BYTES = std::size_t{256} * 1024 * 1024;
constexpr std::size_t MAX_FRAMES = 1000000;
constexpr std::size_t MAX_GROUPS = 4096;
constexpr std::size_t IO_BLOCK_BYTES = std::size_t{256} * 1024;
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
nlohmann::json transform_json(const similarity_transform& t) {
    return {{"rotation_wxyz", {t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z}},
            {"translation", {t.translation.x, t.translation.y, t.translation.z}},
            {"scale", t.scale}};
}
struct staged_directory {
    std::filesystem::path path;
    ~staged_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
struct file_closer {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
} // namespace

result<std::vector<std::byte>> encode_pose_asset(const canonical_pose_asset& asset,
                                                 pose_asset_limits limits,
                                                 const std::stop_token& stop) {
    try {
        const auto checked = validate(asset, limits, stop);
        if (!checked)
            return std::unexpected(checked.error());
        flatbuffers::FlatBufferBuilder builder;
        const auto budget_check = [&]() -> result<void> {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled");
            if (builder.GetSize() > limits.encoded_bytes.bytes)
                return fail(error_code::RESOURCE_EXHAUSTED, "Encoded pose asset exceeds budget");
            return {};
        };
        const auto& map = *asset.snapshot;
        std::vector<flatbuffers::Offset<wire::calibration>> calibrations;
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
        std::vector<flatbuffers::Offset<wire::observation>> observations;
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
        std::vector<flatbuffers::Offset<wire::alignment>> alignments;
        alignments.reserve(map.world_from_submap.size());
        for (const auto& [id, t] : map.world_from_submap) {
            const auto transform = pack(t);
            alignments.push_back(wire::Createalignment(builder, id.value, &transform));
            const auto admitted = budget_check();
            if (!admitted)
                return std::unexpected(admitted.error());
        }
        std::vector<flatbuffers::Offset<wire::pose>> poses;
        poses.reserve(map.poses.size());
        for (const auto& [id, p] : map.poses) {
            const auto local = pack(p.submap_from_camera);
            const auto world =
                pack(compose(map.world_from_submap.at(p.submap), p.submap_from_camera));
            const auto covariance =
                p.covariance ? builder.CreateVector(p.covariance->data(), p.covariance->size()) : 0;
            poses.push_back(wire::Createpose(
                builder, id.value, p.submap.value, p.revision.value, p.capture_time.count(),
                static_cast<wire::tracking_state>(p.tracking), &local, &world, covariance));
            const auto admitted = budget_check();
            if (!admitted)
                return std::unexpected(admitted.error());
        }
        const auto& p = asset.provenance;
        const auto source = builder.CreateString(p.source_sha256);
        const auto decode = builder.CreateString(p.decode_completion_sha256);
        const auto pose = builder.CreateString(p.pose_completion_sha256);
        const auto journal = p.journal_sha256 ? builder.CreateString(*p.journal_sha256) : 0;
        const auto provenance = wire::Createprovenance_record(
            builder, source, decode, pose, journal, wire::scale_provenance::ARBITRARY_MONOCULAR,
            wire::gravity_provenance::UNKNOWN);
        std::vector<std::uint64_t> invalidated;
        invalidated.reserve(map.invalidated_frames.size());
        for (auto id : map.invalidated_frames)
            invalidated.push_back(id.value);
        const auto cameras = builder.CreateVector(calibrations);
        const auto frames = builder.CreateVector(observations);
        const auto anchors = builder.CreateVector(alignments);
        const auto estimates = builder.CreateVector(poses);
        const auto invalidations = builder.CreateVector(invalidated);
        const auto root = wire::Createpose_asset(
            builder, 1, wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER, map.epoch.value,
            map.revision.value, provenance, cameras, frames, anchors, estimates, invalidations);
        wire::Finishpose_assetBuffer(builder, root);
        const auto admitted = budget_check();
        if (!admitted)
            return std::unexpected(admitted.error());
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
        auto map = std::make_shared<map_state>();
        map->epoch = {root->epoch()};
        map->revision = {root->revision()};
        canonical_pose_asset asset{map,
                                   {provenance->source_sha256()->str(),
                                    provenance->decode_completion_sha256()->str(),
                                    provenance->pose_completion_sha256()->str(),
                                    {}}};
        if (provenance->journal_sha256())
            asset.provenance.journal_sha256 = provenance->journal_sha256()->str();
        for (const auto* c : *root->calibrations()) {
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
        for (const auto* o : *root->observations()) {
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
        for (const auto* a : *root->alignments())
            if (!map->world_from_submap
                     .emplace(submap_id{a->submap()}, unpack(*a->world_from_submap()))
                     .second)
                return fail(error_code::INVALID_DATA, "Duplicate pose submap ID");
        for (const auto* p : *root->poses()) {
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
                std::copy(p->covariance()->begin(), p->covariance()->end(),
                          pose.covariance->begin());
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
        for (auto id : *root->invalidated_frames())
            if (!map->invalidated_frames.emplace(frame_id{id}).second)
                return fail(error_code::INVALID_DATA, "Duplicate pose invalidation ID");
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
    const auto encoded = encode_pose_asset(asset, limits, stop);
    if (!encoded)
        return std::unexpected(encoded.error());
    try {
        if (destination.filename().empty())
            return fail(error_code::INVALID_ARGUMENT, "Pose destination must name a file");
        auto parent = destination.parent_path();
        if (parent.empty())
            parent = ".";
        if (!std::filesystem::is_directory(parent))
            return fail(error_code::IO_ERROR, "Pose destination parent is missing");
        static std::atomic<std::uint64_t> sequence{};
        staged_directory staging;
        for (std::size_t attempt = 0; attempt < 32; ++attempt) {
            const auto candidate =
                parent /
                (".weposes-tmp-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(sequence.fetch_add(1)));
            if (std::filesystem::create_directory(candidate)) {
                staging.path = candidate;
                break;
            }
        }
        if (staging.path.empty())
            return fail(error_code::IO_ERROR, "Cannot reserve pose staging directory");
        const auto temporary = staging.path / "poses.weposes";
        std::FILE* raw{};
#ifdef _WIN32
        if (_wfopen_s(&raw, temporary.c_str(), L"wb") != 0)
            raw = nullptr;
#else
        raw = std::fopen(temporary.c_str(), "wb");
#endif
        std::unique_ptr<std::FILE, file_closer> file(raw);
        if (!file)
            return fail(error_code::IO_ERROR, "Cannot open staged pose asset");
        sha256_builder digest;
        for (std::size_t offset = 0; offset < encoded->size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled before publication");
            const auto count = std::min(IO_BLOCK_BYTES, encoded->size() - offset);
            const auto block = std::span(*encoded).subspan(offset, count);
            if (std::fwrite(block.data(), 1, block.size(), file.get()) != block.size())
                return fail(error_code::IO_ERROR, "Pose asset write failed");
            digest.update(block);
            offset += count;
        }
        if (std::fflush(file.get()) != 0)
            return fail(error_code::IO_ERROR, "Pose asset flush failed");
#ifdef _WIN32
        const bool flushed = _commit(_fileno(file.get())) == 0;
#else
        const bool flushed = fsync(fileno(file.get())) == 0;
#endif
        const bool closed = std::fclose(file.release()) == 0;
        if (!flushed || !closed)
            return fail(error_code::IO_ERROR, "Pose asset durable write failed");
        auto hash = digest.finish();
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled before publication");
        // Same filesystem, atomic no-overwrite publication, including concurrent
        // writers. A destination symlink is an existing entry and also fails.
        std::filesystem::create_hard_link(temporary, destination);
#ifndef _WIN32
        const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0)
            return fail(error_code::IO_ERROR, "Cannot open pose directory for durability");
        const int flushed_directory = fsync(directory);
        close(directory);
        if (flushed_directory != 0)
            return fail(error_code::IO_ERROR, "Pose directory flush failed after publication");
#endif
        return hash;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose publication allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Pose publication failed: ") + e.what());
    }
}
result<canonical_pose_asset> read_pose_asset(const std::filesystem::path& path,
                                             const std::string& expected_sha256,
                                             pose_asset_limits limits,
                                             const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (!limits_valid(limits))
            return fail(error_code::INVALID_ARGUMENT, "Invalid pose asset limits");
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
            return fail(error_code::INVALID_DATA, "Pose asset must be a regular nonsymlink file");
        const auto size = std::filesystem::file_size(path);
        if (!size || size > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset file exceeds budget");
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return fail(error_code::IO_ERROR, "Cannot open pose asset");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled");
            const auto count = std::min(IO_BLOCK_BYTES, bytes.size() - offset);
            input.read(reinterpret_cast<char*>(bytes.data() + offset),
                       static_cast<std::streamsize>(count));
            if (input.gcount() != static_cast<std::streamsize>(count))
                return fail(error_code::IO_ERROR, "Pose asset truncated during read");
            offset += count;
        }
        if (input.peek() != std::char_traits<char>::eof() || input.bad())
            return fail(error_code::INVALID_DATA, "Pose asset grew or failed during read");
        return decode_pose_asset(bytes, expected_sha256, limits, stop);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Pose asset read failed: ") + e.what());
    }
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
            value["poses"].push_back(
                {{"frame", id.value},
                 {"submap", p.submap.value},
                 {"revision", p.revision.value},
                 {"capture_nanoseconds", p.capture_time.count()},
                 {"tracking", static_cast<std::uint8_t>(p.tracking)},
                 {"submap_from_camera", transform_json(p.submap_from_camera)},
                 {"world_from_camera", transform_json(compose(map.world_from_submap.at(p.submap),
                                                              p.submap_from_camera))},
                 {"covariance",
                  p.covariance ? nlohmann::json(*p.covariance) : nlohmann::json(nullptr)}});
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
