#include "../fixtures/capture_measurement_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <span>

using namespace world_explorer;
using namespace world_explorer::capture_measurement_test;

TEST_CASE("capture measurements reuse native anchor alignment without changing scale or scene",
          "[capture-measurement]") {
    const auto K_SYNTHETIC = GENERATE(true, false);
    MeasurementFixture fixture(K_SYNTHETIC);
    const auto K_HASH = scene_content_hash(fixture.m_scene);
    const auto K_ADMITTED = fixture.inspect();
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->alignment().m_world_from_source.m_scale == 2);
    CHECK(K_ADMITTED->metres() == 8);
    CHECK(K_ADMITTED->uncertainty_metres() == Catch::Approx(.02));
    CHECK(K_ADMITTED->alignment().m_synthetic == K_SYNTHETIC);
    CHECK(!K_ADMITTED->manual_ancestor());
    CHECK(K_ADMITTED->selection() == fixture.m_selected);
    CHECK(scene_content_hash(fixture.m_scene) == K_HASH);
    CHECK(fixture.m_scene.m_scale == ScaleStatus::ARBITRARY);
    CHECK(!fixture.m_scene.m_world_frame);
    CHECK(fixture.m_capture.inference().frames()[0uz].m_scale == ScaleStatus::ARBITRARY);
}

TEST_CASE("capture measurement rejects rehashed schema view source ancestry and anchor errors",
          "[capture-measurement]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    MeasurementFixture fixture(true, [K_CASE](Json& record, Json& frame) {
        switch (K_CASE) {
        case 0:
            record["extra"] = true;
            break;
        case 1:
            record["schema_version"] = 2;
            break;
        case 2:
            record["view"]["calibration_id"] = 9;
            break;
        case 3:
            record["view"]["frame_id"] = 110.0;
            break;
        case 4:
            record["source_sha256"] = std::string(64uz, 'a');
            break;
        case 5:
            record["pose_revision"] = 6;
            break;
        case 6:
            record["manual_ancestor"] = true;
            break;
        case 7:
            record["scene"]["sha256"] = std::string(64uz, 'a');
            break;
        case 8:
            frame["distance"]["metres"] = 0;
            break;
        case 9:
            frame["distance"]["uncertainty_metres"] = 8;
            break;
        case 10:
            frame["distance"]["uncertainty_metres"] = -.01;
            break;
        case 11:
            frame["distance"]["a"]["barycentric"] = { .5, .5, .5 };
            break;
        case 12:
            frame["distance"]["b"]["triangle"] = 1;
            break;
        case 13:
            frame["distance"]["a"] = frame["distance"]["b"];
            break;
        case 14:
            frame["evidence_kind"] = "INDEPENDENT_MEASUREMENT";
            break;
        case 15:
            frame["distance"]["record"] = std::string(1025uz, 'a');
            break;
        case 16:
            frame["distance"]["record"] = std::string("bad\0record", 10uz);
            break;
        case 17:
            frame["up"]["direction"] = { 0, 0, 0 };
            break;
        case 18:
            frame["up"]["uncertainty_degrees"] = 90;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    const auto K_HASH = scene_content_hash(fixture.m_scene);
    CHECK(!fixture.inspect());
    CHECK(scene_content_hash(fixture.m_scene) == K_HASH);
    CHECK(!std::filesystem::exists(fixture.m_output.m_root / "measurement"));
}

TEST_CASE("capture measurement rejects independent substitutions and stale native geometry",
          "[capture-measurement]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    MeasurementFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_selected.m_product.m_revision++;
        break;
    case 1:
        fixture.m_selected.m_view = fixture.m_views.m_selection.m_frames[1uz].m_view;
        break;
    case 2:
        fixture.m_selected.m_scene.m_revision.m_value++;
        break;
    case 3:
        fixture.m_selected.m_alignment.m_revision++;
        break;
    case 4:
        fixture.m_chunk->m_geometry_valid = false;
        break;
    case 5:
        fixture.m_chunk->m_pose_revision = { 6 };
        break;
    case 6:
        fixture.mesh().m_vertices[0uz].m_position[0uz] = std::numeric_limits<float>::infinity();
        break;
    case 7:
        fixture.m_scene.m_scale = ScaleStatus::METRIC_MEASURED;
        break;
    case 8:
        fixture.m_scene.m_epoch = { 4 };
        break;
    case 9:
        fixture.mesh().m_triangles[0uz][0uz] = 3;
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!fixture.inspect());
}

