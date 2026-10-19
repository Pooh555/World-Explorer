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

void append_chunk(SceneHashWriter& hash,
                  ChunkId id,
                  const SceneChunk& chunk,
                  bool include_baked = true) {
    const auto K_ADD = [&](std::uint64_t value) { hash.add(value); };
    // Zero cannot be an admitted chunk ID. Reserve it as an unambiguous mesh
    // stream/version prefix while retaining every legacy splat-only digest.
    if (chunk.m_mesh) {
        K_ADD(0);
        K_ADD(include_baked && chunk.m_mesh->m_baked                                          ? 3
              : chunk.m_mesh->m_observed && !chunk.m_mesh->m_observed->m_cleanup_json.empty() ? 2
                                                                                              : 1);
    }
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
    // Preserve existing splat-only hashes; mesh presence extends the stream.
    if (chunk.m_mesh) {
        K_ADD(UINT64_MAX);
        const auto& mesh = *chunk.m_mesh;
        K_ADD(mesh.m_observed.has_value());
        if (mesh.m_observed) {
            const auto& source = *mesh.m_observed;
            K_ADD(source.m_context.m_epoch.m_value);
            K_ADD(source.m_context.m_pose_revision.m_value);
            for (const auto& text : {source.m_context.m_manifest_sha256,
                                     source.m_context.m_receipt_sha256,
                                     source.m_context.m_recipe_sha256,
                                     source.m_context.m_pose_sha256,
                                     source.m_asset_sha256,
                                     source.m_manifest_json,
                                     source.m_receipt_json}) {
                K_ADD(text.size());
                hash.add_bytes(std::as_bytes(std::span(text)));
            }
            if (!source.m_cleanup_json.empty()) {
                for (const auto& text : { source.m_cleanup_sha256, source.m_cleanup_json }) {
                    K_ADD(text.size());
                    hash.add_bytes(std::as_bytes(std::span(text)));
                }
            }
        }
        K_ADD(mesh.m_vertices.size());
        K_ADD(mesh.m_triangles.size());
        for (const auto& vertex : mesh.m_vertices) {
            for (auto value : vertex.m_position) K_ADD(std::bit_cast<std::uint32_t>(value));
            for (auto value : vertex.m_normal) K_ADD(std::bit_cast<std::uint32_t>(value));
        }
        for (const auto& triangle : mesh.m_triangles)
            for (auto index : triangle) K_ADD(index);
        if (include_baked && mesh.m_baked) {
            const auto& baked = *mesh.m_baked;
            K_ADD(baked.m_width);
            K_ADD(baked.m_height);
            for (const auto& text :
                 { baked.m_source_chunk_sha256, baked.m_manifest_sha256, baked.m_manifest_json }) {
                K_ADD(text.size());
                hash.add_bytes(std::as_bytes(std::span(text)));
            }
            K_ADD(baked.m_frames.size());
            for (auto frame : baked.m_frames) K_ADD(frame.m_value);
            for (const auto& color : baked.m_rgba)
                for (auto value : color) K_ADD(std::bit_cast<std::uint32_t>(value));
            for (auto owner : baked.m_owners) K_ADD(owner.m_value);
            for (auto mask : baked.m_candidates) K_ADD(mask);
            for (const auto& uv : baked.m_corner_uv)
                for (auto value : uv) K_ADD(std::bit_cast<std::uint32_t>(value));
        }
    }
}

bool digest_valid(const std::string& text) {
    return text.size() == 64uz && text.find_first_not_of("0123456789abcdef") == text.npos;
}

