#include "../fixtures/capture_envelope_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <span>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::capture_envelope_test;

TEST_CASE("capture envelope binds complete sources and every native calibrated view without "
          "inference conversion",
          "[capture-envelope]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedCaptureEnvelope, InferenceCaptureInputs>);
    EnvelopeFixture fixture;
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->selection().m_sources.size() == 2uz);
    CHECK(K_RESULT->selection().m_views.size() == 6uz);
    CHECK(K_RESULT->capture_product() == fixture.m_capture.record().m_selection.m_product);
    CHECK(!K_RESULT->has_unknown_source_counts());
    CHECK(!K_RESULT->has_unassessed_conditions());
    CHECK(!K_RESULT->has_dynamic_sources());
    CHECK(!K_RESULT->has_automatic_exposure());
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(!fixture.m_capture.validate_inference_dependency(
        fixture.m_receipt, fixture.m_selected.m_product, fixture.m_receipt.graph().m_context));
}

TEST_CASE("capture envelope rejects rehashed header source role and identity changes",
          "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    EnvelopeFixture fixture([K_CASE](Json& record, auto&) {
        switch (K_CASE) {
        case 0:
            record["extra"] = true;
            break;
        case 1:
            record["schema_version"] = 2;
            break;
        case 2:
            record["world_epoch"] = 4;
            break;
        case 3:
            record["manual_ancestor"] = true;
            break;
        case 4:
            record["sources"][0uz]["rights"]["revision"] = 2;
            break;
        case 5:
            record["sources"][0uz]["source_frame_count"] = 10.0;
            break;
        case 6:
            record["sources"][0uz]["scene_motion"] = "GUARANTEED_STATIC";
            break;
        case 7:
            record["sources"][0uz]["exposure_control"] = "FIT_ON_TEST";
            break;
        case 8:
            record["sources"][0uz]["condition"] = "another_condition";
            break;
        case 9:
            record["views"][0uz]["role"] = "EVALUATION_ONLY";
            break;
        case 10:
            record["views"][0uz]["camera"]["view"]["frame_id"] = 999;
            break;
        case 11:
            record["capture"]["revision"] = 2;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("capture envelope refuses omitted duplicate foreign and role-swapped independently "
          "selected tables",
          "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    EnvelopeFixture fixture([K_CASE](Json& record, auto& selected) {
        switch (K_CASE) {
        case 0:
            record["sources"].erase(0uz);
            selected.m_sources.erase(selected.m_sources.begin());
            break;
        case 1:
            record["views"].erase(0uz);
            selected.m_views.erase(selected.m_views.begin());
            break;
        case 2:
            record["sources"].push_back(record["sources"][0uz]);
            selected.m_sources.push_back(selected.m_sources[0uz]);
            break;
        case 3:
            record["views"].push_back(record["views"][0uz]);
            selected.m_views.push_back(selected.m_views[0uz]);
            break;
        case 4:
            selected.m_sources[0uz].m_source.m_capture = { 99 };
            record["sources"][0uz]["capture_id"] = 99;
            break;
        case 5:
            selected.m_sources[0uz].m_source_frame_count = 2;
            record["sources"][0uz]["source_frame_count"] = 2;
            break;
        case 6:
            selected.m_sources[0uz].m_source_frame_count = 0;
            record["sources"][0uz]["source_frame_count"] = 0;
            break;
        case 7:
            selected.m_views[0uz].m_role = CaptureFrameRole::EVALUATION_ONLY;
            record["views"][0uz]["role"] = "EVALUATION_ONLY";
            break;
        case 8:
            selected.m_sources[0uz].m_source.m_source_sha256 = hash("foreign source");
            record["sources"][0uz]["source_sha256"] = hash("foreign source");
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "capture envelope verifies exact decoded camera time and calibration fields after rehashing",
    "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    EnvelopeFixture fixture([K_CASE](Json& record, auto&) {
        auto& camera = record["views"][0uz]["camera"];
        auto& calibration = record["views"][0uz]["calibration"];
        switch (K_CASE) {
        case 0:
            camera["pts"] = 1.0;
            break;
        case 1:
            camera["pts"] = std::numeric_limits<std::uint64_t>::max();
            break;
        case 2:
            camera["time_base"][1uz] = 60;
            break;
        case 3:
            camera["capture_time_ns"] = 18;
            break;
        case 4:
            camera["world_from_camera"]["translation"][0uz] = 0;
            break;
        case 5:
            camera["world_from_camera"]["rotation"][0uz] = 1;
            break;
        case 6:
            camera["world_from_camera"]["scale"] = 1;
            break;
        case 7:
            calibration["width"] = 641;
            break;
        case 8:
            calibration["height"] = 480.0;
            break;
        case 9:
            calibration["model"] = "OPENCV";
            break;
        case 10:
            calibration["intrinsics"][0uz] = 501;
            break;
        case 11:
            calibration["distortion"][0uz] = 0.1;
            break;
        case 12:
            calibration["image_transform"][0uz] = 0.5;
            break;
        case 13:
            calibration["intrinsics"].push_back(0);
            break;
        case 14:
            calibration["intrinsics"][0uz] = "500";
            break;
        case 15:
            calibration["calibration_id"] = 9;
            break;
        case 16:
            camera["time_base"].push_back(0);
            break;
        case 17:
            camera["view"]["pose_asset"]["revision"] = 2;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "capture envelope retains OPENCV calibration transforms and signed native source timestamps",
    "[capture-envelope]") {
    EnvelopeFixture fixture({}, {}, {}, [](SelectionFixture& views) {
        const auto K_UPDATE = [](MapState& map) {
            auto& calibration = map.m_calibrations.at({ 8 });
            calibration.m_model = CalibrationModel::OPENCV;
            calibration.m_distortion = { 0.1, -0.02, 0.03, 0.04 };
            calibration.m_image_transform = { 0.5, 0.5, -2, -1 };
        };
        K_UPDATE(*views.m_object.m_map);
        K_UPDATE(*views.m_inference_map);
        views.m_inference_map->m_observations.at({ 110 }).m_timestamp.m_pts =
            std::numeric_limits<std::int64_t>::min();
    });
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(fixture.m_capture.inference().frames()[0uz].m_calibration.m_model ==
          CalibrationModel::OPENCV);
    CHECK(fixture.m_capture.inference().frames()[0uz].m_observation.m_timestamp.m_pts ==
          std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("capture envelope retains unknown conditions dynamic sources automatic exposure and "
          "declared counts",
          "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    EnvelopeFixture fixture([K_CASE](Json& record, auto& selected) {
        auto& source = selected.m_sources[0uz];
        source.m_condition = "revised_condition";
        switch (K_CASE) {
        case 0:
            source.m_source_frame_count.reset();
            source.m_scene_motion = CaptureSceneMotion::UNASSESSED;
            source.m_exposure_control = CaptureExposureControl::UNASSESSED;
            break;
        case 1:
            source.m_scene_motion = CaptureSceneMotion::DYNAMIC;
            source.m_exposure_control = CaptureExposureControl::AUTOMATIC;
            break;
        case 2:
            source.m_source_frame_count = 3;
            break;
        case 3:
            source.m_source_frame_count = std::numeric_limits<std::uint64_t>::max();
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
        record["sources"][0uz] = envelope_source(source);
    });
    const auto K_RESULT = checked(fixture.inspect());
    CHECK(K_RESULT.has_unknown_source_counts() == (K_CASE == 0));
    CHECK(K_RESULT.has_unassessed_conditions() == (K_CASE == 0));
    CHECK(K_RESULT.has_dynamic_sources() == (K_CASE == 1));
    CHECK(K_RESULT.has_automatic_exposure() == (K_CASE == 1));
    EnvelopeFixture baseline;
    CHECK(sha256(fixture.m_capture.inference().frames()[0uz].m_image) ==
          sha256(baseline.m_capture.inference().frames()[0uz].m_image));
}

TEST_CASE("capture envelope normalizes source and view order while retaining exact bindings",
          "[capture-envelope]") {
    EnvelopeFixture fixture([](Json& record, auto& selected) {
        std::ranges::reverse(selected.m_sources);
        std::ranges::reverse(selected.m_views);
        std::reverse(record["sources"].begin(), record["sources"].end());
        std::reverse(record["views"].begin(), record["views"].end());
    });
    const auto K_RESULT = checked(fixture.inspect());
    CHECK(K_RESULT.selection().m_sources[0uz].m_source.m_capture == CaptureId { 7 });
    CHECK(K_RESULT.selection().m_views[0uz].m_view.m_frame == FrameId { 10 });
}

TEST_CASE("capture envelope checks its declared evaluation-root separation without tainting shared "
          "setup metadata",
          "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    EnvelopeFixture fixture({}, [K_CASE](AssetDependencyGraph& graph, const auto& selected) {
        if (K_CASE == 0) {
            graph.m_products[9uz].m_recipe_sha256 = selected.m_product.m_content_sha256;
        }
        if (K_CASE == 1) {
            graph.m_products[9uz].m_recipe_sha256 = graph.m_products.back().m_recipe_sha256;
        }
        if (K_CASE == 2) {
            graph.m_products.back().m_dependencies.clear();
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "capture envelope bounds independent tables labels hashes and current context before copying",
    "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    EnvelopeFixture fixture;
    auto selected = fixture.m_selected;
    auto context = fixture.m_receipt.graph().m_context;
    switch (K_CASE) {
    case 0:
        selected.m_sources.resize(33uz);
        break;
    case 1:
        selected.m_views.resize(65uz);
        break;
    case 2:
        selected.m_sources[0uz].m_condition.resize(65uz, 'a');
        break;
    case 3:
        selected.m_sources[0uz].m_source.m_source_sha256.resize(65uz, 'a');
        break;
    case 4:
        selected.m_product.m_revision++;
        break;
    case 5:
        selected.m_views[0uz].m_view.m_image.m_content_sha256.resize(65uz, 'a');
        break;
    case 6:
        context.m_pose_revision = { 6 };
        break;
    case 7:
        selected.m_sources[0uz].m_condition = "invalid label";
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!inspect_capture_envelope(selected, fixture.m_capture, fixture.m_receipt, context));
    CHECK(!inspect_capture_envelope(fixture.m_selected,
                                    fixture.m_base.m_capture,
                                    fixture.m_receipt,
                                    fixture.m_receipt.graph().m_context));
}

TEST_CASE("capture envelope byte limits duplicate keys nul and depth fail while pinned whitespace "
          "survives",
          "[capture-envelope]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    EnvelopeFixture fixture({}, {}, [K_CASE](std::string& text) {
        switch (K_CASE) {
        case 0:
            text.insert(1uz, "\"schema_version\":1,");
            break;
        case 1:
            text.push_back('\0');
            break;
        case 2:
            text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
            break;
        case 3:
            text.resize(256uz * 1024uz + 1uz, ' ');
            break;
        case 4:
            text.resize(256uz * 1024uz, ' ');
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(fixture.inspect().has_value() == (K_CASE == 4));
}

TEST_CASE("capture envelope stages privately and exact retry selected reads and source protection "
          "recover",
          "[capture-envelope]") {
    EnvelopeFixture fixture;
    const auto K_ENVELOPE = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_capture_envelope(K_ENVELOPE,
                                                   fixture.m_receipt,
                                                   fixture.directory(),
                                                   { 91 },
                                                   0,
                                                   K_CONTEXT,
                                                   stop.get_token(),
                                                   [&](std::size_t) {
                                                       stop.request_stop();
                                                   });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_capture_envelope(
        K_ENVELOPE, fixture.m_receipt, fixture.directory(), { 91 }, 0, K_CONTEXT));
    CHECK(checked(publish_capture_envelope(
                      K_ENVELOPE, fixture.m_receipt, fixture.directory(), { 91 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_capture_envelope(fixture.directory(),
                                K_COMMIT,
                                fixture.m_selected,
                                fixture.m_capture,
                                fixture.m_receipt,
                                K_CONTEXT));
    auto stale = K_COMMIT;
    stale.m_generation++;
    CHECK(!read_capture_envelope(fixture.directory(),
                                 stale,
                                 fixture.m_selected,
                                 fixture.m_capture,
                                 fixture.m_receipt,
                                 K_CONTEXT));
    CHECK(!publish_capture_envelope(
        K_ENVELOPE, fixture.m_receipt, fixture.m_receipt.source_directory(), { 92 }, 0, K_CONTEXT));
    CHECK(!publish_capture_envelope(
        K_ENVELOPE, fixture.m_receipt, fixture.directory(), { 92 }, 0, K_CONTEXT));
    auto wrong = fixture.m_selected;
    wrong.m_product.m_content_sha256 = hash("not committed");
    CHECK(!read_capture_envelope(
        fixture.directory(), K_COMMIT, wrong, fixture.m_capture, fixture.m_receipt, K_CONTEXT));
}

TEST_CASE(
    "capture envelope owns source view and record lifetime and pre-cancel preserves the caller",
    "[capture-envelope]") {
    const auto K_ENVELOPE = [] {
        EnvelopeFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        const auto K_OWNED = checked(fixture.inspect());
        fixture.m_selected.m_sources.clear();
        fixture.m_selected.m_views.clear();
        return K_OWNED;
    }();
    CHECK(K_ENVELOPE.selection().m_sources.size() == 2uz);
    CHECK(K_ENVELOPE.selection().m_views.size() == 6uz);
}

TEST_CASE("corpus schema six requires exact envelope handles derives declaration blockers and "
          "preserves earlier bytes",
          "[capture-envelope][asset-corpus]") {
    const auto K_SCHEMA = GENERATE(1, 2, 3, 4, 5);
    const auto K_CONDITION = GENERATE(0, 1, 2);
    EnvelopeFixture fixture([K_CONDITION](Json& record, auto& selected) {
        auto& source = selected.m_sources[0uz];
        if (K_CONDITION == 1) {
            source.m_source_frame_count.reset();
            source.m_scene_motion = CaptureSceneMotion::UNASSESSED;
            source.m_exposure_control = CaptureExposureControl::UNASSESSED;
        }
        if (K_CONDITION == 2) {
            source.m_scene_motion = CaptureSceneMotion::DYNAMIC;
            source.m_exposure_control = CaptureExposureControl::AUTOMATIC;
        }
        record["sources"][0uz] = envelope_source(source);
    });
    RecipeFixture recipe;
    const auto K_RECIPE = recipe.save();
    const auto K_REGISTRY = fixture.m_output.m_root / "capture";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_CAPTURE_COMMIT = checked(publish_capture_selection(
        fixture.m_capture, fixture.m_receipt, K_REGISTRY, { 101 }, 0, K_CONTEXT));
    const auto& capture = fixture.m_capture.record().m_selection;
    const auto& node = K_RECIPE.graph().m_products.back();
    AssetCorpusDefinition definition {
        "synthetic-envelope",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          capture.m_product,
          capture.m_selection,
          capture.m_revision,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::CAPTURE_ENVELOPE, fixture.m_selected.m_product } } } },
        { { "import",
          node.m_product,
          K_RECIPE.receipt_sha256(),
          node.m_recipe_sha256,
          recipe.m_context,
          recipe.m_host } } };
    definition.m_require_typed_scale = K_SCHEMA == 2;
    definition.m_require_typed_inventory = K_SCHEMA == 3;
    definition.m_require_typed_paths = K_SCHEMA == 4;
    definition.m_require_typed_references = K_SCHEMA == 5;
    std::vector<AssetCorpusCaptureInput> inputs {
        { "corner", fixture.m_capture, fixture.m_receipt, K_REGISTRY } };
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE } };
    const auto K_OLD = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_OLD.canonical_json()).at("schema_version") == K_SCHEMA);
    definition.m_require_typed_envelope = true;
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_envelope = checked(fixture.inspect());
    const auto K_CORPUS = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_CORPUS.canonical_json()).at("schema_version") == 6);
    const auto K_HAS = [&](const char* code) {
        return std::ranges::any_of(K_CORPUS.blockers(), [&](const auto& blocker) {
            return blocker.m_code == code;
        });
    };
    CHECK(K_HAS("SOURCE_COVERAGE_UNVERIFIED"));
    CHECK(K_HAS("CAPTURE_CONDITIONS_UNVERIFIED"));
    CHECK(K_HAS("CAPTURE_CONSENT_UNVERIFIED"));
    CHECK(K_HAS("CAPTURE_QUALITY_UNVERIFIED"));
    CHECK(!K_HAS("CAPTURE_ENVELOPE_UNVERIFIED"));
    CHECK(K_HAS("SOURCE_FRAME_COUNT_UNKNOWN") == (K_CONDITION == 1));
    CHECK(K_HAS("CAPTURE_CONDITIONS_UNASSESSED") == (K_CONDITION == 1));
    CHECK(K_HAS("DYNAMIC_CAPTURE_DECLARED") == (K_CONDITION == 2));
    CHECK(K_HAS("AUTOMATIC_CAPTURE_EXPOSURE_DECLARED") == (K_CONDITION == 2));
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, K_SEAL, { 102 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    EnvelopeFixture foreign([](Json& record, auto& selected) {
        selected.m_sources[0uz].m_condition = "foreign_condition";
        record["sources"][0uz] = envelope_source(selected.m_sources[0uz]);
    });
    inputs[0uz].m_envelope = checked(foreign.inspect());
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_envelope = checked(fixture.inspect());
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        auto doc = Json::parse(K_CORPUS.canonical_json());
        doc["typed_envelope"] = flag;
        const auto K_TEXT = doc.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   inputs,
                                   K_RECIPES));
    }
    inputs[0uz].m_envelope.reset();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    definition.m_require_typed_envelope = false;
    CHECK(checked(admit_asset_corpus(definition, inputs, K_RECIPES)).canonical_json() ==
          K_OLD.canonical_json());
    inputs[0uz].m_envelope = checked(fixture.inspect());
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    definition.m_require_typed_envelope = true;
    definition.m_captures[0uz].m_evidence.clear();
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_envelope.reset();
    const auto K_MISSING = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(std::ranges::any_of(K_MISSING.blockers(), [](const auto& blocker) {
        return blocker.m_code == "CAPTURE_ENVELOPE_MISSING";
    }));
}
