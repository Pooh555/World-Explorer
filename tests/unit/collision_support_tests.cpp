#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/application/collision_inspection.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/collision_support.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <fstream>
#include <limits>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

using Json = nlohmann::json;

template <class Owner> decltype(auto) frame_of(Owner& owner) {
    if (!owner.m_world_frame) {
        throw std::runtime_error("Expected aligned fixture");
    }
    return *owner.m_world_frame;
}

template <class Owner> decltype(auto) mesh_of(Owner& owner) {
    if (!owner.m_mesh) {
        throw std::runtime_error("Expected mesh fixture");
    }
    return *owner.m_mesh;
}

Result<std::shared_ptr<const CollisionSupport>> admit(const SceneState& source, const Json& value) {
    const auto K_TEXT = value.dump();
    return admit_collision_support(source, K_TEXT, collision_digest(K_TEXT));
}

constexpr CapsuleSweep K_SWEEP { { 1.5, 0, 1.5 }, { 4.5, 0, 1.5 }, .2, 1.7 };

void set_cell(Json& value, std::array<std::size_t, 3uz> index, char state) {
    auto& cells = value["grid"]["states"].get_ref<std::string&>();
    cells.at(index[0uz] + 8uz * (index[1uz] + 4uz * index[2uz])) = state;
}

struct Scratch {
    std::filesystem::path m_path {
        std::filesystem::temp_directory_path() /
        ("we-collision-support-" +
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
        throw std::runtime_error("Could not write collision fixture");
    }
}

}  // namespace

TEST_CASE(
    "collision support distinguishes explicit clearance occupied unknown and unsupported floor",
    "[collision-support]") {
    const auto K_SOURCE = collision_source();
    auto document = collision_document(K_SOURCE);
    const auto K_SUPPORT = admit(K_SOURCE, document);
    REQUIRE(K_SUPPORT);
    CHECK((*K_SUPPORT)->cells().size() == 256uz);
    CHECK((*K_SUPPORT)->floors().size() == 64uz);
    const auto K_CLEAR = probe_collision_support(**K_SUPPORT, K_SOURCE, K_SWEEP);
    REQUIRE(K_CLEAR);
    CHECK(K_CLEAR->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    CHECK(K_CLEAR->m_checked_cells == 8uz);
    for (char state : { '#', '?' }) {
        set_cell(document, { 3uz, 0uz, 1uz }, state);
        const auto K_BLOCKED_SUPPORT = admit(K_SOURCE, document);
        REQUIRE(K_BLOCKED_SUPPORT);
        const auto K_BLOCKED = probe_collision_support(**K_BLOCKED_SUPPORT, K_SOURCE, K_SWEEP);
        REQUIRE(K_BLOCKED);
        CHECK(K_BLOCKED->m_state ==
              (state == '#' ? ClearanceState::OCCUPIED : ClearanceState::UNKNOWN));
        CHECK(K_BLOCKED->m_checked_cells < K_CLEAR->m_checked_cells);
    }
    const auto K_UNSUPPORTED =
        probe_collision_support(**K_SUPPORT, K_SOURCE, { { 3.5, 0, 4.5 }, { 3.5, 0, 4.5 }, .2, 1.7 });
    REQUIRE(K_UNSUPPORTED);
    CHECK(K_UNSUPPORTED->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    CHECK(walking_block_reason(K_SOURCE) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("continuous collision envelope catches thin barriers boundaries and missing headroom",
          "[collision-support]") {
    const auto K_SOURCE = collision_source();
    auto document = collision_document(K_SOURCE);
    set_cell(document, { 3uz, 0uz, 1uz }, '#');
    set_cell(document, { 1uz, 2uz, 1uz }, '?');
    const auto K_SUPPORT = admit(K_SOURCE, document);
    REQUIRE(K_SUPPORT);
    auto start = K_SWEEP;
    start.m_end_feet = start.m_start_feet;
    REQUIRE(probe_collision_support(**K_SUPPORT, K_SOURCE, start));
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, start)->m_state ==
          ClearanceState::CLEAR_DIAGNOSTIC);
    auto end = K_SWEEP;
    end.m_start_feet = end.m_end_feet;
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, end)->m_state ==
          ClearanceState::CLEAR_DIAGNOSTIC);
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, K_SWEEP)->m_state ==
          ClearanceState::OCCUPIED);
    start.m_height = 2;
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, start)->m_state ==
          ClearanceState::UNKNOWN);
    start.m_height = 1.7;
    for (const auto& feet :
         std::array<Vec3d, 3uz> { { { .2, 0, 1.5 }, { 7.8, 0, 1.5 }, { 1.5, 0, -1e100 } } }) {
        start.m_start_feet = start.m_end_feet = feet;
        CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, start)->m_state ==
              ClearanceState::UNKNOWN);
    }
    document = collision_document(K_SOURCE);
    set_cell(document, { 2uz, 0uz, 1uz }, '?');
    const auto K_BOUNDARY = admit(K_SOURCE, document);
    REQUIRE(K_BOUNDARY);
    CHECK(
        probe_collision_support(**K_BOUNDARY, K_SOURCE, { { 1.75, 0, 1.5 }, { 1.75, 0, 1.5 }, .25, 1.7 })
            ->m_state == ClearanceState::UNKNOWN);
}

