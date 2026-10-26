#include "../fixtures/native_assets_fixture.hpp"
#include "world_explorer/application/native_pbr_scene.hpp"
#include "world_explorer/render_core/pbr.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cstdlib>
#include <limits>

using namespace world_explorer;
using namespace world_explorer::native_assets_test;

#include <span>

namespace {

SceneSnapshot scene(NativeFixture& fixture, double copy_x = 30, SceneRevisionId revision = { 1 }) {
    auto input = fixture.request();
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    input.m_instances.m_instances[1uz].m_world_from_asset.m_translation.m_x = copy_x;
    input.m_instances.m_revision = revision;
    const auto K_ASSETS = checked(inspect_native_assets(input));
    const NativeExportRequest K_REQUEST { K_ASSETS, fixture.m_receipt, input.m_expected, {} };
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    return checked(compile_native_pbr_scene(K_PACKAGE, input.m_expected));
}

template <class Chunk> decltype(auto) appearance(Chunk& chunk) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_pbr) {
        throw std::logic_error("PBR fixture expected");
    }
    return *chunk.m_mesh->m_pbr;
}

}  // namespace

TEST_CASE(
    "native PBR scene compilation preserves actual geometry images instances and source identity",
    "[pbr-scene]") {
    NativeFixture fixture;
    const auto K_SCENE = scene(fixture);
    const auto K_CHUNK = K_SCENE->m_chunks.begin()->second;
    const auto& pbr = appearance(*K_CHUNK);
    CHECK(K_SCENE->m_scale == ScaleStatus::ARBITRARY);
    CHECK(K_SCENE->m_provenance.at("source_kind") == "synthetic_diagnostic");
    CHECK(pbr.m_definition.m_asset == AssetId { 42 });
    CHECK(pbr.m_instances.size() == 2uz);
    CHECK(pbr.m_materials.size() == 2uz);
    CHECK(pbr.m_uv.size() == 4uz);
    CHECK(pbr.m_triangle_materials == std::vector<std::uint32_t> { 0, 1 });
    CHECK(pbr.m_materials[0uz].m_images[0uz].size() == 16uz);
    CHECK(pbr.m_instances[0uz].m_chunk_from_asset.m_scale == 1);
    CHECK(validate_scene(*K_SCENE));
    const auto K_UPLOAD = checked(prepare_pbr_upload(*K_SCENE, {}, 16uz));
    CHECK(K_UPLOAD.m_vertices == 4uz);
    CHECK(K_UPLOAD.m_triangles == 2uz);
    CHECK(K_UPLOAD.m_draws.size() == 2uz);
    CHECK(K_UPLOAD.m_draws[0uz].m_vertex_offset == K_UPLOAD.m_draws[1uz].m_vertex_offset);
    CHECK(K_UPLOAD.m_draws[0uz].m_instance_words == K_UPLOAD.m_draws[1uz].m_instance_words);
    CHECK(K_UPLOAD.m_draws[0uz].m_instance_count == 2);
    CHECK(K_UPLOAD.m_draws[0uz].m_material_words != K_UPLOAD.m_draws[1uz].m_material_words);
    CHECK(!MeshTextureBaker::create(*K_CHUNK));
    const auto K_DIRECTORY = fixture.m_output.m_root / "pbr.wexplore";
    REQUIRE(write_bundle(K_DIRECTORY, K_SCENE));
    const auto K_READ = checked(load_bundle(K_DIRECTORY));
    CHECK(scene_content_hash(*K_READ) == scene_content_hash(*K_SCENE));
    CHECK(appearance(*K_READ->m_chunks.begin()->second).m_materials[0uz].m_images[0uz] ==
          pbr.m_materials[0uz].m_images[0uz]);
#if defined(__linux__)
    if (auto output = std::getenv("WE_PBR_SCENE_PROBE_OUTPUT")) {
        MaterialFixtureMutations colored;
        colored.m_channel = [](std::string& bytes, MaterialChannelRole role) {
            const std::array<std::array<float, 3>, 4> K_COLORS {
                { { .8F, .05F, .02F }, { .02F, .6F, .08F }, { .04F, .1F, .85F }, { .7F, .5F, .02F } } };
            const std::array<std::array<float, 3>, 4> K_NORMALS {
                { { 0, 0, 1 }, { .6F, 0, .8F }, { 0, -.6F, .8F }, { .70710677F, 0, .70710677F } } };
            for (std::size_t pixel { 0uz }; pixel < 4uz; ++pixel) {
                if (role == MaterialChannelRole::BASE_COLOR ||
                    role == MaterialChannelRole::NORMAL || role == MaterialChannelRole::EMISSION) {
                    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                        const float K_VALUE { role == MaterialChannelRole::EMISSION ? .01F
                                             : role == MaterialChannelRole::NORMAL
                                                 ? K_NORMALS[pixel][channel]
                                                 : K_COLORS[pixel][channel] };
                        put(bytes,
                            64uz + (pixel * 3uz + channel) * 4uz,
                            std::bit_cast<std::uint32_t>(K_VALUE),
                            4uz);
                    }
                } else if (role == MaterialChannelRole::ROUGHNESS) {
                    const float K_VALUE { static_cast<float>(pixel + 1uz) / 4 };
                    put(bytes, 64uz + pixel * 4uz, std::bit_cast<std::uint32_t>(K_VALUE), 4uz);
                }
            }
        };
        NativeFixture preview(false, {}, AssetLightingMode::ESTIMATED_PBR, colored);
        REQUIRE(write_bundle(output, scene(preview)));
    }
