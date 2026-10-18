#include "world_explorer/scene_store/scene.hpp"

#include "world_explorer/core/hash.hpp"

#include <bit>
#include <limits>
#include <set>

namespace world_explorer {
namespace {

// Scene hashes append millions of fixed-width fields. Buffer those fields before
// passing them to OpenSSL while retaining the exact canonical byte stream.
struct SceneHashWriter {
    Sha256Builder m_hash;
    std::array<std::byte, 16384> m_pending {};
    std::size_t m_used {};

    void flush() {
        if (m_used) m_hash.update(std::span(m_pending.data(), m_used));
        m_used = 0;
    }

    void add(std::uint64_t value) {
        if (m_used == m_pending.size()) flush();
        for (unsigned index { 0 }; index < 8; ++index)
            m_pending[m_used++] = static_cast<std::byte>((value >> (8U * index)) & 255U);
    }

    void add_bytes(std::span<const std::byte> bytes) {
        flush();
        m_hash.update(bytes);
    }

    std::string finish() {
        flush();
        return m_hash.finish();
    }
};

void append_chunk(SceneHashWriter& hash, ChunkId id, const SceneChunk& chunk) {
    const auto K_ADD = [&](std::uint64_t value) { hash.add(value); };
    K_ADD(id.m_value);
    K_ADD(chunk.m_generation);
    K_ADD(chunk.m_pose_revision.m_value);
    K_ADD(chunk.m_geometry_valid);
    const auto& t = chunk.m_world_from_chunk;
    for (double value : {t.m_rotation.m_w,
                         t.m_rotation.m_x,
                         t.m_rotation.m_y,
                         t.m_rotation.m_z,
                         t.m_translation.m_x,
                         t.m_translation.m_y,
                         t.m_translation.m_z,
                         t.m_scale,
                         chunk.m_bounds.m_minimum.m_x,
                         chunk.m_bounds.m_minimum.m_y,
                         chunk.m_bounds.m_minimum.m_z,
                         chunk.m_bounds.m_maximum.m_x,
                         chunk.m_bounds.m_maximum.m_y,
                         chunk.m_bounds.m_maximum.m_z})
        K_ADD(std::bit_cast<std::uint64_t>(value));
    K_ADD(chunk.m_source_frames.size());
    for (const auto K_FRAME : chunk.m_source_frames) K_ADD(K_FRAME.m_value);
    K_ADD(chunk.m_splats.size());
    for (const auto& splat : chunk.m_splats) {
        for (float value : splat.m_mean) K_ADD(std::bit_cast<std::uint32_t>(value));
        for (float value : splat.m_scale) K_ADD(std::bit_cast<std::uint32_t>(value));
        for (float value : splat.m_rotation) K_ADD(std::bit_cast<std::uint32_t>(value));
        K_ADD(std::bit_cast<std::uint32_t>(splat.m_opacity));
        K_ADD(splat.m_sh_degree);
        for (float value : splat.m_sh) K_ADD(std::bit_cast<std::uint32_t>(value));
    }
}

}  // namespace

std::string scene_chunk_content_hash(const SceneChunk& chunk) {
    SceneHashWriter hash;
    append_chunk(hash, chunk.m_id, chunk);
    return hash.finish();
}

std::string scene_content_hash(const SceneState& scene) {
    SceneHashWriter hash;
    const auto K_ADD = [&](std::uint64_t value) { hash.add(value); };
    K_ADD(scene.m_id.m_value);
    K_ADD(scene.m_epoch.m_value);
    K_ADD(scene.m_revision.m_value);
    K_ADD(static_cast<std::uint64_t>(scene.m_scale));
    K_ADD(scene.m_provenance.size());
    for (const auto& [K_KEY, K_VALUE] : scene.m_provenance) {
        K_ADD(K_KEY.size());
        hash.add_bytes(std::as_bytes(std::span(K_KEY.data(), K_KEY.size())));
        K_ADD(K_VALUE.size());
        hash.add_bytes(std::as_bytes(std::span(K_VALUE.data(), K_VALUE.size())));
    }
    K_ADD(scene.m_chunks.size());
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        append_chunk(hash, K_ID, *K_CHUNK);
    }
    return hash.finish();
}