TEST_CASE("capture measurement rejects evaluation ancestry and foreign pose source declarations",
          "[capture-measurement]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    MeasurementFixture fixture(false);
    auto& region = fixture.m_views.m_object.m_region;
    if (K_CASE == 0) {
        region.m_graph.m_products.back().m_dependencies.push_back(
            fixture.m_views.m_selection.m_frames[1uz].m_view.m_image);
        fixture.m_receipt = region.freeze();
        fixture.m_capture = checked(fixture.m_views.inspect(fixture.m_receipt, false));
    } else {
        if (K_CASE == 1) {
            fixture.m_chunk->m_source_frames = { { 10 } };
        } else {
            fixture.observed().m_context.m_pose_sha256 = hash("foreign pose source");
        }
        // Rehash the scene selector and sidecar so the semantic source check runs.
        fixture.m_selected.m_scene.m_sha256 = scene_content_hash(fixture.m_scene);
        fixture.m_record["scene"]["sha256"] = fixture.m_selected.m_scene.m_sha256;
        fixture.m_frame["source_sha256"] = fixture.m_selected.m_scene.m_sha256;
        auto& alignment = region.m_graph.m_products[region.m_graph.m_products.size() - 2uz];
        alignment.m_product.m_content_sha256 = hash(fixture.m_frame.dump());
        region.m_data[alignment.m_product.m_content_sha256] = fixture.m_frame.dump();
        fixture.m_selected.m_alignment = alignment.m_product;
        fixture.m_record["alignment"] = region_ref(alignment.m_product);
        auto& measurement = region.m_graph.m_products.back();
        measurement.m_product.m_content_sha256 = hash(fixture.m_record.dump());
        measurement.m_dependencies.back() = alignment.m_product;
        region.m_data[measurement.m_product.m_content_sha256] = fixture.m_record.dump();
        fixture.m_selected.m_product = measurement.m_product;
        fixture.m_views.m_selection.m_measurement = measurement.m_product;
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        fixture.m_receipt = fixture.m_views.freeze(false);
        fixture.m_capture = checked(fixture.m_views.inspect(fixture.m_receipt, false));
    }
    CHECK(!fixture.inspect());
}

