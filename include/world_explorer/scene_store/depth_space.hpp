#pragma once

#include "world_explorer/scene_store/depth_asset.hpp"
#include "world_explorer/scene_store/scene.hpp"

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

struct DepthSpaceGrid {
    Vec3d m_origin;
    double m_size {};
    std::array<std::size_t, 3uz> m_dimensions {};
    std::vector<Bounds3d> m_cells;
};

Result<DepthSpaceGrid> make_depth_space_grid(const SceneState& source,
                                             const std::stop_token& stop = {});
Result<SceneSnapshot> make_depth_space_diagnostic(const SceneSnapshot& source,
                                                  std::span<const DepthCellEvidence> cells,
                                                  const std::stop_token& stop = {});

struct DepthSpaceLoad {
    SceneSnapshot m_source;
    std::filesystem::path m_directory;
    std::string m_report_sha256, m_request_sha256;
};

constexpr std::size_t K_DEPTH_SPACE_REPORT_BYTES { 1024uz * 1024uz };
constexpr std::size_t K_DEPTH_SPACE_LEDGER_BYTES { 120uz * 32uz * 32uz * 32uz };

// Immutable consistency admission under an independently supplied terminal pin.
// Does not recompute source depth or grant collision/navigation eligibility.
class DepthSpaceEvidence {
    struct AdmissionToken {};

  public:
    explicit DepthSpaceEvidence(AdmissionToken) {}

    DepthSpaceEvidence(const DepthSpaceEvidence&) = delete;
    DepthSpaceEvidence& operator=(const DepthSpaceEvidence&) = delete;

    const DepthSpaceGrid& grid() const noexcept {
        return m_grid;
    }

    std::span<const DepthCellEvidence> cells() const noexcept {
        return m_cells;
    }

    std::span<const DepthCellRelation> relations() const noexcept {
        return m_relations;
    }

    std::span<const FrameId> frames() const noexcept {
        return m_frames;
    }

    std::span<const std::uint8_t> uncertainty_available() const noexcept {
        return m_uncertainty;
    }

    std::array<std::size_t, 4uz> categories() const noexcept {
        return m_categories;
    }

    const std::string& report_sha256() const noexcept {
        return m_report_sha256;
    }

    SceneSnapshot diagnostic() const noexcept {
        return m_diagnostic;
    }

    Result<void> validate_source(const SceneState& current, const std::stop_token& stop = {}) const;

  private:
    friend Result<std::shared_ptr<const DepthSpaceEvidence>>
    load_depth_space_evidence(const DepthSpaceLoad&, const std::stop_token&);

    SceneSnapshot m_source, m_diagnostic;
    DepthSpaceGrid m_grid;
    std::vector<DepthCellEvidence> m_cells;
    std::vector<DepthCellRelation> m_relations;
    std::vector<FrameId> m_frames;
    std::vector<std::uint8_t> m_uncertainty;
    std::array<std::size_t, 4uz> m_categories {};
    std::string m_report_sha256, m_source_sha256;
};

Result<std::shared_ptr<const DepthSpaceEvidence>>
load_depth_space_evidence(const DepthSpaceLoad& request, const std::stop_token& stop = {});

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
