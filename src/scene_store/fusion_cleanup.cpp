#include "world_explorer/scene_store/fusion_cleanup.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/fusion_components.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {

using Json = nlohmann::json;
using Triangle = std::array<std::uint32_t, 3>;
using Point = std::array<double, 3>;
constexpr auto K_INFINITY = std::numeric_limits<double>::infinity();
constexpr auto K_ABSENT = std::numeric_limits<std::uint32_t>::max();
constexpr auto K_KIND = "OBSERVED_AFFINE_CLEANUP_V2";
constexpr auto K_LEGACY_KIND = "OBSERVED_INTERIOR_FAN_CLEANUP_V1";
constexpr auto K_MAX_PATCHES = 8192uz;
constexpr auto K_MAX_BYTES = 4uz * 1024uz * 1024uz;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class CleanupCancelled final : public std::exception {
  public:
    const char* what() const noexcept override {
        return "Mesh cleanup cancelled";
    }
};

void check_cancel(const std::stop_token& stop) {
    if (stop.stop_requested()) {
        throw CleanupCancelled {};
    }
}

Point point(const ObservedFusionAsset& asset, std::uint32_t index) {
    const auto& p = asset.m_vertices[index].m_position;
    return { p[0], p[1], p[2] };
}

Point difference(const Point& left, const Point& right) {
    return { left[0] - right[0], left[1] - right[1], left[2] - right[2] };
}

