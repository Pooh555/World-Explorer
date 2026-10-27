#include "../fixtures/native_collider_fixture.hpp"
#include "../fixtures/structure_graph_fixture.hpp"
#include "world_explorer/collision/native_static_shape.hpp"
#include "world_explorer/collision/static_shape.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::native_collider_test;

namespace {

constexpr std::size_t K_MAGIC_SIZE { std::string_view("WE_NATIVE_COLLIDER_TRIANGLES_V1").size() };
constexpr std::size_t K_COUNTS { K_MAGIC_SIZE + 11uz * 8uz + 3uz * 64uz };
constexpr std::size_t K_POSITIONS { K_COUNTS + 2uz * 8uz };

void put_word(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t byte { 0uz }; byte < 8uz; ++byte) {
        bytes.at(offset + byte) = static_cast<std::byte>((value >> (8uz * byte)) & 0xffu);
    }
}

void put_real(std::vector<std::byte>& bytes, std::size_t vertex, std::size_t axis, double value) {
    put_word(bytes, K_POSITIONS + vertex * 24uz + axis * 8uz, std::bit_cast<std::uint64_t>(value));
}

std::vector<std::byte> copy_bytes(const AdmittedColliderInput& input) {
    return { input.canonical_bytes().begin(), input.canonical_bytes().end() };
}

Result<AdmittedColliderInput> reselect(const std::vector<std::byte>& bytes,
                                       ColliderInputBinding selected) {
    selected.m_canonical_sha256 = sha256(bytes);
    return decode_native_collider_input(bytes, selected);
}

AdmittedColliderInput input_from(const ColliderFixture& fixture) {
    const auto K_COLLIDER = checked(inspect_native_collider(fixture.collider()));
    return checked(prepare_native_collider_input(K_COLLIDER, K_COLLIDER.base().context()));
}

AdmittedColliderInput ring_input() {
    GeometryFixture geometry(structure_graph_test::ring_geometry,
                             structure_graph_test::ring_source);
    const auto K_SELECTION = append_collider(geometry).first;
    const auto K_RECEIPT = freeze_collider(geometry);
    const auto K_COLLIDER =
        checked(inspect_native_collider(collider_request(geometry, K_RECEIPT, K_SELECTION)));
    return checked(prepare_native_collider_input(K_COLLIDER, K_COLLIDER.base().context()));
}

Vec3d unit(Vec3d value) {
    return value * (1 / std::sqrt(dot(value, value)));
}

// Independent double-precision Moller-Trumbore segment oracle; front faces only.
std::optional<NativeShapeRayHit> reference_ray(const NativeColliderMesh& mesh,
                                               const SimilarityTransform& frame,
                                               Vec3d start,
                                               Vec3d end) {
    const auto K_DIRECTION = end - start;
    std::optional<NativeShapeRayHit> nearest;
    for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
        const auto& indices = mesh.m_triangles[face];
        const auto K_A = transform_point(frame, mesh.m_positions[indices[0uz]]);
        const auto K_B = transform_point(frame, mesh.m_positions[indices[1uz]]);
        const auto K_C = transform_point(frame, mesh.m_positions[indices[2uz]]);
        const auto K_E1 = K_B - K_A, K_E2 = K_C - K_A;
        const auto K_P = cross(K_DIRECTION, K_E2);
        const auto K_DET = dot(K_E1, K_P);
        if (K_DET <= 1e-12) {
            continue;
        }
        const auto K_T = start - K_A;
        const auto K_U = dot(K_T, K_P) / K_DET;
        const auto K_Q = cross(K_T, K_E1);
        const auto K_V = dot(K_DIRECTION, K_Q) / K_DET;
        const auto K_FRACTION = dot(K_E2, K_Q) / K_DET;
        if (K_U < 0 || K_V < 0 || K_U + K_V > 1 || K_FRACTION < 0 || K_FRACTION > 1) {
            continue;
        }
        if (!nearest || K_FRACTION < nearest->m_fraction) {
            nearest = NativeShapeRayHit {
                K_FRACTION, start + K_DIRECTION * K_FRACTION, mesh.m_source_faces[face] };
        }
    }
    return nearest;
}

Json point_json(Vec3d point) {
    return { point.m_x, point.m_y, point.m_z };
}

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required native shape fixture value missing");
    }
    return *value;
}

