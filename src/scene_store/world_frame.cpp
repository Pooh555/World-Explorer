#include "world_explorer/scene_store/world_frame.hpp"

#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace world_explorer {
namespace {

using Json = nlohmann::json;

void fields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size()) {
        throw std::runtime_error("Unexpected world-frame fields");
    }
    for (const auto* name : names) {
        if (!value.contains(name)) {
            throw std::runtime_error("Missing world-frame field");
        }
    }
}

std::uint64_t integer(const Json& value) {
    if (!value.is_number_unsigned()) {
        throw std::runtime_error("World-frame ID/index must be an unsigned integer");
    }
    return value.get<std::uint64_t>();
}

double real(const Json& value) {
    if (!value.is_number()) {
        throw std::runtime_error("World-frame scalar must be numeric");
    }
    const auto K_VALUE = value.get<double>();
    if (!std::isfinite(K_VALUE)) {
        throw std::runtime_error("Nonfinite world-frame scalar");
    }
    return K_VALUE;
}

Vec3d vector(const Json& value) {
    if (!value.is_array() || value.size() != 3uz) {
        throw std::runtime_error("World-frame vector must have three components");
    }
    return { real(value.at(0uz)), real(value.at(1uz)), real(value.at(2uz)) };
}

Vec3d anchor(const SceneState& source, const Json& value) {
    fields(value, { "chunk_id", "triangle", "barycentric" });
    const auto K_CHUNK = source.m_chunks.find(ChunkId { integer(value.at("chunk_id")) });
    if (K_CHUNK == source.m_chunks.end() || !K_CHUNK->second->m_geometry_valid) {
        throw std::runtime_error("World-frame anchor has no current mesh");
    }
    const auto& chunk = *K_CHUNK->second;
    if (!chunk.m_mesh) {
        throw std::runtime_error("World-frame anchor has no mesh payload");
    }
    const auto& mesh = *chunk.m_mesh;
    const auto K_INDEX = integer(value.at("triangle"));
    if (K_INDEX >= mesh.m_triangles.size()) {
        throw std::runtime_error("World-frame triangle is out of range");
    }
    const auto K_WEIGHTS = vector(value.at("barycentric"));
    if (K_WEIGHTS.m_x < 0 || K_WEIGHTS.m_y < 0 || K_WEIGHTS.m_z < 0 ||
        std::abs(K_WEIGHTS.m_x + K_WEIGHTS.m_y + K_WEIGHTS.m_z - 1) > 1e-12) {
        throw std::runtime_error("World-frame barycentric weights are invalid");
    }
    const auto& triangle = mesh.m_triangles.at(static_cast<std::size_t>(K_INDEX));
    const auto K_POSITION = [&](std::uint32_t index) {
        const auto& position = mesh.m_vertices.at(index).m_position;
        return Vec3d { position[0uz], position[1uz], position[2uz] };
    };
    return transform_point(chunk.m_world_from_chunk,
                           K_POSITION(triangle[0uz]) * K_WEIGHTS.m_x +
                               K_POSITION(triangle[1uz]) * K_WEIGHTS.m_y +
                               K_POSITION(triangle[2uz]) * K_WEIGHTS.m_z);
}

void record(const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    if (K_TEXT.empty() || K_TEXT.size() > 1024uz || K_TEXT.find('\0') != K_TEXT.npos) {
        throw std::runtime_error("Measurement record must be nonempty bounded text");
    }
}

bool same_transform(const SimilarityTransform& a, const SimilarityTransform& b) {
    return a.m_translation == b.m_translation && a.m_scale == b.m_scale &&
           a.m_rotation.m_w == b.m_rotation.m_w && a.m_rotation.m_x == b.m_rotation.m_x &&
           a.m_rotation.m_y == b.m_rotation.m_y && a.m_rotation.m_z == b.m_rotation.m_z;
}

Quatd multiply(Quatd a, Quatd b) {
    return { a.m_w * b.m_w - a.m_x * b.m_x - a.m_y * b.m_y - a.m_z * b.m_z,
            a.m_w * b.m_x + a.m_x * b.m_w + a.m_y * b.m_z - a.m_z * b.m_y,
            a.m_w * b.m_y - a.m_x * b.m_z + a.m_y * b.m_w + a.m_z * b.m_x,
            a.m_w * b.m_z + a.m_x * b.m_y - a.m_y * b.m_x + a.m_z * b.m_w };
}

}  // namespace

