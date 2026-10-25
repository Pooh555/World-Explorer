#include "../fixtures/evaluation_protocol_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <span>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::evaluation_protocol_test;

TEST_CASE("evaluation protocol freezes both tracks complete masks light seeds and closed plan "
          "conventions",
          "[evaluation-protocol]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedEvaluationProtocol, InferenceCaptureInputs>);
    ProtocolFixture fixture;
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->selection().m_artifacts.size() == 6uz);
    CHECK(K_RESULT->masks().size() == 10uz);
    CHECK(K_RESULT->selection().m_lights.size() == 3uz);
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(!K_RESULT->has_empty_masks());
    CHECK(K_RESULT->capture_product() == fixture.m_capture.record().m_selection.m_product);
    for (const auto& mask : K_RESULT->masks()) {
        CHECK(mask.m_pixels == 640uz * 480uz);
        CHECK(mask.m_eligible[0uz] == 0);
        CHECK(mask.m_eligible[1uz] == 1);
        CHECK(mask.m_eligible[2uz] == (mask.m_pin.m_track == EvaluationTrack::OBSERVED ? 0 : 1));
    }
    CHECK(!fixture.m_capture.validate_inference_dependency(
        fixture.m_receipt, fixture.m_selected.m_product, fixture.m_receipt.graph().m_context));
}