Json frame_json(const SimilarityTransform& frame) {
    const auto& q = frame.m_rotation;
    return { { "rotation_wxyz", { q.m_w, q.m_x, q.m_y, q.m_z } },
            { "translation", point_json(frame.m_translation) },
            { "scale", frame.m_scale } };
}

}  // namespace

TEST_CASE("native collider neutral input owns replay without application dependencies",
          "[native-shape]") {
    STATIC_REQUIRE(!std::is_default_constructible_v<AdmittedColliderInput>);
    STATIC_REQUIRE(!std::is_default_constructible_v<NativeStaticCollision>);
    STATIC_REQUIRE(!std::is_convertible_v<NativeStaticCollision, JoltStaticCollision>);
    ColliderFixture fixture;
    const auto K_COLLIDER = checked(inspect_native_collider(fixture.collider()));
    const auto K_INPUT =
        checked(prepare_native_collider_input(K_COLLIDER, K_COLLIDER.base().context()));
    CHECK(K_INPUT.mesh().m_positions == K_COLLIDER.mesh().m_positions);
    CHECK(K_INPUT.mesh().m_triangles == K_COLLIDER.mesh().m_triangles);
    CHECK(K_INPUT.mesh().m_source_faces == K_COLLIDER.mesh().m_source_faces);
    CHECK(copy_bytes(K_INPUT) == std::vector<std::byte>(K_COLLIDER.canonical_bytes().begin(),
                                                        K_COLLIDER.canonical_bytes().end()));
    auto bytes = copy_bytes(K_INPUT);
    const auto K_REPLAY = checked(decode_native_collider_input(bytes, K_INPUT.binding()));
    bytes.clear();
    CHECK(copy_bytes(K_REPLAY) == copy_bytes(K_INPUT));
    auto wrong = K_COLLIDER.base().context();
    ++wrong.m_pose_revision.m_value;
    CHECK(!prepare_native_collider_input(K_COLLIDER, wrong));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOP = prepare_native_collider_input(
        K_COLLIDER, K_COLLIDER.base().context(), cancelled.get_token());
    REQUIRE(!K_STOP);
    CHECK(K_STOP.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("native collider input rejects every truncated rehashed prefix and trailing bytes",
          "[native-shape]") {
    const auto K_INPUT = input_from(ColliderFixture {});
    const auto K_BYTES = copy_bytes(K_INPUT);
    for (std::size_t size { 0uz }; size < K_BYTES.size(); ++size) {
        const std::vector<std::byte> K_PREFIX(K_BYTES.begin(),
                                              K_BYTES.begin() + static_cast<std::ptrdiff_t>(size));
        INFO(size);
        CHECK(!reselect(K_PREFIX, K_INPUT.binding()));
    }
    auto extra = K_BYTES;
    extra.push_back(std::byte {});
    CHECK(!reselect(extra, K_INPUT.binding()));
    const std::vector<std::byte> K_OVERSIZE(1024uz * 1024uz + 1uz);
    CHECK(!reselect(K_OVERSIZE, K_INPUT.binding()));
    CHECK(copy_bytes(K_INPUT) == K_BYTES);
}

TEST_CASE("native collider input refuses rehashed malformed bounded geometry", "[native-shape]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    const auto K_INPUT = input_from(ColliderFixture {});
    auto bytes = copy_bytes(K_INPUT);
    const auto K_FACES = K_POSITIONS + 4uz * 24uz;
    switch (K_CASE) {
    case 0:
        bytes[0uz] = std::byte {};
        break;
    case 1:
        put_word(bytes, K_MAGIC_SIZE, 2);
        break;
    case 2:
        put_word(bytes, K_MAGIC_SIZE + 1uz * 8uz, 4);
        break;
    case 3:
        put_word(bytes, K_MAGIC_SIZE + 2uz * 8uz, 6);
        break;
    case 4:
        put_word(bytes, K_MAGIC_SIZE + 3uz * 8uz, 1);
        break;
    case 5:
        put_word(bytes, K_MAGIC_SIZE + 4uz * 8uz, 1);
        break;
    case 6:
        put_word(bytes, K_MAGIC_SIZE + 5uz * 8uz, 8193);
        break;
    case 7:
        put_word(bytes, K_MAGIC_SIZE + 6uz * 8uz, 0);
        break;
    case 8:
        put_word(bytes, K_MAGIC_SIZE + 7uz * 8uz, 100);
        break;
    case 9:
        bytes[K_MAGIC_SIZE + 11uz * 8uz] = std::byte { 'f' };
        break;
    case 10:
        put_word(bytes, K_COUNTS, std::numeric_limits<std::uint64_t>::max());
        break;
    case 11:
        put_real(bytes, 0uz, 0uz, std::numeric_limits<double>::quiet_NaN());
        break;
    case 12:
        put_real(bytes, 0uz, 0uz, std::numeric_limits<double>::infinity());
        break;
    case 13:
        put_real(bytes, 0uz, 0uz, 1000001);
        break;
    case 14:
        put_real(bytes, 0uz, 0uz, -0.0);
        break;
    case 15:
        put_word(bytes, K_FACES, 2);
        break;
    case 16:
        put_word(bytes, K_FACES + 32uz, 1);
        break;
    case 17:
        put_word(bytes, K_FACES + 8uz, 4);
        break;
    case 18:
        put_word(bytes, K_FACES + 16uz, 0);
        put_word(bytes, K_FACES + 8uz, 0);
        break;
    case 19:
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            for (std::size_t byte { 0uz }; byte < 8uz; ++byte) {
                bytes[K_FACES + 32uz + 8uz + corner * 8uz + byte] =
                    bytes[K_FACES + 8uz + corner * 8uz + byte];
            }
        }
        break;
    default:
        throw std::logic_error("Unknown collider input mutation");
    }
    CHECK(!reselect(bytes, K_INPUT.binding()));
    CHECK(checked(build_native_static_collision(K_INPUT, K_INPUT.binding()))
              ->report()
              .m_triangle_count == 2uz);
}

TEST_CASE("native diagnostic shape binds exact source and backend build with bounded loss",
          "[native-shape]") {
    const auto K_INPUT = input_from(ColliderFixture {});
    const auto K_SHAPE = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    const auto K_REBUILD = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    CHECK(K_SHAPE->report().m_triangle_count == 2uz);
    CHECK(K_SHAPE->report().m_backend.starts_with("Jolt 5.3.0 id="));
    CHECK(K_SHAPE->report().m_build_sha256 == K_REBUILD->report().m_build_sha256);
    CHECK(K_SHAPE->report().m_geometry_sha256 == K_REBUILD->report().m_geometry_sha256);
    CHECK(K_SHAPE->report().m_adapter_source_sha256.size() == 64uz);
    CHECK(K_SHAPE->report().m_local_vertex_error_units <= 1e-4);
    CHECK(K_SHAPE->binding().m_canonical_sha256 == sha256(K_INPUT.canonical_bytes()));
    auto policy = K_SHAPE->report().m_policy;
    policy.m_max_world_ray_endpoint_error_units = 1e-3;
    const auto K_CHANGED =
        checked(build_native_static_collision(K_INPUT, K_INPUT.binding(), policy));
    CHECK(K_CHANGED->report().m_build_sha256 != K_SHAPE->report().m_build_sha256);
    CHECK(K_CHANGED->report().m_geometry_sha256 == K_SHAPE->report().m_geometry_sha256);
    for (std::size_t face { 0uz }; face < K_INPUT.mesh().m_triangles.size(); ++face) {
        const auto& triangle = K_SHAPE->backend_triangles()[face];
        CHECK(triangle.m_source_face == K_INPUT.mesh().m_source_faces[face]);
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
            const auto K_DELTA =
                triangle.m_vertices[corner] -
                K_INPUT.mesh().m_positions[K_INPUT.mesh().m_triangles[face][corner]];
            CHECK(std::sqrt(dot(K_DELTA, K_DELTA)) <= K_SHAPE->report().m_local_vertex_error_units);
        }
    }
}

