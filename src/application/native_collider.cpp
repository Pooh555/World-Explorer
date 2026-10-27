#include "world_explorer/application/native_collider.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_BYTES = 1024uz * 1024uz;

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
    require(K_INDEX < count, "Native collider index outside base mesh");
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

NativeColliderMesh mesh(const Json& root,
                        const AdmittedGeometryProduct& base,
                        const NativeColliderPolicy& policy,
                        const std::stop_token& stop) {
    const auto& original = base.mesh();
    const auto& vertices = root.at("vertex_sources");
    const auto& faces = root.at("faces");
    array(vertices, policy.m_max_vertices);
    array(faces, policy.m_max_triangles);
    require(vertices.size() == original.m_positions.size() &&
                faces.size() == original.m_triangles.size(),
            "Native collider must preserve every base vertex and face");
    NativeColliderMesh result;
    std::vector<std::uint32_t> remap;
    std::vector<bool> seen_vertices(vertices.size()), seen_faces(faces.size());
    for (const auto& vertex : vertices) {
        check_stop(stop);
        const auto K_SOURCE = index(vertex, vertices.size());
        require(!seen_vertices[K_SOURCE], "Native collider vertex source duplicated");
        seen_vertices[K_SOURCE] = true;
        remap.push_back(K_SOURCE);
        result.m_positions.push_back(original.m_positions[K_SOURCE]);
    }
    for (const auto& face : faces) {
        check_stop(stop);
        keys(face, { "source_face", "vertices" });
        const auto K_SOURCE = index(face.at("source_face"), faces.size());
        require(!seen_faces[K_SOURCE], "Native collider source face duplicated");
        seen_faces[K_SOURCE] = true;
        const auto& indices = face.at("vertices");
        array(indices, 3uz);
        require(indices.size() == 3uz, "Native collider face needs three vertices");
        const std::array K_TRIANGLE { index(indices[0uz], vertices.size()),
                                     index(indices[1uz], vertices.size()),
                                     index(indices[2uz], vertices.size()) };
        const std::array K_BASE {
            remap[K_TRIANGLE[0uz]], remap[K_TRIANGLE[1uz]], remap[K_TRIANGLE[2uz]] };
        require(cyclic(K_BASE, original.m_triangles[K_SOURCE]),
                "Native collider changes base winding, holes or topology");
        result.m_triangles.push_back(K_TRIANGLE);
        result.m_source_faces.push_back(K_SOURCE);
    }
    return result;
}

void word(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t byte { 0uz }; byte < 8uz; ++byte) {
        bytes.push_back(static_cast<std::byte>((value >> (8uz * byte)) & 255U));
    }
}

void text(std::vector<std::byte>& bytes, const std::string_view value) {
    const auto K_VIEW = std::as_bytes(std::span(value));
    bytes.insert(bytes.end(), K_VIEW.begin(), K_VIEW.end());
}

std::vector<std::byte> cook(const NativeColliderMesh& mesh,
                            const NativeColliderSelection& selection,
                            const AdmittedGeometryProduct& base,
                            const std::string& receipt,
                            const std::stop_token& stop) {
    std::vector<std::byte> bytes;
    text(bytes, "WE_NATIVE_COLLIDER_TRIANGLES_V1");
    const auto& product = selection.m_product;
    const auto& source = base.selection().m_product;
    for (auto value : { std::uint64_t { 1 },
                       base.context().m_world_epoch.m_value,
                       base.context().m_pose_revision.m_value,
                       static_cast<std::uint64_t>(selection.m_policy.m_weld_vertices),
                       static_cast<std::uint64_t>(selection.m_policy.m_double_sided),
                       static_cast<std::uint64_t>(selection.m_policy.m_max_vertices),
                       static_cast<std::uint64_t>(selection.m_policy.m_max_triangles),
                       product.m_product.m_value,
                       product.m_revision,
                       source.m_product.m_value,
                       source.m_revision }) {
        word(bytes, value);
    }
    for (const auto& hash : { product.m_content_sha256, source.m_content_sha256, receipt }) {
        text(bytes, hash);
    }
    word(bytes, mesh.m_positions.size());
    word(bytes, mesh.m_triangles.size());
    for (const auto& position : mesh.m_positions) {
        check_stop(stop);
        for (auto value : { position.m_x, position.m_y, position.m_z }) {
            word(bytes, std::bit_cast<std::uint64_t>(value == 0 ? 0.0 : value));
        }
    }
    for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
        check_stop(stop);
        word(bytes, mesh.m_source_faces[face]);
        for (auto vertex : mesh.m_triangles[face]) {
            word(bytes, vertex);
        }
    }
    require(bytes.size() <= K_BYTES,
            "Native collider cook budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    return bytes;
}