TEST_CASE("collision admission rejects rehashed ambiguous unsupported and stale dependencies",
          "[collision-support]") {
    const auto K_SOURCE = collision_source();
    const auto K_SOURCE_HASH = scene_content_hash(K_SOURCE);
    auto value = collision_document(K_SOURCE);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    switch (K_CASE) {
    case 0:
        value["schema"] = "MEASURED_COLLISION_SUPPORT_V1";
        break;
    case 1:
        value["scene_sha256"] = std::string(64uz, '0');
        break;
    case 2:
        value["epoch"] = 1U;
        break;
    case 3:
        value["revision"] = 1U;
        break;
    case 4:
        value["world_frame_sha256"] = std::string(64uz, '0');
        break;
    case 5:
        value["grid"]["dimensions"] = { 64U, 65U, 64U };
        break;
    case 6:
        value["grid"]["dimensions"] = { 8., 4U, 8U };
        break;
    case 7:
        value["grid"]["cell_size"] = 0;
        break;
    case 8:
        value["grid"]["cell_size"] = 1e-7;
        break;
    case 9:
        value["grid"]["origin"] = { 1e100, 0, 0 };
        break;
    case 10:
        value["grid"]["states"] = std::string(255uz, '.');
        break;
    case 11:
        value["grid"]["states"] = std::string(256uz, 'F');
        break;
    case 12:
        value["floors"][0uz]["chunk_id"] = 1U;
        break;
    case 13:
        value["floors"][0uz]["generation"] = 2U;
        break;
    case 14:
        value["floors"][0uz]["pose_revision"] = 3U;
        break;
    case 15:
        value["floors"][0uz]["chunk_sha256"] = std::string(64uz, '0');
        break;
    case 16:
        value["floors"][0uz]["triangles"] = { 0U, 0U };
        break;
    case 17:
        value["floors"][0uz]["triangles"] = { 2U };
        break;
    case 18:
        value["walking_enabled"] = true;
        break;
    default:
        FAIL("Unknown collision mutation");
    }
    INFO(K_CASE);
    CHECK_FALSE(admit(K_SOURCE, value));
    CHECK(scene_content_hash(K_SOURCE) == K_SOURCE_HASH);
}