TEST_CASE("native diagnostic shape rejects stale source pins without modifying prior owner",
          "[native-shape]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    const auto K_INPUT = input_from(ColliderFixture {});
    const auto K_SHAPE = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    auto current = K_INPUT.binding();
    switch (K_CASE) {
    case 0:
        ++current.m_context.m_world_epoch.m_value;
        break;
    case 1:
        ++current.m_context.m_pose_revision.m_value;
        break;
    case 2:
        ++current.m_collider.m_product.m_value;
        break;
    case 3:
        ++current.m_collider.m_revision;
        break;
    case 4:
        current.m_collider.m_content_sha256 = hash("other collider");
        break;
    case 5:
        ++current.m_base.m_product.m_value;
        break;
    case 6:
        ++current.m_base.m_revision;
        break;
    case 7:
        current.m_base.m_content_sha256 = hash("other base");
        break;
    case 8:
        current.m_receipt_sha256 = hash("other receipt");
        break;
    case 9:
        current.m_canonical_sha256 = hash("other cook");
        break;
    default:
        throw std::logic_error("Unknown collider source mutation");
    }
    const auto K_STALE = K_SHAPE->ray_cast(current, {}, { .3, .4, 1 }, { .3, .4, -1 });
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(!build_native_static_collision(K_INPUT, current));
    const auto K_PRIOR =
        checked(K_SHAPE->ray_cast(K_INPUT.binding(), {}, { .3, .4, 1 }, { .3, .4, -1 }));
    REQUIRE(K_PRIOR.m_hit);
    CHECK(required(K_PRIOR.m_hit).m_source_face == 0);
}

