#include "world_explorer/application/structure_graph.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_BYTES = 256uz * 1024uz;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return *result;
}

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_NAME = value.get<std::string>();
    for (std::size_t index { 0uz }; index < N; ++index) {
        if (K_NAME == names[index]) {
            return static_cast<Enum>(index);
        }
    }
    throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported structure enum");
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

double bound(const Json& value, double maximum) {
    const auto K_VALUE = number(value);
    require(K_VALUE >= 0 && K_VALUE <= maximum, "Structure residual policy exceeded");
    return K_VALUE;
}

std::uint32_t index(const Json& value, std::size_t count) {
    const auto K_INDEX = integer(value);
    require(K_INDEX < count, "Structure mesh index outside geometry");
    return static_cast<std::uint32_t>(K_INDEX);
}

using Edge = std::pair<std::uint32_t, std::uint32_t>;
using Point = std::array<double, 2>;

Point project(Vec3d point, Vec3d normal) {
    if (std::abs(normal.m_x) >= std::abs(normal.m_y) &&
        std::abs(normal.m_x) >= std::abs(normal.m_z)) {
        return { point.m_y * (normal.m_x >= 0 ? 1 : -1), point.m_z };
    }
    if (std::abs(normal.m_y) >= std::abs(normal.m_z)) {
        return { point.m_z * (normal.m_y >= 0 ? 1 : -1), point.m_x };
    }
    return { point.m_x * (normal.m_z >= 0 ? 1 : -1), point.m_y };
}

double turn(const Point& origin, const Point& first, const Point& second) {
    return (first[0uz] - origin[0uz]) * (second[1uz] - origin[1uz]) -
           (first[1uz] - origin[1uz]) * (second[0uz] - origin[0uz]);
}

bool on_segment(const Point& point, const Point& first, const Point& second) {
    return turn(first, second, point) == 0 && point[0uz] >= std::min(first[0uz], second[0uz]) &&
           point[0uz] <= std::max(first[0uz], second[0uz]) &&
           point[1uz] >= std::min(first[1uz], second[1uz]) &&
           point[1uz] <= std::max(first[1uz], second[1uz]);
}

bool crosses(const std::array<Point, 2>& first, const std::array<Point, 2>& second) {
    const auto K_A = turn(first[0uz], first[1uz], second[0uz]);
    const auto K_B = turn(first[0uz], first[1uz], second[1uz]);
    const auto K_C = turn(second[0uz], second[1uz], first[0uz]);
    const auto K_D = turn(second[0uz], second[1uz], first[1uz]);
    const bool K_STRADDLES = ((K_A < 0 && K_B > 0) || (K_A > 0 && K_B < 0)) &&
                             ((K_C < 0 && K_D > 0) || (K_C > 0 && K_D < 0));
    return K_STRADDLES || on_segment(second[0uz], first[0uz], first[1uz]) ||
           on_segment(second[1uz], first[0uz], first[1uz]) ||
           on_segment(first[0uz], second[0uz], second[1uz]) ||
           on_segment(first[1uz], second[0uz], second[1uz]);
}

bool inside(const Point& point, const std::vector<Point>& loop) {
    int winding {};
    for (std::size_t edge { 0uz }; edge < loop.size(); ++edge) {
        const auto& first = loop[edge];
        const auto& second = loop[(edge + 1uz) % loop.size()];
        if (on_segment(point, first, second)) {
            return false;
        }
        if (first[1uz] <= point[1uz] && second[1uz] > point[1uz] &&
            turn(first, second, point) > 0) {
            ++winding;
        }
        if (first[1uz] > point[1uz] && second[1uz] <= point[1uz] &&
            turn(first, second, point) < 0) {
            --winding;
        }
    }
    return winding != 0;
}

