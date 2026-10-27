#include "world_explorer/collision/native_static_shape.hpp"

#include "jolt_internal.hpp"
#include "world_explorer/core/hash.hpp"

#include <Jolt/ConfigurationString.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/RegisterTypes.h>

#include <bit>
#include <stdexcept>

namespace world_explorer {
namespace {

class ShapeFailure : public std::runtime_error {
  public:
    explicit ShapeFailure(const char* message, ErrorCode code = ErrorCode::INVALID_DATA)
        : std::runtime_error(message), m_code(code) { }

    ErrorCode m_code;
};

void require(bool condition, const char* message, ErrorCode code = ErrorCode::INVALID_DATA) {
    if (!condition) {
        throw ShapeFailure(message, code);
    }
}

void check_stop(const std::stop_token& stop) {
    require(!stop.stop_requested(), "Native diagnostic shape cancelled", ErrorCode::CANCELLED);
}

double distance(Vec3d a, Vec3d b) {
    const auto K_DELTA = a - b;
    return std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z);
}

Vec3d point(JPH::Vec3Arg value) {
    return { value.GetX(), value.GetY(), value.GetZ() };
}

Vec3d normal(const std::array<Vec3d, 3>& triangle) {
    return cross(triangle[1uz] - triangle[0uz], triangle[2uz] - triangle[0uz]);
}

void word(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t byte { 0uz }; byte < 8uz; ++byte) {
        bytes.push_back(static_cast<std::byte>((value >> (byte * 8uz)) & 0xffu));
    }
}

void real(std::vector<std::byte>& bytes, double value) {
    word(bytes, std::bit_cast<std::uint64_t>(value == 0 ? 0.0 : value));
}

void text(std::vector<std::byte>& bytes, std::string_view value) {
    word(bytes, value.size());
    const auto K_BYTES = std::as_bytes(std::span(value.data(), value.size()));
    bytes.insert(bytes.end(), K_BYTES.begin(), K_BYTES.end());
}

std::string geometry_digest(std::span<const NativeShapeTriangle> triangles) {
    std::vector<std::byte> bytes;
    text(bytes, "WE_NATIVE_JOLT_DECODED_TRIANGLES_V1");
    word(bytes, triangles.size());
    for (const auto& triangle : triangles) {
        word(bytes, triangle.m_source_face);
        for (auto vertex : triangle.m_vertices) {
            real(bytes, vertex.m_x);
            real(bytes, vertex.m_y);
            real(bytes, vertex.m_z);
        }
    }
    return sha256(bytes);
}

std::string build_digest(const ColliderInputBinding& binding, const NativeShapeReport& report) {
    std::vector<std::byte> bytes;
    text(bytes, "WE_NATIVE_JOLT_DIAGNOSTIC_BUILD_V1");
    text(bytes, binding.m_canonical_sha256);
    text(bytes, report.m_backend);
    text(bytes, report.m_adapter_source_sha256);
    text(bytes, report.m_geometry_sha256);
    real(bytes, report.m_policy.m_max_local_vertex_error_units);
    real(bytes, report.m_policy.m_max_world_ray_endpoint_error_units);
    return sha256(bytes);
}

bool valid_policy(const NativeShapePolicy& policy) {
    const auto K_VALID = [](double value) {
        return std::isfinite(value) && value >= 0 && value <= 1;
    };
    return K_VALID(policy.m_max_local_vertex_error_units) &&
           K_VALID(policy.m_max_world_ray_endpoint_error_units);
}

std::uint64_t jolt_version_id() {
    // The upstream version macro requires this name in its expansion.
    using JPH::uint64;
    return JPH_VERSION_ID;
}

struct DecodedShape {
    std::vector<NativeShapeTriangle> m_triangles;
    double m_error {};
};