TEST_CASE("native diagnostic shape shares local geometry across fresh rotated instance frames",
          "[native-shape]") {
    const auto K_INPUT = input_from(ColliderFixture {});
    const auto K_SHAPE = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    const auto K_S = std::sin(.37), K_COSINE = std::cos(.37);
    const std::array<SimilarityTransform, 3> K_FRAMES { SimilarityTransform {},
                                                       { { K_COSINE, 0, K_S, 0 }, { 12, -4, 9 }, 2 },
                                                       { { K_COSINE, K_S, 0, 0 }, { 30, 5, -9 }, .4 } };
    for (const auto& frame : K_FRAMES) {
        for (std::size_t face { 0uz }; face < K_INPUT.mesh().m_triangles.size(); ++face) {
            const auto& indices = K_INPUT.mesh().m_triangles[face];
            const auto K_A = K_INPUT.mesh().m_positions[indices[0uz]],
                       K_B = K_INPUT.mesh().m_positions[indices[1uz]],
                       K_C = K_INPUT.mesh().m_positions[indices[2uz]];
            const auto K_CENTER = K_A * .2 + K_B * .3 + K_C * .5;
            const auto K_NORMAL = unit(cross(K_B - K_A, K_C - K_A));
            const auto K_START = transform_point(frame, K_CENTER + K_NORMAL * .2);
            const auto K_END = transform_point(frame, K_CENTER - K_NORMAL * .2);
            const auto K_RAY = checked(K_SHAPE->ray_cast(K_INPUT.binding(), frame, K_START, K_END));
            const auto K_EXPECTED = reference_ray(K_INPUT.mesh(), frame, K_START, K_END);
            REQUIRE(K_RAY.m_hit);
            REQUIRE(K_EXPECTED);
            CHECK(required(K_RAY.m_hit).m_source_face == required(K_EXPECTED).m_source_face);
            CHECK(required(K_RAY.m_hit).m_fraction ==
                  Catch::Approx(required(K_EXPECTED).m_fraction).margin(2e-4));
            CHECK(K_RAY.m_world_endpoint_error_units <= 1e-4);
            CHECK(K_RAY.m_world_surface_error_units ==
                  K_SHAPE->report().m_local_vertex_error_units * frame.m_scale);
            CHECK(!checked(K_SHAPE->ray_cast(K_INPUT.binding(), frame, K_END, K_START)).m_hit);
        }
    }
}