TEST_CASE("collision admission bounds parsing and refuses metric flags without a frame",
          "[collision-support]") {
    auto source = collision_source();
    const auto K_TEXT = collision_document(source).dump();
    CHECK_FALSE(admit_collision_support(source, K_TEXT, std::string(64uz, '0')));
    for (const auto& text :
         std::vector<std::string> { "",
                                   K_TEXT + std::string(1uz, '\0'),
                                   "{\"schema\":1,\"schema\":1}",
                                   std::string(9uz, '[') + std::string(9uz, ']'),
                                   std::string(K_COLLISION_SUPPORT_DOCUMENT_BYTES + 1uz, ' ') }) {
        CHECK_FALSE(admit_collision_support(source, text, collision_digest(text)));
    }
    source.m_world_frame.reset();
    source.m_scale = ScaleStatus::METRIC_MEASURED;
    const auto K_REFUSED = admit_collision_support(source, K_TEXT, collision_digest(K_TEXT));
    REQUIRE_FALSE(K_REFUSED);
    CHECK(K_REFUSED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE("supported collision floors require exact current upward flat mesh coverage",
          "[collision-support]") {
    auto source = collision_source();
    // Rebuild the source frame after changing original geometry; old proof cannot follow it.
    REQUIRE(source.m_world_frame);
    source.m_epoch = frame_of(source).m_source_epoch;
    source.m_revision = frame_of(source).m_source_revision;
    source.m_world_frame.reset();
    auto chunk = std::make_shared<SceneChunk>(*source.m_chunks.begin()->second);
    REQUIRE(chunk->m_mesh);
    const auto K_CASE = GENERATE(0, 1, 2);
    switch (K_CASE) {
    case 0:
        mesh_of(*chunk).m_triangles[1uz] = { 1, 3, 2 };
        break;
    case 1:
        mesh_of(*chunk).m_vertices[3uz].m_position[1uz] = .125F;
        chunk->m_bounds.m_maximum.m_y = .125;
        break;
    case 2:
        mesh_of(*chunk).m_vertices[3uz].m_position[0uz] = 7.5F;
        break;
    default:
        FAIL("Unknown floor mutation");
    }
    source.m_chunks[chunk->m_id] = chunk;
    auto baseline = collision_source();
    REQUIRE(baseline.m_world_frame);
    auto frame_document = Json::parse(frame_of(baseline).m_document_json);
    frame_document["source_sha256"] = scene_content_hash(source);
    const auto K_FRAME_TEXT = frame_document.dump();
    const auto K_FRAME = admit_world_frame(source, K_FRAME_TEXT, collision_digest(K_FRAME_TEXT));
    REQUIRE(K_FRAME);
    source.m_world_frame = *K_FRAME;
    ++source.m_epoch.m_value;
    ++source.m_revision.m_value;
    CHECK_FALSE(admit(source, collision_document(source)));
}

TEST_CASE("collision candidates stop on revision epoch pose content and geometry corrections",
          "[collision-support]") {
    const auto K_SOURCE = collision_source();
    const auto K_SUPPORT = admit(K_SOURCE, collision_document(K_SOURCE));
    REQUIRE(K_SUPPORT);
    SceneStore store(K_SOURCE.m_id);
    REQUIRE(store.reset(std::make_shared<SceneState>(K_SOURCE)));
    auto corrected = std::make_shared<SceneState>(K_SOURCE);
    ++corrected->m_epoch.m_value;
    corrected->m_world_frame.reset();
    REQUIRE(store.reset(corrected));
    const auto K_STALE = probe_collision_support(**K_SUPPORT, *store.acquire_snapshot(), K_SWEEP);
    REQUIRE_FALSE(K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_EPOCH);
    auto revision = K_SOURCE;
    ++revision.m_revision.m_value;
    CHECK_FALSE(probe_collision_support(**K_SUPPORT, revision, K_SWEEP));
    auto changed = K_SOURCE;
    changed.m_provenance["edited"] = "true";
    CHECK_FALSE(probe_collision_support(**K_SUPPORT, changed, K_SWEEP));
    auto chunk = std::make_shared<SceneChunk>(*K_SOURCE.m_chunks.begin()->second);
    ++chunk->m_pose_revision.m_value;
    changed = K_SOURCE;
    changed.m_chunks[chunk->m_id] = chunk;
    CHECK_FALSE(probe_collision_support(**K_SUPPORT, changed, K_SWEEP));
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, K_SWEEP)->m_state ==
          ClearanceState::CLEAR_DIAGNOSTIC);
}

