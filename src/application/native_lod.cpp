#include "world_explorer/application/native_lod.hpp"

#include "../core/json_records.hpp"

#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return std::move(*result);
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

std::uint32_t index(const Json& value, std::size_t count) {
    const auto K_INDEX = integer(value);
    require(K_INDEX < count, "Native LOD index outside selected mesh");
    return static_cast<std::uint32_t>(K_INDEX);
}

bool cyclic(const std::array<std::uint32_t, 3>& a, const std::array<std::uint32_t, 3>& b) {
    for (std::size_t shift { 0uz }; shift < 3uz; ++shift) {
        if (a[0uz] == b[shift] && a[1uz] == b[(shift + 1uz) % 3uz] &&
            a[2uz] == b[(shift + 2uz) % 3uz]) {
            return true;
        }
    }
    return false;
}

std::array<double, 3> components(Vec3d value) {
    return { value.m_x, value.m_y, value.m_z };
}

// The explicitly bounded dyadic profile makes the affine identity an integer
// proof, independent of floating evaluation order or the active rounding mode.
// |coordinate| <= 2^20, grid 2^-20: weighted sums fit well inside int64.
std::int64_t fixed(double value) {
    const auto K_SCALED = std::ldexp(value, 20);
    require(std::isfinite(value) && std::abs(value) <= 1048576 && K_SCALED == std::trunc(K_SCALED),
            "Native exact LOD attribute outside dyadic profile",
            ErrorCode::UNSUPPORTED_OPERATION);
    return static_cast<std::int64_t>(K_SCALED);
}

template <std::size_t N>
void affine(const std::array<double, N>& center,
            const std::array<std::array<double, N>, 3>& ring,
            const std::array<std::uint32_t, 3>& weights) {
    for (std::size_t axis { 0uz }; axis < N; ++axis) {
        std::int64_t sum {};
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            sum += fixed(ring[corner][axis]) * weights[corner];
        }
        require(sum == fixed(center[axis]) * 256, "Native LOD affine attribute differs");
    }
}

void attributes(const GeometryRuntimeMesh& mesh,
                std::uint32_t center,
                const std::array<std::uint32_t, 3>& ring,
                const std::array<std::uint32_t, 3>& weights) {
    affine(components(mesh.m_positions[center]),
           { components(mesh.m_positions[ring[0uz]]),
            components(mesh.m_positions[ring[1uz]]),
            components(mesh.m_positions[ring[2uz]]) },
           weights);
    affine(components(mesh.m_normals[center]),
           { components(mesh.m_normals[ring[0uz]]),
            components(mesh.m_normals[ring[1uz]]),
            components(mesh.m_normals[ring[2uz]]) },
           weights);
    affine(mesh.m_uv[center],
           { mesh.m_uv[ring[0uz]], mesh.m_uv[ring[1uz]], mesh.m_uv[ring[2uz]] },
           weights);
    affine(mesh.m_tangents[center],
           { mesh.m_tangents[ring[0uz]], mesh.m_tangents[ring[1uz]], mesh.m_tangents[ring[2uz]] },
           weights);
}