TEST_CASE(
    "evaluation protocol rejects rehashed metric changes threshold injection and unblinded review",
    "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    ProtocolFixture fixture([K_CASE](Json& record, auto& selected) {
        switch (K_CASE) {
        case 0:
            record["metrics"]["lpips_backbone"] = "VGG";
            break;
        case 1:
            record["metrics"]["fit_test_exposure"] = true;
            break;
        case 2:
            record["metrics"]["fit_test_color"] = true;
            break;
        case 3:
            record["metrics"]["lpips_normalize"] = 1;
            break;
        case 4:
            record["metrics"]["ssim_sigma"] = 1.0;
            break;
        case 5:
            record["metrics"]["ssim_window"] = 11.0;
            break;
        case 6:
            record["metrics"]["psnr_reduction"] = "BEST_IMAGE";
            break;
        case 7:
            record["metrics"]["color_space"] = "LINEAR_RGB";
            break;
        case 8:
            record["psnr_threshold"] = 0;
            break;
        case 9:
            record["review"]["blinded"] = false;
            break;
        case 10:
            record["review"]["rating_max"] = 4;
            break;
        case 11:
            selected.m_reviewers_min = 2;
            record["review"]["reviewers_min"] = 2;
            break;
        case 12:
            record["render_columns"] = Json::array({ "PBR" });
            break;
        case 13:
            record["world_epoch"] = 4;
            break;
        case 14:
            record["manual_ancestor"] = true;
            break;
        case 15:
            record["sampling"]["geometry_method"] = "PICK_BEST_SEED";
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation protocol freezes exact artifact mask light and seed selections",
          "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    ProtocolFixture fixture([K_CASE](Json& record, auto& selected) {
        switch (K_CASE) {
        case 0:
            record["artifacts"]["METRICS"]["revision"] = 2;
            break;
        case 1:
            selected.m_artifacts.erase(EvaluationArtifact::UV_SAMPLER);
            record["artifacts"].erase("UV_SAMPLER");
            break;
        case 2:
            selected.m_masks.pop_back();
            record["masks"].erase(record["masks"].size() - 1uz);
            break;
        case 3:
            selected.m_masks[1uz] = selected.m_masks[0uz];
            record["masks"][1uz] = record["masks"][0uz];
            break;
        case 4:
            record["masks"][0uz]["view"]["frame_id"] = 110;
            break;
        case 5:
            selected.m_lights.pop_back();
            record["lights"].erase(2uz);
            break;
        case 6:
            for (auto& light : selected.m_lights) {
                light.m_near_light = false;
            }
            for (auto& light : record["lights"]) {
                light["near_light"] = false;
            }
            break;
        case 7:
            selected.m_lights[1uz].m_label = selected.m_lights[0uz].m_label;
            record["lights"][1uz]["label"] = selected.m_lights[0uz].m_label;
            break;
        case 8:
            selected.m_lights[1uz].m_product = selected.m_lights[0uz].m_product;
            record["lights"][1uz]["product"] = region_ref(selected.m_lights[0uz].m_product);
            break;
        case 9:
            record["lights"][0uz]["seed"] = 101;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "evaluation masks reject completed observed leakage unknown eligibility and broken partitions",
    "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    ProtocolFixture fixture({}, [K_CASE](Json& mask, const auto& pin) {
        if (pin.m_track != EvaluationTrack::OBSERVED) {
            return;
        }
        auto& runs = mask["runs"];
        switch (K_CASE) {
        case 0:
            runs[0uz]["surface"] = "COMPLETED";
            break;
        case 1:
            runs[0uz]["surface"] = "UNKNOWN";
            break;
        case 2:
            runs[0uz]["eligible"] = 1;
            break;
        case 3:
            runs[0uz]["count"] = 0;
            break;
        case 4:
            runs[0uz]["count"] = std::numeric_limits<std::uint64_t>::max();
            break;
        case 5:
            runs[1uz]["first"] = 2;
            break;
        case 6:
            runs.erase(2uz);
            break;
        case 7:
            mask["width"] = 641;
            break;
        case 8:
            mask["track"] = "COMPLETED";
            break;
        case 9:
            mask["view"]["calibration_id"] = 9;
            break;
        case 10:
            mask["pose_revision"] = 6;
            break;
        case 11:
            runs = std::vector<int>(16385uz, 0);
            break;
        case 12:
            runs[0uz]["surface"] = "MEASURED_BY_LABEL";
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation protocol retains empty masks and excludes completed eligibility from "
          "observed counts",
          "[evaluation-protocol]") {
    ProtocolFixture fixture({}, [](Json& mask, const auto& pin) {
        if (pin.m_track == EvaluationTrack::OBSERVED) {
            for (auto& run : mask["runs"]) {
                run["eligible"] = false;
            }
        }
    });
    const auto K_RESULT = checked(fixture.inspect());
    CHECK(K_RESULT.has_empty_masks());
    for (const auto& mask : K_RESULT.masks()) {
        if (mask.m_pin.m_track == EvaluationTrack::OBSERVED) {
            CHECK(mask.m_eligible[1uz] == 0);
        } else {
            CHECK(mask.m_eligible[2uz] == 1);
        }
    }
}

TEST_CASE("evaluation protocol normalizes selection order and keeps changed evaluation seeds out "
          "of inference",
          "[evaluation-protocol]") {
    ProtocolFixture baseline;
    ProtocolFixture fixture([](Json& record, auto& selected) {
        selected.m_geometry_seed = 999;
        record["sampling"]["geometry_seed"] = 999;
        selected.m_lights[0uz].m_seed = 0;
        record["lights"][0uz]["seed"] = 0;
        std::ranges::reverse(selected.m_masks);
        std::ranges::reverse(selected.m_lights);
        std::reverse(record["masks"].begin(), record["masks"].end());
        std::reverse(record["lights"].begin(), record["lights"].end());
    });
    const auto K_RESULT = checked(fixture.inspect());
    CHECK(K_RESULT.selection().m_geometry_seed == 999);
    CHECK(K_RESULT.selection().m_lights[0uz].m_seed == 0);
    CHECK(sha256(fixture.m_capture.inference().frames()[0uz].m_image) ==
          sha256(baseline.m_capture.inference().frames()[0uz].m_image));
}

TEST_CASE(
    "evaluation protocol rejects config mask and root content aliases and missing dependency edges",
    "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    ProtocolFixture fixture({}, {}, [K_CASE](AssetDependencyGraph& graph, const auto& selected) {
        if (K_CASE == 0) {
            graph.m_products[9uz].m_recipe_sha256 =
                selected.m_artifacts.at(EvaluationArtifact::RENDERER).m_content_sha256;
        }
        if (K_CASE == 1) {
            graph.m_products[9uz].m_recipe_sha256 =
                selected.m_masks[0uz].m_product.m_content_sha256;
        }
        if (K_CASE == 2) {
            graph.m_products.back().m_dependencies.pop_back();
        }
        if (K_CASE == 3) {
            const auto K_FOUND = std::ranges::find(
                graph.m_products, selected.m_masks[0uz].m_product, &AssetDependencyNode::m_product);
            if (K_FOUND == graph.m_products.end()) {
                throw std::logic_error("Expected fixture mask");
            }
            K_FOUND->m_dependencies.clear();
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation protocol bounds independent selections and refuses stale context and foreign "
          "captures",
          "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    ProtocolFixture fixture;
    auto selected = fixture.m_selected;
    auto context = fixture.m_receipt.graph().m_context;
    switch (K_CASE) {
    case 0:
        selected.m_geometry_samples = 0;
        break;
    case 1:
        selected.m_geometry_samples = 1000001;
        break;
    case 2:
        selected.m_uv_resolution = 8193;
        break;
    case 3:
        selected.m_lights.resize(9uz);
        break;
    case 4:
        selected.m_masks.resize(33uz);
        break;
    case 5:
        selected.m_lights[0uz].m_label.resize(65uz, 'a');
        break;
    case 6:
        selected.m_artifacts[EvaluationArtifact::METRICS].m_content_sha256.resize(65uz, 'a');
        break;
    case 7:
        selected.m_product.m_revision++;
        break;
    case 8:
        context.m_pose_revision = { 6 };
        break;
    case 9:
        selected.m_reviewers_min = 33;
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!inspect_evaluation_protocol(selected, fixture.m_capture, fixture.m_receipt, context));
    CHECK(!inspect_evaluation_protocol(fixture.m_selected,
                                       fixture.m_base.m_capture,
                                       fixture.m_receipt,
                                       fixture.m_receipt.graph().m_context));
}

TEST_CASE("evaluation protocol and mask byte parsers bound depth size duplicates and raw nul",
          "[evaluation-protocol]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    ProtocolFixture::ByteMutation bytes;
    bytes.m_record = [K_CASE](std::string& text) {
        if (K_CASE == 0) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 1) {
            text.push_back('\0');
        }
        if (K_CASE == 2) {
            text.resize(256uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 3) {
            text.resize(256uz * 1024uz, ' ');
        }
    };
    bytes.m_mask = [K_CASE](std::string& text) {
        if (K_CASE == 4) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 5) {
            text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
        }
        if (K_CASE == 6) {
            text.resize(256uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 7) {
            text.resize(256uz * 1024uz, ' ');
        }
    };
    bytes.m_config = [K_CASE](std::string& text) {
        if (K_CASE == 8) {
            text.resize(64uz * 1024uz + 1uz, ' ');
        }
    };
    ProtocolFixture fixture({}, {}, {}, bytes);
    CHECK(fixture.inspect().has_value() == (K_CASE == 3 || K_CASE == 7));
}

TEST_CASE(
    "evaluation protocol mask pixels are bounded without allocating native image-sized buffers",
    "[evaluation-protocol]") {
    const auto K_SIZE = GENERATE(4096U, 4097U);
    ProtocolFixture fixture({}, {}, {}, {}, [K_SIZE](SelectionFixture& views) {
        auto& calibration = views.m_object.m_map->m_calibrations.at({ 8 });
        calibration.m_width = K_SIZE;
        calibration.m_height = 4096;
    });
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("evaluation protocol cancellation hides staged metadata exact retry and selected reload "
          "recover",
          "[evaluation-protocol]") {
    ProtocolFixture fixture;
    const auto K_PROTOCOL = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_evaluation_protocol(K_PROTOCOL,
                                                      fixture.m_receipt,
                                                      fixture.directory(),
                                                      { 121 },
                                                      0,
                                                      K_CONTEXT,
                                                      stop.get_token(),
                                                      [&](std::size_t) {
                                                          stop.request_stop();
                                                      });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_evaluation_protocol(
        K_PROTOCOL, fixture.m_receipt, fixture.directory(), { 121 }, 0, K_CONTEXT));
    CHECK(checked(publish_evaluation_protocol(
                      K_PROTOCOL, fixture.m_receipt, fixture.directory(), { 121 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_evaluation_protocol(fixture.directory(),
                                   K_COMMIT,
                                   fixture.m_selected,
                                   fixture.m_capture,
                                   fixture.m_receipt,
                                   K_CONTEXT));
    auto stale = K_COMMIT;
    stale.m_generation++;
    CHECK(!read_evaluation_protocol(fixture.directory(),
                                    stale,
                                    fixture.m_selected,
                                    fixture.m_capture,
                                    fixture.m_receipt,
                                    K_CONTEXT));
    CHECK(!publish_evaluation_protocol(
        K_PROTOCOL, fixture.m_receipt, fixture.m_receipt.source_directory(), { 122 }, 0, K_CONTEXT));
    CHECK(!publish_evaluation_protocol(
        K_PROTOCOL, fixture.m_receipt, fixture.directory(), { 122 }, 0, K_CONTEXT));
    auto wrong = fixture.m_selected;
    wrong.m_product.m_content_sha256 = hash("not committed");
    CHECK(!read_evaluation_protocol(
        fixture.directory(), K_COMMIT, wrong, fixture.m_capture, fixture.m_receipt, K_CONTEXT));
}

TEST_CASE("evaluation protocol owns normalized selections and summary lifetimes with explicit "
          "cancellation",
          "[evaluation-protocol]") {
    const auto K_PROTOCOL = [] {
        ProtocolFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        const auto K_OWNED = checked(fixture.inspect());
        fixture.m_selected.m_masks.clear();
        fixture.m_selected.m_lights.clear();
        return K_OWNED;
    }();
    CHECK(K_PROTOCOL.masks().size() == 10uz);
    CHECK(K_PROTOCOL.selection().m_masks.size() == 10uz);
}

TEST_CASE(
    "corpus schema seven requires exact protocol handles and preserves every earlier encoding",
    "[evaluation-protocol][asset-corpus]") {
    const auto K_SCHEMA = GENERATE(1, 2, 3, 4, 5, 6);
    const bool K_EMPTY = GENERATE(false, true);
    ProtocolFixture fixture({}, [K_EMPTY](Json& mask, const auto&) {
        if (K_EMPTY) {
            for (auto& run : mask["runs"]) {
                run["eligible"] = false;
            }
        }
    });
    RecipeFixture recipe;
    const auto K_RECIPE = recipe.save();
    const auto K_REGISTRY = fixture.m_output.m_root / "capture";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_CAPTURE_COMMIT = checked(publish_capture_selection(
        fixture.m_capture, fixture.m_receipt, K_REGISTRY, { 131 }, 0, K_CONTEXT));
    const auto& capture = fixture.m_capture.record().m_selection;
    const auto& node = K_RECIPE.graph().m_products.back();
    AssetCorpusDefinition definition {
        "synthetic-protocol",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          capture.m_product,
          capture.m_selection,
          capture.m_revision,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::EVALUATION_PROTOCOL, fixture.m_selected.m_product } } } },
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
    definition.m_require_typed_envelope = K_SCHEMA == 6;
    std::vector<AssetCorpusCaptureInput> inputs {
        { "corner", fixture.m_capture, fixture.m_receipt, K_REGISTRY } };
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE } };
    const auto K_OLD = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_OLD.canonical_json()).at("schema_version") == K_SCHEMA);
    definition.m_require_typed_protocol = true;
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_protocol = checked(fixture.inspect());
    const auto K_CORPUS = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_CORPUS.canonical_json()).at("schema_version") == 7);
    const auto K_HAS = [&](const char* code) {
        return std::ranges::any_of(K_CORPUS.blockers(), [&](const auto& blocker) {
            return blocker.m_code == code;
        });
    };
    CHECK(K_HAS("EVALUATION_RUNNER_UNIMPLEMENTED"));
    CHECK(K_HAS("ELIGIBLE_SURFACE_CORRESPONDENCE_UNVERIFIED"));
    CHECK(K_HAS("BLINDED_REVIEW_UNVERIFIED"));
    CHECK(K_HAS("LIGHT_NOVELTY_UNVERIFIED"));
    CHECK(K_HAS("EMPTY_EVALUATION_MASK") == K_EMPTY);
    CHECK(!K_HAS("EVALUATION_PROTOCOL_UNVERIFIED"));
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, K_SEAL, { 132 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    ProtocolFixture foreign([](Json& record, auto& selected) {
        selected.m_geometry_seed = 999;
        record["sampling"]["geometry_seed"] = 999;
    });
    inputs[0uz].m_protocol = checked(foreign.inspect());
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_protocol = checked(fixture.inspect());
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        auto doc = Json::parse(K_CORPUS.canonical_json());
        doc["typed_protocol"] = flag;
        const auto K_TEXT = doc.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   inputs,
                                   K_RECIPES));
    }
    inputs[0uz].m_protocol.reset();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    definition.m_require_typed_protocol = false;
    CHECK(checked(admit_asset_corpus(definition, inputs, K_RECIPES)).canonical_json() ==
          K_OLD.canonical_json());
    inputs[0uz].m_protocol = checked(fixture.inspect());
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    definition.m_require_typed_protocol = true;
    definition.m_captures[0uz].m_evidence.clear();
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_protocol.reset();
    const auto K_MISSING = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(std::ranges::any_of(K_MISSING.blockers(), [](const auto& blocker) {
        return blocker.m_code == "EVALUATION_PROTOCOL_MISSING";
    }));
}
