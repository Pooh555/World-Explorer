#include "world_explorer/scene_store/depth_space.hpp"

#include <algorithm>
#include <limits>

namespace world_explorer {

DepthEvidenceState DepthCellEvidence::state() const noexcept {
    const bool K_FRONT { m_counts[2uz] != 0uz };
    const bool K_BLOCKED { m_counts[3uz] != 0uz || m_counts[4uz] != 0uz };
    return K_FRONT
               ? (K_BLOCKED ? DepthEvidenceState::CONFLICT : DepthEvidenceState::FRONT_ONLY)
               : (K_BLOCKED ? DepthEvidenceState::SURFACE_OR_BEHIND : DepthEvidenceState::UNKNOWN);
}

Result<DepthSpaceView> DepthSpaceView::create(const DepthImageAsset& depth,
                                              const DepthSpaceRecipe& recipe,
                                              const std::stop_token& stop) {
    const auto K_VALID = validate_depth_asset(depth, {}, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (!valid(recipe.m_world_from_camera) || !std::isfinite(recipe.m_world_guard) ||
        recipe.m_world_guard <= 0 ||
        !std::isfinite(recipe.m_world_guard / recipe.m_world_from_camera.m_scale) ||
        depth.m_metadata.m_convention != DepthConvention::AXIAL_CAMERA_Z) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid depth-space recipe or depth convention");
    }
    constexpr std::size_t K_MAX_PIXELS { 4uz * 1024uz * 1024uz };
    if (depth.m_valid.size() > K_MAX_PIXELS) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space pyramid exceeds pixel budget");
    }
    try {
        DepthSpaceView result;
        result.m_recipe = recipe;
        result.m_camera = depth.m_metadata.m_camera;
        result.m_uncertainty_available = depth.m_standard_deviation.has_value();
        Level base { depth.m_metadata.m_width, depth.m_metadata.m_height, {} };
        base.m_ranges.reserve(depth.m_valid.size());
        for (std::size_t index { 0uz }; index < depth.m_valid.size(); ++index) {
            if (index % 4096uz == 0uz && stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Depth-space pyramid cancelled");
            }
            const auto K_SIGMA =
                depth.m_standard_deviation ? depth.m_standard_deviation->at(index) : 0.;
            base.m_ranges.push_back({ depth.m_axial_depth[index] - 2. * K_SIGMA,
                                     depth.m_axial_depth[index] + 2. * K_SIGMA,
                                     depth.m_valid[index] != 0 && depth.m_confidence[index] > 0 &&
                                         depth.m_support_count[index] >= 2 });
        }
        result.m_levels.push_back(std::move(base));
        while (result.m_levels.back().m_width > 1uz || result.m_levels.back().m_height > 1uz) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Depth-space pyramid cancelled");
            }
            const auto& prior = result.m_levels.back();
            Level next { (prior.m_width + 1uz) / 2uz, (prior.m_height + 1uz) / 2uz, {} };
            next.m_ranges.reserve(next.m_width * next.m_height);
            for (std::size_t y { 0uz }; y < next.m_height; ++y) {
                for (std::size_t x { 0uz }; x < next.m_width; ++x) {
                    Range combined { std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity(),
                                    true };
                    for (std::size_t dy { 0uz }; dy < 2uz; ++dy) {
                        for (std::size_t dx { 0uz }; dx < 2uz; ++dx) {
                            const auto K_X = 2uz * x + dx;
                            const auto K_Y = 2uz * y + dy;
                            if (K_X < prior.m_width && K_Y < prior.m_height) {
                                const auto& sample = prior.m_ranges[K_X + prior.m_width * K_Y];
                                combined.m_lower = std::min(combined.m_lower, sample.m_lower);
                                combined.m_upper = std::max(combined.m_upper, sample.m_upper);
                                combined.m_supported = combined.m_supported && sample.m_supported;
                            }
                        }
                    }
                    next.m_ranges.push_back(combined);
                }
            }
            result.m_levels.push_back(std::move(next));
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space pyramid allocation failed");
    }
}