DecodedShape inspect_shape(const JPH::Shape& shape,
                           const NativeColliderMesh& source,
                           double tolerance,
                           const std::stop_token& stop) {
    require(shape.GetStats().mNumTriangles == source.m_triangles.size(),
            "Jolt compressed shape lost source triangles");
    DecodedShape decoded { std::vector<NativeShapeTriangle>(source.m_triangles.size()) };
    std::vector<bool> matched(source.m_triangles.size());
    JPH::Shape::GetTrianglesContext context;
    shape.GetTrianglesStart(context,
                            JPH::AABox::sBiggest(),
                            shape.GetCenterOfMass(),
                            JPH::Quat::sIdentity(),
                            JPH::Vec3::sReplicate(1));
    std::array<JPH::Float3, 32uz * 3uz> vertices;
    std::size_t count {};
    for (;;) {
        check_stop(stop);
        const auto K_COUNT = shape.GetTrianglesNext(context, 32, vertices.data());
        require(K_COUNT >= 0 && K_COUNT <= 32, "Jolt returned invalid triangle batch");
        if (K_COUNT == 0) {
            break;
        }
        for (std::size_t face { 0uz }; face < static_cast<std::size_t>(K_COUNT); ++face) {
            require(count < source.m_triangles.size(), "Jolt returned extra triangles");
            std::array<Vec3d, 3> actual;
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                const auto& v = vertices[face * 3uz + corner];
                actual[corner] = { v.x, v.y, v.z };
                require(finite(actual[corner]), "Jolt returned nonfinite vertex");
            }
            const auto K_NORMAL = normal(actual);
            require(finite(K_NORMAL) && dot(K_NORMAL, K_NORMAL) > 0,
                    "Jolt compressed triangle became degenerate");
            // Conservative bounded bijection. Ambiguous near-coincident geometry
            // may refuse; never accept a missing, reversed or displaced face.
            bool found {};
            for (std::size_t candidate { 0uz }; candidate < matched.size() && !found; ++candidate) {
                check_stop(stop);
                if (matched[candidate]) {
                    continue;
                }
                const auto& indices = source.m_triangles[candidate];
                const std::array K_EXPECTED { source.m_positions[indices[0uz]],
                                             source.m_positions[indices[1uz]],
                                             source.m_positions[indices[2uz]] };
                if (dot(K_NORMAL, normal(K_EXPECTED)) <= 0) {
                    continue;
                }
                for (std::size_t shift { 0uz }; shift < 3uz; ++shift) {
                    double error {};
                    std::array<Vec3d, 3> ordered;
                    for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                        ordered[corner] = actual[(corner + shift) % 3uz];
                        error = std::max(error, distance(K_EXPECTED[corner], ordered[corner]));
                    }
                    if (error <= tolerance) {
                        matched[candidate] = true;
                        decoded.m_triangles[candidate] = { ordered,
                                                          source.m_source_faces[candidate] };
                        decoded.m_error = std::max(decoded.m_error, error);
                        found = true;
                        break;
                    }
                }
            }
            require(found, "Jolt compressed triangles exceed selected oriented surface loss");
            ++count;
        }
    }
    require(count == matched.size() && std::ranges::all_of(matched,
                                                           [](bool value) {
                                                               return value;
                                                           }),
            "Jolt decoded triangles do not cover source surface");
    return decoded;
}

}  // namespace

struct NativeStaticCollision::Implementation {
    AdmittedColliderInput m_input;
    JPH::RefConst<JPH::MeshShape> m_shape;
    NativeShapeReport m_report;
    std::vector<NativeShapeTriangle> m_triangles;
};

NativeStaticCollision::NativeStaticCollision(std::unique_ptr<Implementation> state)
    : m_state(std::move(state)) { }

NativeStaticCollision::~NativeStaticCollision() = default;

const ColliderInputBinding& NativeStaticCollision::binding() const {
    return m_state->m_input.binding();
}

const NativeShapeReport& NativeStaticCollision::report() const {
    return m_state->m_report;
}

std::span<const NativeShapeTriangle> NativeStaticCollision::backend_triangles() const {
    return m_state->m_triangles;
}

Result<std::shared_ptr<const NativeStaticCollision>>
build_native_static_collision(const AdmittedColliderInput& input,
                              const ColliderInputBinding& current,
                              const NativeShapePolicy& policy,
                              const std::stop_token& stop) {
    const auto K_VALID = input.validate_binding(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    try {
        require(valid_policy(policy),
                "Invalid independently selected native shape loss policy",
                ErrorCode::INVALID_ARGUMENT);
        require(JPH::VerifyJoltVersionID(), "Jolt binary/header configuration mismatch");
        collision_detail::ensure_jolt_runtime();
        JPH::VertexList vertices;
        const auto& mesh = input.mesh();
        vertices.reserve(mesh.m_positions.size());
        for (auto vertex : mesh.m_positions) {
            check_stop(stop);
            const auto K_VERTEX =
                collision_detail::checked_vec(vertex, policy.m_max_local_vertex_error_units);
            if (!K_VERTEX) {
                return std::unexpected(K_VERTEX.error());
            }
            vertices.emplace_back(K_VERTEX->GetX(), K_VERTEX->GetY(), K_VERTEX->GetZ());
        }
        JPH::IndexedTriangleList triangles;
        for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
            check_stop(stop);
            const auto& indices = mesh.m_triangles[face];
            triangles.emplace_back(indices[0uz],
                                   indices[1uz],
                                   indices[2uz],
                                   0,
                                   static_cast<std::uint32_t>(face + 1uz));
        }
        JPH::MeshShapeSettings settings { std::move(vertices), std::move(triangles) };
        require(settings.mIndexedTriangles.size() == mesh.m_triangles.size(),
                "Jolt sanitizer discarded selected native triangles");
        settings.mPerTriangleUserData = true;
        const auto K_SHAPE = settings.Create();
        require(!K_SHAPE.HasError() && K_SHAPE.IsValid(), "Jolt native static shape build failed");
        const auto& shape_ref = K_SHAPE.Get();
        require(shape_ref->GetSubType() == JPH::EShapeSubType::Mesh,
                "Jolt returned wrong native shape type");
        const JPH::RefConst<JPH::MeshShape> K_MESH {
            static_cast<const JPH::MeshShape*>(shape_ref.GetPtr()) };
        auto decoded = inspect_shape(*K_MESH, mesh, policy.m_max_local_vertex_error_units, stop);
        NativeShapeReport report;
        report.m_backend =
            "Jolt " + std::to_string(JPH_VERSION_MAJOR) + "." + std::to_string(JPH_VERSION_MINOR) +
            "." + std::to_string(JPH_VERSION_PATCH) + " id=" + std::to_string(jolt_version_id()) +
            " " + JPH::GetConfigurationString();
        report.m_adapter_source_sha256 = WE_NATIVE_SHAPE_SOURCE_SHA;
        report.m_geometry_sha256 = geometry_digest(decoded.m_triangles);
        report.m_triangle_count = decoded.m_triangles.size();
        report.m_local_vertex_error_units = decoded.m_error;
        report.m_policy = policy;
        report.m_build_sha256 = build_digest(current, report);
        check_stop(stop);
        auto state = std::make_unique<NativeStaticCollision::Implementation>(
            NativeStaticCollision::Implementation {
                input, K_MESH, std::move(report), std::move(decoded.m_triangles) });
        return std::shared_ptr<const NativeStaticCollision> {
            new NativeStaticCollision(std::move(state)) };
    } catch (const ShapeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native shape allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Native shape backend failed: ") + error.what());
    }
}

