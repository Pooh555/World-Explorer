#pragma once

#include "world_explorer/scene_store/depth_asset.hpp"

namespace world_explorer {

// Relations to observed axial depth, never physical occupancy or walking clearance.
enum class DepthCellRelation : std::uint8_t {
    OUTSIDE_VIEW,
    UNKNOWN_DEPTH,
    FRONT_DIAGNOSTIC,
    SURFACE_BAND,
    BEHIND_SURFACE
};

struct DepthSpaceRecipe {
    SimilarityTransform m_world_from_camera;
    double m_world_guard {};
};

enum class DepthEvidenceState : std::uint8_t { UNKNOWN, FRONT_ONLY, SURFACE_OR_BEHIND, CONFLICT };

struct DepthCellEvidence {
    // DepthCellRelation order. Unknown/outside votes remain recorded even if a
    // different view contributes a diagnostic relation. Never a clearance mask.
    std::array<std::size_t, 5uz> m_counts {};
    DepthEvidenceState state() const noexcept;
};

// Complete projected boxes, including all eight corners and every pixel in a
// conservative enclosing mip footprint. Unknown pixels cannot support a cell.
// Missing sigma remains unavailable; the guard is a diagnostic recipe parameter.
class DepthSpaceView {
  public:
    static Result<DepthSpaceView> create(const DepthImageAsset& depth,
                                         const DepthSpaceRecipe& recipe,
                                         const std::stop_token& stop = {});
    Result<DepthCellRelation> classify(const Bounds3d& cell) const;

    bool uncertainty_available() const noexcept {
        return m_uncertainty_available;
    }

  private:
    struct Range {
        double m_lower {}, m_upper {};
        bool m_supported {};
    };

    struct Level {
        std::size_t m_width {}, m_height {};
        std::vector<Range> m_ranges;
    };

    DepthSpaceView() = default;
    PinholeCamera m_camera;
    DepthSpaceRecipe m_recipe;
    bool m_uncertainty_available {};
    std::vector<Level> m_levels;
};

}  // namespace world_explorer