Result<std::size_t> validate_chunk(const SceneChunk& chunk, SceneLimits limits) {
    if (chunk.m_id.m_value == 0 || chunk.m_generation == 0 || !valid(chunk.m_bounds) ||
        !valid(chunk.m_world_from_chunk))
        return fail(ErrorCode::INVALID_DATA, "invalid chunk identity, bounds or transform");
    if (chunk.m_splats.size() > limits.m_max_splats ||
        chunk.m_splats.size() > limits.m_max_payload_bytes / sizeof(Gaussian) ||
        chunk.m_source_frames.size() > limits.m_max_source_frames)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "chunk exceeds configured limits");
    if (chunk.m_geometry_valid && chunk.m_pose_revision.m_value == 0)
        return fail(ErrorCode::INVALID_DATA, "geometry has no source pose revision");
    for (const auto& splat : chunk.m_splats) {
        if (splat.m_sh_degree > 3 || !std::isfinite(splat.m_opacity) || splat.m_opacity < 0 ||
            splat.m_opacity > 1)
            return fail(ErrorCode::INVALID_DATA, "invalid Gaussian opacity or SH degree");
        if (!valid(Quatd {splat.m_rotation[0],
                          splat.m_rotation[1],
                          splat.m_rotation[2],
                          splat.m_rotation[3]}))
            return fail(ErrorCode::INVALID_DATA, "Gaussian quaternion is not normalized");
        for (std::size_t axis { 0uz }; axis < 3uz; ++axis)
            if (!std::isfinite(splat.m_mean[axis]) || !std::isfinite(splat.m_scale[axis]) ||
                splat.m_scale[axis] <= 0)
                return fail(ErrorCode::INVALID_DATA,
                            "invalid Gaussian position or covariance scale");
        const Vec3d K_MEAN { splat.m_mean[0], splat.m_mean[1], splat.m_mean[2] };
        if (K_MEAN.m_x < chunk.m_bounds.m_minimum.m_x ||
            K_MEAN.m_y < chunk.m_bounds.m_minimum.m_y ||
            K_MEAN.m_z < chunk.m_bounds.m_minimum.m_z ||
            K_MEAN.m_x > chunk.m_bounds.m_maximum.m_x ||
            K_MEAN.m_y > chunk.m_bounds.m_maximum.m_y || K_MEAN.m_z > chunk.m_bounds.m_maximum.m_z)
            return fail(ErrorCode::INVALID_DATA, "Gaussian outside chunk bounds");
        for (float coefficient : splat.m_sh)
            if (!std::isfinite(coefficient))
                return fail(ErrorCode::INVALID_DATA, "nonfinite SH coefficient");
    }
    return chunk.m_splats.size() * sizeof(Gaussian);
}

Result<std::size_t> validate_scene(const SceneState& scene, SceneLimits limits) {
    if (scene.m_id.m_value == 0) return fail(ErrorCode::INVALID_DATA, "scene ID must be nonzero");
    if (scene.m_scale != ScaleStatus::ARBITRARY && scene.m_scale != ScaleStatus::METRIC_MEASURED)
        return fail(ErrorCode::INVALID_DATA, "unsupported scene scale status");
    if (scene.m_provenance.size() > 32)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Too many provenance fields");
    for (const auto& [K_KEY, K_VALUE] : scene.m_provenance)
        if (K_KEY.empty() || K_KEY.size() > 128 || K_VALUE.size() > 4096)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Provenance field exceeds limit");
    if (scene.m_chunks.size() > limits.m_max_chunks)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "scene chunk budget exceeded");
    std::size_t bytes {}, splats {};
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        if (!K_CHUNK || K_ID != K_CHUNK->m_id)
            return fail(ErrorCode::INVALID_DATA, "chunk key differs from identity");
        const auto K_SIZE = validate_chunk(*K_CHUNK, limits);
        if (!K_SIZE) return std::unexpected(K_SIZE.error());
        if (*K_SIZE > limits.m_max_payload_bytes - bytes ||
            K_CHUNK->m_splats.size() > limits.m_max_splats - splats)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "aggregate scene budget exceeded");
        bytes += *K_SIZE;
        splats += K_CHUNK->m_splats.size();
    }
    return bytes;
}

Result<SceneDeltaBatch> make_snapshot_batch(SceneSnapshot snapshot, OperationId operation) {
    if (!snapshot || operation.m_value == 0)
        return fail(ErrorCode::INVALID_ARGUMENT, "missing snapshot or operation ID");
    const auto K_BYTES = validate_scene(*snapshot);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    return SceneDeltaBatch {snapshot->m_id,
                            snapshot->m_epoch,
                            {},
                            snapshot->m_revision,
                            operation,
                            std::move(snapshot),
                            true,
                            *K_BYTES};
}

}  // namespace world_explorer