Result<WorldFrameAlignment>
admit_world_frame(const SceneState& source, std::string_view document, std::string_view digest) {
    if (document.size() > K_WORLD_FRAME_DOCUMENT_BYTES) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "World-frame document exceeds byte budget");
    }
    if (source.m_world_frame || source.m_scale != ScaleStatus::ARBITRARY) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Alignment requires an arbitrary source snapshot");
    }
    const auto K_VALID = validate_scene(source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (source.m_epoch.m_value == std::numeric_limits<std::uint64_t>::max() ||
        source.m_revision.m_value == std::numeric_limits<std::uint64_t>::max()) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "World-frame epoch/revision exhausted");
    }
    if (document.find('\0') != document.npos ||
        sha256(std::as_bytes(std::span(document.data(), document.size()))) != digest) {
        return fail(ErrorCode::INVALID_DATA, "World-frame checksum or text differs");
    }
    try {
        std::vector<std::set<std::string>> keys;
        const auto K_DOCUMENT =
            Json::parse(document, [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 8) {
                    throw std::runtime_error("World-frame nesting exceeds budget");
                }
                const auto K_DEPTH = static_cast<std::size_t>(depth);
                if (event == Json::parse_event_t::object_start) {
                    keys.resize(K_DEPTH + 1uz);
                    keys[K_DEPTH].clear();
                }
                if (event == Json::parse_event_t::key &&
                    !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second) {
                    throw std::runtime_error("Duplicate world-frame key");
                }
                return true;
            });
        fields(K_DOCUMENT,
               { "schema",
                "source_sha256",
                "source_epoch",
                "source_revision",
                "evidence_kind",
                "distance",
                "up",
                "origin" });
        if (K_DOCUMENT.at("schema") != "WORLD_FRAME_ALIGNMENT_V1") {
            throw std::runtime_error("Unsupported world-frame schema");
        }
        if (integer(K_DOCUMENT.at("source_epoch")) != source.m_epoch.m_value) {
            return fail(ErrorCode::WRONG_EPOCH, "Measurement source epoch differs; resync");
        }
        if (integer(K_DOCUMENT.at("source_revision")) != source.m_revision.m_value ||
            K_DOCUMENT.at("source_sha256") != scene_content_hash(source)) {
            return fail(ErrorCode::WRONG_BASE,
                        "Measurement source content/revision differs; resync");
        }
        const auto K_KIND = source.m_provenance.find("source_kind");
        const bool K_SYNTHETIC { K_KIND != source.m_provenance.end() &&
                                K_KIND->second == "synthetic_diagnostic" };
        if (K_DOCUMENT.at("evidence_kind") !=
            (K_SYNTHETIC ? "SYNTHETIC_DIAGNOSTIC" : "INDEPENDENT_MEASUREMENT")) {
            throw std::runtime_error("Measurement evidence kind differs from scene provenance");
        }
        const auto& distance = K_DOCUMENT.at("distance");
        fields(distance, { "a", "b", "metres", "uncertainty_metres", "method", "record" });
        record(distance.at("record"));
        if (distance.at("method") != (K_SYNTHETIC ? "SYNTHETIC" : "MEASURED_DISTANCE")) {
            throw std::runtime_error("Unsupported distance measurement method");
        }
        const auto K_DELTA = anchor(source, distance.at("b")) - anchor(source, distance.at("a"));
        const double K_LENGTH { std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z) };
        const auto K_METRES = real(distance.at("metres"));
        const auto K_UNCERTAINTY = real(distance.at("uncertainty_metres"));
        if (!std::isfinite(K_LENGTH) || K_LENGTH <= 0 || K_METRES <= 0 || K_UNCERTAINTY < 0 ||
            K_UNCERTAINTY >= K_METRES) {
            throw std::runtime_error("Invalid measured distance or uncertainty");
        }
        const auto& up = K_DOCUMENT.at("up");
        fields(up, { "direction", "uncertainty_degrees", "method", "record" });
        record(up.at("record"));
        const auto K_METHOD = up.at("method").get<std::string>();
        if (K_SYNTHETIC ? K_METHOD != "SYNTHETIC"
                        : (K_METHOD != "IMU" && K_METHOD != "USER_CONFIRMED_FLOOR")) {
            throw std::runtime_error("Unsupported up reference method");
        }
        const auto K_ANGLE = real(up.at("uncertainty_degrees"));
        if (K_ANGLE < 0 || K_ANGLE >= 90) {
            throw std::runtime_error("Invalid up reference uncertainty");
        }
        const auto K_UP = vector(up.at("direction"));
        const double K_UP_LENGTH { std::hypot(K_UP.m_x, K_UP.m_y, K_UP.m_z) };
        if (!std::isfinite(K_UP_LENGTH) || K_UP_LENGTH <= 0) {
            throw std::runtime_error("Invalid up reference direction");
        }
        const Vec3d K_UNIT_UP {
            K_UP.m_x / K_UP_LENGTH, K_UP.m_y / K_UP_LENGTH, K_UP.m_z / K_UP_LENGTH };
        // Stable shortest arc to +Y, including nearly antiparallel directions.
        // Yaw remains arbitrary; no reconstructed plane supplies an up reference.
        const auto K_AXIS = cross(K_UNIT_UP, { 0, 1, 0 });
        const double K_SINE { std::hypot(K_AXIS.m_x, K_AXIS.m_y, K_AXIS.m_z) };
        Quatd rotation;
        if (K_SINE == 0) {
            rotation = K_UNIT_UP.m_y < 0 ? Quatd { 0, 1, 0, 0 } : Quatd {};
        } else {
            const double K_HALF_ANGLE { std::atan2(K_SINE, K_UNIT_UP.m_y) / 2 };
            const auto K_ROTATION_AXIS = K_AXIS * (std::sin(K_HALF_ANGLE) / K_SINE);
            rotation = { std::cos(K_HALF_ANGLE),
                        K_ROTATION_AXIS.m_x,
                        K_ROTATION_AXIS.m_y,
                        K_ROTATION_AXIS.m_z };
        }
        SimilarityTransform transform { rotation, {}, K_METRES / K_LENGTH };
        transform.m_translation =
            rotate(rotation, anchor(source, K_DOCUMENT.at("origin"))) * -transform.m_scale;
        if (!valid(transform)) {
            throw std::runtime_error("Derived world-frame transform is not representable");
        }
        WorldFrameAlignment alignment { source.m_epoch,
                                       source.m_revision,
                                       transform,
                                       K_SYNTHETIC,
                                       std::string(document),
                                       std::string(digest) };
        auto aligned = source;
        aligned.m_world_frame = alignment;
        for (const auto& [K_ID, K_CHUNK] : source.m_chunks) {
            (void)K_ID;
            if (K_CHUNK->m_mesh && !K_CHUNK->m_geometry_valid) {
                throw std::runtime_error("Stale geometry cannot be aligned");
            }
            const auto K_TRANSFORM = scene_world_from_chunk(aligned, *K_CHUNK);
            if (!valid(K_TRANSFORM)) {
                throw std::runtime_error("Aligned chunk transform overflows");
            }
            for (unsigned corner { 0 }; corner < 8; ++corner) {
                const auto& bounds = K_CHUNK->m_bounds;
                if (!finite(transform_point(
                        K_TRANSFORM,
                        { (corner & 1U) ? bounds.m_maximum.m_x : bounds.m_minimum.m_x,
                         (corner & 2U) ? bounds.m_maximum.m_y : bounds.m_minimum.m_y,
                         (corner & 4U) ? bounds.m_maximum.m_z : bounds.m_minimum.m_z }))) {
                    throw std::runtime_error("Aligned bounds overflow");
                }
            }
        }
        return alignment;
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Invalid world-frame measurement: ") + error.what());
    }
}

