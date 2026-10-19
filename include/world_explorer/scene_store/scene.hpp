#ifndef WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
#define WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"
#include "world_explorer/scene_store/fusion_asset.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace world_explorer {

// Canonical SH: real basis, coefficients ordered as gsplat/3DGS, coefficient-major
// RGB triplets. Degree 0 color = max(0.5 + 0.2820947918 * sh[0..2], 0).
// Values above one retain energy until composition; clamp only for SDR presentation.
struct Gaussian {
    std::array<float, 3> m_mean {};
    std::array<float, 3> m_scale { 1, 1, 1 };
    std::array<float, 4> m_rotation { 1, 0, 0, 0 };
    float m_opacity { 1 };
    std::array<float, 48> m_sh {};
    std::uint8_t m_sh_degree {};
};

struct ObservedMeshSource {
    FusionContext m_context;
    std::string m_asset_sha256, m_manifest_json, m_receipt_json;
    // Empty for raw geometry; otherwise pins the verified derived certificate.
    std::string m_cleanup_sha256 {}, m_cleanup_json {};
};

// Raw observed surfaces or explicitly labeled synthetic diagnostics. No material,
// complete-space or collision eligibility is inferred from drawable triangles.
struct SceneMesh {
    std::vector<FusionVertex> m_vertices;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    std::optional<ObservedMeshSource> m_observed;
};

struct SceneChunk {
    ChunkId m_id;
    std::uint64_t m_generation { 1 };
    std::vector<Gaussian> m_splats;
    std::optional<SceneMesh> m_mesh;
    Bounds3d m_bounds;
    SimilarityTransform m_world_from_chunk;
    SceneRevisionId m_pose_revision;
    bool m_geometry_valid {};
    std::vector<FrameId> m_source_frames;
};

struct SceneState {
    SceneId m_id;
    WorldEpoch m_epoch;
    SceneRevisionId m_revision;
    ScaleStatus m_scale { ScaleStatus::ARBITRARY };
    std::map<std::string, std::string> m_provenance;
    std::map<ChunkId, std::shared_ptr<const SceneChunk>> m_chunks;
};

using SceneSnapshot = std::shared_ptr<const SceneState>;
enum class SceneOperationKind : std::uint8_t {
    CREATE,
    REPLACE,
    ERASE,
    INVALIDATE_GEOMETRY,
    TRANSFORM
};

struct SceneOperation {
    SceneOperationKind m_kind { SceneOperationKind::CREATE };
    ChunkId m_id;
    std::shared_ptr<const SceneChunk> m_chunk;
    SimilarityTransform m_world_from_chunk;
    SceneRevisionId m_pose_revision;
};

struct SceneTransaction {
    SceneId m_id;
    WorldEpoch m_epoch;
    SceneRevisionId m_base_revision, m_new_revision;
    OperationId m_operation;
    std::vector<SceneOperation> m_changes;
};

// A full resync is explicitly marked. Otherwise base_revision must match the
// currently accepted revision. The snapshot owns all immutable upload inputs.
struct SceneDeltaBatch {
    SceneId m_id;
    WorldEpoch m_epoch;
    SceneRevisionId m_base_revision, m_new_revision;
    OperationId m_operation;
    SceneSnapshot m_snapshot;
    bool m_full_snapshot {};
    std::size_t m_upload_bytes {};
};

struct SceneLimits {
    std::size_t m_max_chunks { 4096uz };
    std::size_t m_max_splats { 10000000uz };
    std::size_t m_max_payload_bytes { 3uz * 1024uz * 1024uz * 1024uz };
    std::size_t m_max_source_frames { 1000000uz };
    std::size_t m_max_mesh_vertices { 4000000uz };
    std::size_t m_max_mesh_triangles { 8000000uz };
};

Result<std::size_t> validate_scene(const SceneState& scene, SceneLimits limits = {});
Result<std::size_t> validate_chunk(const SceneChunk& chunk, SceneLimits limits = {});
std::string scene_content_hash(const SceneState& scene);
std::string scene_chunk_content_hash(const SceneChunk& chunk);
Result<SceneDeltaBatch> make_snapshot_batch(SceneSnapshot snapshot, OperationId operation);

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
