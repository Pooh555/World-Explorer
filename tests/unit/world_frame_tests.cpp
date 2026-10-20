#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/scene_store/gltf.hpp"
#include "world_explorer/scene_store/scene_store.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <limits>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

template <class Owner> decltype(auto) frame_of(Owner& owner) {
    if (!owner.m_world_frame) {
        throw std::runtime_error("Expected world-frame fixture");
    }
    return *owner.m_world_frame;
}

std::string digest(std::string_view text) {
    return sha256(std::as_bytes(std::span(text.data(), text.size())));
}

SceneState fixture(bool independent_profile = false) {
    SceneState scene;
    scene.m_id = { 31 };
    scene.m_epoch = { 7 };
    scene.m_revision = { 13 };
    scene.m_provenance = {
        { "source_kind",
         independent_profile ? "synthetic_observed_contract_fixture" : "synthetic_diagnostic" },
        { "source_inspection_view",
         R"({"schema_version":2,"position":[1,2,3],"forward":[0,0,1],"up":[0,1,0],"up_reference":"CAPTURE_ORIENTATION","speed":2,"vertical_fov_radians":1,"frame_name":"synthetic"})" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 9007199254740993ULL };
    chunk->m_geometry_valid = true;
    chunk->m_pose_revision = { 4 };
    chunk->m_bounds = { { 0, 0, 0 }, { 2, 3, 1 } };
    chunk->m_world_from_chunk.m_translation = { 10, -4, 3 };
    chunk->m_world_from_chunk.m_scale = 2;
    chunk->m_mesh = SceneMesh {
        { { { 0, 0, 0 }, { 0, 0, 1 } }, { { 2, 0, 0 }, { 0, 0, 1 } }, { { 0, 3, 0 }, { 0, 0, 1 } } }, { { 0, 1, 2 } }, {} };
    Gaussian splat;
    splat.m_mean = { .5F, .75F, .5F };
    splat.m_scale = { .2F, .3F, .4F };
    splat.m_sh_degree = 1;
    splat.m_sh[5uz] = .75F;
    chunk->m_splats.push_back(splat);
    if (independent_profile) {
        // Synthetic contract fixture for admitting the independent-input profile;
        // the test supplies declarations, not real-world measurement evidence.
        ObservedMeshSource source;
        source.m_context.m_epoch = scene.m_epoch;
        source.m_context.m_pose_revision = { chunk->m_pose_revision.m_value };
        source.m_manifest_json = source.m_receipt_json = "{}";
        source.m_context.m_manifest_sha256 = source.m_context.m_receipt_sha256 = digest("{}");
        source.m_asset_sha256 = source.m_context.m_pose_sha256 = source.m_context.m_recipe_sha256 =
            std::string(64uz, 'a');
        chunk->m_mesh->m_observed = source;
    }
    scene.m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

Json anchor(std::array<double, 3uz> weights) {
    return { { "chunk_id", 9007199254740993ULL }, { "triangle", 0U }, { "barycentric", weights } };
}

Json document(const SceneState& source, bool independent_profile = false) {
    return {
        { "schema", "WORLD_FRAME_ALIGNMENT_V1" },
        { "source_sha256", scene_content_hash(source) },
        { "source_epoch", source.m_epoch.m_value },
        { "source_revision", source.m_revision.m_value },
        { "evidence_kind", independent_profile ? "INDEPENDENT_MEASUREMENT" : "SYNTHETIC_DIAGNOSTIC" },
        { "distance",
         { { "a", anchor({ 1, 0, 0 }) },
          { "b", anchor({ 0, 1, 0 }) },
          { "metres", 8 },
          { "uncertainty_metres", .02 },
          { "method", independent_profile ? "MEASURED_DISTANCE" : "SYNTHETIC" },
          { "record", "Synthetic test record; not a physical measurement" } } },
        { "up",
         { { "direction", { 0, 0, 1 } },
          { "uncertainty_degrees", .5 },
          { "method", independent_profile ? "IMU" : "SYNTHETIC" },
          { "record", "Synthetic test up reference" } } },
        { "origin", anchor({ 1, 0, 0 }) } };
}

Result<WorldFrameAlignment> admit(const SceneState& source, const Json& value) {
    const auto K_TEXT = value.dump();
    return admit_world_frame(source, K_TEXT, digest(K_TEXT));
}

SceneState aligned_fixture(bool independent_profile = false) {
    auto scene = fixture(independent_profile);
    const auto K_FRAME = admit(scene, document(scene, independent_profile));
    if (!K_FRAME) {
        throw std::runtime_error(K_FRAME.error().m_message);
    }
    scene.m_world_frame = *K_FRAME;
    scene.m_scale = independent_profile ? ScaleStatus::METRIC_MEASURED : ScaleStatus::ARBITRARY;
    ++scene.m_epoch.m_value;
    ++scene.m_revision.m_value;
    return scene;
}

struct Scratch {
    std::filesystem::path m_path {
        std::filesystem::temp_directory_path() /
        ("we-world-frame-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) };

    Scratch() {
        std::filesystem::create_directory(m_path);
    }

    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) {
        throw std::runtime_error("Could not write synthetic fixture");
    }
}

void near(Vec3d actual, Vec3d expected) {
    CHECK(actual.m_x == Catch::Approx(expected.m_x).margin(1e-10));
    CHECK(actual.m_y == Catch::Approx(expected.m_y).margin(1e-10));
    CHECK(actual.m_z == Catch::Approx(expected.m_z).margin(1e-10));
}

}  // namespace

TEST_CASE("world alignment resolves pinned mesh anchors without changing source bytes",
          "[world-frame]") {
    const auto K_SOURCE = fixture();
    const auto K_ORIGINAL_HASH = scene_content_hash(K_SOURCE);
    const auto K_ALIGNED = aligned_fixture();
    REQUIRE(validate_scene(K_ALIGNED));
    REQUIRE(K_ALIGNED.m_world_frame);
    const auto& frame = frame_of(K_ALIGNED);
    CHECK(frame.m_world_from_source.m_scale == 2);
    near(transform_point(frame.m_world_from_source, { 10, -4, 3 }), {});
    near(transform_point(frame.m_world_from_source, { 14, -4, 3 }), { 8, 0, 0 });
    near(rotate(frame.m_world_from_source.m_rotation, { 0, 0, 1 }), { 0, 1, 0 });
    const auto& chunk = *K_ALIGNED.m_chunks.begin()->second;
    const auto K_TRANSFORM = scene_world_from_chunk(K_ALIGNED, chunk);
    near(transform_point(K_TRANSFORM, { .25, .5, .75 }), { 1, 3, -2 });
    near(world_bounds(K_ALIGNED).m_minimum, { 0, 0, -12 });
    near(world_bounds(K_ALIGNED).m_maximum, { 8, 4, 0 });
    CHECK(scene_chunk_content_hash(chunk) ==
          scene_chunk_content_hash(*K_SOURCE.m_chunks.begin()->second));
    CHECK(scene_content_hash(K_SOURCE) == K_ORIGINAL_HASH);
    CHECK(scene_content_hash(K_ALIGNED) != K_ORIGINAL_HASH);
    CHECK(K_ALIGNED.m_scale == ScaleStatus::ARBITRARY);
    CHECK(walking_block_reason(K_ALIGNED) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
    CHECK(world_frame_label(K_ALIGNED).find("synthetic") != std::string_view::npos);
    CHECK_FALSE(encode_baked_glb(std::make_shared<SceneState>(K_ALIGNED)));
    const auto K_CAMERA = source_view(K_ALIGNED);
    REQUIRE(K_CAMERA);
    near(K_CAMERA->m_camera.m_position, transform_point(frame.m_world_from_source, { 1, 2, 3 }));
    near(K_CAMERA->m_camera.forward(), { 0, 1, 0 });
    CHECK(K_CAMERA->m_camera.m_speed == 4);
    CHECK_FALSE(
        restore_bookmark({ K_SOURCE.m_id, K_SOURCE.m_epoch, {} }, K_ALIGNED.m_id, K_ALIGNED.m_epoch));
}

TEST_CASE("independent declaration admission does not certify geometry or walking",
          "[world-frame]") {
    const auto K_ALIGNED = aligned_fixture(true);
    REQUIRE(validate_scene(K_ALIGNED));
    CHECK(K_ALIGNED.m_scale == ScaleStatus::METRIC_MEASURED);
    CHECK(walking_block_reason(K_ALIGNED) == WalkingBlockReason::SUPPORTED_COLLISION_UNAVAILABLE);
    auto flag_only = fixture();
    flag_only.m_scale = ScaleStatus::METRIC_MEASURED;
    CHECK(walking_block_reason(flag_only) == WalkingBlockReason::MISSING_MEASUREMENT);
    CHECK(world_frame_label(flag_only).find("unavailable") != std::string_view::npos);
    auto source = fixture(true);
    CHECK_FALSE(admit(source, document(source, false)));
    source = fixture();
    CHECK_FALSE(admit(source, document(source, true)));
}

TEST_CASE("world alignment rejects malformed rehashed declarations and preserves source",
          "[world-frame]") {
    const auto K_SOURCE = fixture();
    const auto K_HASH = scene_content_hash(K_SOURCE);
    auto value = document(K_SOURCE);
    const auto K_CASE = GENERATE(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24);
    switch (K_CASE) {
    case 0:
        value["schema"] = "WORLD_FRAME_ALIGNMENT_V2";
        break;
    case 1:
        value["source_sha256"] = std::string(64uz, '0');
        break;
    case 2:
        value["source_epoch"] = 6U;
        break;
    case 3:
        value["source_revision"] = 12U;
        break;
    case 4:
        value["source_epoch"] = 7.;
        break;
    case 5:
        value["origin"]["chunk_id"] = 1U;
        break;
    case 6:
        value["origin"]["triangle"] = 1U;
        break;
    case 7:
        value["origin"]["barycentric"] = { 1, -.1, .1 };
        break;
    case 8:
        value["origin"]["barycentric"] = { 1, 0, .1 };
        break;
    case 9:
        value["distance"]["b"] = value["distance"]["a"];
        break;
    case 10:
        value["distance"]["metres"] = 0;
        break;
    case 11:
        value["distance"]["uncertainty_metres"] = -.1;
        break;
    case 12:
        value["distance"]["uncertainty_metres"] = 8;
        break;
    case 13:
        value["distance"]["method"] = "MONOCULAR_PRIOR";
        break;
    case 14:
        value["distance"]["record"] = "";
        break;
    case 15:
        value["up"]["direction"] = { 0, 0, 0 };
        break;
    case 16:
        value["up"]["method"] = "RECONSTRUCTED_FLOOR";
        break;
    case 17:
        value["up"]["uncertainty_degrees"] = 90;
        break;
    case 18:
        value["up"]["uncertainty_degrees"] = -.1;
        break;
    case 19:
        value["up"]["record"] = std::string(1025uz, 'a');
        break;
    case 20:
        value["up"]["record"] = std::string("x\0y", 3uz);
        break;
    case 21:
        value["walking_enabled"] = true;
        break;
    case 22:
        value["distance"]["metres"] = true;
        break;
    case 23:
        value["origin"]["triangle"] = -1;
        break;
    case 24:
        value["up"]["direction"] = { 0, 1 };
        break;
    default:
        FAIL("Unknown mutation");
    }
    INFO(K_CASE);
    CHECK_FALSE(admit(K_SOURCE, value));
    CHECK(scene_content_hash(K_SOURCE) == K_HASH);
}

TEST_CASE("world alignment bounds parsing and handles opposing up references", "[world-frame]") {
    const auto K_SOURCE = fixture();
    auto value = document(K_SOURCE);
    for (const auto& direction : std::array<Vec3d, 5uz> {
             { { 0, 1, 0 }, { 0, -1, 0 }, { 1, 2, 3 }, { 1e-8, -1, 0 }, { 0, 1e-200, 0 } } }) {
        value["up"]["direction"] = { direction.m_x, direction.m_y, direction.m_z };
        const auto K_FRAME = admit(K_SOURCE, value);
        REQUIRE(K_FRAME);
        near(rotate(K_FRAME->m_world_from_source.m_rotation,
                    direction * (1 / std::hypot(direction.m_x, direction.m_y, direction.m_z))),
             { 0, 1, 0 });
    }
    const auto K_TEXT = value.dump();
    CHECK_FALSE(admit_world_frame(K_SOURCE, K_TEXT, std::string(64uz, '0')));
    for (const auto& invalid :
         std::vector<std::string> { "",
                                   K_TEXT + std::string(1uz, '\0'),
                                   "{\"schema\":1,\"schema\":1}",
                                   std::string(9uz, '[') + std::string(9uz, ']'),
                                   std::string(K_WORLD_FRAME_DOCUMENT_BYTES + 1uz, ' ') }) {
        CHECK_FALSE(admit_world_frame(K_SOURCE, invalid, digest(invalid)));
    }
    value["distance"]["metres"] = 1e308;
    CHECK_FALSE(admit(K_SOURCE, value));
    auto exhausted = K_SOURCE;
    exhausted.m_epoch.m_value = std::numeric_limits<std::uint64_t>::max();
    CHECK_FALSE(admit(exhausted, document(exhausted)));
    auto stale = fixture();
    auto chunk = std::make_shared<SceneChunk>(*stale.m_chunks.begin()->second);
    chunk->m_geometry_valid = false;
    stale.m_chunks[chunk->m_id] = chunk;
    CHECK_FALSE(admit(stale, document(stale)));
}

TEST_CASE("alignment publication advances epoch atomically and replay is idempotent",
          "[world-frame]") {
    const auto K_SOURCE = fixture();
    const auto K_FRAME = admit(K_SOURCE, document(K_SOURCE));
    REQUIRE(K_FRAME);
    SceneStore store(K_SOURCE.m_id);
    REQUIRE(store.reset(std::make_shared<SceneState>(K_SOURCE)));
    const auto K_PRIOR = store.acquire_snapshot();
    SceneTransaction transaction { K_SOURCE.m_id,
                                  K_SOURCE.m_epoch,
                                  K_SOURCE.m_revision,
                                  { K_SOURCE.m_revision.m_value + 1 },
                                  { 74 },
                                  {},
                                  *K_FRAME };
    REQUIRE(store.commit(transaction));
    const auto K_ALIGNED = store.acquire_snapshot();
    CHECK(K_ALIGNED->m_epoch.m_value == K_SOURCE.m_epoch.m_value + 1);
    CHECK(K_ALIGNED->m_chunks.begin()->second == K_PRIOR->m_chunks.begin()->second);
    CHECK(store.commit(transaction).value() == K_ALIGNED->m_revision);
    CHECK_FALSE(store.changes_since(K_SOURCE.m_revision, { 1000000uz }));
    transaction.m_world_frame->m_document_json += ' ';
    const auto K_CONFLICT = store.commit(transaction);
    REQUIRE_FALSE(K_CONFLICT);
    CHECK(K_CONFLICT.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(store.acquire_snapshot() == K_ALIGNED);
    SceneTransaction correction { K_ALIGNED->m_id,
                                 K_ALIGNED->m_epoch,
                                 K_ALIGNED->m_revision,
                                 { K_ALIGNED->m_revision.m_value + 1 },
                                 { 75 },
                                 {} };
    const auto K_CORRECTION = store.commit(correction);
    REQUIRE_FALSE(K_CORRECTION);
    CHECK(K_CORRECTION.error().m_code == ErrorCode::RESYNC_REQUIRED);
    auto corrected = std::make_shared<SceneState>(K_SOURCE);
    corrected->m_epoch.m_value = K_ALIGNED->m_epoch.m_value + 1;
    REQUIRE(store.reset(corrected));
    CHECK_FALSE(store.acquire_snapshot()->m_world_frame);
    CHECK(walking_block_reason(*store.acquire_snapshot()) ==
          WalkingBlockReason::MISSING_MEASUREMENT);
}

TEST_CASE("failed alignment admission preserves store and renderer identity includes the document",
          "[world-frame]") {
    const auto K_SOURCE = fixture();
    SceneStore store(K_SOURCE.m_id);
    REQUIRE(store.reset(std::make_shared<SceneState>(K_SOURCE)));
    const auto K_PRIOR = store.acquire_snapshot();
    auto frame = *admit(K_SOURCE, document(K_SOURCE));
    frame.m_world_from_source.m_scale += 1;
    SceneTransaction transaction { K_SOURCE.m_id,
                                  K_SOURCE.m_epoch,
                                  K_SOURCE.m_revision,
                                  { K_SOURCE.m_revision.m_value + 1 },
                                  { 87 },
                                  {},
                                  frame };
    CHECK_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot() == K_PRIOR);
    transaction.m_world_frame = *admit(K_SOURCE, document(K_SOURCE));
    transaction.m_changes.push_back(
        { SceneOperationKind::ERASE, K_SOURCE.m_chunks.begin()->first, {}, {}, {} });
    CHECK_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot() == K_PRIOR);
    RendererUpdateSink sink;
    auto aligned = std::make_shared<SceneState>(aligned_fixture());
    auto batch = *make_snapshot_batch(aligned, { 88 });
    REQUIRE(sink.enqueue(batch));
    auto changed_document = document(K_SOURCE);
    changed_document["up"]["record"] = "Another synthetic record";
    auto other = std::make_shared<SceneState>(*aligned);
    other->m_world_frame = *admit(K_SOURCE, changed_document);
    auto other_batch = *make_snapshot_batch(other, { 88 });
    const auto K_OTHER = sink.enqueue(other_batch);
    REQUIRE_FALSE(K_OTHER);
    CHECK(K_OTHER.error().m_code == ErrorCode::DUPLICATE_OPERATION);
}

TEST_CASE("alignment bundles and shared application preserve original chunk bytes",
          "[world-frame]") {
    Scratch scratch;
    const bool K_INDEPENDENT { GENERATE(false, true) };
    const auto K_SOURCE = std::make_shared<SceneState>(fixture(K_INDEPENDENT));
    REQUIRE(write_bundle(scratch.m_path / "source", K_SOURCE));
    const auto K_TEXT = document(*K_SOURCE, K_INDEPENDENT).dump();
    const auto K_DOCUMENT = scratch.m_path / "measurement.json";
    write(K_DOCUMENT, K_TEXT);
    const auto K_OUTPUT = scratch.m_path / "aligned";
    REQUIRE(align_world_scene({ scratch.m_path / "source", K_DOCUMENT, digest(K_TEXT), K_OUTPUT }));
    const auto K_LOADED = load_bundle(K_OUTPUT);
    REQUIRE(K_LOADED);
    CHECK(scene_content_hash(**K_LOADED) == scene_content_hash(aligned_fixture(K_INDEPENDENT)));
    for (const auto& entry : std::filesystem::directory_iterator(scratch.m_path / "source")) {
        if (entry.path().filename() == "manifest.json") {
            continue;
        }
        const auto K_ORIGINAL = asset_files::read_bytes(entry.path(), 1000000uz, {});
        const auto K_COPY =
            asset_files::read_bytes(K_OUTPUT / entry.path().filename(), 1000000uz, {});
        REQUIRE(K_ORIGINAL);
        REQUIRE(K_COPY);
        CHECK(*K_ORIGINAL == *K_COPY);
    }
    CHECK_FALSE(
        align_world_scene({ scratch.m_path / "source", K_DOCUMENT, digest(K_TEXT), K_OUTPUT }));
    CHECK_FALSE(align_world_scene({ scratch.m_path / "source",
                                   K_DOCUMENT,
                                   digest(K_TEXT),
                                   scratch.m_path / "source" / "nested" }));
    CHECK_FALSE(align_world_scene(
        { scratch.m_path / "source", K_DOCUMENT, std::string(64uz, '0'), scratch.m_path / "bad" }));
    CHECK_FALSE(std::filesystem::exists(scratch.m_path / "bad"));
    std::stop_source stopped;
    stopped.request_stop();
    CHECK_FALSE(align_world_scene(
        { scratch.m_path / "source", K_DOCUMENT, digest(K_TEXT), scratch.m_path / "cancelled" },
        stopped.get_token()));
    CHECK_FALSE(std::filesystem::exists(scratch.m_path / "cancelled"));
}

TEST_CASE("rehashed bundle alignment corruption cannot promote stale or unsupported frames",
          "[world-frame]") {
    Scratch scratch;
    const auto K_SCENE = std::make_shared<SceneState>(aligned_fixture());
    REQUIRE(write_bundle(scratch.m_path / "aligned", K_SCENE));
    const auto K_PATH = scratch.m_path / "aligned" / "manifest.json";
    const auto K_BYTES = asset_files::read_bytes(K_PATH, 1000000uz, {});
    REQUIRE(K_BYTES);
    auto manifest =
        Json::parse(std::string(reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size()));
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    switch (K_CASE) {
    case 0:
        manifest["coordinates"]["scale_status"] = "METRIC_MEASURED";
        break;
    case 1:
        manifest["epoch"] = 9U;
        break;
    case 2:
        manifest["revision"] = 15U;
        break;
    case 3:
        manifest["payload_schema_version"] = 5U;
        break;
    case 4:
        manifest["schema_version"] = 2U;
        break;
    case 5:
        manifest["world_frame_sha256"] = std::string(64uz, '0');
        break;
    case 6: {
        auto value = document(fixture());
        value["source_sha256"] = std::string(64uz, '0');
        const auto K_TEXT = value.dump();
        write(scratch.m_path / "aligned" / manifest.at("world_frame").get<std::string>(), K_TEXT);
        manifest["world_frame_sha256"] = digest(K_TEXT);
        break;
    }
    default:
        FAIL("Unknown bundle mutation");
    }
    write(K_PATH, manifest.dump());
    CHECK_FALSE(load_bundle(scratch.m_path / "aligned"));
}

TEST_CASE("world-frame proof caches and source pose dependencies cannot be edited independently",
          "[world-frame]") {
    auto aligned = aligned_fixture();
    REQUIRE(aligned.m_world_frame);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    switch (K_CASE) {
    case 0:
        frame_of(aligned).m_world_from_source.m_translation.m_x += 1;
        break;
    case 1:
        frame_of(aligned).m_world_from_source.m_rotation = {};
        break;
    case 2:
        frame_of(aligned).m_source_epoch.m_value = 6;
        break;
    case 3:
        frame_of(aligned).m_source_revision.m_value = 12;
        break;
    case 4:
        frame_of(aligned).m_synthetic = false;
        break;
    case 5: {
        auto chunk = std::make_shared<SceneChunk>(*aligned.m_chunks.begin()->second);
        ++chunk->m_pose_revision.m_value;
        aligned.m_chunks[chunk->m_id] = chunk;
        break;
    }
    case 6:
        aligned.m_provenance["correction"] = "changed";
        break;
    default:
        FAIL("Unknown proof mutation");
    }
    CHECK_FALSE(validate_scene(aligned));
    CHECK(walking_block_reason(aligned) == WalkingBlockReason::MISSING_MEASUREMENT);
}

TEST_CASE("alignment payload admission includes the measurement record byte budget",
          "[world-frame]") {
    const auto K_ALIGNED = aligned_fixture();
    const auto K_BYTES = validate_scene(K_ALIGNED);
    REQUIRE(K_BYTES);
    const auto K_SOURCE_BYTES = validate_scene(fixture());
    REQUIRE(K_SOURCE_BYTES);
    REQUIRE(K_ALIGNED.m_world_frame);
    CHECK(*K_BYTES == *K_SOURCE_BYTES + frame_of(K_ALIGNED).m_document_json.size());
    SceneLimits limits;
    limits.m_max_payload_bytes = *K_BYTES;
    CHECK(validate_scene(K_ALIGNED, limits));
    --limits.m_max_payload_bytes;
    CHECK_FALSE(validate_scene(K_ALIGNED, limits));
    RendererUpdateSink sink({ *K_BYTES - 1uz, 100uz, 2uz });
    const auto K_BATCH = make_snapshot_batch(std::make_shared<SceneState>(K_ALIGNED), { 81 });
    REQUIRE(K_BATCH);
    CHECK_FALSE(sink.prepare(*K_BATCH));
}