TEST_CASE("typed measurement metadata publishes atomically and retains owned declarations",
          "[capture-measurement]") {
    const auto K_AT = GENERATE(1uz, 2uz);
    MeasurementFixture fixture;
    const auto K_ADMITTED = checked(fixture.inspect());
    const auto K_OUTPUT = fixture.m_output.m_root / "measurement";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source source;
    const auto K_CANCELLED = publish_capture_measurement(K_ADMITTED,
                                                         fixture.m_receipt,
                                                         K_OUTPUT,
                                                         { 71 },
                                                         0,
                                                         K_CONTEXT,
                                                         source.get_token(),
                                                         [&](std::size_t count) {
                                                             if (count == K_AT) {
                                                                 source.request_stop();
                                                             }
                                                         });
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    const auto K_PENDING = checked(read_immutable_records(K_OUTPUT));
    CHECK(K_PENDING.m_records.empty());
    const auto K_COMMIT = checked(
        publish_capture_measurement(K_ADMITTED, fixture.m_receipt, K_OUTPUT, { 71 }, 0, K_CONTEXT));
    const auto K_SNAPSHOT = checked(read_immutable_records(K_OUTPUT));
    CHECK(K_SNAPSHOT.m_records.size() == 2uz);
    CHECK(sha256(K_SNAPSHOT.m_records.at("measurement-8001-1")) ==
          fixture.m_selected.m_product.m_content_sha256);
    CHECK(sha256(K_SNAPSHOT.m_records.at("measurement-8001-1-frame")) ==
          fixture.m_selected.m_alignment.m_content_sha256);
    const auto K_REPLAY = checked(
        publish_capture_measurement(K_ADMITTED, fixture.m_receipt, K_OUTPUT, { 71 }, 0, K_CONTEXT));
    CHECK(K_REPLAY.m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(!publish_capture_measurement(K_ADMITTED,
                                       fixture.m_receipt,
                                       fixture.m_receipt.source_directory() / "nested",
                                       { 72 },
                                       0,
                                       K_CONTEXT));
    fixture.m_chunk->m_world_from_chunk.m_scale = 10;
    CHECK(K_ADMITTED.alignment().m_world_from_source.m_scale == 2);
}

TEST_CASE("typed scale corpus requires exact private evidence and preserves v1 encoding",
          "[capture-measurement]") {
    MeasurementFixture fixture;
    RecipeFixture recipe;
    const auto K_RECIPE_RECEIPT = recipe.save();
    const auto& recipe_node = K_RECIPE_RECEIPT.graph().m_products.back();
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_REGISTRY = fixture.m_output.m_root / "capture";
    const auto K_CAPTURE_COMMIT = checked(publish_capture_selection(
        fixture.m_capture, fixture.m_receipt, K_REGISTRY, { 1 }, 0, K_CONTEXT));
    AssetCorpusDefinition definition {
        "scale-fixture",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          fixture.m_views.m_selection.m_product,
          fixture.m_views.m_selection.m_selection,
          1,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::INDEPENDENT_SCALE, fixture.m_selected.m_product } } } },
        { { "import",
          recipe_node.m_product,
          K_RECIPE_RECEIPT.receipt_sha256(),
          recipe_node.m_recipe_sha256,
          recipe.m_context,
          recipe.m_host } } };
    std::vector<AssetCorpusCaptureInput> captures {
        { "corner", fixture.m_capture, fixture.m_receipt, K_REGISTRY } };
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE_RECEIPT } };
    const auto K_V1 = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(Json::parse(K_V1.canonical_json()).at("schema_version") == 1);
    definition.m_require_typed_scale = true;
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_measurement = checked(fixture.inspect());
    const auto K_V2 = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(K_V2.reference().m_sha256 != K_V1.reference().m_sha256);
    CHECK(Json::parse(K_V2.canonical_json()).at("schema_version") == 2);
    CHECK(std::ranges::any_of(K_V2.blockers(), [](const auto& blocker) {
        return blocker.m_code == "SYNTHETIC_SCALE_DIAGNOSTIC";
    }));
    CHECK(std::ranges::any_of(K_V2.blockers(), [](const auto& blocker) {
        return blocker.m_code == "MEASUREMENT_AUTHENTICITY_UNVERIFIED";
    }));
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_V2, K_SEAL, { 81 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_V2.reference(), captures, K_RECIPES));
    MeasurementFixture other(true, [](Json&, Json& frame) {
        frame["distance"]["metres"] = 9;
    });
    captures[0uz].m_measurement = checked(other.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_measurement = checked(fixture.inspect());
    auto malformed = Json::parse(K_V2.canonical_json());
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        malformed["typed_scale"] = flag;
        const auto K_TEXT = malformed.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   captures,
                                   K_RECIPES));
    }
    malformed.erase("typed_scale");
    const auto K_MISSING = malformed.dump();
    CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_MISSING)),
                               { definition.m_corpus, definition.m_revision, hash(K_MISSING) },
                               captures,
                               K_RECIPES));
    captures[0uz].m_measurement.reset();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_V2.reference(), captures, K_RECIPES));
    definition.m_require_typed_scale = false;
    CHECK(checked(admit_asset_corpus(definition, captures, K_RECIPES)).canonical_json() ==
          K_V1.canonical_json());
    captures[0uz].m_measurement = checked(fixture.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
}

TEST_CASE("capture measurement cancellation and owned lifetime preserve arbitrary scale",
          "[capture-measurement]") {
    const auto K_OWNED = [] {
        MeasurementFixture fixture;
        std::stop_source source;
        source.request_stop();
        CHECK(!fixture.inspect(source.get_token()));
        return checked(fixture.inspect());
    }();
    CHECK(K_OWNED.metres() == 8);
    CHECK(K_OWNED.alignment().m_world_from_source.m_scale == 2);
    CHECK(K_OWNED.alignment().m_document_sha256 ==
          K_OWNED.selection().m_alignment.m_content_sha256);
}

TEST_CASE("capture measurement strict byte and nesting limits reject before publication",
          "[capture-measurement]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    MeasurementFixture::ByteMutation bytes;
    if (K_CASE < 4) {
        bytes.m_record = [K_CASE](std::string& text) {
            if (K_CASE == 0) {
                text.insert(1uz, "\"schema_version\":1,");
            } else if (K_CASE == 1) {
                text.push_back('\0');
            } else if (K_CASE == 2) {
                text.assign(16uz * 1024uz + 1uz, ' ');
            } else {
                text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
            }
        };
    } else {
        bytes.m_alignment = [K_CASE](std::string& text) {
            if (K_CASE == 4) {
                text.assign(K_WORLD_FRAME_DOCUMENT_BYTES + 1uz, ' ');
            } else {
                text.insert(1uz, "\"schema\":\"WORLD_FRAME_ALIGNMENT_V1\",");
            }
        };
    }
    MeasurementFixture fixture(true, {}, bytes);
    CHECK(!fixture.inspect());
}