#endif
}

TEST_CASE(
    "native PBR updates use ordinary replay replacement cancellation and geometry invalidation",
    "[pbr-scene]") {
    NativeFixture fixture;
    const auto K_A = scene(fixture);
    auto next = std::make_shared<SceneState>(*scene(fixture, 33, { 2 }));
    auto changed = std::make_shared<SceneChunk>(*next->m_chunks.begin()->second);
    changed->m_generation = 2;
    next->m_chunks[changed->m_id] = changed;
    SceneStore store(K_A->m_id);
    REQUIRE(store.reset(K_A));
    const SceneTransaction K_CHANGE {
        K_A->m_id,
        K_A->m_epoch,
        K_A->m_revision,
        { 2 },
        { 77 },
        { { SceneOperationKind::REPLACE, changed->m_id, changed, {}, {} } } };
    REQUIRE(store.commit(K_CHANGE));
    CHECK(store.commit(K_CHANGE));
    CHECK(store.acquire_snapshot()->m_revision == SceneRevisionId { 2 });
    RendererUpdateSink sink;
    auto batch = checked(make_snapshot_batch(K_A, { 1 }));
    const auto K_TICKET = checked(sink.enqueue(batch));
    REQUIRE(sink.take_next());
    sink.acknowledge_uploaded(K_TICKET, 10);
    sink.acknowledge_visible(K_TICKET, K_A, 10);
    sink.retire_completed(10);
    auto delta = checked(store.changes_since(K_A->m_revision, { 1024uz * 1024uz }));
    const auto K_NEXT = checked(sink.enqueue(delta));
    sink.cancel_pending();
    CHECK(checked(sink.query_progress(K_NEXT)).m_state == UpdateState::CANCELLED);
    auto reloaded = checked(make_snapshot_batch(store.acquire_snapshot(), { 2 }));
    const auto K_RELOAD = checked(sink.enqueue(reloaded));
    REQUIRE(sink.take_next());
    sink.acknowledge_uploaded(K_RELOAD, 20);
    sink.acknowledge_visible(K_RELOAD, store.acquire_snapshot(), 20);
    sink.retire_completed(20);
    CHECK(sink.visible_snapshot()->m_revision == SceneRevisionId { 2 });
    const SceneTransaction K_INVALIDATE {
        K_A->m_id,
        K_A->m_epoch,
        { 2 },
        { 3 },
        { 78 },
        { { SceneOperationKind::INVALIDATE_GEOMETRY, changed->m_id, {}, {}, { 6 } } } };
    REQUIRE(store.commit(K_INVALIDATE));
    CHECK(checked(prepare_pbr_upload(*store.acquire_snapshot(), {}, 16uz)).m_draws.empty());
}