Result<NativeShapeRayResult>
NativeStaticCollision::ray_cast(const ColliderInputBinding& current,
                                const SimilarityTransform& world_from_base,
                                Vec3d start,
                                Vec3d end,
                                const std::stop_token& stop) const {
    const auto K_VALID = m_state->m_input.validate_binding(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    try {
        const auto& q = world_from_base.m_rotation;
        const auto K_NORM = q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z;
        require(valid(world_from_base) && std::abs(K_NORM - 1) <= 1e-12 &&
                    world_from_base.m_scale >= 1e-6 && world_from_base.m_scale <= 1e6 &&
                    finite(start) && finite(end) && start != end,
                "Native shape query needs finite segment and rigid positive uniform instance frame",
                ErrorCode::INVALID_ARGUMENT);
        const auto K_TOLERANCE = report().m_policy.m_max_world_ray_endpoint_error_units;
        const auto K_START = collision_detail::checked_vec(
            inverse_transform_point(world_from_base, start), K_TOLERANCE / world_from_base.m_scale);
        const auto K_END = collision_detail::checked_vec(
            inverse_transform_point(world_from_base, end), K_TOLERANCE / world_from_base.m_scale);
        if (!K_START) {
            return std::unexpected(K_START.error());
        }
        if (!K_END) {
            return std::unexpected(K_END.error());
        }
        const auto K_COM = m_state->m_shape->GetCenterOfMass();
        const auto K_ORIGIN = *K_START - K_COM, K_DIRECTION = *K_END - *K_START;
        require(K_DIRECTION.LengthSq() > 0 && !K_DIRECTION.IsNaN(),
                "Native shape ray collapsed in backend precision",
                ErrorCode::INVALID_ARGUMENT);
        const auto K_A = point(K_ORIGIN) + point(K_COM), K_B = K_A + point(K_DIRECTION);
        const auto K_ERROR = std::max(distance(transform_point(world_from_base, K_A), start),
                                      distance(transform_point(world_from_base, K_B), end));
        require(std::isfinite(K_ERROR) && K_ERROR <= K_TOLERANCE,
                "Native shape ray endpoints exceed world conversion loss");
        NativeShapeRayResult result {
            {}, K_ERROR, report().m_local_vertex_error_units * world_from_base.m_scale };
        check_stop(stop);
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
        JPH::RayCastSettings settings;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
        m_state->m_shape->CastRay({ K_ORIGIN, K_DIRECTION }, settings, {}, collector);
        check_stop(stop);
        if (collector.HadHit()) {
            const auto& hit = collector.mHit;
            const auto K_FACE = m_state->m_shape->GetTriangleUserData(hit.mSubShapeID2);
            require(K_FACE > 0 && K_FACE <= m_state->m_input.mesh().m_source_faces.size() &&
                        std::isfinite(hit.mFraction) && hit.mFraction >= 0 && hit.mFraction <= 1,
                    "Native shape backend returned invalid hit/source mapping");
            const auto K_FRACTION = static_cast<double>(hit.mFraction);
            const auto K_POINT = start + (end - start) * K_FRACTION;
            require(finite(K_POINT), "Native shape hit interpolation overflow");
            result.m_hit = NativeShapeRayHit {
                K_FRACTION, K_POINT, m_state->m_input.mesh().m_source_faces[K_FACE - 1uz] };
        }
        return result;
    } catch (const ShapeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native shape query allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Native shape query failed: ") + error.what());
    }
}

}  // namespace world_explorer
