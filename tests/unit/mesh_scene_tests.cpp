#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <chrono>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

template <class Chunk> decltype(auto) mesh_of(Chunk& chunk) {
    if (!chunk.m_mesh) throw std::runtime_error("Expected mesh fixture payload");
    return *chunk.m_mesh;
}

std::shared_ptr<SceneChunk> mesh_chunk() {
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = {{ -1, -1, -1 }, { 1, 1, 1 }};
    chunk->m_geometry_valid = true;
    chunk->m_pose_revision = { 1 };
    chunk->m_mesh = SceneMesh {
        {{{ -.5F, -.5F, 0 }, { 0, 0, 1 }}, {{ .5F, -.5F, 0 }, { 0, 0, 1 }}, {{ 0, .5F, 0 }, { 0, 0, 1 }}},
        {{ 0, 1, 2 }},
        {}};
    return chunk;
}

SceneSnapshot snapshot(const std::shared_ptr<SceneChunk>& chunk) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 9 };
    scene->m_revision = { 1 };
    scene->m_provenance = {{ "source_kind", "synthetic_diagnostic" }};
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

}  // namespace

TEST_CASE("mesh scenes bound all payloads and reject malformed raw geometry", "[mesh]") {
    auto chunk = mesh_chunk();
    const auto K_BASE = snapshot(chunk);
    const auto K_BYTES = validate_scene(*K_BASE);
    REQUIRE(K_BYTES);
    CHECK(*K_BYTES == 3uz * sizeof(FusionVertex) + 12uz);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6);
    switch (K_MUTATION) {
    case 0:
        mesh_of(*chunk).m_triangles[0][0] = 3;
        break;
    case 1:
        mesh_of(*chunk).m_triangles[0][0] = 1;
        break;
    case 2:
        mesh_of(*chunk).m_vertices[0].m_position[0] = std::numeric_limits<float>::quiet_NaN();
        break;
    case 3:
        mesh_of(*chunk).m_vertices[0].m_normal[0] = 1;
        break;
    case 4:
        mesh_of(*chunk).m_vertices[0].m_position[0] = 2;
        break;
    case 5:
        mesh_of(*chunk).m_observed = ObservedMeshSource {};
        break;
    case 6:
        chunk->m_pose_revision = {};
        break;
    default:
        FAIL("Unknown mesh mutation");
    }
    CHECK(validate_scene(*K_BASE).has_value() == false);
}

TEST_CASE("mesh scene count and byte limits include aggregate geometry", "[mesh]") {
    auto chunk = mesh_chunk();
    auto scene = std::make_shared<SceneState>(*snapshot(chunk));
    auto second = std::make_shared<SceneChunk>(*chunk);
    second->m_id = { 2 };
    scene->m_chunks.emplace(second->m_id, second);
    SceneLimits limits;
    limits.m_max_mesh_vertices = 6uz;
    limits.m_max_mesh_triangles = 2uz;
    limits.m_max_payload_bytes = 168uz;
    REQUIRE(validate_scene(*scene, limits));
    const auto K_LIMIT = GENERATE(0, 1, 2);
    if (K_LIMIT == 0) --limits.m_max_mesh_vertices;
    if (K_LIMIT == 1) --limits.m_max_mesh_triangles;
    if (K_LIMIT == 2) --limits.m_max_payload_bytes;
    CHECK(validate_scene(*scene, limits).has_value() == false);
}