TEST_CASE("native PBR snapshot validation rejects altered source pixels groups frames samplers and "
          "context",
          "[pbr-scene]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    NativeFixture fixture;
    auto altered = std::make_shared<SceneState>(*scene(fixture));
    auto chunk = std::make_shared<SceneChunk>(*altered->m_chunks.begin()->second);
    auto& pbr = appearance(*chunk);
    switch (K_CASE) {
    case 0:
        pbr.m_materials[0uz].m_images[0uz][0uz] ^= std::byte { 1 };
        break;
    case 1:
        pbr.m_uv[0uz][0uz] = .125F;
        break;
    case 2:
        pbr.m_triangle_materials[0uz] = 1;
        break;
    case 3:
        pbr.m_materials[0uz].m_linear_filter = !pbr.m_materials[0uz].m_linear_filter;
        break;
    case 4:
        pbr.m_tangents[0uz][3uz] = -1;
        break;
    case 5:
        pbr.m_instances[1uz].m_chunk_from_asset.m_translation.m_x += 1;
        break;
    case 6:
        pbr.m_synthetic = false;
        break;
    case 7:
        pbr.m_epoch = { 4 };
        break;
    case 8:
        chunk->m_pose_revision = { 6 };
        break;
    case 9:
        pbr.m_materials[0uz].m_lighting = AssetLightingMode::CAPTURED_APPEARANCE;
        break;
    case 10:
        pbr.m_instances[1uz].m_instance = pbr.m_instances[0uz].m_instance;
        break;
    case 11:
        altered->m_scale = ScaleStatus::METRIC_MEASURED;
        break;
    case 12:
        pbr.m_export_manifest += ' ';
        break;
    default:
        throw std::logic_error("Unexpected PBR mutation");
    }
    altered->m_chunks[chunk->m_id] = chunk;
    CHECK(!validate_scene(*altered));
    CHECK(!make_snapshot_batch(altered, { 1 }));
}

TEST_CASE("native PBR binary decoder enforces bounded counts truncation and exact payload length",
          "[pbr-scene]") {
    NativeFixture fixture;
    const auto K_SCENE = scene(fixture);
    const auto& pbr = appearance(*K_SCENE->m_chunks.begin()->second);
    const auto K_BYTES = checked(encode_pbr_appearance(pbr));
    CHECK(checked(encode_pbr_appearance(checked(decode_pbr_appearance(K_BYTES)))) == K_BYTES);
    for (std::size_t size { 0uz }; size < std::min(128uz, K_BYTES.size()); ++size) {
        CHECK(!decode_pbr_appearance(std::span(K_BYTES).first(size)));
    }
    auto extra = K_BYTES;
    extra.push_back(std::byte {});
    CHECK(!decode_pbr_appearance(extra));
    auto oversized = pbr;
    oversized.m_instances.resize(65uz);
    CHECK(!encode_pbr_appearance(oversized));
    CHECK(!prepare_pbr_upload(*K_SCENE, {}, 3uz));
    CHECK(!prepare_pbr_upload(*K_SCENE, { 1e20, 0, 0 }, 16uz));
    auto lighting = PbrLighting {};
    CHECK(validate_pbr_lighting(lighting));
    lighting.m_to_light = {};
    CHECK(!validate_pbr_lighting(lighting));
    lighting = {};
    lighting.m_exposure = std::numeric_limits<double>::quiet_NaN();
    CHECK(!validate_pbr_lighting(lighting));
}