TEST_CASE("native diagnostic shape preserves ring hole and original face queries",
          "[native-shape][native-shape-product]") {
    const auto K_INPUT = ring_input();
    const auto K_SHAPE = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    CHECK(K_SHAPE->report().m_triangle_count == 8uz);
    const auto K_S = std::sin(.43), K_C = std::cos(.43);
    const std::array<SimilarityTransform, 3> K_FRAMES { SimilarityTransform {},
                                                       { { K_C, 0, K_S, 0 }, { 12, -4, 9 }, 2 },
                                                       { { K_C, K_S, 0, 0 }, { -30, 5, -9 }, .4 } };
    Json product { { "label", "SYNTHETIC_DIAGNOSTIC_STATIC_GAMEPLAY_ARBITRARY_UNITS" },
                  { "backend", K_SHAPE->report().m_backend },
                  { "adapter_source_sha256", K_SHAPE->report().m_adapter_source_sha256 },
                  { "build_sha256", K_SHAPE->report().m_build_sha256 },
                  { "geometry_sha256", K_SHAPE->report().m_geometry_sha256 },
                  { "canonical_sha256", K_INPUT.binding().m_canonical_sha256 },
                  { "local_vertex_error_units", K_SHAPE->report().m_local_vertex_error_units },
                  { "max_local_vertex_error_units", 1e-4 },
                  { "max_world_endpoint_error_units", 1e-4 },
                  { "backend_triangles", Json::array() },
                  { "rays", Json::array() } };
    for (const auto& triangle : K_SHAPE->backend_triangles()) {
        product["backend_triangles"].push_back({ { "source_face", triangle.m_source_face },
                                                { "positions",
                                                 { point_json(triangle.m_vertices[0uz]),
                                                  point_json(triangle.m_vertices[1uz]),
                                                  point_json(triangle.m_vertices[2uz]) } } });
    }
    for (const auto& frame : K_FRAMES) {
        const auto K_HOLE = checked(K_SHAPE->ray_cast(K_INPUT.binding(),
                                                      frame,
                                                      transform_point(frame, { 2, 2, 1 }),
                                                      transform_point(frame, { 2, 2, -1 })));
        CHECK(!K_HOLE.m_hit);
        for (std::size_t y { 0uz }; y < 13uz; ++y) {
            for (std::size_t x { 0uz }; x < 13uz; ++x) {
                // Offset from edges/diagonals; unambiguous original source face.
                const auto K_X = -.4 + static_cast<double>(x) * .4 + .037;
                const auto K_Y = -.4 + static_cast<double>(y) * .4 + .081;
                const auto K_START = transform_point(frame, { K_X, K_Y, 1.3 });
                const auto K_END = transform_point(frame, { K_X, K_Y, -.9 });
                const auto K_ACTUAL =
                    checked(K_SHAPE->ray_cast(K_INPUT.binding(), frame, K_START, K_END));
                const auto K_EXPECTED = reference_ray(K_INPUT.mesh(), frame, K_START, K_END);
                REQUIRE(K_ACTUAL.m_hit.has_value() == K_EXPECTED.has_value());
                Json row { { "frame", frame_json(frame) },
                          { "start", point_json(K_START) },
                          { "end", point_json(K_END) },
                          { "endpoint_error_units", K_ACTUAL.m_world_endpoint_error_units },
                          { "hit", nullptr } };
                if (K_ACTUAL.m_hit) {
                    CHECK(K_ACTUAL.m_hit->m_fraction ==
                          Catch::Approx(required(K_EXPECTED).m_fraction).margin(2e-5));
                    CHECK(K_ACTUAL.m_hit->m_source_face == required(K_EXPECTED).m_source_face);
                    row["hit"] = { { "fraction", K_ACTUAL.m_hit->m_fraction },
                                  { "source_face", K_ACTUAL.m_hit->m_source_face },
                                  { "world_position", point_json(K_ACTUAL.m_hit->m_world_position) } };
                }
                product["rays"].push_back(std::move(row));
                CHECK(!checked(K_SHAPE->ray_cast(K_INPUT.binding(), frame, K_END, K_START)).m_hit);
            }
        }
    }
    if (const auto K_PATH = std::getenv("WE_NATIVE_SHAPE_PROBE_ROOT")) {
        const std::filesystem::path K_ROOT { K_PATH };
        std::filesystem::create_directories(K_ROOT);
        std::ofstream report(K_ROOT / "shape.json");
        report << product.dump(2) << '\n';
        REQUIRE(report.good());
        std::ofstream input(K_ROOT / "collider.bin", std::ios::binary);
        input.write(reinterpret_cast<const char*>(K_INPUT.canonical_bytes().data()),
                    static_cast<std::streamsize>(K_INPUT.canonical_bytes().size()));
        REQUIRE(input.good());
    }
}