Result<std::size_t> mesh_bytes(const SceneChunk& chunk, const SceneLimits& limits) {
    if (!chunk.m_mesh) return 0uz;
    const auto& mesh = *chunk.m_mesh;
    if (mesh.m_vertices.size() > limits.m_max_mesh_vertices ||
        mesh.m_triangles.size() > limits.m_max_mesh_triangles ||
        mesh.m_vertices.size() > limits.m_max_payload_bytes / sizeof(FusionVertex))
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mesh exceeds configured count limits");
    auto bytes = mesh.m_vertices.size() * sizeof(FusionVertex);
    if (mesh.m_triangles.size() > (limits.m_max_payload_bytes - bytes) / 12uz)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mesh index budget exceeded");
    bytes += mesh.m_triangles.size() * 12uz;
    if (mesh.m_observed) {
        const auto& source = *mesh.m_observed;
        const auto& context = source.m_context;
        for (const auto& pin : {context.m_manifest_sha256,
                                context.m_receipt_sha256,
                                context.m_recipe_sha256,
                                context.m_pose_sha256,
                                source.m_asset_sha256})
            if (!digest_valid(pin))
                return fail(ErrorCode::INVALID_DATA, "Invalid mesh dependency pin");
        if (!context.m_pose_revision.m_value ||
            (chunk.m_geometry_valid &&
             context.m_pose_revision.m_value != chunk.m_pose_revision.m_value))
            return fail(ErrorCode::RESYNC_REQUIRED, "Mesh pose revision differs");
        for (const auto& text : { source.m_manifest_json, source.m_receipt_json }) {
            if (text.empty() || text.size() > 4uz * 1024uz * 1024uz ||
                text.size() > limits.m_max_payload_bytes - bytes)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mesh metadata budget exceeded");
            bytes += text.size();
        }
        if (sha256(std::as_bytes(std::span(source.m_manifest_json))) != context.m_manifest_sha256 ||
            sha256(std::as_bytes(std::span(source.m_receipt_json))) != context.m_receipt_sha256)
            return fail(ErrorCode::INVALID_DATA, "Mesh coverage metadata checksum differs");
        if (!source.m_cleanup_json.empty() || !source.m_cleanup_sha256.empty()) {
            if (source.m_cleanup_json.size() > 4uz * 1024uz * 1024uz ||
                source.m_cleanup_json.size() > limits.m_max_payload_bytes - bytes)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mesh cleanup metadata budget exceeded");
            if (source.m_cleanup_json.empty() || !digest_valid(source.m_cleanup_sha256) ||
                sha256(std::as_bytes(std::span(source.m_cleanup_json))) != source.m_cleanup_sha256)
                return fail(ErrorCode::INVALID_DATA, "Mesh cleanup metadata checksum differs");
            bytes += source.m_cleanup_json.size();
        }
    }
    for (const auto& vertex : mesh.m_vertices) {
        const Vec3d K_POSITION { vertex.m_position[0], vertex.m_position[1], vertex.m_position[2] };
        const Vec3d K_NORMAL { vertex.m_normal[0], vertex.m_normal[1], vertex.m_normal[2] };
        if (!finite(K_POSITION) || !finite(K_NORMAL) ||
            K_POSITION.m_x < chunk.m_bounds.m_minimum.m_x ||
            K_POSITION.m_x > chunk.m_bounds.m_maximum.m_x ||
            K_POSITION.m_y < chunk.m_bounds.m_minimum.m_y ||
            K_POSITION.m_y > chunk.m_bounds.m_maximum.m_y ||
            K_POSITION.m_z < chunk.m_bounds.m_minimum.m_z ||
            K_POSITION.m_z > chunk.m_bounds.m_maximum.m_z ||
            (dot(K_NORMAL, K_NORMAL) != 0 && std::abs(dot(K_NORMAL, K_NORMAL) - 1) > .001))
            return fail(ErrorCode::INVALID_DATA, "Invalid mesh position or normal");
    }
    for (const auto& triangle : mesh.m_triangles)
        if (triangle[0] >= mesh.m_vertices.size() || triangle[1] >= mesh.m_vertices.size() ||
            triangle[2] >= mesh.m_vertices.size() || triangle[0] == triangle[1] ||
            triangle[0] == triangle[2] || triangle[1] == triangle[2])
            return fail(ErrorCode::INVALID_DATA, "Invalid mesh triangle");
    const auto K_BAKED = validate_baked_appearance(chunk, limits);
    if (!K_BAKED) return std::unexpected(K_BAKED.error());
    if (*K_BAKED > limits.m_max_payload_bytes - bytes)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Combined mesh texture budget exceeded");
    return bytes + *K_BAKED;
}

}  // namespace

std::string scene_chunk_content_hash(const SceneChunk& chunk) {
    SceneHashWriter hash;
    append_chunk(hash, chunk.m_id, chunk);
    return hash.finish();
}

std::string scene_untextured_chunk_hash(const SceneChunk& chunk) {
    SceneHashWriter hash;
    append_chunk(hash, chunk.m_id, chunk, false);
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
    const auto K_MESH_BYTES = mesh_bytes(chunk, limits);
    if (!K_MESH_BYTES) return std::unexpected(K_MESH_BYTES.error());
    const auto K_SPLAT_BYTES = chunk.m_splats.size() * sizeof(Gaussian);
    if (*K_MESH_BYTES > limits.m_max_payload_bytes - K_SPLAT_BYTES)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Combined chunk payload budget exceeded");
    return K_SPLAT_BYTES + *K_MESH_BYTES;
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
    std::size_t bytes {}, splats {}, vertices {}, triangles {}, texels {};
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        if (!K_CHUNK || K_ID != K_CHUNK->m_id)
            return fail(ErrorCode::INVALID_DATA, "chunk key differs from identity");
        const auto K_SIZE = validate_chunk(*K_CHUNK, limits);
        if (!K_SIZE) return std::unexpected(K_SIZE.error());
        if (*K_SIZE > limits.m_max_payload_bytes - bytes ||
            K_CHUNK->m_splats.size() > limits.m_max_splats - splats)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "aggregate scene budget exceeded");
        if (K_CHUNK->m_mesh) {
            const auto& mesh = *K_CHUNK->m_mesh;
            if (mesh.m_vertices.size() > limits.m_max_mesh_vertices - vertices ||
                mesh.m_triangles.size() > limits.m_max_mesh_triangles - triangles)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Aggregate mesh count exceeded");
            if (mesh.m_baked) {
                if (mesh.m_baked->m_rgba.size() > limits.m_max_texture_texels - texels)
                    return fail(ErrorCode::RESOURCE_EXHAUSTED, "Aggregate texture count exceeded");
                texels += mesh.m_baked->m_rgba.size();
            }
            vertices += mesh.m_vertices.size();
            triangles += mesh.m_triangles.size();
            if (mesh.m_observed) {
                if (scene.m_scale != ScaleStatus::ARBITRARY ||
                    (K_CHUNK->m_geometry_valid &&
                     mesh.m_observed->m_context.m_epoch != scene.m_epoch))
                    return fail(ErrorCode::WRONG_EPOCH, "Observed mesh world context differs");
            } else {
                const auto K_KIND = scene.m_provenance.find("source_kind");
                if (K_KIND == scene.m_provenance.end() || K_KIND->second != "synthetic_diagnostic")
                    return fail(ErrorCode::INVALID_DATA,
                                "Synthetic mesh requires explicit provenance");
            }
        }
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