TEST_CASE("collision probes reject invalid dimensions and cancellation without consuming support",
          "[collision-support]") {
    const auto K_SOURCE = collision_source();
    const auto K_VALUE = collision_document(K_SOURCE);
    const auto K_SUPPORT = admit(K_SOURCE, K_VALUE);
    REQUIRE(K_SUPPORT);
    auto sweep = K_SWEEP;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    switch (K_CASE) {
    case 0:
        sweep.m_radius = 0;
        break;
    case 1:
        sweep.m_radius = 1;
        break;
    case 2:
        sweep.m_height = -1;
        break;
    case 3:
        sweep.m_height = std::numeric_limits<double>::infinity();
        break;
    case 4:
        sweep.m_end_feet.m_x = std::numeric_limits<double>::quiet_NaN();
        break;
    default:
        FAIL("Unknown capsule mutation");
    }
    CHECK_FALSE(probe_collision_support(**K_SUPPORT, K_SOURCE, sweep));
    std::stop_source stopped;
    stopped.request_stop();
    const auto K_TEXT = K_VALUE.dump();
    CHECK_FALSE(
        admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT), stopped.get_token()));
    CHECK_FALSE(probe_collision_support(**K_SUPPORT, K_SOURCE, K_SWEEP, stopped.get_token()));
    CHECK(probe_collision_support(**K_SUPPORT, K_SOURCE, K_SWEEP)->m_state ==
          ClearanceState::CLEAR_DIAGNOSTIC);
}

