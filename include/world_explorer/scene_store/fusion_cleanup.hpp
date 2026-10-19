#ifndef WORLD_EXPLORER_SCENE_STORE_FUSION_CLEANUP_HPP
#define WORLD_EXPLORER_SCENE_STORE_FUSION_CLEANUP_HPP

#include "world_explorer/scene_store/fusion_asset.hpp"

namespace world_explorer {

struct FusionCleanupPatch {
    std::uint32_t m_vertex {};
    // UINT32_MAX selects the three-face fan; otherwise collapse to this endpoint.
    std::uint32_t m_target { UINT32_MAX };
    std::vector<std::uint32_t> m_triangles, m_ring;
    std::array<double, 2> m_barycentric_uv {};
    double m_distance_upper_bound {};
};

struct FusionCleanup {
    std::vector<FusionVertex> m_vertices;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    std::vector<std::uint32_t> m_output_to_source_vertices;
    std::vector<FusionCleanupPatch> m_patches;
    // Per source vertex: ineligible degree, locked, topology, support, shape,
    // distance bound, accepted. This ledger belongs to generation, not replay.
    std::array<std::size_t, 7> m_decisions {};
    double m_distance_upper_bound {};
    std::string m_geometry_sha256, m_document;
};

// One frozen, bounded local policy. No component deletion, boundary movement,
// position welding, smoothing, hole filling or collision/quality promotion.
Result<FusionCleanup> simplify_fusion_mesh(const ObservedFusionAsset& asset,
                                           const std::stop_token& stop = {});
// Rechecks the independently pinned certificate, original support/topology and
// outward-rounded whole-surface bound before deriving the owned mesh arrays.
Result<FusionCleanup> decode_fusion_cleanup(std::span<const std::byte> bytes,
                                            const std::string& expected_sha256,
                                            const ObservedFusionAsset& asset,
                                            const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
