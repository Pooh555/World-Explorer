#include "world_explorer/scene_store/scene.hpp"
#include "world_explorer/core/hash.hpp"
#include <bit>
#include <limits>
#include <set>

namespace world_explorer {
namespace {
// Scene hashes append millions of fixed-width fields. Buffer those fields before
// passing them to OpenSSL while retaining the exact canonical byte stream.
struct scene_hash_writer {
    sha256_builder hash;
    std::array<std::byte, 16384> pending{};
    std::size_t used{};

    void flush() {
        if (used)
            hash.update(std::span(pending.data(), used));
        used = 0;
    }
    void add(std::uint64_t value) {
        if (used == pending.size())
            flush();
        for (unsigned index = 0; index < 8; ++index)
            pending[used++] = static_cast<std::byte>((value >> (8U * index)) & 255U);
    }
    void add_bytes(std::span<const std::byte> bytes) {
        flush();
        hash.update(bytes);
    }
    std::string finish() {
        flush();
        return hash.finish();
    }
};
void append_chunk(scene_hash_writer& hash, chunk_id id, const scene_chunk& chunk) {
    const auto add = [&](std::uint64_t value) { hash.add(value); };
    add(id.value);
    add(chunk.generation);
    add(chunk.pose_revision.value);
    add(chunk.geometry_valid);
    const auto& t = chunk.world_from_chunk;
    for (double value : {t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z, t.translation.x,
                         t.translation.y, t.translation.z, t.scale, chunk.bounds.minimum.x,
                         chunk.bounds.minimum.y, chunk.bounds.minimum.z, chunk.bounds.maximum.x,
                         chunk.bounds.maximum.y, chunk.bounds.maximum.z})
        add(std::bit_cast<std::uint64_t>(value));
    add(chunk.source_frames.size());
    for (const auto frame : chunk.source_frames)
        add(frame.value);
    add(chunk.splats.size());
    for (const auto& splat : chunk.splats) {
        for (float value : splat.mean)
            add(std::bit_cast<std::uint32_t>(value));
        for (float value : splat.scale)
            add(std::bit_cast<std::uint32_t>(value));
        for (float value : splat.rotation)
            add(std::bit_cast<std::uint32_t>(value));
        add(std::bit_cast<std::uint32_t>(splat.opacity));
        add(splat.sh_degree);
        for (float value : splat.sh)
            add(std::bit_cast<std::uint32_t>(value));
    }
}
} // namespace
std::string scene_chunk_content_hash(const scene_chunk& chunk) {
    scene_hash_writer hash;
    append_chunk(hash, chunk.id, chunk);
    return hash.finish();
}
std::string scene_content_hash(const scene_state& scene) {
    scene_hash_writer hash;
    const auto add = [&](std::uint64_t value) { hash.add(value); };
    add(scene.id.value);
    add(scene.epoch.value);
    add(scene.revision.value);
    add(static_cast<std::uint64_t>(scene.scale));
    add(scene.provenance.size());
    for (const auto& [key, value] : scene.provenance) {
        add(key.size());
        hash.add_bytes(std::as_bytes(std::span(key.data(), key.size())));
        add(value.size());
        hash.add_bytes(std::as_bytes(std::span(value.data(), value.size())));
    }
    add(scene.chunks.size());
    for (const auto& [id, chunk] : scene.chunks) {
        append_chunk(hash, id, *chunk);
    }
    return hash.finish();
}
result<std::size_t> validate_chunk(const scene_chunk& chunk, scene_limits limits) {
    if (chunk.id.value == 0 || chunk.generation == 0 || !valid(chunk.bounds) ||
        !valid(chunk.world_from_chunk))
        return fail(error_code::INVALID_DATA, "invalid chunk identity, bounds or transform");
    if (chunk.splats.size() > limits.max_splats ||
        chunk.splats.size() > limits.max_payload_bytes / sizeof(gaussian) ||
        chunk.source_frames.size() > limits.max_source_frames)
        return fail(error_code::RESOURCE_EXHAUSTED, "chunk exceeds configured limits");
    if (chunk.geometry_valid && chunk.pose_revision.value == 0)
        return fail(error_code::INVALID_DATA, "geometry has no source pose revision");
    for (const auto& splat : chunk.splats) {
        if (splat.sh_degree > 3 || !std::isfinite(splat.opacity) || splat.opacity < 0 ||
            splat.opacity > 1)
            return fail(error_code::INVALID_DATA, "invalid Gaussian opacity or SH degree");
        if (!valid(
                quatd{splat.rotation[0], splat.rotation[1], splat.rotation[2], splat.rotation[3]}))
            return fail(error_code::INVALID_DATA, "Gaussian quaternion is not normalized");
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(splat.mean[axis]) || !std::isfinite(splat.scale[axis]) ||
                splat.scale[axis] <= 0)
                return fail(error_code::INVALID_DATA,
                            "invalid Gaussian position or covariance scale");
        const vec3d mean{splat.mean[0], splat.mean[1], splat.mean[2]};
        if (mean.x < chunk.bounds.minimum.x || mean.y < chunk.bounds.minimum.y ||
            mean.z < chunk.bounds.minimum.z || mean.x > chunk.bounds.maximum.x ||
            mean.y > chunk.bounds.maximum.y || mean.z > chunk.bounds.maximum.z)
            return fail(error_code::INVALID_DATA, "Gaussian outside chunk bounds");
        for (float coefficient : splat.sh)
            if (!std::isfinite(coefficient))
                return fail(error_code::INVALID_DATA, "nonfinite SH coefficient");
    }
    return chunk.splats.size() * sizeof(gaussian);
}
result<std::size_t> validate_scene(const scene_state& scene, scene_limits limits) {
    if (scene.id.value == 0)
        return fail(error_code::INVALID_DATA, "scene ID must be nonzero");
    if (scene.scale != scale_status::ARBITRARY && scene.scale != scale_status::METRIC_MEASURED)
        return fail(error_code::INVALID_DATA, "unsupported scene scale status");
    if (scene.provenance.size() > 32)
        return fail(error_code::RESOURCE_EXHAUSTED, "Too many provenance fields");
    for (const auto& [key, value] : scene.provenance)
        if (key.empty() || key.size() > 128 || value.size() > 4096)
            return fail(error_code::RESOURCE_EXHAUSTED, "Provenance field exceeds limit");
    if (scene.chunks.size() > limits.max_chunks)
        return fail(error_code::RESOURCE_EXHAUSTED, "scene chunk budget exceeded");
    std::size_t bytes{}, splats{};
    for (const auto& [id, chunk] : scene.chunks) {
        if (!chunk || id != chunk->id)
            return fail(error_code::INVALID_DATA, "chunk key differs from identity");
        const auto size = validate_chunk(*chunk, limits);
        if (!size)
            return std::unexpected(size.error());
        if (*size > limits.max_payload_bytes - bytes ||
            chunk->splats.size() > limits.max_splats - splats)
            return fail(error_code::RESOURCE_EXHAUSTED, "aggregate scene budget exceeded");
        bytes += *size;
        splats += chunk->splats.size();
    }
    return bytes;
}
result<scene_delta_batch> make_snapshot_batch(scene_snapshot snapshot, operation_id operation) {
    if (!snapshot || operation.value == 0)
        return fail(error_code::INVALID_ARGUMENT, "missing snapshot or operation ID");
    const auto bytes = validate_scene(*snapshot);
    if (!bytes)
        return std::unexpected(bytes.error());
    return scene_delta_batch{snapshot->id, snapshot->epoch,     {},   snapshot->revision,
                             operation,    std::move(snapshot), true, *bytes};
}
} // namespace world_explorer