double dot(const Point& left, const Point& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

// Each operation encloses the exact real result on IEEE binary64 in nearest
// rounding mode. Calls to nextafter separate operations and prevent contraction.
struct Interval {
    double m_low {}, m_high {};
};

Interval plus(Interval left, Interval right) {
    return { std::nextafter(left.m_low + right.m_low, -K_INFINITY),
            std::nextafter(left.m_high + right.m_high, K_INFINITY) };
}

Interval minus(Interval left, Interval right) {
    return plus(left, { -right.m_high, -right.m_low });
}

Interval multiply_positive(Interval value, double factor) {
    return { std::nextafter(value.m_low * factor, -K_INFINITY),
            std::nextafter(value.m_high * factor, K_INFINITY) };
}

Interval multiply(Interval left, Interval right) {
    const std::array<double, 4> K_VALUES { left.m_low * right.m_low,
                                          left.m_low * right.m_high,
                                          left.m_high * right.m_low,
                                          left.m_high * right.m_high };
    return { std::nextafter(*std::ranges::min_element(K_VALUES), -K_INFINITY),
            std::nextafter(*std::ranges::max_element(K_VALUES), K_INFINITY) };
}

bool positive_orientation(const ObservedFusionAsset& asset,
                          const Triangle& original,
                          const Triangle& updated) {
    const auto K_NORMAL = [&](const Triangle& triangle) {
        const auto K_A = point(asset, triangle[0]), K_B = point(asset, triangle[1]),
                   K_C = point(asset, triangle[2]);
        std::array<Interval, 3> u {}, v {}, normal {};
        for (auto axis = 0uz; axis < 3uz; ++axis) {
            u[axis] = minus({ K_B[axis], K_B[axis] }, { K_A[axis], K_A[axis] });
            v[axis] = minus({ K_C[axis], K_C[axis] }, { K_A[axis], K_A[axis] });
        }
        for (auto axis = 0uz; axis < 3uz; ++axis) {
            const auto K_NEXT = (axis + 1uz) % 3uz, K_LAST = (axis + 2uz) % 3uz;
            normal[axis] = minus(multiply(u[K_NEXT], v[K_LAST]), multiply(u[K_LAST], v[K_NEXT]));
        }
        return normal;
    };
    const auto K_LEFT = K_NORMAL(original), K_RIGHT = K_NORMAL(updated);
    Interval total;
    for (auto axis = 0uz; axis < 3uz; ++axis) {
        total = plus(total, multiply(K_LEFT[axis], K_RIGHT[axis]));
    }
    return std::isfinite(total.m_low) && total.m_low > 0;
}

double distance_bound(const ObservedFusionAsset& asset, const FusionCleanupPatch& patch) {
    const auto K_CENTER = point(asset, patch.m_vertex);
    const auto K_A = point(asset, patch.m_ring[0]);
    const auto K_B = point(asset, patch.m_ring[1]);
    const auto K_C = point(asset, patch.m_ring[2]);
    double squared {};
    for (auto axis = 0uz; axis < 3uz; ++axis) {
        const Interval K_ORIGIN { K_A[axis], K_A[axis] };
        auto q = plus(
            plus(K_ORIGIN,
                 multiply_positive(minus({ K_B[axis], K_B[axis] }, K_ORIGIN),
                                   patch.m_barycentric_uv[0])),
            multiply_positive(minus({ K_C[axis], K_C[axis] }, K_ORIGIN), patch.m_barycentric_uv[1]));
        if (patch.m_target != K_ABSENT) {
            const auto K_TARGET = point(asset, patch.m_target);
            q = { K_TARGET[axis], K_TARGET[axis] };
        }
        const auto K_DELTA = minus({ K_CENTER[axis], K_CENTER[axis] }, q);
        const auto K_MAXIMUM = std::max(std::abs(K_DELTA.m_low), std::abs(K_DELTA.m_high));
        const auto K_TERM = std::nextafter(K_MAXIMUM * K_MAXIMUM, K_INFINITY);
        squared = std::nextafter(squared + K_TERM, K_INFINITY);
    }
    return std::nextafter(std::sqrt(squared), K_INFINITY);
}

struct Graph {
    std::vector<std::size_t> m_offsets;
    std::vector<std::uint32_t> m_incident;
    std::vector<std::uint8_t> m_bad_edge, m_locked, m_manifold, m_support;

    explicit Graph(const ObservedFusionAsset& asset, const std::stop_token& stop)
        : m_offsets(asset.m_vertices.size() + 1uz), m_incident(asset.m_triangles.size() * 3uz),
          m_bad_edge(asset.m_vertices.size()), m_locked(asset.m_vertices.size()),
          m_manifold(asset.m_vertices.size()), m_support(asset.m_vertices.size()) {
        std::vector<std::array<std::uint32_t, 2>> edges;
        edges.reserve(asset.m_triangles.size() * 3uz);
        for (const auto& triangle : asset.m_triangles) {
            check_cancel(stop);
            for (auto axis = 0uz; axis < 3uz; ++axis) {
                ++m_offsets[triangle[axis] + 1uz];
                const auto K_NEXT = triangle[(axis + 1uz) % 3uz];
                edges.push_back(
                    { std::min(triangle[axis], K_NEXT), std::max(triangle[axis], K_NEXT) });
            }
        }
        for (auto index = 1uz; index < m_offsets.size(); ++index) {
            m_offsets[index] += m_offsets[index - 1uz];
        }
        auto cursor = m_offsets;
        for (auto index = 0uz; index < asset.m_triangles.size(); ++index) {
            check_cancel(stop);
            for (auto vertex : asset.m_triangles[index]) {
                m_incident[cursor[vertex]++] = static_cast<std::uint32_t>(index);
            }
        }
        std::sort(edges.begin(), edges.end());
        for (auto begin = 0uz; begin < edges.size();) {
            check_cancel(stop);
            auto end = begin + 1uz;
            while (end < edges.size() && edges[end] == edges[begin]) {
                ++end;
            }
            if (end - begin != 2uz) {
                for (auto vertex : edges[begin]) {
                    m_bad_edge[vertex] = 1;
                }
            }
            begin = end;
        }
    }

    std::span<const std::uint32_t> faces(std::uint32_t vertex) const {
        return std::span(m_incident)
            .subspan(m_offsets[vertex], m_offsets[vertex + 1uz] - m_offsets[vertex]);
    }

    bool manifold(const ObservedFusionAsset& asset, std::uint32_t vertex) {
        if (m_manifold[vertex]) {
            return m_manifold[vertex] == 1;
        }
        m_manifold[vertex] = 2;
        const auto K_FACES = faces(vertex);
        if (m_bad_edge[vertex] || K_FACES.size() < 3uz || K_FACES.size() > 64uz) {
            return false;
        }
        std::map<std::uint32_t, std::vector<std::uint32_t>> links;
        for (auto face : K_FACES) {
            const auto& triangle = asset.m_triangles[face];
            const auto K_FOUND = std::ranges::find(triangle, vertex);
            const auto K_INDEX = static_cast<std::size_t>(K_FOUND - triangle.begin());
            const auto K_A = triangle[(K_INDEX + 1uz) % 3uz];
            const auto K_B = triangle[(K_INDEX + 2uz) % 3uz];
            links[K_A].push_back(K_B);
            links[K_B].push_back(K_A);
        }
        for (const auto& [K_KEY, K_NEIGHBORS] : links) {
            (void)K_KEY;
            if (K_NEIGHBORS.size() != 2uz || K_NEIGHBORS[0] == K_NEIGHBORS[1]) {
                return false;
            }
        }
        auto previous = K_ABSENT;
        auto current = links.begin()->first;
        const auto K_START = current;
        for (auto step = 0uz; step < links.size(); ++step) {
            const auto& neighbors = links.at(current);
            const auto K_NEXT = neighbors[0] == previous ? neighbors[1] : neighbors[0];
            previous = current;
            current = K_NEXT;
            if (current == K_START) {
                if (step + 1uz != links.size()) {
                    return false;
                }
                m_manifold[vertex] = 1;
                return true;
            }
        }
        return false;
    }

    bool supported(const ObservedFusionAsset& asset, std::uint32_t vertex) {
        if (m_support[vertex]) {
            return m_support[vertex] == 1;
        }
        m_support[vertex] = 2;
        const auto K_POINT = point(asset, vertex);
        std::array<std::int32_t, 3> cell {};
        for (auto axis = 0uz; axis < 3uz; ++axis) {
            cell[axis] = static_cast<std::int32_t>(std::floor(K_POINT[axis] / asset.m_voxel_size));
        }
        for (auto corner = 0uz; corner < 8uz; ++corner) {
            auto key = cell;
            for (auto axis = 0uz; axis < 3uz; ++axis) {
                key[axis] += static_cast<std::int32_t>((corner >> axis) & 1uz);
            }
            const auto K_FOUND =
                std::lower_bound(asset.m_voxels.begin(),
                                 asset.m_voxels.end(),
                                 key,
                                 [](const ObservedVoxel& voxel, const auto& lattice) {
                                     return voxel.m_lattice < lattice;
                                 });
            if (K_FOUND == asset.m_voxels.end() || K_FOUND->m_lattice != key ||
                K_FOUND->m_observation_count < 2 ||
                K_FOUND->m_support_sum < 2U * K_FOUND->m_observation_count) {
                return false;
            }
        }
        m_support[vertex] = 1;
        return true;
    }
};

// Returns a decision-ledger index; zero means this is not a degree-three fan.
std::size_t fan_candidate(const ObservedFusionAsset& asset,
                          Graph& graph,
                          FusionCleanupPatch& patch,
                          bool use_stored_weights) {
    const auto K_FACES = graph.faces(patch.m_vertex);
    if (K_FACES.size() != 3uz) {
        return 0uz;
    }
    patch.m_triangles.assign(K_FACES.begin(), K_FACES.end());
    const auto& triangle = asset.m_triangles[K_FACES[0]];
    const auto K_INDEX =
        static_cast<std::size_t>(std::ranges::find(triangle, patch.m_vertex) - triangle.begin());
    const auto K_A = triangle[(K_INDEX + 1uz) % 3uz];
    const auto K_B = triangle[(K_INDEX + 2uz) % 3uz];
    auto c = K_ABSENT;
    for (auto face : K_FACES) {
        for (auto vertex : asset.m_triangles[face]) {
            if (vertex != patch.m_vertex && vertex != K_A && vertex != K_B) {
                if (c != K_ABSENT && c != vertex) {
                    return 2uz;
                }
                c = vertex;
            }
        }
    }
    if (c == K_ABSENT) {
        return 2uz;
    }
    patch.m_ring = { K_A, K_B, c };
    const std::array<std::uint32_t, 4> K_VERTICES { patch.m_vertex, K_A, K_B, c };
    for (auto vertex : K_VERTICES) {
        if (graph.m_locked[vertex]) {
            return 1uz;
        }
    }
    for (auto vertex : K_VERTICES) {
        if (!graph.manifold(asset, vertex)) {
            return 2uz;
        }
    }
    std::set<std::array<std::uint32_t, 2>> oriented;
    for (auto face : K_FACES) {
        const auto& t = asset.m_triangles[face];
        const auto K_OFFSET =
            static_cast<std::size_t>(std::ranges::find(t, patch.m_vertex) - t.begin());
        oriented.insert({ t[(K_OFFSET + 1uz) % 3uz], t[(K_OFFSET + 2uz) % 3uz] });
    }
    if (oriented != std::set<std::array<std::uint32_t, 2>> { { K_A, K_B }, { K_B, c }, { c, K_A } }) {
        return 2uz;
    }
    auto ring_sorted = patch.m_ring;
    std::sort(ring_sorted.begin(), ring_sorted.end());
    for (auto face : graph.faces(K_A)) {
        auto sorted = asset.m_triangles[face];
        std::sort(sorted.begin(), sorted.end());
        if (std::equal(sorted.begin(), sorted.end(), ring_sorted.begin(), ring_sorted.end())) {
            return 2uz;
        }
    }
    for (auto vertex : K_VERTICES) {
        if (!graph.supported(asset, vertex)) {
            return 3uz;
        }
    }
    for (auto left = 0uz; left < K_VERTICES.size(); ++left) {
        for (auto right = left + 1uz; right < K_VERTICES.size(); ++right) {
            const auto K_D =
                difference(point(asset, K_VERTICES[left]), point(asset, K_VERTICES[right]));
            if (std::sqrt(dot(K_D, K_D)) > 2 * asset.m_voxel_size) {
                return 4uz;
            }
        }
    }
    const auto K_AB = difference(point(asset, K_B), point(asset, K_A));
    const auto K_AC = difference(point(asset, c), point(asset, K_A));
    const auto K_AV = difference(point(asset, patch.m_vertex), point(asset, K_A));
    const auto K_AB2 = dot(K_AB, K_AB);
    const auto K_AC2 = dot(K_AC, K_AC);
    const auto K_CROSS = dot(K_AB, K_AC);
    const auto K_DETERMINANT = K_AB2 * K_AC2 - K_CROSS * K_CROSS;
    if (!std::isfinite(K_DETERMINANT) || K_DETERMINANT <= 1e-12 * K_AB2 * K_AC2) {
        return 4uz;
    }
    if (!use_stored_weights) {
        patch.m_barycentric_uv = {
            (K_AC2 * dot(K_AV, K_AB) - K_CROSS * dot(K_AV, K_AC)) / K_DETERMINANT,
            (K_AB2 * dot(K_AV, K_AC) - K_CROSS * dot(K_AV, K_AB)) / K_DETERMINANT };
    }
    const auto K_U = patch.m_barycentric_uv[0];
    const auto K_V = patch.m_barycentric_uv[1];
    // The outward sum check certifies strict convexity, independent of the
    // numerical projection used to propose these two binary64 coefficients.
    if (!std::isfinite(K_U) || !std::isfinite(K_V) || K_U < 1.0 / 64 || K_V < 1.0 / 64 ||
        std::nextafter(K_U + K_V, K_INFINITY) > 63.0 / 64) {
        return 4uz;
    }
    for (auto face : K_FACES) {
        if (!positive_orientation(asset, asset.m_triangles[face], { K_A, K_B, c })) {
            return 4uz;
        }
    }
    const auto K_BOUND = distance_bound(asset, patch);
    if (!std::isfinite(K_BOUND) || K_BOUND > asset.m_voxel_size * .25) {
        return 5uz;
    }
    if (use_stored_weights) {
        if (!std::isfinite(patch.m_distance_upper_bound) ||
            patch.m_distance_upper_bound < K_BOUND ||
            patch.m_distance_upper_bound > asset.m_voxel_size * .25) {
            return 5uz;
        }
    } else {
        patch.m_distance_upper_bound = K_BOUND;
    }
    return 6uz;
}

std::set<std::uint32_t>
neighbors(const ObservedFusionAsset& asset, const Graph& graph, std::uint32_t vertex) {
    std::set<std::uint32_t> result;
    for (auto face : graph.faces(vertex)) {
        for (auto index : asset.m_triangles[face]) {
            if (index != vertex) {
                result.insert(index);
            }
        }
    }
    return result;
}

std::size_t edge_candidate(const ObservedFusionAsset& asset,
                           Graph& graph,
                           FusionCleanupPatch& patch,
                           bool use_stored_weights) {
    const auto K_FACES = graph.faces(patch.m_vertex);
    if (K_FACES.size() < 4uz || K_FACES.size() > 64uz) {
        return 0uz;
    }
    const auto K_NEIGHBORS = neighbors(asset, graph, patch.m_vertex);
    if (!K_NEIGHBORS.contains(patch.m_target)) {
        return 2uz;
    }
    patch.m_triangles.assign(K_FACES.begin(), K_FACES.end());
    patch.m_ring.assign(K_NEIGHBORS.begin(), K_NEIGHBORS.end());
    auto vertices = patch.m_ring;
    vertices.push_back(patch.m_vertex);
    for (auto vertex : vertices) {
        if (graph.m_locked[vertex]) {
            return 1uz;
        }
    }
    for (auto vertex : vertices) {
        if (!graph.manifold(asset, vertex)) {
            return 2uz;
        }
    }
    for (auto vertex : vertices) {
        if (!graph.supported(asset, vertex)) {
            return 3uz;
        }
    }
    for (auto left = 0uz; left < vertices.size(); ++left) {
        for (auto right = left + 1uz; right < vertices.size(); ++right) {
            const auto K_D =
                difference(point(asset, vertices[left]), point(asset, vertices[right]));
            if (std::sqrt(dot(K_D, K_D)) > 2 * asset.m_voxel_size) {
                return 4uz;
            }
        }
    }
    const auto K_TARGET_NEIGHBORS = neighbors(asset, graph, patch.m_target);
    std::set<std::uint32_t> common, opposite;
    std::set_intersection(K_NEIGHBORS.begin(),
                          K_NEIGHBORS.end(),
                          K_TARGET_NEIGHBORS.begin(),
                          K_TARGET_NEIGHBORS.end(),
                          std::inserter(common, common.end()));
    std::set<Triangle> existing, updated;
    std::set<std::array<std::uint32_t, 2>> image_edges;
    for (auto face : graph.faces(patch.m_target)) {
        auto triangle = asset.m_triangles[face];
        if (std::ranges::find(triangle, patch.m_vertex) != triangle.end()) {
            continue;
        }
        std::sort(triangle.begin(), triangle.end());
        existing.insert(triangle);
    }
    std::size_t collapsed_faces {};
    for (auto face : K_FACES) {
        const auto& original = asset.m_triangles[face];
        if (std::ranges::find(original, patch.m_target) != original.end()) {
            ++collapsed_faces;
            for (auto vertex : original) {
                if (vertex != patch.m_vertex && vertex != patch.m_target) {
                    opposite.insert(vertex);
                }
            }
            continue;
        }
        auto triangle = original;
        for (auto& vertex : triangle) {
            if (vertex == patch.m_vertex) {
                vertex = patch.m_target;
            }
        }
        if (!positive_orientation(asset, original, triangle)) {
            return 4uz;
        }
        for (auto axis = 0uz; axis < 3uz; ++axis) {
            const auto K_A = triangle[axis], K_B = triangle[(axis + 1uz) % 3uz];
            image_edges.insert({ std::min(K_A, K_B), std::max(K_A, K_B) });
        }
        std::sort(triangle.begin(), triangle.end());
        if (existing.contains(triangle) || !updated.insert(triangle).second) {
            return 2uz;
        }
    }
    if (collapsed_faces != 2uz || opposite.size() != 2uz || common != opposite) {
        return 2uz;
    }
    for (auto vertex : opposite) {
        if (!image_edges.contains(
                { std::min(vertex, patch.m_target), std::max(vertex, patch.m_target) })) {
            return 2uz;
        }
    }
    if (patch.m_barycentric_uv != std::array<double, 2> {}) {
        return 4uz;
    }
    const auto K_BOUND = distance_bound(asset, patch);
    if (!std::isfinite(K_BOUND) || K_BOUND > asset.m_voxel_size * .25) {
        return 5uz;
    }
    if (use_stored_weights) {
        if (!std::isfinite(patch.m_distance_upper_bound) ||
            patch.m_distance_upper_bound < K_BOUND ||
            patch.m_distance_upper_bound > asset.m_voxel_size * .25) {
            return 5uz;
        }
    } else {
        patch.m_distance_upper_bound = K_BOUND;
    }
    return 6uz;
}

std::size_t candidate(const ObservedFusionAsset& asset,
                      Graph& graph,
                      FusionCleanupPatch& patch,
                      bool use_stored_weights) {
    if (graph.faces(patch.m_vertex).size() == 3uz) {
        if (patch.m_target != K_ABSENT) {
            return 2uz;
        }
        return fan_candidate(asset, graph, patch, use_stored_weights);
    }
    if (use_stored_weights) {
        return edge_candidate(asset, graph, patch, true);
    }
    const auto K_DEGREE = graph.faces(patch.m_vertex).size();
    if (K_DEGREE < 4uz || K_DEGREE > 64uz) {
        return 0uz;
    }
    auto decision = 5uz;
    for (auto target : neighbors(asset, graph, patch.m_vertex)) {
        const auto K_DELTA = difference(point(asset, patch.m_vertex), point(asset, target));
        if (std::sqrt(dot(K_DELTA, K_DELTA)) > asset.m_voxel_size * .25) {
            continue;
        }
        auto proposal = patch;
        proposal.m_target = target;
        decision = edge_candidate(asset, graph, proposal, false);
        if (decision == 6uz) {
            patch = std::move(proposal);
            return decision;
        }
    }
    return decision;
}

void lock_patch(Graph& graph, const FusionCleanupPatch& patch) {
    graph.m_locked[patch.m_vertex] = 1;
    for (auto vertex : patch.m_ring) {
        graph.m_locked[vertex] = 1;
    }
}

std::string mesh_hash(const FusionCleanup& output) {
    Sha256Builder hash;
    const auto K_ADD = [&](std::uint64_t value, std::size_t width) {
        std::array<std::byte, 8> bytes {};
        for (auto index = 0uz; index < width; ++index) {
            bytes[index] = static_cast<std::byte>(value & 255U);
            value >>= 8U;
        }
        hash.update(std::span(bytes).first(width));
    };
    K_ADD(output.m_vertices.size(), 8uz);
    K_ADD(output.m_triangles.size(), 8uz);
    for (const auto& vertex : output.m_vertices) {
        for (auto value : vertex.m_position) {
            K_ADD(std::bit_cast<std::uint32_t>(value), 4uz);
        }
        for (auto value : vertex.m_normal) {
            K_ADD(std::bit_cast<std::uint32_t>(value), 4uz);
        }
    }
    for (const auto& triangle : output.m_triangles) {
        for (auto index : triangle) {
            K_ADD(index, 4uz);
        }
    }
    return hash.finish();
}

void derive_mesh(const ObservedFusionAsset& asset,
                 FusionCleanup& result,
                 const std::stop_token& stop) {
    std::vector<std::uint32_t> patches(asset.m_triangles.size(), K_ABSENT);
    std::vector<std::uint8_t> erased(asset.m_triangles.size());
    for (auto index = 0uz; index < result.m_patches.size(); ++index) {
        const auto& patch = result.m_patches[index];
        if (patch.m_target == K_ABSENT) {
            patches[patch.m_triangles[0]] = static_cast<std::uint32_t>(index);
            erased[patch.m_triangles[1]] = erased[patch.m_triangles[2]] = 1;
        } else {
            for (auto face : patch.m_triangles) {
                patches[face] = static_cast<std::uint32_t>(index);
                const auto& triangle = asset.m_triangles[face];
                if (std::ranges::find(triangle, patch.m_target) != triangle.end()) {
                    erased[face] = 1;
                }
            }
        }
        result.m_distance_upper_bound =
            std::max(result.m_distance_upper_bound, patch.m_distance_upper_bound);
    }
    std::vector<std::uint8_t> used(asset.m_vertices.size());
    for (auto index = 0uz; index < asset.m_triangles.size(); ++index) {
        check_cancel(stop);
        if (erased[index]) {
            continue;
        }
        auto triangle = asset.m_triangles[index];
        if (patches[index] != K_ABSENT) {
            const auto& patch = result.m_patches[patches[index]];
            if (patch.m_target == K_ABSENT) {
                std::copy(patch.m_ring.begin(), patch.m_ring.end(), triangle.begin());
            } else {
                for (auto& vertex : triangle) {
                    if (vertex == patch.m_vertex) {
                        vertex = patch.m_target;
                    }
                }
            }
        }
        result.m_triangles.push_back(triangle);
        for (auto vertex : triangle) {
            used[vertex] = 1;
        }
    }
    std::vector<std::uint32_t> remap(asset.m_vertices.size(), K_ABSENT);
    for (auto index = 0uz; index < used.size(); ++index) {
        check_cancel(stop);
        if (!used[index]) {
            continue;
        }
        remap[index] = static_cast<std::uint32_t>(result.m_vertices.size());
        result.m_vertices.push_back(asset.m_vertices[index]);
        result.m_output_to_source_vertices.push_back(static_cast<std::uint32_t>(index));
    }
    for (auto& triangle : result.m_triangles) {
        for (auto& index : triangle) {
            index = remap[index];
        }
    }
    result.m_geometry_sha256 = mesh_hash(result);
}

Json document(const ObservedFusionAsset& asset, const FusionCleanup& result) {
    auto patches = Json::array();
    for (const auto& patch : result.m_patches) {
        patches.push_back({ { "vertex", patch.m_vertex },
                           { "target", patch.m_target },
                           { "triangles", patch.m_triangles },
                           { "ring", patch.m_ring },
                           { "barycentric_uv", patch.m_barycentric_uv },
                           { "distance_upper_bound", patch.m_distance_upper_bound } });
    }
    return { { "schema_version", 1U },
            { "kind", K_KIND },
            { "parent_asset_sha256", asset.m_asset_sha256 },
            { "voxel_size", asset.m_voxel_size },
            { "error_bound", asset.m_voxel_size * .25 },
            { "maximum_distance_upper_bound", result.m_distance_upper_bound },
            { "input_vertices", asset.m_vertices.size() },
            { "input_triangles", asset.m_triangles.size() },
            { "output_vertices", result.m_vertices.size() },
            { "output_triangles", result.m_triangles.size() },
            { "mesh_sha256", result.m_geometry_sha256 },
            { "patches", std::move(patches) } };
}

std::uint32_t index_value(const Json& value) {
    require(value.is_number_unsigned() && value.get<std::uint64_t>() <= K_ABSENT,
            "Invalid cleanup index");
    return value.get<std::uint32_t>();
}

std::vector<std::uint32_t> indices(const Json& value) {
    require(value.is_array() && value.size() >= 3uz && value.size() <= 64uz,
            "Invalid cleanup index array");
    std::vector<std::uint32_t> result;
    for (const auto& entry : value) {
        result.push_back(index_value(entry));
    }
    return result;
}

Result<FusionCleanup>
run(const ObservedFusionAsset& asset, const Json& input, const std::stop_token& stop) {
    try {
        static_assert(std::numeric_limits<double>::is_iec559);
        check_cancel(stop);
        require(std::fegetround() == FE_TONEAREST, "Cleanup requires IEEE nearest rounding");
        if (asset.m_vertices.size() > 500000uz || asset.m_triangles.size() > 1000000uz) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup development count budget exceeded");
        }
        require(!asset.m_triangles.empty(), "Cleanup requires a drawable source mesh");
        require(std::isfinite(asset.m_voxel_size * .25) && asset.m_voxel_size * .25 > 0,
                "Invalid cleanup error bound");
        const auto K_CHECKED = analyze_fusion_components(asset, {}, stop);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        Graph graph(asset, stop);
        FusionCleanup result;
        if (input.is_null()) {
            for (auto vertex = 0uz; vertex < asset.m_vertices.size(); ++vertex) {
                check_cancel(stop);
                FusionCleanupPatch patch;
                patch.m_vertex = static_cast<std::uint32_t>(vertex);
                const auto K_DECISION = candidate(asset, graph, patch, false);
                ++result.m_decisions[K_DECISION];
                if (K_DECISION != 6uz) {
                    continue;
                }
                if (result.m_patches.size() == K_MAX_PATCHES) {
                    return fail(ErrorCode::RESOURCE_EXHAUSTED,
                                "Cleanup certificate patch budget exceeded");
                }
                lock_patch(graph, patch);
                result.m_patches.push_back(patch);
            }
        } else {
            require(input.is_object() && input.size() == 12uz, "Invalid cleanup schema");
            require(input.at("schema_version").is_number_unsigned() &&
                        input.at("schema_version") == 1 &&
                        (input.at("kind").get<std::string>() == K_KIND ||
                         input.at("kind").get<std::string>() == K_LEGACY_KIND) &&
                        input.at("parent_asset_sha256").get<std::string>() ==
                            asset.m_asset_sha256 &&
                        input.at("voxel_size").is_number() &&
                        input.at("voxel_size").get<double>() == asset.m_voxel_size &&
                        input.at("error_bound").is_number() &&
                        input.at("error_bound").get<double>() == asset.m_voxel_size * .25,
                    "Cleanup source or policy differs");
            const auto& entries = input.at("patches");
            require(entries.is_array() && entries.size() <= K_MAX_PATCHES,
                    "Invalid cleanup patch count");
            for (const auto& entry : entries) {
                check_cancel(stop);
                require(entry.is_object() &&
                            (entry.size() == 6uz ||
                             (input.at("kind").get<std::string>() == K_LEGACY_KIND &&
                              entry.size() == 5uz)),
                        "Invalid cleanup patch schema");
                FusionCleanupPatch patch;
                patch.m_vertex = index_value(entry.at("vertex"));
                if (input.at("kind").get<std::string>() == K_KIND) {
                    patch.m_target = index_value(entry.at("target"));
                }
                require(patch.m_vertex < asset.m_vertices.size() &&
                            (result.m_patches.empty() ||
                             patch.m_vertex > result.m_patches.back().m_vertex),
                        "Invalid cleanup vertex order/range");
                const auto K_TRIANGLES = indices(entry.at("triangles"));
                const auto K_RING = indices(entry.at("ring"));
                const auto& uv = entry.at("barycentric_uv");
                require(uv.is_array() && uv.size() == 2uz && uv[0].is_number() &&
                            uv[1].is_number() && entry.at("distance_upper_bound").is_number(),
                        "Invalid cleanup distance weights");
                patch.m_barycentric_uv = { uv[0].get<double>(), uv[1].get<double>() };
                patch.m_distance_upper_bound = entry.at("distance_upper_bound").get<double>();
                require(candidate(asset, graph, patch, true) == 6uz &&
                            patch.m_triangles == K_TRIANGLES && patch.m_ring == K_RING,
                        "Cleanup patch fails topology/support/distance validation");
                lock_patch(graph, patch);
                result.m_patches.push_back(patch);
            }
        }
        derive_mesh(asset, result, stop);
        auto expected = document(asset, result);
        if (!input.is_null() && input.at("kind").get<std::string>() == K_LEGACY_KIND) {
            expected["kind"] = K_LEGACY_KIND;
            for (auto& patch : expected["patches"]) {
                patch.erase("target");
            }
        }
        if (!input.is_null()) {
            // Numeric type checks prevent signed/float count aliases; comparison
            // then covers every field, including output counts and geometry hash.
            for (const auto* key :
                 { "input_vertices", "input_triangles", "output_vertices", "output_triangles" }) {
                require(input.at(key).is_number_unsigned(), "Invalid cleanup count type");
            }
            require(input == expected, "Cleanup derived geometry or certificate totals differ");
        }
        result.m_document = expected.dump(2) + '\n';
        if (result.m_document.size() > K_MAX_BYTES) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup certificate byte budget exceeded");
        }
        check_cancel(stop);
        return result;
    } catch (const CleanupCancelled& exception) {
        return fail(ErrorCode::CANCELLED, exception.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, std::string("Cleanup failed: ") + exception.what());
    }
}

}  // namespace