TEST_CASE(
    "shared collision inspection publishes a checked labeled diagnostic without altering inputs",
    "[collision-support]") {
    Scratch scratch;
    const auto K_SOURCE = std::make_shared<SceneState>(collision_source());
    const auto K_SOURCE_ROOT = scratch.m_path / "source";
    REQUIRE(write_bundle(K_SOURCE_ROOT, K_SOURCE));
    const auto K_MANIFEST_HASH = sha256_file(K_SOURCE_ROOT / "manifest.json", 1000000uz);
    REQUIRE(K_MANIFEST_HASH);
    const auto K_SUPPORT = collision_document(*K_SOURCE).dump();
    write(scratch.m_path / "support.json", K_SUPPORT);
    Json request { { "kind", "COLLISION_SUPPORT_INSPECTION_V1" },
                  { "source_bundle", "source" },
                  { "source_manifest_sha256", *K_MANIFEST_HASH },
                  { "support", "support.json" },
                  { "support_sha256", collision_digest(K_SUPPORT) },
                  { "probes",
                   Json::array({ { { "start_feet", { 1.5, 0, 1.5 } },
                                 { "end_feet", { 4.5, 0, 1.5 } },
                                 { "radius", .2 },
                                 { "height", 1.7 } } }) } };
    const auto K_REQUEST_PATH = scratch.m_path / "request.json";
    const auto K_TEXT = request.dump();
    write(K_REQUEST_PATH, K_TEXT);
    const auto K_OUTPUT = scratch.m_path / "inspection";
    const auto K_RESULT =
        inspect_collision_support(K_REQUEST_PATH, collision_digest(K_TEXT), K_OUTPUT);
    INFO((K_RESULT ? "" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto K_DIAGNOSTIC = load_bundle(K_OUTPUT / "diagnostic.wexplore");
    REQUIRE(K_DIAGNOSTIC);
    CHECK((*K_DIAGNOSTIC)->m_provenance.at("source_kind") == "synthetic_diagnostic");
    CHECK((*K_DIAGNOSTIC)->m_provenance.contains("collision_support"));
    CHECK((*K_DIAGNOSTIC)->m_scale == ScaleStatus::ARBITRARY);
    CHECK((*K_DIAGNOSTIC)->m_chunks.begin()->second->m_splats.size() == 320uz);
    const auto K_AFTER = sha256_file(K_SOURCE_ROOT / "manifest.json", 1000000uz);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_MANIFEST_HASH);
    CHECK_FALSE(inspect_collision_support(K_REQUEST_PATH, collision_digest(K_TEXT), K_OUTPUT));
    CHECK_FALSE(inspect_collision_support(
        K_REQUEST_PATH, collision_digest(K_TEXT), K_SOURCE_ROOT / "nested"));
    CHECK_FALSE(
        inspect_collision_support(K_REQUEST_PATH, std::string(64uz, '0'), scratch.m_path / "bad"));
    CHECK_FALSE(std::filesystem::exists(scratch.m_path / "bad"));
    std::stop_source stopped;
    stopped.request_stop();
    CHECK_FALSE(inspect_collision_support(K_REQUEST_PATH,
                                          collision_digest(K_TEXT),
                                          scratch.m_path / "cancelled",
                                          stopped.get_token()));
    CHECK_FALSE(std::filesystem::exists(scratch.m_path / "cancelled"));
    request["probes"][0uz]["radius"] = "invalid";
    write(K_REQUEST_PATH, request.dump());
    CHECK_FALSE(inspect_collision_support(
        K_REQUEST_PATH, collision_digest(request.dump()), scratch.m_path / "malformed"));
    CHECK_FALSE(std::filesystem::exists(scratch.m_path / "malformed"));
}

TEST_CASE("collision sweeps match an independent analytic slab oracle in both directions",
          "[collision-support]") {
    const auto K_SOURCE = collision_source();
    auto value = collision_document(K_SOURCE);
    for (std::size_t y { 0uz }; y < 4uz; ++y) {
        set_cell(value, { 3uz, y, 1uz }, '#');
    }
    const auto K_SUPPORT = admit(K_SOURCE, value);
    REQUIRE(K_SUPPORT);
    for (std::size_t a { 0uz }; a < 18uz; ++a) {
        for (std::size_t b { 0uz }; b < 18uz; ++b) {
            const double K_START { 1.25 + static_cast<double>(a) / 4 };
            const double K_END { 1.25 + static_cast<double>(b) / 4 };
            const bool K_INTERSECTS { std::min(K_START, K_END) - .25 <= 4 &&
                                     std::max(K_START, K_END) + .25 >= 3 };
            const auto K_PROBE = probe_collision_support(
                **K_SUPPORT, K_SOURCE, { { K_START, 0, 1.5 }, { K_END, 0, 1.5 }, .25, 1.7 });
            REQUIRE(K_PROBE);
            CHECK(K_PROBE->m_state ==
                  (K_INTERSECTS ? ClearanceState::OCCUPIED : ClearanceState::CLEAR_DIAGNOSTIC));
        }
    }
}

TEST_CASE("externally pinned native manifest validates the actual decoded bytes",
          "[collision-support][bundle]") {
    Scratch scratch;
    const auto K_SOURCE = std::make_shared<SceneState>(collision_source());
    const auto K_ROOT = scratch.m_path / "source";
    REQUIRE(write_bundle(K_ROOT, K_SOURCE));
    const auto K_BYTES = asset_files::read_bytes(K_ROOT / "manifest.json", 1000000uz, {});
    REQUIRE(K_BYTES);
    const auto K_HASH = sha256(*K_BYTES);
    REQUIRE(load_bundle(K_ROOT, {}, K_HASH));
    CHECK_FALSE(load_bundle(K_ROOT, {}, std::string(64uz, '0')));
    CHECK_FALSE(load_bundle(K_ROOT, {}, "bad"));
    const std::string K_TEXT(reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size());
    write(K_ROOT / "manifest.json", K_TEXT + "\n");
    CHECK_FALSE(load_bundle(K_ROOT, {}, K_HASH));
    const auto K_UNPINNED = load_bundle(K_ROOT);
    REQUIRE(K_UNPINNED);
    CHECK(scene_content_hash(**K_UNPINNED) == scene_content_hash(*K_SOURCE));
}
