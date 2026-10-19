#ifndef WORLD_EXPLORER_SCENE_STORE_FUSION_COMPONENTS_HPP
#define WORLD_EXPLORER_SCENE_STORE_FUSION_COMPONENTS_HPP

#include "world_explorer/scene_store/fusion_asset.hpp"

namespace world_explorer {

struct FusionComponentSupport {
    // Referenced vertices, grouped by number of observed corners in their stencil.
    std::array<std::uint64_t, 9> m_corner_coverage {};
    std::uint64_t m_observations {}, m_support_sum {}, m_front_count {}, m_sigma_corners {};
    double m_weight_sum {}, m_weight_min {}, m_weight_max {}, m_sigma2_sum {}, m_sigma_weight {};
};

struct FusionMeshComponent {
    std::size_t m_first_triangle {}, m_vertices {}, m_triangles {}, m_zero_area_triangles {};
    std::size_t m_triangles_with_unknown_corners {}, m_triangles_with_unbacked_vertices {};
    double m_area {}, m_area_with_unknown_corners {}, m_area_with_unbacked_vertices {};
    FusionComponentSupport m_support;
};

struct FusionComponentAnalysis {
    std::size_t m_unused_vertices {};
    FusionMeshComponent m_total;
    std::vector<FusionMeshComponent> m_components;
};

struct FusionComponentLimits {
    FusionAssetLimits m_asset;
    std::size_t m_max_components { 65536uz };
    ByteBudget m_working_bytes { 128uz * 1024uz * 1024uz };
};

// Read-only analysis of admitted fusion. Components share vertex indices; no
// welding, filling, removal, scale inference or geometry-quality classification.
// Support is a diagnostic eight-corner floor-cell stencil, not interpolated
// confidence or a list of independent observations. Unknown corners stay unknown.
Result<FusionComponentAnalysis> analyze_fusion_components(const ObservedFusionAsset& asset,
                                                          FusionComponentLimits limits = {},
                                                          const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