std::vector<std::vector<std::uint32_t>> replay(const Json& doc,
                                               const AdmittedGeometryProduct& base,
                                               const AdmittedGeometryProduct& candidate,
                                               const std::stop_token& stop) {
    const auto& input = base.mesh();
    const auto& output = candidate.mesh();
    require(output.m_triangles.size() < input.m_triangles.size() &&
                output.m_positions.size() < input.m_positions.size() && !input.m_uv.empty() &&
                !output.m_uv.empty(),
            "Native LOD must reduce textured geometry",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto& vertices = doc.at("vertex_sources");
    const auto& faces = doc.at("faces");
    array(vertices, 8192uz);
    array(faces, 8192uz);
    require(vertices.size() == output.m_positions.size() &&
                faces.size() == output.m_triangles.size(),
            "Native LOD certificate count differs");
    std::vector<std::uint32_t> remap;
    std::set<std::uint32_t> retained;
    for (std::size_t vertex { 0uz }; vertex < vertices.size(); ++vertex) {
        check_stop(stop);
        const auto K_SOURCE = index(vertices[vertex], input.m_positions.size());
        require(retained.insert(K_SOURCE).second &&
                    output.m_positions[vertex] == input.m_positions[K_SOURCE] &&
                    output.m_normals[vertex] == input.m_normals[K_SOURCE] &&
                    output.m_uv[vertex] == input.m_uv[K_SOURCE] &&
                    output.m_tangents[vertex] == input.m_tangents[K_SOURCE],
                "Native LOD retained vertex attributes differ");
        remap.push_back(K_SOURCE);
    }
    std::vector<std::vector<std::uint32_t>> incidence(input.m_positions.size());
    for (std::size_t face { 0uz }; face < input.m_triangles.size(); ++face) {
        for (auto vertex : input.m_triangles[face]) {
            incidence[vertex].push_back(static_cast<std::uint32_t>(face));
        }
    }
    std::vector<bool> covered(input.m_triangles.size());
    std::vector<std::vector<std::uint32_t>> mapping;
    for (std::size_t face { 0uz }; face < faces.size(); ++face) {
        check_stop(stop);
        const auto& record = faces[face];
        keys(record, { "source_faces", "center", "weights" });
        const auto& sources = record.at("source_faces");
        array(sources, 3uz);
        require(sources.size() == 1uz || sources.size() == 3uz,
                "Native LOD face requires identity or three-face fan");
        std::vector<std::uint32_t> source_faces;
        const auto& support = candidate.support()[face];
        for (const auto& item : sources) {
            const auto K_SOURCE = index(item, input.m_triangles.size());
            require(!covered[K_SOURCE], "Native LOD source face covered twice");
            covered[K_SOURCE] = true;
            source_faces.push_back(K_SOURCE);
            const auto& original = base.support()[K_SOURCE];
            require(input.m_groups[K_SOURCE] == output.m_groups[face] &&
                        original.m_class == support.m_class &&
                        original.m_views == support.m_views &&
                        original.m_uncertainty_units == support.m_uncertainty_units,
                    "Native LOD material or evidence differs");
        }
        require(std::ranges::is_sorted(source_faces), "Native LOD source faces not canonical");
        const auto& triangle = output.m_triangles[face];
        const std::array K_RING { remap[triangle[0uz]], remap[triangle[1uz]], remap[triangle[2uz]] };
        if (source_faces.size() == 1uz) {
            require(record.at("center").is_null() && record.at("weights").is_null() &&
                        cyclic(input.m_triangles[source_faces[0uz]], K_RING),
                    "Native LOD identity face differs");
        } else {
            const auto K_CENTER = index(record.at("center"), input.m_positions.size());
            require(!retained.contains(K_CENTER) && incidence[K_CENTER] == source_faces,
                    "Native LOD fan is not the complete removed interior star");
            const auto& weights = record.at("weights");
            array(weights, 3uz);
            require(weights.size() == 3uz, "Native LOD weights missing");
            std::array<std::uint32_t, 3> coefficients {};
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                coefficients[corner] = index(weights[corner], 256uz);
                require(coefficients[corner] > 0, "Native LOD center is not strictly interior");
                const std::array K_FAN { K_RING[corner], K_RING[(corner + 1uz) % 3uz], K_CENTER };
                require(std::ranges::any_of(source_faces,
                                            [&](auto source) {
                                                return cyclic(input.m_triangles[source], K_FAN);
                                            }),
                        "Native LOD fan orientation or boundary differs");
            }
            require(coefficients[0uz] + coefficients[1uz] + coefficients[2uz] == 256,
                    "Native LOD barycentric weights do not sum to one");
            attributes(input, K_CENTER, K_RING, coefficients);
        }
        mapping.push_back(std::move(source_faces));
    }
    require(std::ranges::all_of(covered,
                                [](bool value) {
                                    return value;
                                }),
            "Native LOD omits source faces");
    return mapping;
}

}  // namespace

struct AdmittedNativeLod::Implementation {
    NativeLodSelection m_selection;
    AdmittedGeometryProduct m_base, m_candidate;
    std::vector<std::vector<std::uint32_t>> m_faces;
    std::string m_receipt;
};