void planar_loops(const StructureSurface& surface,
                  const GeometryRuntimeMesh& mesh,
                  const std::stop_token& stop) {
    if (!surface.m_plane) {
        return;
    }
    std::vector<std::vector<Point>> loops;
    for (const auto& loop : surface.m_loops) {
        std::vector<Point> points;
        points.reserve(loop.size());
        for (auto vertex : loop) {
            points.push_back(project(mesh.m_positions[vertex], surface.m_plane->m_normal));
        }
        double area {};
        for (std::size_t edge { 0uz }; edge < points.size(); ++edge) {
            const auto& first = points[edge];
            const auto& second = points[(edge + 1uz) % points.size()];
            require(first != second, "Collapsed projected structure edge");
            const auto& previous = points[(edge + points.size() - 1uz) % points.size()];
            require(turn(previous, first, second) != 0 ||
                        (previous[0uz] - first[0uz]) * (second[0uz] - first[0uz]) +
                                (previous[1uz] - first[1uz]) * (second[1uz] - first[1uz]) <
                            0,
                    "Backtracking projected structure boundary");
            area += first[0uz] * second[1uz] - first[1uz] * second[0uz];
        }
        require(loops.empty() ? area > 0 : area < 0, "Structure outer/hole orientation differs");
        loops.push_back(std::move(points));
    }
    for (std::size_t first { 0uz }; first < loops.size(); ++first) {
        for (std::size_t edge { 0uz }; edge < loops[first].size(); ++edge) {
            check_stop(stop);
            const auto K_NEXT = (edge + 1uz) % loops[first].size();
            for (std::size_t second { first }; second < loops.size(); ++second) {
                for (std::size_t other { second == first ? edge + 1uz : 0uz };
                     other < loops[second].size();
                     ++other) {
                    const auto K_OTHER_NEXT = (other + 1uz) % loops[second].size();
                    if (first == second && (other == K_NEXT || K_OTHER_NEXT == edge)) {
                        continue;
                    }
                    require(!crosses({ loops[first][edge], loops[first][K_NEXT] },
                                     { loops[second][other], loops[second][K_OTHER_NEXT] }),
                            "Intersecting or touching structure loops");
                }
            }
        }
        if (first > 0uz) {
            require(inside(loops[first][0uz], loops[0uz]),
                    "Structure opening lies outside outer boundary");
            for (std::size_t other { 1uz }; other < first; ++other) {
                require(!inside(loops[first][0uz], loops[other]) &&
                            !inside(loops[other][0uz], loops[first]),
                        "Nested structure openings");
            }
        }
    }
}

struct PatchTopology {
    std::set<std::uint32_t> m_vertices;
    std::set<Edge> m_boundary;
};

PatchTopology topology(const StructureSurface& surface, const GeometryRuntimeMesh& mesh) {
    std::map<Edge, std::vector<std::uint32_t>> edges;
    PatchTopology result;
    for (auto face : surface.m_faces) {
        const auto& triangle = mesh.m_triangles[face];
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            const auto K_A = triangle[corner], K_B = triangle[(corner + 1uz) % 3uz];
            result.m_vertices.insert(K_A);
            edges[std::minmax(K_A, K_B)].push_back(face);
        }
    }
    std::map<std::uint32_t, std::vector<std::uint32_t>> neighbors;
    for (const auto& [K_EDGE, K_FACES] : edges) {
        if (K_FACES.size() == 1uz) {
            const auto& triangle = mesh.m_triangles[K_FACES[0uz]];
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                const Edge K_DIRECTED { triangle[corner], triangle[(corner + 1uz) % 3uz] };
                if (std::minmax(K_DIRECTED.first, K_DIRECTED.second) ==
                    std::minmax(K_EDGE.first, K_EDGE.second)) {
                    result.m_boundary.insert(K_DIRECTED);
                }
            }
        } else {
            require(K_FACES.size() == 2uz, "Nonmanifold structure patch");
            neighbors[K_FACES[0uz]].push_back(K_FACES[1uz]);
            neighbors[K_FACES[1uz]].push_back(K_FACES[0uz]);
        }
    }
    std::set<std::uint32_t> reached;
    std::vector<std::uint32_t> pending { surface.m_faces[0uz] };
    while (!pending.empty()) {
        const auto K_FACE = pending.back();
        pending.pop_back();
        if (reached.insert(K_FACE).second) {
            for (auto neighbor : neighbors[K_FACE]) {
                pending.push_back(neighbor);
            }
        }
    }
    require(reached.size() == surface.m_faces.size(), "Disconnected structure surface");
    return result;
}