Result<void> validate_world_frame(const SceneState& aligned) {
    if (!aligned.m_world_frame) {
        return {};
    }
    const auto& frame = *aligned.m_world_frame;
    if (frame.m_source_epoch.m_value == std::numeric_limits<std::uint64_t>::max() ||
        frame.m_source_revision.m_value == std::numeric_limits<std::uint64_t>::max() ||
        aligned.m_epoch.m_value != frame.m_source_epoch.m_value + 1 ||
        aligned.m_revision.m_value != frame.m_source_revision.m_value + 1 ||
        aligned.m_scale !=
            (frame.m_synthetic ? ScaleStatus::ARBITRARY : ScaleStatus::METRIC_MEASURED)) {
        return fail(ErrorCode::WRONG_EPOCH, "Aligned snapshot identity differs from its source");
    }
    auto source = aligned;
    source.m_world_frame.reset();
    source.m_scale = ScaleStatus::ARBITRARY;
    source.m_epoch = frame.m_source_epoch;
    source.m_revision = frame.m_source_revision;
    const auto K_FRAME = admit_world_frame(source, frame.m_document_json, frame.m_document_sha256);
    if (!K_FRAME) {
        return std::unexpected(K_FRAME.error());
    }
    if (K_FRAME->m_synthetic != frame.m_synthetic ||
        !same_transform(K_FRAME->m_world_from_source, frame.m_world_from_source)) {
        return fail(ErrorCode::INVALID_DATA,
                    "Cached world-frame transform differs from measurement");
    }
    return {};
}

SimilarityTransform scene_world_from_chunk(const SceneState& scene, const SceneChunk& chunk) {
    if (!scene.m_world_frame) {
        return chunk.m_world_from_chunk;
    }
    const auto& a = scene.m_world_frame->m_world_from_source;
    const auto& b = chunk.m_world_from_chunk;
    return { multiply(a.m_rotation, b.m_rotation),
            transform_point(a, b.m_translation),
            a.m_scale * b.m_scale };
}

WalkingBlockReason walking_block_reason(const SceneState& scene) {
    if (!scene.m_world_frame || !validate_world_frame(scene)) {
        return WalkingBlockReason::MISSING_MEASUREMENT;
    }
    return scene.m_world_frame->m_synthetic ? WalkingBlockReason::SYNTHETIC_DIAGNOSTIC
                                            : WalkingBlockReason::SUPPORTED_COLLISION_UNAVAILABLE;
}

std::string_view world_frame_label(const SceneState& scene) {
    if (!scene.m_world_frame) {
        return scene.m_scale == ScaleStatus::ARBITRARY
                   ? "arbitrary (not measured)"
                   : "legacy metric flag (measurement unavailable)";
    }
    return scene.m_world_frame->m_synthetic ? "synthetic alignment (not measured)"
                                            : "declared measured scale/up (geometry unverified)";
}

}  // namespace world_explorer