TEST_CASE("mesh transactions deep copy inputs and fingerprint every vertex and index", "[mesh]") {
    SceneStore store({ 9 });
    auto initial = std::make_shared<SceneState>();
    initial->m_id = { 9 };
    initial->m_epoch = { 1 };
    initial->m_provenance = {{ "source_kind", "synthetic_diagnostic" }};
    REQUIRE(store.reset(initial));
    auto chunk = mesh_chunk();
    const SceneTransaction K_CREATE {
        { 9 }, { 1 }, {}, { 1 }, { 1 }, {{SceneOperationKind::CREATE, { 1 }, chunk, {}, {}}}};
    REQUIRE(store.commit(K_CREATE));
    const auto K_ORIGINAL = store.acquire_snapshot();
    REQUIRE(K_ORIGINAL->m_chunks.at({ 1 })->m_mesh);
    mesh_of(*chunk).m_vertices[0].m_position[0] = -.25F;
    CHECK(mesh_of(*K_ORIGINAL->m_chunks.at({ 1 })).m_vertices[0].m_position[0] == -.5F);
    const auto K_DUPLICATE = store.commit(K_CREATE);
    REQUIRE(K_DUPLICATE.has_value() == false);
    CHECK(K_DUPLICATE.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(store.acquire_snapshot() == K_ORIGINAL);
    auto replacement = std::make_shared<SceneChunk>(*K_ORIGINAL->m_chunks.at({ 1 }));
    replacement->m_generation = 2;
    mesh_of(*replacement).m_vertices[0].m_normal = { 0, 1, 0 };
    const SceneTransaction K_REPLACE {
        { 9 }, { 1 }, { 1 }, { 2 }, { 2 }, {{SceneOperationKind::REPLACE, { 1 }, replacement, {}, {}}}};
    REQUIRE(store.commit(K_REPLACE));
    CHECK(scene_content_hash(*K_ORIGINAL) != scene_content_hash(*store.acquire_snapshot()));
    REQUIRE(store.commit(
        {{ 9 }, { 1 }, { 2 }, { 3 }, { 3 }, {{SceneOperationKind::INVALIDATE_GEOMETRY, { 1 }, {}, {}, { 2 }}}}));
    CHECK(store.acquire_snapshot()->m_chunks.at({ 1 })->m_geometry_valid == false);
    CHECK(store.acquire_snapshot()->m_chunks.at({ 1 })->m_mesh.has_value());
    REQUIRE(
        store.commit({{ 9 }, { 1 }, { 3 }, { 4 }, { 4 }, {{SceneOperationKind::ERASE, { 1 }, {}, {}, {}}}}));
    CHECK(store.acquire_snapshot()->m_chunks.empty());
}

TEST_CASE("mesh renderer admission accounts for geometry and preserves failed retries", "[mesh]") {
    const auto K_SCENE = snapshot(mesh_chunk());
    auto batch = *make_snapshot_batch(K_SCENE, { 1 });
    RendererUpdateSink sink({ 83uz, 1uz, 1uz });
    REQUIRE(sink.enqueue(batch).has_value() == false);
    CHECK(batch.m_snapshot == K_SCENE);
    REQUIRE(sink.set_residency_budget({ 168uz, 1uz, 1uz }));
    auto prepared = sink.prepare(batch);
    REQUIRE(prepared);
    const auto K_TICKET = sink.enqueue(*prepared);
    REQUIRE(K_TICKET);
    auto next = sink.take_next();
    REQUIRE(next.has_value());
    if (!next) throw std::runtime_error("Missing admitted mesh update");
    sink.acknowledge_uploaded(*K_TICKET, 1);
    sink.acknowledge_visible(*K_TICKET, next.value().m_batch.m_snapshot, 1);
    CHECK(sink.visible_snapshot() == K_SCENE);
    auto changed = std::make_shared<SceneState>(*K_SCENE);
    auto chunk = std::make_shared<SceneChunk>(*changed->m_chunks.at({ 1 }));
    mesh_of(*chunk).m_triangles[0] = { 0, 2, 1 };
    changed->m_chunks[{ 1 }] = chunk;
    auto retry = *make_snapshot_batch(changed, { 1 });
    CHECK(sink.enqueue(retry).has_value() == false);
    CHECK(retry.m_snapshot == changed);
    CHECK(sink.visible_snapshot() == K_SCENE);
}

TEST_CASE("mesh diagnostics require synthetic provenance and never lose mesh on bundle export",
          "[mesh]") {
    auto scene = std::make_shared<SceneState>(*snapshot(mesh_chunk()));
    scene->m_provenance.clear();
    CHECK(validate_scene(*scene).has_value() == false);
    scene->m_provenance = {{ "source_kind", "synthetic_diagnostic" }};
    const auto K_RESULT = write_bundle({}, scene);
    REQUIRE(K_RESULT.has_value() == false);
    CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE("fusion scene requests reject malformed pinned JSON without output or caller mutation",
          "[mesh]") {
    const auto K_ROOT =
        std::filesystem::temp_directory_path() /
        ("we_mesh_request_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(K_ROOT));

    struct Cleanup {
        std::filesystem::path m_path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
    } cleanup { K_ROOT };

    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6);
    std::string text;
    switch (K_MUTATION) {
    case 0:
        text = "{}";
        break;
    case 1:
        text = "{\"schema_version\":1,\"schema_version\":1}";
        break;
    case 2:
        text = "{}" + std::string(1uz, '\0');
        break;
    case 3:
        text = "{\"nested\":{\"unsupported\":1}}";
        break;
    case 4:
        text = "[1,2,3]";
        break;
    case 5:
        text = std::string(40uz, '[') + "0" + std::string(40uz, ']');
        break;
    case 6:
        text = "{\"schema_version\":true}";
        break;
    default:
        FAIL("Unknown request mutation");
    }
    const auto K_PATH = K_ROOT / "request.json";
    const auto K_HASH = asset_files::publish_asset(K_PATH, std::as_bytes(std::span(text)), {});
    REQUIRE(K_HASH);
    const auto K_REJECTED = load_fusion_scene_request(K_PATH, *K_HASH);
    REQUIRE(K_REJECTED.has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(*sha256_file(K_PATH, 1024uz * 1024uz) == *K_HASH);
    CHECK(load_fusion_scene_request(K_PATH, std::string(64uz, '0')).has_value() == false);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = load_fusion_scene_request(K_PATH, *K_HASH, cancelled.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(std::distance(std::filesystem::directory_iterator(K_ROOT),
                        std::filesystem::directory_iterator {}) == 1);
}