StructureSurface surface(const Json& doc,
                         const AdmittedGeometryProduct& geometry,
                         const StructureGraphSelection& selection,
                         std::set<std::uint32_t>& used_faces,
                         std::size_t& boundary_budget,
                         const std::stop_token& stop) {
    keys(doc, { "id", "kind", "patch", "faces", "loops", "plane", "thickness", "coverage" });
    const auto& mesh = geometry.mesh();
    StructureSurface result {};
    result.m_id = { integer(doc.at("id")) };
    require(result.m_id.m_value != 0, "Zero structure surface ID");
    result.m_kind = named<StructureSurfaceKind>(doc.at("kind"),
                                                std::array { "WALL", "FLOOR", "CEILING", "OTHER" });
    array(doc.at("faces"), 8192uz);
    require(!doc.at("faces").empty(), "Empty structure patch");
    for (const auto& row : doc.at("faces")) {
        check_stop(stop);
        const auto K_FACE = index(row, mesh.m_triangles.size());
        require(used_faces.insert(K_FACE).second, "Duplicated structure face assignment");
        result.m_faces.push_back(K_FACE);
        ++result.m_coverage.at(static_cast<std::size_t>(geometry.support()[K_FACE].m_class));
    }
    array(doc.at("coverage"), 5uz);
    require(doc.at("coverage").size() == 5uz, "Incomplete structure evidence coverage");
    for (std::size_t kind { 0uz }; kind < 5uz; ++kind) {
        require(integer(doc.at("coverage")[kind]) == result.m_coverage[kind],
                "Structure coverage differs from native face support");
    }
    const auto K_TOPOLOGY = topology(result, mesh);
    for (auto vertex : K_TOPOLOGY.m_vertices) {
        const auto K_POSITION = mesh.m_positions[vertex];
        require(std::max({ std::abs(K_POSITION.m_x),
                          std::abs(K_POSITION.m_y),
                          std::abs(K_POSITION.m_z) }) <= 1e6,
                "Structure local coordinate budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
    }
    array(doc.at("loops"), 16uz);
    require(!doc.at("loops").empty(), "Structure patch requires an open boundary");
    std::set<Edge> actual;
    std::set<std::uint32_t> boundary_vertices;
    for (const auto& row : doc.at("loops")) {
        array(row, 1024uz);
        require(row.size() >= 3uz && row.size() <= 1024uz - boundary_budget,
                "Structure boundary budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        boundary_budget += row.size();
        std::vector<std::uint32_t> loop;
        loop.reserve(row.size());
        for (const auto& value : row) {
            const auto K_VERTEX = index(value, mesh.m_positions.size());
            require(boundary_vertices.insert(K_VERTEX).second,
                    "Repeated structure boundary vertex");
            loop.push_back(K_VERTEX);
        }
        for (std::size_t edge { 0uz }; edge < loop.size(); ++edge) {
            require(actual.emplace(loop[edge], loop[(edge + 1uz) % loop.size()]).second,
                    "Repeated structure boundary edge");
        }
        result.m_loops.push_back(std::move(loop));
    }
    require(actual == K_TOPOLOGY.m_boundary,
            "Structure loops do not equal complete oriented mesh boundary");
    const auto K_PATCH = doc.at("patch").get<std::string>();
    if (K_PATCH == "PLANE") {
        const auto& plane = doc.at("plane");
        keys(plane, { "normal", "offset_units", "residual_bound_units" });
        StructurePlane parsed {
            vector3(plane.at("normal")),
            number(plane.at("offset_units")),
            bound(plane.at("residual_bound_units"), selection.m_max_plane_residual_units),
            0 };
        require(std::abs(dot(parsed.m_normal, parsed.m_normal) - 1) <= 1e-8 &&
                    std::abs(parsed.m_offset_units) <= 1e6,
                "Invalid structure plane");
        for (auto vertex : K_TOPOLOGY.m_vertices) {
            parsed.m_actual_residual_units = std::max(
                parsed.m_actual_residual_units,
                std::abs(dot(parsed.m_normal, mesh.m_positions[vertex]) + parsed.m_offset_units));
        }
        require(parsed.m_actual_residual_units <= parsed.m_residual_bound_units,
                "Structure plane understates native residual");
        for (auto face : result.m_faces) {
            const auto& triangle = mesh.m_triangles[face];
            require(dot(cross(mesh.m_positions[triangle[1uz]] - mesh.m_positions[triangle[0uz]],
                              mesh.m_positions[triangle[2uz]] - mesh.m_positions[triangle[0uz]]),
                        parsed.m_normal) > 0,
                    "Structure plane orientation differs from native winding");
        }
        result.m_plane = parsed;
        planar_loops(result, mesh, stop);
    } else {
        require(K_PATCH == "MESH" && doc.at("plane").is_null(),
                "Unsupported structure patch profile",
                ErrorCode::UNSUPPORTED_OPERATION);
    }
    const auto& thickness = doc.at("thickness");
    keys(thickness, { "source", "value_units" });
    result.m_thickness_source = named<StructureThicknessSource>(
        thickness.at("source"), std::array { "UNKNOWN", "DECLARED_INFERRED", "DECLARED_GENERATED" });
    if (result.m_thickness_source == StructureThicknessSource::UNKNOWN) {
        require(thickness.at("value_units").is_null(), "Unknown thickness must remain absent");
    } else {
        result.m_thickness_units = bound(thickness.at("value_units"), 1e6);
        require(result.m_thickness_units.value() > 0, "Nonpositive declared thickness");
    }
    std::ranges::sort(result.m_faces);
    return result;
}

std::string key(const AssetRevisionRef& ref) {
    return "structure-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

struct SpatialRecords {
    std::vector<StructureSurface> m_surfaces;
    std::vector<StructureOpening> m_openings;
    std::vector<StructureRoom> m_rooms;
    std::vector<StructureRelation> m_relations;
};

const StructureSurface& find_surface(const std::vector<StructureSurface>& surfaces,
                                     StructureSurfaceId id) {
    const auto K_FOUND = std::ranges::find(surfaces, id, &StructureSurface::m_id);
    require(K_FOUND != surfaces.end(), "Unknown structure surface reference");
    return *K_FOUND;
}

std::set<std::uint32_t> vertices(const StructureSurface& surface, const GeometryRuntimeMesh& mesh) {
    std::set<std::uint32_t> result;
    for (auto face : surface.m_faces) {
        result.insert(mesh.m_triangles[face].begin(), mesh.m_triangles[face].end());
    }
    return result;
}

std::vector<StructureRoom> rooms(const Json& rows,
                                 const std::vector<StructureSurface>& surfaces,
                                 const GeometryRuntimeMesh& mesh,
                                 double maximum) {
    array(rows, 32uz);
    std::set<StructureRoomId> ids;
    std::map<StructureSurfaceId, std::size_t> memberships;
    std::vector<StructureRoom> result;
    for (const auto& row : rows) {
        keys(row, { "id", "boundaries", "contains" });
        StructureRoom room { { integer(row.at("id")) }, {}, {} };
        require(room.m_id.m_value && ids.insert(room.m_id).second,
                "Zero/duplicate structure room ID");
        array(row.at("boundaries"), 32uz);
        require(!row.at("boundaries").empty(), "Empty room boundary selection");
        std::set<StructureSurfaceId> seen;
        Vec3d minimum { 1e6, 1e6, 1e6 }, maximum_point { -1e6, -1e6, -1e6 };
        for (const auto& id : row.at("boundaries")) {
            const StructureSurfaceId K_ID { integer(id) };
            require(seen.insert(K_ID).second && ++memberships[K_ID] <= 2uz,
                    "Repeated or nonmanifold room boundary membership");
            for (auto vertex : vertices(find_surface(surfaces, K_ID), mesh)) {
                const auto K_POINT = mesh.m_positions[vertex];
                minimum = { std::min(minimum.m_x, K_POINT.m_x),
                           std::min(minimum.m_y, K_POINT.m_y),
                           std::min(minimum.m_z, K_POINT.m_z) };
                maximum_point = { std::max(maximum_point.m_x, K_POINT.m_x),
                                 std::max(maximum_point.m_y, K_POINT.m_y),
                                 std::max(maximum_point.m_z, K_POINT.m_z) };
            }
            room.m_boundaries.push_back(K_ID);
        }
        array(row.at("contains"), 32uz);
        seen.clear();
        for (const auto& item : row.at("contains")) {
            keys(item, { "surface_id", "residual_bound_units" });
            StructureContainment containment { { integer(item.at("surface_id")) },
                                              bound(item.at("residual_bound_units"), maximum),
                                              0 };
            require(seen.insert(containment.m_surface).second, "Repeated room containment");
            for (auto vertex : vertices(find_surface(surfaces, containment.m_surface), mesh)) {
                const auto K_POINT = mesh.m_positions[vertex];
                const Vec3d K_OUTSIDE {
                    std::max({ minimum.m_x - K_POINT.m_x, K_POINT.m_x - maximum_point.m_x, 0.0 }),
                    std::max({ minimum.m_y - K_POINT.m_y, K_POINT.m_y - maximum_point.m_y, 0.0 }),
                    std::max({ minimum.m_z - K_POINT.m_z, K_POINT.m_z - maximum_point.m_z, 0.0 }) };
                containment.m_actual_residual_units = std::max(
                    containment.m_actual_residual_units, std::sqrt(dot(K_OUTSIDE, K_OUTSIDE)));
            }
            require(containment.m_actual_residual_units <= containment.m_residual_bound_units,
                    "Room AABB containment understates residual");
            room.m_contains.push_back(containment);
        }
        std::ranges::sort(room.m_boundaries);
        std::ranges::sort(room.m_contains, {}, &StructureContainment::m_surface);
        result.push_back(std::move(room));
    }
    std::ranges::sort(result, {}, &StructureRoom::m_id);
    return result;
}

std::vector<StructureOpening> openings(const Json& rows, const SpatialRecords& records) {
    array(rows, 64uz);
    std::set<StructureOpeningId> ids;
    std::set<std::pair<StructureSurfaceId, std::uint32_t>> holes;
    std::vector<StructureOpening> result;
    for (const auto& row : rows) {
        keys(row, { "id", "surface_id", "loop", "rooms", "class", "confidence" });
        StructureOpening opening {};
        opening.m_id = { integer(row.at("id")) };
        opening.m_surface = { integer(row.at("surface_id")) };
        require(opening.m_id.m_value && ids.insert(opening.m_id).second,
                "Zero/duplicate structure opening ID");
        const auto& surface = find_surface(records.m_surfaces, opening.m_surface);
        opening.m_loop = index(row.at("loop"), surface.m_loops.size());
        require(surface.m_plane.has_value() && opening.m_loop &&
                    holes.emplace(opening.m_surface, opening.m_loop).second,
                "Opening must select one distinct planar hole");
        opening.m_class = named<AssetEvidenceClass>(
            row.at("class"),
            std::array { "UNKNOWN", "OBSERVED", "INFERRED", "GENERATED", "MANUALLY_EDITED" });
        require(opening.m_class == AssetEvidenceClass::UNKNOWN ||
                    opening.m_class == AssetEvidenceClass::INFERRED ||
                    opening.m_class == AssetEvidenceClass::GENERATED,
                "Opening void observation/manual evidence is not supported by this profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        if (opening.m_class == AssetEvidenceClass::UNKNOWN) {
            require(row.at("confidence").is_null(),
                    "Unknown opening confidence must remain absent");
        } else {
            opening.m_confidence = bound(row.at("confidence"), 1);
        }
        const auto& links = row.at("rooms");
        array(links, 2uz);
        require(links.size() == 2uz, "Opening needs two sides including explicit exterior null");
        opening.m_first_room = { integer(links[0uz]) };
        if (!links[1uz].is_null()) {
            opening.m_second_room = StructureRoomId { integer(links[1uz]) };
            require(opening.m_second_room.value() != opening.m_first_room,
                    "Opening cannot link room to itself");
        }
        std::set<StructureRoomId> actual;
        for (const auto& room : records.m_rooms) {
            if (std::ranges::find(room.m_boundaries, opening.m_surface) !=
                room.m_boundaries.end()) {
                actual.insert(room.m_id);
            }
        }
        std::set<StructureRoomId> expected { opening.m_first_room };
        if (opening.m_second_room) {
            expected.insert(opening.m_second_room.value());
        }
        require(expected == actual && opening.m_first_room.m_value,
                "Opening adjacency differs from room boundary membership");
        result.push_back(opening);
    }
    for (const auto& surface : records.m_surfaces) {
        if (surface.m_plane) {
            for (std::size_t loop { 1uz }; loop < surface.m_loops.size(); ++loop) {
                require(holes.contains({ surface.m_id, static_cast<std::uint32_t>(loop) }),
                        "Unaccounted planar opening boundary");
            }
        }
    }
    std::ranges::sort(result, {}, &StructureOpening::m_id);
    return result;
}

std::vector<StructureRelation> relations(const Json& rows,
                                         const std::vector<StructureSurface>& surfaces,
                                         const GeometryRuntimeMesh& mesh,
                                         double maximum) {
    array(rows, 128uz);
    std::vector<StructureRelation> result;
    std::set<std::tuple<StructureRelationKind, StructureSurfaceId, StructureSurfaceId>> seen;
    std::map<StructureSurfaceId, std::set<StructureSurfaceId>> supported;
    for (const auto& row : rows) {
        keys(
            row,
            { "kind", "source", "target", "source_vertex", "target_vertex", "residual_bound_units" });
        StructureRelation relation {};
        relation.m_kind =
            named<StructureRelationKind>(row.at("kind"), std::array { "CONTACT", "SUPPORT" });
        relation.m_source = { integer(row.at("source")) };
        relation.m_target = { integer(row.at("target")) };
        require(relation.m_source != relation.m_target &&
                    seen.emplace(relation.m_kind, relation.m_source, relation.m_target).second,
                "Self/duplicate structure relation");
        require(relation.m_kind != StructureRelationKind::CONTACT ||
                    relation.m_source < relation.m_target,
                "Contact relation must use ascending surface IDs");
        relation.m_source_vertex = index(row.at("source_vertex"), mesh.m_positions.size());
        relation.m_target_vertex = index(row.at("target_vertex"), mesh.m_positions.size());
        require(vertices(find_surface(surfaces, relation.m_source), mesh)
                        .contains(relation.m_source_vertex) &&
                    vertices(find_surface(surfaces, relation.m_target), mesh)
                        .contains(relation.m_target_vertex),
                "Relation anchors are outside selected surfaces");
        relation.m_residual_bound_units = bound(row.at("residual_bound_units"), maximum);
        const auto K_GAP =
            mesh.m_positions[relation.m_source_vertex] - mesh.m_positions[relation.m_target_vertex];
        relation.m_actual_residual_units = std::sqrt(dot(K_GAP, K_GAP));
        require(relation.m_actual_residual_units <= relation.m_residual_bound_units,
                "Structure relation understates native anchor gap");
        if (relation.m_kind == StructureRelationKind::SUPPORT) {
            supported[relation.m_source].insert(relation.m_target);
        }
        result.push_back(relation);
    }
    for (const auto& [K_SOURCE, K_TARGETS] : supported) {
        std::set<StructureSurfaceId> reached;
        std::vector<StructureSurfaceId> pending(K_TARGETS.begin(), K_TARGETS.end());
        while (!pending.empty()) {
            const auto K_CURRENT = pending.back();
            pending.pop_back();
            require(K_CURRENT != K_SOURCE, "Cyclic declared support relation");
            if (reached.insert(K_CURRENT).second) {
                const auto K_FOUND = supported.find(K_CURRENT);
                if (K_FOUND != supported.end()) {
                    pending.insert(pending.end(), K_FOUND->second.begin(), K_FOUND->second.end());
                }
            }
        }
    }
    std::ranges::sort(result, {}, [](const auto& value) {
        return std::tuple { value.m_kind, value.m_source, value.m_target };
    });
    return result;
}

}  // namespace

struct AdmittedStructureGraph::Implementation {
    StructureGraphSelection m_selection;
    AssetRevisionRef m_geometry;
    SpatialRecords m_records;
    std::string m_receipt;
    AssetDependencyContext m_context;
    bool m_synthetic {}, m_manual {};
    std::vector<std::byte> m_bytes;
};

AdmittedStructureGraph::AdmittedStructureGraph(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const StructureGraphSelection& AdmittedStructureGraph::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedStructureGraph::geometry_product() const {
    return m_state->m_geometry;
}

const std::vector<StructureSurface>& AdmittedStructureGraph::surfaces() const {
    return m_state->m_records.m_surfaces;
}

const std::vector<StructureOpening>& AdmittedStructureGraph::openings() const {
    return m_state->m_records.m_openings;
}

const std::vector<StructureRoom>& AdmittedStructureGraph::rooms() const {
    return m_state->m_records.m_rooms;
}

const std::vector<StructureRelation>& AdmittedStructureGraph::relations() const {
    return m_state->m_records.m_relations;
}

const std::string& AdmittedStructureGraph::receipt_sha256() const {
    return m_state->m_receipt;
}

const AssetDependencyContext& AdmittedStructureGraph::context() const {
    return m_state->m_context;
}

bool AdmittedStructureGraph::synthetic_source() const {
    return m_state->m_synthetic;
}

bool AdmittedStructureGraph::manual_ancestor() const {
    return m_state->m_manual;
}

Result<AdmittedStructureGraph> inspect_structure_graph(const StructureGraphRequest& request,
                                                       const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_geometry || !request.m_capture ||
            !request.m_receipt) {
            return fail(
                ErrorCode::INVALID_ARGUMENT,
                "Independent structure selection, geometry, capture and receipt are required");
        }
        const auto& selected = request.m_selection.value();
        const auto& geometry = request.m_geometry.value();
        const auto& capture = request.m_capture.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        require(std::isfinite(selected.m_max_plane_residual_units) &&
                    selected.m_max_plane_residual_units >= 0 &&
                    selected.m_max_plane_residual_units <= 1e6 &&
                    std::isfinite(selected.m_max_relation_residual_units) &&
                    selected.m_max_relation_residual_units >= 0 &&
                    selected.m_max_relation_residual_units <= 1e6,
                "Invalid independent structure residual policy",
                ErrorCode::INVALID_ARGUMENT);
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        require(geometry.receipt_sha256() == receipt.receipt_sha256() &&
                    capture.receipt_sha256() == receipt.receipt_sha256() &&
                    geometry.context().m_world_epoch == expected.m_world_epoch &&
                    geometry.context().m_pose_revision == expected.m_pose_revision &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision &&
                    geometry.capture_product() == capture.record().m_selection.m_product,
                "Structure geometry/capture/receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        (void)take(
            capture.validate_inference_dependency(receipt, selected.m_product, expected, stop));
        const auto K_NODE = std::ranges::find(
            receipt.graph().m_products, selected.m_product, &AssetDependencyNode::m_product);
        require(K_NODE != receipt.graph().m_products.end(),
                "Structure product differs from original receipt",
                ErrorCode::RESYNC_REQUIRED);
        require(K_NODE->m_dependencies ==
                    std::vector<AssetRevisionRef> { geometry.selection().m_product },
                "Structure must depend exactly on selected geometry");
        auto bytes = take(receipt.copy_blob(selected.m_product.m_content_sha256, K_BYTES, stop));
        const auto K_DOC = parse(bytes, K_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "geometry",
              "capture",
              "frame",
              "scope",
              "surfaces",
              "openings",
              "rooms",
              "relations",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1 &&
                    K_DOC.at("kind").get<std::string>() == "STRUCTURE_GRAPH" &&
                    K_DOC.at("scope").get<std::string>() == "PARTIAL_PATCHES" &&
                    K_DOC.at("frame").get<std::string>() == "GEOMETRY_LOCAL_ARBITRARY_UNITS",
                "Unsupported structure graph profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_DOC.at("recipe_sha256")) == K_NODE->m_recipe_sha256 &&
                    reference(K_DOC.at("geometry")) == geometry.selection().m_product &&
                    reference(K_DOC.at("capture")) == geometry.capture_product(),
                "Structure root pins differ",
                ErrorCode::RESYNC_REQUIRED);
        require(K_DOC.at("manual_ancestor").is_boolean() &&
                    K_DOC.at("manual_ancestor").get<bool>() == K_NODE->m_manual_ancestor,
                "Structure manual ancestry differs");
        array(K_DOC.at("surfaces"), 32uz);
        require(!K_DOC.at("surfaces").empty(), "Structure requires at least one surface");
        SpatialRecords records;
        std::set<StructureSurfaceId> ids;
        std::set<std::uint32_t> used_faces;
        std::size_t boundary_budget {};
        for (const auto& row : K_DOC.at("surfaces")) {
            auto parsed = surface(row, geometry, selected, used_faces, boundary_budget, stop);
            require(ids.insert(parsed.m_id).second, "Duplicate structure surface ID");
            records.m_surfaces.push_back(std::move(parsed));
        }
        std::ranges::sort(records.m_surfaces, {}, &StructureSurface::m_id);
        records.m_rooms = rooms(K_DOC.at("rooms"),
                                records.m_surfaces,
                                geometry.mesh(),
                                selected.m_max_relation_residual_units);
        records.m_openings = openings(K_DOC.at("openings"), records);
        records.m_relations = relations(K_DOC.at("relations"),
                                        records.m_surfaces,
                                        geometry.mesh(),
                                        selected.m_max_relation_residual_units);
        check_stop(stop);
        return AdmittedStructureGraph(
            std::make_shared<const AdmittedStructureGraph::Implementation>(
                AdmittedStructureGraph::Implementation { selected,
                                                        geometry.selection().m_product,
                                                        std::move(records),
                                                        receipt.receipt_sha256(),
                                                        expected,
                                                        geometry.synthetic_source(),
                                                        K_NODE->m_manual_ancestor,
                                                        std::move(bytes) }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Structure allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_structure_graph(const AdmittedStructureGraph& structure,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop,
                                             const std::function<void(std::size_t)>& observed) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        return std::unexpected(K_GRAPH.error());
    }
    if (structure.receipt_sha256() != receipt.receipt_sha256() ||
        structure.context().m_world_epoch != expected.m_world_epoch ||
        structure.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Structure publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { key(structure.selection().m_product), structure.m_state->m_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Structure publication allocation failed");
    }
}

Result<AdmittedStructureGraph> read_structure_graph(const std::filesystem::path& directory,
                                                    const RecordCommit& commit,
                                                    const StructureGraphRequest& request,
                                                    const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(commit.m_manifest_sha256);
        auto structure = take(inspect_structure_graph(request, stop));
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && K_SNAPSHOT.m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Structure selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto& ref = structure.selection().m_product;
        const auto K_FOUND = K_SNAPSHOT.m_records.find(key(ref));
        require(K_FOUND != K_SNAPSHOT.m_records.end() &&
                    sha256(K_FOUND->second) == ref.m_content_sha256,
                "Selected structure is not committed",
                ErrorCode::RESYNC_REQUIRED);
        return structure;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Structure reload allocation failed");
    }
}

}  // namespace world_explorer