std::string key(const AssetRevisionRef& ref, bool cooked) {
    return std::string(cooked ? "collider-cook-" : "collider-") +
           std::to_string(ref.m_product.m_value) + "-" + std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedNativeCollider::Implementation {
    NativeColliderSelection m_selection;
    AdmittedGeometryProduct m_base;
    NativeColliderMesh m_mesh;
    std::vector<std::byte> m_root, m_cooked;
    std::string m_digest, m_receipt;
};

AdmittedNativeCollider::AdmittedNativeCollider(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const NativeColliderSelection& AdmittedNativeCollider::selection() const {
    return m_state->m_selection;
}

const AdmittedGeometryProduct& AdmittedNativeCollider::base() const {
    return m_state->m_base;
}

const NativeColliderMesh& AdmittedNativeCollider::mesh() const {
    return m_state->m_mesh;
}

std::span<const std::byte> AdmittedNativeCollider::canonical_bytes() const {
    return m_state->m_cooked;
}

const std::string& AdmittedNativeCollider::digest() const {
    return m_state->m_digest;
}

const std::string& AdmittedNativeCollider::receipt_sha256() const {
    return m_state->m_receipt;
}

Result<AdmittedNativeCollider> inspect_native_collider(const NativeColliderRequest& request,
                                                       const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_base || !request.m_receipt) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Native collider needs independent selection, base and receipt");
        }
        const auto& selection = request.m_selection.value();
        const auto& base = request.m_base.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        const auto& policy = selection.m_policy;
        require(policy.m_max_vertices >= 3uz && policy.m_max_vertices <= 8192uz &&
                    policy.m_max_triangles >= 1uz && policy.m_max_triangles <= 8192uz &&
                    !policy.m_weld_vertices && !policy.m_double_sided,
                "Unsupported native static collider cook policy",
                ErrorCode::UNSUPPORTED_OPERATION);
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        require(base.receipt_sha256() == receipt.receipt_sha256() &&
                    base.context().m_world_epoch == expected.m_world_epoch &&
                    base.context().m_pose_revision == expected.m_pose_revision,
                "Native collider base context/receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_NODE = std::ranges::find(
            receipt.graph().m_products, selection.m_product, &AssetDependencyNode::m_product);
        require(K_NODE != receipt.graph().m_products.end(),
                "Native collider root not in receipt",
                ErrorCode::RESYNC_REQUIRED);
        require(K_NODE->m_dependencies == std::vector { base.selection().m_product },
                "Native collider must depend exactly on selected base");
        auto bytes = take(receipt.copy_blob(selection.m_product.m_content_sha256, K_BYTES, stop));
        const auto K_ROOT = parse(bytes, K_BYTES, stop);
        keys(K_ROOT,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "base",
              "purpose",
              "body",
              "units",
              "frame",
              "cook",
              "vertex_sources",
              "faces",
              "manual_ancestor" });
        require(integer(K_ROOT.at("schema_version")) == 1 &&
                    K_ROOT.at("kind").get<std::string>() == "NATIVE_STATIC_TRIANGLE_COLLIDER" &&
                    K_ROOT.at("purpose").get<std::string>() == "SYNTHETIC_GAMEPLAY" &&
                    K_ROOT.at("body").get<std::string>() == "STATIC_STRUCTURAL" &&
                    K_ROOT.at("units").get<std::string>() == "ARBITRARY" &&
                    K_ROOT.at("frame").get<std::string>() == "BASE_GEOMETRY_LOCAL",
                "Unsupported native collider schema or authority",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_ROOT.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_ROOT.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_ROOT.at("recipe_sha256")) == K_NODE->m_recipe_sha256 &&
                    reference(K_ROOT.at("base")) == base.selection().m_product,
                "Native collider source/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(K_ROOT.at("manual_ancestor").is_boolean() &&
                    K_ROOT.at("manual_ancestor").get<bool>() == K_NODE->m_manual_ancestor &&
                    K_NODE->m_manual_ancestor == base.manual_ancestor(),
                "Native collider manual ancestry differs");
        const auto& parameters = K_ROOT.at("cook");
        keys(parameters,
             { "format", "weld_vertices", "double_sided", "max_vertices", "max_triangles" });
        require(parameters.at("format").get<std::string>() == "WE_NATIVE_COLLIDER_TRIANGLES_V1" &&
                    parameters.at("weld_vertices").is_boolean() &&
                    parameters.at("double_sided").is_boolean() &&
                    parameters.at("weld_vertices").get<bool>() == policy.m_weld_vertices &&
                    parameters.at("double_sided").get<bool>() == policy.m_double_sided &&
                    integer(parameters.at("max_vertices")) == policy.m_max_vertices &&
                    integer(parameters.at("max_triangles")) == policy.m_max_triangles,
                "Native collider independent cook selection differs");
        auto geometry = mesh(K_ROOT, base, policy, stop);
        auto canonical = cook(geometry, selection, base, receipt.receipt_sha256(), stop);
        auto hash = sha256(canonical);
        check_stop(stop);
        return AdmittedNativeCollider(
            std::make_shared<const AdmittedNativeCollider::Implementation>(
                AdmittedNativeCollider::Implementation { selection,
                                                        base,
                                                        std::move(geometry),
                                                        std::move(bytes),
                                                        std::move(canonical),
                                                        std::move(hash),
                                                        receipt.receipt_sha256() }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native collider allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_native_collider(const AdmittedNativeCollider& collider,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop,
                                             const std::function<void(std::size_t)>& observed) {
    const auto K_VALID = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (collider.receipt_sha256() != receipt.receipt_sha256() ||
        collider.base().context().m_world_epoch != expected.m_world_epoch ||
        collider.base().context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Native collider publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { key(collider.selection().m_product, false), collider.m_state->m_root },
             { key(collider.selection().m_product, true), collider.m_state->m_cooked } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native collider publication allocation failed");
    }
}

Result<AdmittedNativeCollider> read_native_collider(const std::filesystem::path& directory,
                                                    const RecordCommit& commit,
                                                    const NativeColliderRequest& request,
                                                    const std::stop_token& stop) {
    try {
        if (!request.m_receipt) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Native collider reload needs original receipt");
        }
        auto collider = take(inspect_native_collider(request, stop));
        const auto K_RECORDS = take(read_immutable_records(directory, stop));
        require(commit.m_generation && K_RECORDS.m_commit.m_generation == commit.m_generation &&
                    K_RECORDS.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Native collider selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto& receipt = request.m_receipt.value();
        const auto K_ROOT =
            take(receipt.copy_blob(collider.selection().m_product.m_content_sha256, K_BYTES, stop));
        for (auto cooked : { false, true }) {
            const auto K_FOUND =
                K_RECORDS.m_records.find(key(collider.selection().m_product, cooked));
            const auto K_EXPECTED_BYTES =
                cooked ? collider.canonical_bytes() : std::span<const std::byte> { K_ROOT };
            require(K_FOUND != K_RECORDS.m_records.end() &&
                        std::ranges::equal(K_FOUND->second, K_EXPECTED_BYTES),
                    "Native collider saved root or cook differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return collider;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native collider reload allocation failed");
    }
}

}  // namespace world_explorer
