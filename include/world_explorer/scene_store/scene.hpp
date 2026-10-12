#ifndef WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
#define WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
#include <cstdint>

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"
#include <map>
#include <memory>
#include <vector>

namespace world_explorer {
// Canonical SH: real basis, coefficients ordered as gsplat/3DGS, coefficient-major
// RGB triplets. Degree 0 color = max(0.5 + 0.2820947918 * sh[0..2], 0).
// Values above one retain energy until composition; clamp only for SDR presentation.
struct gaussian {
    std::array<float, 3> mean{};
    std::array<float, 3> scale{1, 1, 1};
    std::array<float, 4> rotation{1, 0, 0, 0};
    float opacity{1};
    std::array<float, 48> sh{};
    std::uint8_t sh_degree{};
};
struct scene_chunk {
    chunk_id id;
    std::uint64_t generation{1};
    std::vector<gaussian> splats;
    bounds3d bounds;
    similarity_transform world_from_chunk;
    scene_revision_id pose_revision;
    bool geometry_valid{};
    std::vector<frame_id> source_frames;
};
struct scene_state {
    scene_id id;
    world_epoch epoch;
    scene_revision_id revision;
    scale_status scale{scale_status::ARBITRARY};
    std::map<std::string, std::string> provenance;
    std::map<chunk_id, std::shared_ptr<const scene_chunk>> chunks;
};
using scene_snapshot = std::shared_ptr<const scene_state>;
enum class scene_operation_kind : std::uint8_t {
    CREATE,
    REPLACE,
    ERASE,
    INVALIDATE_GEOMETRY,
    TRANSFORM
};
struct scene_operation {
    scene_operation_kind kind{scene_operation_kind::CREATE};
    chunk_id id;
    std::shared_ptr<const scene_chunk> chunk;
    similarity_transform world_from_chunk;
    scene_revision_id pose_revision;
};
struct scene_transaction {
    scene_id id;
    world_epoch epoch;
    scene_revision_id base_revision, new_revision;
    operation_id operation;
    std::vector<scene_operation> changes;
};
// A full resync is explicitly marked. Otherwise base_revision must match the
// currently accepted revision. The snapshot owns all immutable upload inputs.
struct scene_delta_batch {
    scene_id id;
    world_epoch epoch;
    scene_revision_id base_revision, new_revision;
    operation_id operation;
    scene_snapshot snapshot;
    bool full_snapshot{};
    std::size_t upload_bytes{};
};
struct scene_limits {
    std::size_t max_chunks{4096};
    std::size_t max_splats{10000000};
    std::size_t max_payload_bytes{3ULL * 1024 * 1024 * 1024};
    std::size_t max_source_frames{1000000};
};
result<std::size_t> validate_scene(const scene_state& scene, scene_limits limits = {});
result<std::size_t> validate_chunk(const scene_chunk& chunk, scene_limits limits = {});
std::string scene_content_hash(const scene_state& scene);
result<scene_delta_batch> make_snapshot_batch(scene_snapshot snapshot, operation_id operation);
} // namespace world_explorer

#endif // WORLD_EXPLORER_SCENE_STORE_SCENE_HPP
