#include "world_explorer/scene_store/fusion_components.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <new>

namespace world_explorer {
namespace {

constexpr auto K_ABSENT = std::numeric_limits<std::uint32_t>::max();

std::uint32_t root(std::vector<std::uint32_t>& parents, std::uint32_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

void join(std::vector<std::uint32_t>& parents,
          std::vector<std::uint8_t>& ranks,
          std::uint32_t left,
          std::uint32_t right) {
    left = root(parents, left);
    right = root(parents, right);
    if (left == right) return;
    if (ranks[left] < ranks[right] || (ranks[left] == ranks[right] && left > right))
        std::swap(left, right);
    parents[right] = left;
    if (ranks[left] == ranks[right]) ++ranks[left];
}

void add(FusionComponentSupport& target, const ObservedVoxel& voxel) {
    if (target.m_weight_sum == 0) target.m_weight_min = voxel.m_weight;
    target.m_weight_min = std::min(target.m_weight_min, static_cast<double>(voxel.m_weight));
    target.m_weight_max = std::max(target.m_weight_max, static_cast<double>(voxel.m_weight));
    target.m_weight_sum += voxel.m_weight;
    target.m_observations += voxel.m_observation_count;
    target.m_support_sum += voxel.m_support_sum;
    target.m_front_count += voxel.m_front_count;
    target.m_sigma_corners += voxel.m_sigma_weight > 0 ? 1uz : 0uz;
    target.m_sigma2_sum += voxel.m_sigma2_sum;
    target.m_sigma_weight += voxel.m_sigma_weight;
}

double triangle_area(const ObservedFusionAsset& asset,
                     const std::array<std::uint32_t, 3>& triangle) {
    const auto& a = asset.m_vertices[triangle[0]].m_position;
    const auto& b = asset.m_vertices[triangle[1]].m_position;
    const auto& c = asset.m_vertices[triangle[2]].m_position;
    std::array<double, 3> u {}, v {};
    for (auto axis = 0uz; axis < 3uz; ++axis) {
        u[axis] = static_cast<double>(b[axis]) - a[axis];
        v[axis] = static_cast<double>(c[axis]) - a[axis];
    }
    return .5 * std::hypot(u[1] * v[2] - u[2] * v[1],
                           u[2] * v[0] - u[0] * v[2],
                           u[0] * v[1] - u[1] * v[0]);
}

}  // namespace

Result<FusionComponentAnalysis> analyze_fusion_components(const ObservedFusionAsset& asset,
                                                          FusionComponentLimits limits,
                                                          const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
        const auto K_VERTICES = asset.m_vertices.size();
        const auto K_TRIANGLES = asset.m_triangles.size();
        // Hard caps also bound indices, accumulator arithmetic and union rank.
        if (K_VERTICES > std::min(limits.m_asset.m_max_vertices, 4000000uz) ||
            K_TRIANGLES > std::min(limits.m_asset.m_max_triangles, 8000000uz) ||
            asset.m_voxels.size() > std::min(limits.m_asset.m_max_voxels, 16777216uz))
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Component input count budget exceeded");
        const auto K_COMPONENT_CAP =
            std::min({ K_VERTICES, K_TRIANGLES, limits.m_max_components, 65536uz });
        const auto K_WORK_BYTES = K_VERTICES * 10uz + K_COMPONENT_CAP * sizeof(FusionMeshComponent);
        if (K_WORK_BYTES > limits.m_working_bytes.m_bytes)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Component working byte budget exceeded");
        if (!std::isfinite(asset.m_voxel_size) || asset.m_voxel_size <= 0)
            return fail(ErrorCode::INVALID_DATA, "Invalid component voxel size");
        for (auto index = 0uz; index < asset.m_voxels.size(); ++index) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
            const auto& voxel = asset.m_voxels[index];
            if ((index > 0uz && asset.m_voxels[index - 1uz].m_lattice >= voxel.m_lattice) ||
                !std::isfinite(voxel.m_weight) || voxel.m_weight <= 0 ||
                voxel.m_observation_count == 0 || voxel.m_observation_count > 18 ||
                voxel.m_weight > static_cast<float>(voxel.m_observation_count) ||
                voxel.m_support_sum > 18U * voxel.m_observation_count ||
                voxel.m_front_count > voxel.m_observation_count ||
                !std::isfinite(voxel.m_sigma2_sum) || voxel.m_sigma2_sum < 0 ||
                !std::isfinite(voxel.m_sigma_weight) || voxel.m_sigma_weight < 0 ||
                voxel.m_sigma_weight > voxel.m_weight ||
                (voxel.m_sigma_weight == 0 && voxel.m_sigma2_sum != 0))
                return fail(ErrorCode::INVALID_DATA, "Invalid component voxel evidence");
        }
        std::vector<std::uint32_t> parents(K_VERTICES, K_ABSENT), components(K_VERTICES, K_ABSENT);
        std::vector<std::uint8_t> ranks(K_VERTICES), known_corners(K_VERTICES);
        for (const auto& triangle : asset.m_triangles) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
            if (triangle[0] >= K_VERTICES || triangle[1] >= K_VERTICES ||
                triangle[2] >= K_VERTICES || triangle[0] == triangle[1] ||
                triangle[0] == triangle[2] || triangle[1] == triangle[2])
                return fail(ErrorCode::INVALID_DATA, "Invalid component triangle indices");
            for (auto vertex : triangle)
                if (parents[vertex] == K_ABSENT) parents[vertex] = vertex;
            join(parents, ranks, triangle[0], triangle[1]);
            join(parents, ranks, triangle[0], triangle[2]);
        }
        FusionComponentAnalysis result;
        result.m_components.reserve(K_COMPONENT_CAP);
        for (auto index = 0uz; index < K_TRIANGLES; ++index) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
            const auto K_ROOT = root(parents, asset.m_triangles[index][0]);
            if (components[K_ROOT] != K_ABSENT) continue;
            if (result.m_components.size() >= K_COMPONENT_CAP)
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Component report count budget exceeded");
            components[K_ROOT] = static_cast<std::uint32_t>(result.m_components.size());
            FusionMeshComponent component;
            component.m_first_triangle = index;
            result.m_components.push_back(component);
        }
        for (auto index = 0uz; index < K_VERTICES; ++index) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
            std::array<std::int32_t, 3> cell {};
            for (auto axis = 0uz; axis < 3uz; ++axis) {
                const auto K_VALUE =
                    std::floor(static_cast<double>(asset.m_vertices[index].m_position[axis]) /
                               asset.m_voxel_size);
                if (!std::isfinite(K_VALUE) || std::abs(K_VALUE) > 1048576)
                    return fail(ErrorCode::INVALID_DATA,
                                "Component vertex outside lattice precision range");
                cell[axis] = static_cast<std::int32_t>(K_VALUE);
            }
            if (parents[index] == K_ABSENT) {
                ++result.m_unused_vertices;
                continue;
            }
            auto& component =
                result.m_components[components[root(parents, static_cast<std::uint32_t>(index))]];
            ++component.m_vertices;
            ++result.m_total.m_vertices;
            for (auto corner = 0uz; corner < 8uz; ++corner) {
                auto lattice = cell;
                for (auto axis = 0uz; axis < 3uz; ++axis)
                    lattice[axis] += static_cast<std::int32_t>((corner >> axis) & 1uz);
                const auto K_FOUND =
                    std::lower_bound(asset.m_voxels.begin(),
                                     asset.m_voxels.end(),
                                     lattice,
                                     [](const ObservedVoxel& voxel, const auto& key) {
                                         return voxel.m_lattice < key;
                                     });
                if (K_FOUND == asset.m_voxels.end() || K_FOUND->m_lattice != lattice) continue;
                ++known_corners[index];
                add(component.m_support, *K_FOUND);
                add(result.m_total.m_support, *K_FOUND);
            }
            ++component.m_support.m_corner_coverage[known_corners[index]];
            ++result.m_total.m_support.m_corner_coverage[known_corners[index]];
        }
        for (const auto& triangle : asset.m_triangles) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Component analysis cancelled");
            auto& component = result.m_components[components[root(parents, triangle[0])]];
            const auto K_AREA = triangle_area(asset, triangle);
            const auto K_UNKNOWN = std::ranges::any_of(
                triangle, [&](auto vertex) { return known_corners[vertex] < 8; });
            const auto K_UNBACKED = std::ranges::any_of(
                triangle, [&](auto vertex) { return known_corners[vertex] == 0; });
            for (auto& stats : { std::ref(component), std::ref(result.m_total) }) {
                auto& target = stats.get();
                ++target.m_triangles;
                target.m_area += K_AREA;
                if (K_AREA == 0) ++target.m_zero_area_triangles;
                if (K_UNKNOWN) {
                    ++target.m_triangles_with_unknown_corners;
                    target.m_area_with_unknown_corners += K_AREA;
                }
                if (K_UNBACKED) {
                    ++target.m_triangles_with_unbacked_vertices;
                    target.m_area_with_unbacked_vertices += K_AREA;
                }
            }
        }
        if (!std::isfinite(result.m_total.m_support.m_sigma2_sum) ||
            !std::isfinite(result.m_total.m_area))
            return fail(ErrorCode::INVALID_DATA, "Component accumulation overflow");
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Component analysis allocation failed");
    }
}

}  // namespace world_explorer