TEST_CASE("measurement records admit exact byte ceilings without normalizing pinned bytes",
          "[capture-measurement]") {
    MeasurementFixture::ByteMutation bytes;
    bytes.m_record = [](std::string& text) {
        text.insert(0uz, 16uz * 1024uz - text.size(), ' ');
    };
    bytes.m_alignment = [](std::string& text) {
        text.insert(0uz, K_WORLD_FRAME_DOCUMENT_BYTES - text.size(), ' ');
    };
    MeasurementFixture fixture(true, {}, bytes);
    const auto K_ADMITTED = fixture.inspect();
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->alignment().m_document_json.size() == K_WORLD_FRAME_DOCUMENT_BYTES);
    CHECK(K_ADMITTED->alignment().m_document_sha256 ==
          fixture.m_selected.m_alignment.m_content_sha256);
}

TEST_CASE("measurement registry reads require committed pins and fresh native source admission",
          "[capture-measurement]") {
    MeasurementFixture fixture;
    const auto K_ADMITTED = checked(fixture.inspect());
    const auto K_ROOT = fixture.m_output.m_root / "measurement";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    CHECK(!publish_capture_measurement(K_ADMITTED,
                                       fixture.m_receipt,
                                       K_ROOT,
                                       { 91 },
                                       0,
                                       K_CONTEXT,
                                       stop.get_token(),
                                       [&](std::size_t) {
                                           stop.request_stop();
                                       }));
    CHECK(!read_capture_measurement(K_ROOT,
                                    { 1, hash("uncommitted") },
                                    fixture.m_selected,
                                    fixture.m_capture,
                                    fixture.m_receipt,
                                    fixture.m_scene,
                                    K_CONTEXT));
    const auto K_COMMIT = checked(
        publish_capture_measurement(K_ADMITTED, fixture.m_receipt, K_ROOT, { 91 }, 0, K_CONTEXT));
    const auto K_READ = read_capture_measurement(K_ROOT,
                                                 K_COMMIT,
                                                 fixture.m_selected,
                                                 fixture.m_capture,
                                                 fixture.m_receipt,
                                                 fixture.m_scene,
                                                 K_CONTEXT);
    REQUIRE(K_READ);
    CHECK(K_READ->alignment().m_world_from_source.m_scale == 2);
    auto wrong = K_COMMIT;
    wrong.m_generation++;
    CHECK(!read_capture_measurement(K_ROOT,
                                    wrong,
                                    fixture.m_selected,
                                    fixture.m_capture,
                                    fixture.m_receipt,
                                    fixture.m_scene,
                                    K_CONTEXT));
    const auto K_LATER = checked(
        publish_immutable_records(K_ROOT, { 92 }, 1, { { "later-observation", { std::byte { 1 } } } }, {}));
    CHECK(!read_capture_measurement(K_ROOT,
                                    K_COMMIT,
                                    fixture.m_selected,
                                    fixture.m_capture,
                                    fixture.m_receipt,
                                    fixture.m_scene,
                                    K_CONTEXT));
    CHECK(read_capture_measurement(K_ROOT,
                                   K_LATER,
                                   fixture.m_selected,
                                   fixture.m_capture,
                                   fixture.m_receipt,
                                   fixture.m_scene,
                                   K_CONTEXT));
    fixture.m_chunk->m_world_from_chunk.m_scale = 10;
    CHECK(!read_capture_measurement(K_ROOT,
                                    K_LATER,
                                    fixture.m_selected,
                                    fixture.m_capture,
                                    fixture.m_receipt,
                                    fixture.m_scene,
                                    K_CONTEXT));
    CHECK(K_READ->alignment().m_world_from_source.m_scale == 2);
}

TEST_CASE("measurement source preflight bounds observed metadata before native validation copies",
          "[capture-measurement]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    MeasurementFixture fixture(false);
    if (K_CASE == 0) {
        fixture.observed().m_context.m_recipe_sha256.assign(65uz, 'a');
    } else if (K_CASE == 1) {
        fixture.observed().m_manifest_json.assign(4uz * 1024uz * 1024uz + 1uz, ' ');
    } else {
        fixture.mesh().m_vertices.resize(262145uz);
    }
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}