AdmittedNativeLod::AdmittedNativeLod(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const NativeLodSelection& AdmittedNativeLod::selection() const {
    return m_state->m_selection;
}

const AdmittedGeometryProduct& AdmittedNativeLod::base() const {
    return m_state->m_base;
}

const AdmittedGeometryProduct& AdmittedNativeLod::candidate() const {
    return m_state->m_candidate;
}

const std::vector<std::vector<std::uint32_t>>& AdmittedNativeLod::source_faces() const {
    return m_state->m_faces;
}

const std::string& AdmittedNativeLod::receipt_sha256() const {
    return m_state->m_receipt;
}

Result<AdmittedNativeLod> inspect_native_lod(const NativeLodRequest& request,
                                             const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_base || !request.m_candidate || !request.m_receipt) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Native LOD requires independent selection, base, candidate and receipt");
        }
        const auto& selected = request.m_selection.value();
        const auto& base = request.m_base.value();
        const auto& candidate = request.m_candidate.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        require(std::isfinite(selected.m_max_deviation_units) &&
                    selected.m_max_deviation_units == 0,
                "Native exact LOD requires an independently selected zero error ceiling",
                ErrorCode::UNSUPPORTED_OPERATION);
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        for (const auto& geometry : { base, candidate }) {
            require(geometry.receipt_sha256() == receipt.receipt_sha256() &&
                        geometry.context().m_world_epoch == expected.m_world_epoch &&
                        geometry.context().m_pose_revision == expected.m_pose_revision,
                    "Native LOD geometry receipt/context differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        require(base.selection().m_source == candidate.selection().m_source &&
                    base.selection().m_native == candidate.selection().m_native &&
                    base.selection().m_views == candidate.selection().m_views &&
                    base.capture_product() == candidate.capture_product() &&
                    base.closed_surface() == candidate.closed_surface() &&
                    base.manual_ancestor() == candidate.manual_ancestor(),
                "Native LOD source, pose, capture or ancestry differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_NODE = std::ranges::find(
            receipt.graph().m_products, selected.m_product, &AssetDependencyNode::m_product);
        require(K_NODE != receipt.graph().m_products.end(),
                "Native LOD binding not in receipt",
                ErrorCode::RESYNC_REQUIRED);
        auto parents = K_NODE->m_dependencies;
        std::vector wanted { base.selection().m_product, candidate.selection().m_product };
        std::ranges::sort(parents);
        std::ranges::sort(wanted);
        require(parents == wanted, "Native LOD exact dependencies differ");
        const auto K_DOC = parse(
            take(receipt.copy_blob(selected.m_product.m_content_sha256, 1024uz * 1024uz, stop)),
            1024uz * 1024uz,
            stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "base",
              "candidate",
              "max_deviation_units",
              "vertex_sources",
              "faces",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1 &&
                    K_DOC.at("kind").get<std::string>() == "NATIVE_EXACT_FAN_LOD",
                "Unsupported native LOD schema/profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_DOC.at("recipe_sha256")) == K_NODE->m_recipe_sha256 &&
                    reference(K_DOC.at("base")) == base.selection().m_product &&
                    reference(K_DOC.at("candidate")) == candidate.selection().m_product,
                "Native LOD exact root binding differs",
                ErrorCode::RESYNC_REQUIRED);
        require(number(K_DOC.at("max_deviation_units")) == selected.m_max_deviation_units &&
                    K_DOC.at("manual_ancestor").is_boolean() &&
                    K_DOC.at("manual_ancestor").get<bool>() == K_NODE->m_manual_ancestor &&
                    K_NODE->m_manual_ancestor == base.manual_ancestor(),
                "Native LOD error policy or manual ancestry differs");
        auto faces = replay(K_DOC, base, candidate, stop);
        check_stop(stop);
        return AdmittedNativeLod(std::make_shared<const AdmittedNativeLod::Implementation>(
            AdmittedNativeLod::Implementation {
                selected, base, candidate, std::move(faces), receipt.receipt_sha256() }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native LOD allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