Result<FusionCleanup> simplify_fusion_mesh(const ObservedFusionAsset& asset,
                                           const std::stop_token& stop) {
    return run(asset, Json {}, stop);
}

Result<FusionCleanup> decode_fusion_cleanup(std::span<const std::byte> bytes,
                                            const std::string& expected_sha256,
                                            const ObservedFusionAsset& asset,
                                            const std::stop_token& stop) {
    try {
        check_cancel(stop);
        if (bytes.empty() || bytes.size() > K_MAX_BYTES) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup document byte budget exceeded");
        }
        require(sha256(bytes) == expected_sha256, "Cleanup document checksum differs");
        const std::string K_TEXT { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        require(K_TEXT.find('\0') == K_TEXT.npos, "Cleanup document contains raw NUL");
        std::map<int, std::set<std::string>> keys;
        const auto K_DOCUMENT =
            Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
                require(depth <= 5, "Cleanup JSON nesting exceeded");
                if (event == Json::parse_event_t::object_start) {
                    keys[depth + 1].clear();
                }
                if (event == Json::parse_event_t::key) {
                    require(keys[depth].insert(value.get<std::string>()).second,
                            "Duplicate cleanup JSON key");
                }
                return true;
            });
        require(K_DOCUMENT.is_object(), "Cleanup document must be an object");
        auto result = run(asset, K_DOCUMENT, stop);
        if (result) {
            result->m_document = K_TEXT;
        }
        return result;
    } catch (const CleanupCancelled& exception) {
        return fail(ErrorCode::CANCELLED, exception.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Cleanup decoder allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Cleanup document failed: ") + exception.what());
    }
}

}  // namespace world_explorer
