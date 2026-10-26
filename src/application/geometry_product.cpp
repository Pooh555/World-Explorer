#include "world_explorer/application/geometry_product.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_BYTES = 4uz * 1024uz * 1024uz;
constexpr auto K_ELEMENTS = 8192uz;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return *result;
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

ObjectViewSelection view(const Json& value) {
    keys(
        value,
        { "capture_id", "frame_id", "calibration_id", "frame_pose_revision", "image", "pose_asset" });
    return { { integer(value.at("capture_id")) },
            { integer(value.at("frame_id")) },
            { integer(value.at("calibration_id")) },
            { integer(value.at("frame_pose_revision")) },
            reference(value.at("image")),
            reference(value.at("pose_asset")) };
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    require(ref.m_product.m_value && ref.m_revision, "Unselected geometry product");
    const auto K_FOUND =
        std::ranges::find(receipt.graph().m_products, ref, &AssetDependencyNode::m_product);
    require(K_FOUND != receipt.graph().m_products.end(),
            "Geometry product differs from selected receipt",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

void parents(const AssetDependencyNode& node,
             const std::map<AssetProductId, AssetRevisionRef>& selected) {
    auto actual = node.m_dependencies;
    std::vector<AssetRevisionRef> wanted;
    wanted.reserve(selected.size());
    for (const auto& [K_ID, K_REF] : selected) {
        wanted.push_back(K_REF);
    }
    std::ranges::sort(actual);
    std::ranges::sort(wanted);
    require(actual == wanted, "Geometry dependency set differs");
}

void header(const Json& value,
            const AssetDependencyNode& node,
            const AssetDependencyContext& expected) {
    require(integer(value.at("schema_version")) == 1,
            "Unsupported geometry schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(value.at("recipe_sha256")) == node.m_recipe_sha256,
            "Geometry context/recipe differs",
            ErrorCode::RESYNC_REQUIRED);
    require(value.at("manual_ancestor").is_boolean() &&
                value.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Geometry manual ancestry differs");
}

Vec3d position(const FusionVertex& value) {
    return { value.m_position[0uz], value.m_position[1uz], value.m_position[2uz] };
}

const SceneChunk& source_chunk(const GeometryProductRequest& request,
                               const GeometryNativeSource& selected,
                               const std::stop_token& stop) {
    const auto& source = *request.m_source;
    digest_text(selected.m_scene_sha256);
    digest_text(selected.m_chunk_sha256);
    require(selected.m_scene.m_value && selected.m_epoch.m_value && selected.m_revision.m_value &&
                selected.m_chunk.m_value && selected.m_generation,
            "Unselected native geometry identity");
    require(source.m_id == selected.m_scene && source.m_epoch == selected.m_epoch &&
                source.m_revision == selected.m_revision &&
                source.m_epoch == request.m_expected.m_world_epoch &&
                source.m_scale == ScaleStatus::ARBITRARY && !source.m_world_frame,
            "Geometry native scene identity/units differ",
            ErrorCode::RESYNC_REQUIRED);
    const SceneLimits K_LIMITS { 64uz, 0uz, 32uz * 1024uz * 1024uz, 4096uz, 262144uz, 524288uz, 0uz };
    require(source.m_provenance.size() <= 32uz,
            "Geometry source provenance budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    for (const auto& [K_NAME, K_VALUE] : source.m_provenance) {
        require(K_NAME.size() <= 4096uz && K_VALUE.size() <= 4096uz,
                "Geometry source provenance text exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
    }
    const auto K_KIND = source.m_provenance.find("source_kind");
    require(K_KIND != source.m_provenance.end() && (K_KIND->second == "synthetic_diagnostic" ||
                                                    K_KIND->second == "partial_observed_geometry"),
            "Geometry source classification is missing or unsupported");
    require(source.m_chunks.size() <= K_LIMITS.m_max_chunks,
            "Geometry source chunk budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::size_t vertices {}, faces {}, metadata {};
    for (const auto& [K_ID, K_CHUNK] : source.m_chunks) {
        check_stop(stop);
        if (!K_CHUNK || !K_CHUNK->m_mesh || !K_CHUNK->m_splats.empty() ||
            K_CHUNK->m_mesh->m_baked) {
            throw RecordFailure(ErrorCode::INVALID_DATA,
                                "Geometry source requires untextured native meshes");
        }
        const auto& mesh = *K_CHUNK->m_mesh;
        require(mesh.m_vertices.size() <= K_LIMITS.m_max_mesh_vertices - vertices &&
                    mesh.m_triangles.size() <= K_LIMITS.m_max_mesh_triangles - faces,
                "Geometry native array budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        vertices += mesh.m_vertices.size();
        faces += mesh.m_triangles.size();
        if (mesh.m_observed) {
            const auto& observed = *mesh.m_observed;
            for (std::string_view text : std::array<std::string_view, 3> {
                     observed.m_manifest_json, observed.m_receipt_json, observed.m_cleanup_json }) {
                require(text.size() <= 4uz * 1024uz * 1024uz - metadata,
                        "Geometry native metadata budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                metadata += text.size();
            }
            for (std::string_view hash :
                 std::array<std::string_view, 5> { observed.m_context.m_manifest_sha256,
                                                  observed.m_context.m_receipt_sha256,
                                                  observed.m_context.m_recipe_sha256,
                                                  observed.m_context.m_pose_sha256,
                                                  observed.m_asset_sha256 }) {
                require(hash.size() == 64uz &&
                            hash.find_first_not_of("0123456789abcdef") == hash.npos,
                        "Invalid geometry source digest");
            }
            require(observed.m_cleanup_sha256.empty() || observed.m_cleanup_sha256.size() == 64uz,
                    "Geometry cleanup digest budget exceeded");
        }
    }
    (void)take(validate_scene(source, K_LIMITS));
    check_stop(stop);
    require(scene_content_hash(source) == selected.m_scene_sha256,
            "Geometry scene bytes differ",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_FOUND = source.m_chunks.find(selected.m_chunk);
    require(K_FOUND != source.m_chunks.end() &&
                K_FOUND->second->m_generation == selected.m_generation &&
                K_FOUND->second->m_geometry_valid &&
                K_FOUND->second->m_pose_revision.m_value ==
                    request.m_expected.m_pose_revision.m_value &&
                scene_chunk_content_hash(*K_FOUND->second) == selected.m_chunk_sha256,
            "Geometry chunk revision/bytes differ",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND->second;
}

GeometryNativeSource native(const Json& value) {
    keys(value,
         { "scene_id",
          "world_epoch",
          "scene_revision",
          "scene_sha256",
          "chunk_id",
          "generation",
          "chunk_sha256" });
    return { { integer(value.at("scene_id")) },
            { integer(value.at("world_epoch")) },
            { integer(value.at("scene_revision")) },
            digest(value.at("scene_sha256")),
            { integer(value.at("chunk_id")) },
            integer(value.at("generation")),
            digest(value.at("chunk_sha256")) };
}

// Edge pairing plus connected vertex fans reject nonmanifold edges and bow ties.
void topology(const GeometryRuntimeMesh& mesh, bool closed, const std::stop_token& stop) {
    using Edge = std::pair<std::uint32_t, std::uint32_t>;

    struct Use {
        std::size_t m_face;
        bool m_forward;
    };

    std::map<Edge, std::vector<Use>> edges;
    std::vector<std::vector<std::size_t>> incident(mesh.m_positions.size());
    for (std::size_t index { 0uz }; index < mesh.m_triangles.size(); ++index) {
        check_stop(stop);
        const auto& face = mesh.m_triangles[index];
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            incident[face[corner]].push_back(index);
            const auto K_A = face[corner];
            const auto K_B = face[(corner + 1uz) % 3uz];
            auto& uses = edges[{ std::min(K_A, K_B), std::max(K_A, K_B) }];
            uses.push_back({ index, K_A < K_B });
            require(uses.size() <= 2uz &&
                        (uses.size() != 2uz || uses[0uz].m_forward != uses[1uz].m_forward),
                    "Nonmanifold or inconsistently wound geometry edge");
        }
    }
    std::vector<std::map<std::size_t, std::vector<std::size_t>>> neighbors(mesh.m_positions.size());
    for (const auto& [K_EDGE, K_USES] : edges) {
        require(!closed || K_USES.size() == 2uz, "Declared closed geometry has a boundary");
        if (K_USES.size() == 2uz) {
            for (auto vertex : { K_EDGE.first, K_EDGE.second }) {
                neighbors[vertex][K_USES[0uz].m_face].push_back(K_USES[1uz].m_face);
                neighbors[vertex][K_USES[1uz].m_face].push_back(K_USES[0uz].m_face);
            }
        }
    }
    for (std::size_t vertex { 0uz }; vertex < incident.size(); ++vertex) {
        check_stop(stop);
        require(!incident[vertex].empty(), "Unused geometry vertex");
        std::set<std::size_t> visited;
        std::vector<std::size_t> pending { incident[vertex].front() };
        while (!pending.empty()) {
            const auto K_FACE = pending.back();
            pending.pop_back();
            if (visited.insert(K_FACE).second) {
                for (auto adjacent : neighbors[vertex][K_FACE]) {
                    pending.push_back(adjacent);
                }
            }
        }
        require(visited.size() == incident[vertex].size(), "Disconnected geometry vertex fan");
    }
}

GeometryRuntimeMesh mesh(const Json& value, bool closed, const std::stop_token& stop) {
    keys(value, { "positions", "normals", "uv", "tangents", "triangles", "groups" });
    array(value.at("positions"), K_ELEMENTS);
    array(value.at("normals"), K_ELEMENTS);
    array(value.at("triangles"), K_ELEMENTS);
    array(value.at("groups"), K_ELEMENTS);
    const auto& points = value.at("positions");
    const auto& faces = value.at("triangles");
    require(points.size() >= 3uz && !faces.empty() && value.at("normals").size() == points.size() &&
                value.at("groups").size() == faces.size(),
            "Incomplete geometry arrays");
    const bool K_UV = !value.at("uv").is_null();
    require(K_UV == !value.at("tangents").is_null(), "UV and tangent presence differs");
    if (K_UV) {
        array(value.at("uv"), K_ELEMENTS);
        array(value.at("tangents"), K_ELEMENTS);
        require(value.at("uv").size() == points.size() &&
                    value.at("tangents").size() == points.size(),
                "Geometry attribute counts differ");
    }
    GeometryRuntimeMesh result;
    for (std::size_t index { 0uz }; index < points.size(); ++index) {
        check_stop(stop);
        const auto K_NORMAL = vector3(value.at("normals")[index]);
        require(std::abs(dot(K_NORMAL, K_NORMAL) - 1) <= 1e-5, "Nonunit geometry normal");
        result.m_positions.push_back(vector3(points[index]));
        result.m_normals.push_back(K_NORMAL);
        if (K_UV) {
            const auto& uv = value.at("uv")[index];
            const auto& tangent = value.at("tangents")[index];
            array(uv, 2uz);
            array(tangent, 4uz);
            require(uv.size() == 2uz && tangent.size() == 4uz, "Invalid geometry attributes");
            const std::array K_UV_VALUE { number(uv[0uz]), number(uv[1uz]) };
            const Vec3d K_T { number(tangent[0uz]), number(tangent[1uz]), number(tangent[2uz]) };
            const auto K_W = number(tangent[3uz]);
            require(K_UV_VALUE[0uz] >= 0 && K_UV_VALUE[0uz] <= 1 && K_UV_VALUE[1uz] >= 0 &&
                        K_UV_VALUE[1uz] <= 1 && std::abs(dot(K_T, K_T) - 1) <= 1e-5 &&
                        std::abs(dot(K_NORMAL, K_T)) <= 1e-5 && (K_W == 1 || K_W == -1),
                    "Invalid geometry UV/tangent basis");
            result.m_uv.push_back(K_UV_VALUE);
            result.m_tangents.push_back({ K_T.m_x, K_T.m_y, K_T.m_z, K_W });
        }
    }
    std::set<std::array<std::uint32_t, 3>> unique;
    std::set<AssetMaterialVariantId> groups;
    for (std::size_t index { 0uz }; index < faces.size(); ++index) {
        check_stop(stop);
        const auto& row = faces[index];
        array(row, 3uz);
        require(row.size() == 3uz, "Invalid geometry face");
        std::array<std::uint32_t, 3> face {};
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            const auto K_INDEX = integer(row[corner]);
            require(K_INDEX < points.size(), "Geometry index out of bounds");
            face[corner] = static_cast<std::uint32_t>(K_INDEX);
        }
        auto sorted = face;
        std::ranges::sort(sorted);
        require(sorted[0uz] != sorted[1uz] && sorted[1uz] != sorted[2uz] &&
                    unique.insert(sorted).second,
                "Repeated or duplicate geometry face");
        const auto K_AB = result.m_positions[face[1uz]] - result.m_positions[face[0uz]];
        const auto K_AC = result.m_positions[face[2uz]] - result.m_positions[face[0uz]];
        const auto K_CROSS = cross(K_AB, K_AC);
        require(finite(K_CROSS) && std::isfinite(dot(K_CROSS, K_CROSS)) &&
                    dot(K_CROSS, K_CROSS) > 0,
                "Degenerate/overflowing geometry face");
        for (auto vertex : face) {
            require(dot(K_CROSS, result.m_normals[vertex]) > 0,
                    "Geometry winding differs from normals");
        }
        if (K_UV) {
            const auto& a = result.m_uv[face[0uz]];
            const auto& b = result.m_uv[face[1uz]];
            const auto& c = result.m_uv[face[2uz]];
            const auto K_U1 = b[0uz] - a[0uz];
            const auto K_V1 = b[1uz] - a[1uz];
            const auto K_U2 = c[0uz] - a[0uz];
            const auto K_V2 = c[1uz] - a[1uz];
            const auto K_DET = K_U1 * K_V2 - K_V1 * K_U2;
            require(K_DET != 0, "Degenerate geometry UV triangle");
            const auto K_DPDU = (K_AB * K_V2 - K_AC * K_V1) * (1 / K_DET);
            const auto K_DPDV = (K_AC * K_U1 - K_AB * K_U2) * (1 / K_DET);
            require(finite(K_DPDU) && finite(K_DPDV), "Overflowing geometry tangent derivatives");
            for (auto vertex : face) {
                const auto& t = result.m_tangents[vertex];
                const Vec3d K_T { t[0uz], t[1uz], t[2uz] };
                const auto K_DU = dot(K_T, K_DPDU);
                const auto K_DV = dot(cross(result.m_normals[vertex], K_T) * t[3uz], K_DPDV);
                require(std::isfinite(K_DU) && std::isfinite(K_DV) && K_DU > 0 && K_DV > 0,
                        "Geometry tangent orientation differs from UVs");
            }
        }
        const AssetMaterialVariantId K_GROUP { integer(value.at("groups")[index]) };
        require(K_GROUP.m_value != 0, "Unselected geometry material group");
        groups.insert(K_GROUP);
        require(groups.size() <= 16uz,
                "Geometry material group budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        result.m_triangles.push_back(face);
        result.m_groups.push_back(K_GROUP);
    }
    topology(result, closed, stop);
    return result;
}

std::string key(const AssetRevisionRef& ref, bool source) {
    return std::string(source ? "geometry-source-" : "geometry-") +
           std::to_string(ref.m_product.m_value) + "-" + std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedGeometryProduct::Implementation {
    GeometryProductSelection m_selection;
    GeometryRuntimeMesh m_mesh;
    SceneMesh m_high;
    SimilarityTransform m_world;
    std::vector<GeometryFaceSupport> m_support;
    std::array<std::uint64_t, 5> m_coverage {};
    std::string m_receipt;
    AssetDependencyContext m_context;
    bool m_synthetic {}, m_manual {}, m_closed {};
    std::vector<std::byte> m_bytes, m_source_bytes;
};

AdmittedGeometryProduct::AdmittedGeometryProduct(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const GeometryProductSelection& AdmittedGeometryProduct::selection() const {
    return m_state->m_selection;
}

const GeometryRuntimeMesh& AdmittedGeometryProduct::mesh() const {
    return m_state->m_mesh;
}

const SceneMesh& AdmittedGeometryProduct::high_mesh() const {
    return m_state->m_high;
}

const SimilarityTransform& AdmittedGeometryProduct::world_from_geometry() const {
    return m_state->m_world;
}

const std::vector<GeometryFaceSupport>& AdmittedGeometryProduct::support() const {
    return m_state->m_support;
}

const std::array<std::uint64_t, 5>& AdmittedGeometryProduct::coverage() const {
    return m_state->m_coverage;
}

const std::string& AdmittedGeometryProduct::receipt_sha256() const {
    return m_state->m_receipt;
}

const AssetDependencyContext& AdmittedGeometryProduct::context() const {
    return m_state->m_context;
}

bool AdmittedGeometryProduct::synthetic_source() const {
    return m_state->m_synthetic;
}

bool AdmittedGeometryProduct::manual_ancestor() const {
    return m_state->m_manual;
}

bool AdmittedGeometryProduct::closed_surface() const {
    return m_state->m_closed;
}

Result<AdmittedGeometryProduct> inspect_geometry_product(const GeometryProductRequest& request,
                                                         const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_capture || !request.m_receipt || !request.m_source) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Independent geometry selection, capture, receipt and native mesh source "
                        "are required");
        }
        const auto& selected = request.m_selection.value();
        const auto& capture = request.m_capture.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        require(!selected.m_views.empty() && selected.m_views.size() <= 16uz,
                "Geometry view budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(std::isfinite(selected.m_max_deviation_units) &&
                    selected.m_max_deviation_units >= 0 &&
                    selected.m_max_deviation_units <= 1000000,
                "Invalid geometry deviation policy");
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        require(capture.receipt_sha256() == receipt.receipt_sha256() &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision,
                "Geometry capture/context differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FIREWALL =
            capture.validate_inference_dependency(receipt, selected.m_product, expected, stop);
        if (!K_FIREWALL) {
            return std::unexpected(K_FIREWALL.error());
        }
        const auto& node = bind(selected.m_product, receipt);
        const auto& source_node = bind(selected.m_source, receipt);
        const auto& chunk = source_chunk(request, selected.m_native, stop);
        if (!chunk.m_mesh) {
            return fail(ErrorCode::INVALID_DATA, "Geometry source has no native mesh");
        }
        const auto& high = *chunk.m_mesh;
        const auto K_INFERENCE = capture.inference();
        std::set<AssetSourceViewId> views;
        std::set<FrameId> frames;
        std::map<AssetProductId, AssetRevisionRef> dependencies;
        for (const auto& pin : selected.m_views) {
            check_stop(stop);
            const auto K_FOUND =
                std::ranges::find(K_INFERENCE.frames(), pin, &CaptureFrameInputs::m_view);
            require(K_FOUND != K_INFERENCE.frames().end() &&
                        views.insert({ pin.m_capture, pin.m_frame }).second &&
                        frames.insert(pin.m_frame).second,
                    "Geometry view is duplicated or not selected for inference",
                    ErrorCode::RESYNC_REQUIRED);
            dependencies.emplace(pin.m_image.m_product, pin.m_image);
            dependencies.emplace(pin.m_pose_asset.m_product, pin.m_pose_asset);
            if (high.m_observed) {
                const auto& context = high.m_observed->m_context;
                require(context.m_epoch == expected.m_world_epoch &&
                            context.m_pose_revision == expected.m_pose_revision &&
                            context.m_pose_sha256 == pin.m_pose_asset.m_content_sha256,
                        "Geometry source pose dependency differs",
                        ErrorCode::RESYNC_REQUIRED);
            }
        }
        require(std::set<FrameId>(chunk.m_source_frames.begin(), chunk.m_source_frames.end()) ==
                        frames &&
                    chunk.m_source_frames.size() == frames.size(),
                "Geometry native source-frame set differs",
                ErrorCode::RESYNC_REQUIRED);
        parents(source_node, dependencies);
        dependencies.emplace(selected.m_source.m_product, selected.m_source);
        parents(node, dependencies);
        const auto K_SOURCE_BYTES =
            take(receipt.copy_blob(selected.m_source.m_content_sha256, 16uz * 1024uz, stop));
        const auto K_SOURCE = parse(K_SOURCE_BYTES, 16uz * 1024uz, stop);
        keys(K_SOURCE,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "native",
              "manual_ancestor" });
        header(K_SOURCE, source_node, expected);
        require(K_SOURCE.at("kind").get<std::string>() == "NATIVE_GEOMETRY_SOURCE" &&
                    native(K_SOURCE.at("native")) == selected.m_native,
                "Geometry native source pin differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_ROOT_BYTES =
            take(receipt.copy_blob(selected.m_product.m_content_sha256, K_BYTES, stop));
        const auto K_DOC = parse(K_ROOT_BYTES, K_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "capture",
              "source",
              "views",
              "frame",
              "surface",
              "max_deviation_units",
              "mesh",
              "support",
              "manual_ancestor" });
        header(K_DOC, node, expected);
        require(K_DOC.at("kind").get<std::string>() == "GEOMETRY_PRODUCT" &&
                    K_DOC.at("frame").get<std::string>() ==
                        "SOURCE_CHUNK_LOCAL_RIGHT_HANDED_Y_UP" &&
                    reference(K_DOC.at("capture")) == capture.record().m_selection.m_product &&
                    reference(K_DOC.at("source")) == selected.m_source &&
                    number(K_DOC.at("max_deviation_units")) == selected.m_max_deviation_units,
                "Geometry product selection differs",
                ErrorCode::RESYNC_REQUIRED);
        array(K_DOC.at("views"), 16uz);
        require(K_DOC.at("views").size() == selected.m_views.size(),
                "Geometry selected view count differs");
        auto wanted = selected.m_views;
        std::vector<ObjectViewSelection> actual;
        for (const auto& row : K_DOC.at("views")) {
            actual.push_back(view(row));
        }
        std::ranges::sort(wanted);
        std::ranges::sort(actual);
        require(actual == wanted, "Geometry selected views differ", ErrorCode::RESYNC_REQUIRED);
        const auto K_SURFACE = K_DOC.at("surface").get<std::string>();
        require(K_SURFACE == "OPEN_SURFACE_ALLOWED" || K_SURFACE == "CLOSED_MANIFOLD",
                "Unsupported geometry surface policy");
        const bool K_CLOSED = K_SURFACE == "CLOSED_MANIFOLD";
        auto runtime = mesh(K_DOC.at("mesh"), K_CLOSED, stop);
        for (const auto& point : runtime.m_positions) {
            check_stop(stop);
            require(finite(transform_point(chunk.m_world_from_chunk, point)),
                    "Geometry world transform overflow");
        }
        array(K_DOC.at("support"), K_ELEMENTS);
        require(K_DOC.at("support").size() == runtime.m_triangles.size(),
                "Incomplete geometry support table");
        std::vector<GeometryFaceSupport> support;
        std::array<std::uint64_t, 5> coverage {};
        constexpr std::array K_CLASSES {
            "UNKNOWN", "OBSERVED", "INFERRED", "GENERATED", "MANUALLY_EDITED" };
        std::size_t support_count {};
        for (std::size_t index { 0uz }; index < runtime.m_triangles.size(); ++index) {
            check_stop(stop);
            const auto& row = K_DOC.at("support")[index];
            keys(row, { "class", "anchors", "views", "uncertainty_units" });
            const auto K_CLASS = std::ranges::find(K_CLASSES, row.at("class").get<std::string>());
            require(K_CLASS != K_CLASSES.end(), "Unsupported geometry evidence class");
            const auto K_CLASS_INDEX =
                static_cast<std::size_t>(std::distance(K_CLASSES.begin(), K_CLASS));
            GeometryFaceSupport face {
                static_cast<AssetEvidenceClass>(K_CLASS_INDEX), {}, {}, {}, 0 };
            ++coverage[K_CLASS_INDEX];
            if (!row.at("uncertainty_units").is_null()) {
                const auto K_UNCERTAINTY = number(row.at("uncertainty_units"));
                require(K_UNCERTAINTY >= 0 && K_UNCERTAINTY <= 1000000,
                        "Invalid geometry uncertainty");
                face.m_uncertainty_units = K_UNCERTAINTY;
            }
            array(row.at("views"), 16uz);
            require(row.at("views").size() <= 65536uz - support_count,
                    "Geometry support budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            support_count += row.at("views").size();
            std::set<AssetSourceViewId> seen;
            for (const auto& pin : row.at("views")) {
                keys(pin, { "capture_id", "frame_id" });
                const AssetSourceViewId K_VIEW { { integer(pin.at("capture_id")) },
                                                { integer(pin.at("frame_id")) } };
                require(views.contains(K_VIEW) && seen.insert(K_VIEW).second,
                        "Geometry support view differs");
                face.m_views.push_back(K_VIEW);
            }
            if (!row.at("anchors").is_null()) {
                array(row.at("anchors"), 3uz);
                require(row.at("anchors").size() == 3uz, "Geometry anchor count differs");
                std::array<GeometryAnchor, 3> anchors {};
                for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                    const auto& anchor = row.at("anchors")[corner];
                    keys(anchor, { "triangle", "weights" });
                    const auto K_TRIANGLE = integer(anchor.at("triangle"));
                    require(K_TRIANGLE < high.m_triangles.size(),
                            "Geometry anchor outside native source");
                    const auto& weights = anchor.at("weights");
                    array(weights, 3uz);
                    require(weights.size() == 3uz, "Invalid geometry barycentric weights");
                    Vec3d point {};
                    double sum {};
                    for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
                        const auto K_WEIGHT = number(weights[axis]);
                        require(K_WEIGHT >= 0 && K_WEIGHT <= 1,
                                "Geometry anchor outside source triangle");
                        anchors[corner].m_weights[axis] = K_WEIGHT;
                        sum += K_WEIGHT;
                        point =
                            point + position(high.m_vertices[high.m_triangles[K_TRIANGLE][axis]]) *
                                        K_WEIGHT;
                    }
                    require(sum == 1 && finite(point), "Geometry barycentric partition differs");
                    anchors[corner].m_triangle = static_cast<std::uint32_t>(K_TRIANGLE);
                    const auto K_POINT = runtime.m_positions[runtime.m_triangles[index][corner]];
                    const auto K_DISTANCE2 = dot(K_POINT - point, K_POINT - point);
                    require(std::isfinite(K_DISTANCE2), "Geometry correspondence overflow");
                    face.m_deviation_units =
                        std::max(face.m_deviation_units, std::sqrt(K_DISTANCE2));
                    if (face.m_class == AssetEvidenceClass::OBSERVED) {
                        require(K_POINT == point, "Observed geometry moved off its source anchor");
                    }
                }
                require(face.m_deviation_units <= selected.m_max_deviation_units,
                        "Geometry correspondence exceeds selected bound");
                face.m_anchors = anchors;
            }
            if (face.m_class == AssetEvidenceClass::OBSERVED ||
                face.m_class == AssetEvidenceClass::INFERRED) {
                if (!face.m_anchors || face.m_views.empty()) {
                    throw RecordFailure(ErrorCode::INVALID_DATA,
                                        "Geometry support/correspondence missing");
                }
                if (face.m_class == AssetEvidenceClass::OBSERVED) {
                    const auto& anchors = face.m_anchors.value();
                    require(high.m_observed.has_value() &&
                                anchors[0uz].m_triangle == anchors[1uz].m_triangle &&
                                anchors[0uz].m_triangle == anchors[2uz].m_triangle,
                            "Observed geometry crosses source triangles or lacks native observed "
                            "provenance");
                }
            } else if (face.m_class == AssetEvidenceClass::MANUALLY_EDITED) {
                require(node.m_manual_ancestor, "Edited geometry lacks declared manual ancestry");
            } else {
                require(!face.m_anchors && face.m_views.empty() && !face.m_uncertainty_units,
                        "Unknown/generated geometry cannot claim source support");
            }
            support.push_back(std::move(face));
        }
        const auto K_KIND = request.m_source->m_provenance.find("source_kind");
        const bool K_SYNTHETIC = K_KIND != request.m_source->m_provenance.end() &&
                                 K_KIND->second == "synthetic_diagnostic";
        SceneMesh owned_high { high.m_vertices, high.m_triangles, {} };
        auto owned = selected;
        std::ranges::sort(owned.m_views);
        check_stop(stop);
        return AdmittedGeometryProduct(
            std::make_shared<const AdmittedGeometryProduct::Implementation>(
                AdmittedGeometryProduct::Implementation { std::move(owned),
                                                         std::move(runtime),
                                                         std::move(owned_high),
                                                         chunk.m_world_from_chunk,
                                                         std::move(support),
                                                         coverage,
                                                         receipt.receipt_sha256(),
                                                         expected,
                                                         K_SYNTHETIC,
                                                         node.m_manual_ancestor,
                                                         K_CLOSED,
                                                         K_ROOT_BYTES,
                                                         K_SOURCE_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry product allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_geometry_product(const AdmittedGeometryProduct& product,
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
    if (product.receipt_sha256() != receipt.receipt_sha256() ||
        product.context().m_world_epoch != expected.m_world_epoch ||
        product.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Geometry publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { key(product.selection().m_product, false), product.m_state->m_bytes },
             { key(product.selection().m_source, true), product.m_state->m_source_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry publication allocation failed");
    }
}

Result<AdmittedGeometryProduct> read_geometry_product(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const GeometryProductRequest& request,
                                                      const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Independent geometry selection is required");
        }
        digest_text(commit.m_manifest_sha256);
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && K_SNAPSHOT.m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Geometry selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& [K_REF, K_SOURCE] : { std::pair { request.m_selection->m_product, false },
                                              std::pair { request.m_selection->m_source, true } }) {
            digest_text(K_REF.m_content_sha256);
            const auto K_FOUND = K_SNAPSHOT.m_records.find(key(K_REF, K_SOURCE));
            require(K_FOUND != K_SNAPSHOT.m_records.end() &&
                        sha256(K_FOUND->second) == K_REF.m_content_sha256,
                    "Selected geometry bytes are not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return inspect_geometry_product(request, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Geometry read allocation failed");
    }
}

}  // namespace world_explorer