TEST_CASE("native diagnostic shape rejects unsupported losses transforms and cancellation",
          "[native-shape]") {
    const auto K_INPUT = input_from(ColliderFixture {});
    const auto K_SHAPE = checked(build_native_static_collision(K_INPUT, K_INPUT.binding()));
    for (auto value : { -1.0,
                       1.1,
                       std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity() }) {
        CHECK(!build_native_static_collision(K_INPUT, K_INPUT.binding(), { value, 1e-4 }));
        CHECK(!build_native_static_collision(K_INPUT, K_INPUT.binding(), { 1e-4, value }));
        SimilarityTransform frame;
        frame.m_scale = value;
        if (value != 1.1) {
            CHECK(!K_SHAPE->ray_cast(K_INPUT.binding(), frame, { 1, 1, 1 }, { 1, 1, -1 }));
        }
    }
    CHECK(!K_SHAPE->ray_cast(K_INPUT.binding(), {}, {}, {}));
    CHECK(
        !K_SHAPE->ray_cast(K_INPUT.binding(), { { 1.000001, 0, 0, 0 }, {}, 1 }, { 1, 1, 1 }, { 1, 1, -1 }));
    CHECK(!K_SHAPE->ray_cast(K_INPUT.binding(), {}, { 999999.01, 0, 1 }, { 999999.02, 0, -1 }));
    CHECK(!K_SHAPE->ray_cast(K_INPUT.binding(), {}, { 1e7, 0, 1 }, { 1e7, 0, -1 }));
    CHECK(!K_SHAPE->ray_cast(
        K_INPUT.binding(), {}, { 0, 0, std::numeric_limits<double>::infinity() }, { 0, 0, -1 }));
    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(!decode_native_collider_input(
        K_INPUT.canonical_bytes(), K_INPUT.binding(), cancelled.get_token()));
    const auto K_BUILD =
        build_native_static_collision(K_INPUT, K_INPUT.binding(), {}, cancelled.get_token());
    REQUIRE(!K_BUILD);
    CHECK(K_BUILD.error().m_code == ErrorCode::CANCELLED);
    const auto K_QUERY =
        K_SHAPE->ray_cast(K_INPUT.binding(), {}, { 1, 1, 1 }, { 1, 1, -1 }, cancelled.get_token());
    REQUIRE(!K_QUERY);
    CHECK(K_QUERY.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(K_SHAPE->ray_cast(K_INPUT.binding(), {}, { .3, .4, 1 }, { .3, .4, -1 })).m_hit);
}

TEST_CASE(
    "native diagnostic shape refuses sanitizer collapse and independently bounded displacement",
    "[native-shape]") {
    const auto K_INPUT = input_from(ColliderFixture {});
    auto bytes = copy_bytes(K_INPUT);
    for (std::size_t vertex { 0uz }; vertex < 4uz; ++vertex) {
        put_real(bytes, vertex, 0uz, 900000 + static_cast<double>(vertex) * .001);
    }
    // Four nondegenerate binary64 vertices; all X values collapse in float32.
    const auto K_NARROW = checked(reselect(bytes, K_INPUT.binding()));
    CHECK(!build_native_static_collision(K_NARROW, K_NARROW.binding(), { 1, 1e-4 }));
    bytes = copy_bytes(K_INPUT);
    put_real(bytes, 0uz, 2uz, .500000003);
    const auto K_DECIMAL = checked(reselect(bytes, K_INPUT.binding()));
    CHECK(!build_native_static_collision(K_DECIMAL, K_DECIMAL.binding(), { 0, 1e-4 }));
    CHECK(build_native_static_collision(K_DECIMAL, K_DECIMAL.binding(), { 1e-4, 1e-4 }));
    // A broad range makes Jolt compression's decoded displacement exceed a
    // stricter independent policy even though binary32 input is exact.
    bytes = copy_bytes(K_INPUT);
    put_real(bytes, 0uz, 0uz, 10000.125);
    const auto K_WIDE = checked(reselect(bytes, K_INPUT.binding()));
    CHECK(!build_native_static_collision(K_WIDE, K_WIDE.binding(), { 0, 1e-4 }));
    const auto K_LOOSE = build_native_static_collision(K_WIDE, K_WIDE.binding(), { 1, 1e-4 });
    REQUIRE(K_LOOSE);
    CHECK((*K_LOOSE)->report().m_local_vertex_error_units > 0);
    CHECK((*K_LOOSE)->report().m_local_vertex_error_units <= 1);
}