Result<DepthCellRelation> DepthSpaceView::classify(const Bounds3d& cell) const {
    if (!valid(cell)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid depth-space cell bounds");
    }
    double x_min { std::numeric_limits<double>::infinity() }, y_min { x_min }, z_min { x_min };
    double x_max { -x_min }, y_max { -x_min }, z_max { -x_min };
    for (std::size_t corner { 0uz }; corner < 8uz; ++corner) {
        const Vec3d K_WORLD { (corner & 1uz) ? cell.m_maximum.m_x : cell.m_minimum.m_x,
                             (corner & 2uz) ? cell.m_maximum.m_y : cell.m_minimum.m_y,
                             (corner & 4uz) ? cell.m_maximum.m_z : cell.m_minimum.m_z };
        const auto K_CAMERA = inverse_transform_point(m_recipe.m_world_from_camera, K_WORLD);
        const auto K_PIXEL = project(m_camera, K_CAMERA);
        if (!K_PIXEL || !std::isfinite((*K_PIXEL)[0uz]) || !std::isfinite((*K_PIXEL)[1uz])) {
            return DepthCellRelation::OUTSIDE_VIEW;
        }
        x_min = std::min(x_min, (*K_PIXEL)[0uz]);
        y_min = std::min(y_min, (*K_PIXEL)[1uz]);
        x_max = std::max(x_max, (*K_PIXEL)[0uz]);
        y_max = std::max(y_max, (*K_PIXEL)[1uz]);
        z_min = std::min(z_min, K_CAMERA.m_z);
        z_max = std::max(z_max, K_CAMERA.m_z);
    }
    // Outward expansion also includes both neighbours at integer centers.
    constexpr double K_PIXEL_GUARD { 1e-7 };
    const double K_X0 { std::floor(x_min - K_PIXEL_GUARD) }, K_X1 { std::ceil(x_max + K_PIXEL_GUARD) };
    const double K_Y0 { std::floor(y_min - K_PIXEL_GUARD) }, K_Y1 { std::ceil(y_max + K_PIXEL_GUARD) };
    const auto& base = m_levels.front();
    if (K_X0 < 0 || K_Y0 < 0 || K_X1 >= static_cast<double>(base.m_width) ||
        K_Y1 >= static_cast<double>(base.m_height)) {
        return DepthCellRelation::OUTSIDE_VIEW;
    }
    const auto K_LEFT = static_cast<std::size_t>(K_X0), K_RIGHT = static_cast<std::size_t>(K_X1);
    const auto K_TOP = static_cast<std::size_t>(K_Y0), K_BOTTOM = static_cast<std::size_t>(K_Y1);
    const auto K_EXTENT = std::max(K_RIGHT - K_LEFT + 1uz, K_BOTTOM - K_TOP + 1uz);
    std::size_t level { 0uz }, side { 1uz };
    while (side < K_EXTENT) {
        side *= 2uz;
        ++level;
    }
    const auto& mip = m_levels.at(level);
    double lower { std::numeric_limits<double>::infinity() }, upper { -lower };
    for (auto y = K_TOP / side; y <= K_BOTTOM / side; ++y) {
        for (auto x = K_LEFT / side; x <= K_RIGHT / side; ++x) {
            const auto& range = mip.m_ranges[x + mip.m_width * y];
            if (!range.m_supported) {
                return DepthCellRelation::UNKNOWN_DEPTH;
            }
            lower = std::min(lower, range.m_lower);
            upper = std::max(upper, range.m_upper);
        }
    }
    const double K_GUARD { m_recipe.m_world_guard / m_recipe.m_world_from_camera.m_scale };
    if (z_max + K_GUARD < lower) {
        return DepthCellRelation::FRONT_DIAGNOSTIC;
    }
    if (z_min - K_GUARD > upper) {
        return DepthCellRelation::BEHIND_SURFACE;
    }
    return DepthCellRelation::SURFACE_BAND;
}

}  // namespace world_explorer
