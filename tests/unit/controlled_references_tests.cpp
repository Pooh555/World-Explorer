#include "../fixtures/controlled_references_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <span>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::controlled_references_test;

TEST_CASE("controlled reference admission binds three independent prop condition kinds and "
          "lighting units",
          "[controlled-references]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedControlledReference, InferenceCaptureInputs>);
    const auto K_KIND = GENERATE(ControlledReferenceKind::BACKSIDE,
                                 ControlledReferenceKind::EMPTY_BACKGROUND,
                                 ControlledReferenceKind::CHANGED_LIGHT);
    ControlledFixture fixture;
    const auto K_RESULT = fixture.inspect(K_KIND);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->selection().m_pairs.size() == 1uz);
    CHECK(K_RESULT->lighting().size() == 2uz);
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(!K_RESULT->has_missing_lighting());
    CHECK(K_RESULT->capture_product() == fixture.m_capture.record().m_selection.m_product);
    CHECK(K_RESULT->inventory_product() == fixture.m_inventory.selection().m_inventory);
    CHECK(K_RESULT->visibility_product() == fixture.m_inventory.selection().m_visibility);
    CHECK(K_RESULT->lighting()[0uz].m_illuminance_lux == 100.0);
    CHECK(K_RESULT->lighting()[0uz].m_color_temperature_kelvin == 4000.0);
    CHECK(fixture.m_capture.inference().frames().size() == 1uz);
    CHECK(!fixture.m_capture.validate_inference_dependency(
        fixture.m_receipt, K_RESULT->selection().m_product, fixture.m_receipt.graph().m_context));
}

TEST_CASE("controlled references reject rehashed schema and independent pair mismatches",
          "[controlled-references]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    ControlledFixture fixture([K_CASE](Json& record, auto&) {
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
            record["inventory"]["revision"] = 2;
            break;
        case 5:
            record["kind"] = "ARTIST_APPROVAL";
            break;
        case 6:
            record["pairs"][0uz]["pair_id"] = 1.0;
            break;
        case 7:
            record["pairs"][0uz]["reference"]["frame_pose_revision"] = 99;
            break;
        case 8:
            record["pairs"][0uz]["baseline_condition"] = "not_independently_selected";
            break;
        case 9:
            record["pairs"].push_back(record["pairs"][0uz]);
            break;
        case 10:
            record["pairs"][0uz]["reference_state"] = "UNKNOWN";
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("controlled pairs require inventoried props selected roles distinct conditions and "
          "matching visibility",
          "[controlled-references]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    ControlledFixture fixture([K_CASE](Json& record, auto& selected) {
        auto& pair = selected.m_pairs[0uz];
        switch (K_CASE) {
        case 0:
            pair.m_subject = { 999 };
            break;
        case 1:
            pair.m_subject = { 43 };
            break;
        case 2:
            pair.m_baseline = pair.m_reference;
            break;
        case 3:
            pair.m_reference = pair.m_baseline;
            break;
        case 4:
            pair.m_id = 0;
            break;
        case 5:
            pair.m_reference_state = ControlledSubjectState::REMOVED;
            break;
        case 6:
            pair.m_reference_condition = pair.m_baseline_condition;
            break;
        case 7:
            pair.m_baseline_condition = std::string(65uz, 'a');
            break;
        case 8:
            pair.m_reference_condition = "unknown condition";
            break;
        case 9:
            pair.m_reference_lighting = pair.m_baseline_lighting;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
        record["pairs"][0uz] = controlled_pair(pair);
    });
    CHECK(!fixture.inspect(ControlledReferenceKind::CHANGED_LIGHT));
}

TEST_CASE("empty background requires removed out-of-view declarations and other kinds require "
          "visible present subjects",
          "[controlled-references]") {
    const auto K_KIND = GENERATE(ControlledReferenceKind::BACKSIDE,
                                 ControlledReferenceKind::EMPTY_BACKGROUND,
                                 ControlledReferenceKind::CHANGED_LIGHT);
    ControlledFixture original;
    const auto K_OTHER =
        original.m_selected[K_KIND == ControlledReferenceKind::EMPTY_BACKGROUND ? 0uz : 1uz]
            .m_pairs[0uz];
    ControlledFixture fixture([K_KIND, K_OTHER](Json& record, auto& selected) {
        if (selected.m_kind != K_KIND) {
            return;
        }
        auto& pair = selected.m_pairs[0uz];
        pair.m_reference = K_OTHER.m_reference;
        pair.m_reference_lighting = K_OTHER.m_reference_lighting;
        record["pairs"][0uz] = controlled_pair(pair);
    });
    CHECK(!fixture.inspect(K_KIND));
}

TEST_CASE("controlled lighting records reject unknown units invalid ranges stale view and empty "
          "observations",
          "[controlled-references]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    ControlledFixture fixture({}, [K_CASE](Json& light, std::size_t index) {
        if (index != 0uz) {
            return;
        }
        switch (K_CASE) {
        case 0:
            light["illuminance"]["unit"] = "cd/m2";
            break;
        case 1:
            light["color_temperature"]["unit"] = "K";
            break;
        case 2:
            light["illuminance"]["value"] = -1;
            break;
        case 3:
            light["color_temperature"]["value"] = 0;
            break;
        case 4:
            light["illuminance"] = nullptr;
            light["color_temperature"] = nullptr;
            break;
        case 5:
            light["illuminance"]["value"] = true;
            break;
        case 6:
            light["view"]["frame_id"] = 10;
            break;
        case 7:
            light["pose_revision"] = 6;
            break;
        case 8:
            light["recipe_sha256"] = hash("not the measurement recipe");
            break;
        case 9:
            light["manual_ancestor"] = true;
            break;
        case 10:
            light["illuminance"]["extra"] = "unselected";
            break;
        case 11:
            light["schema_version"] = 2;
            break;
        case 12:
            light["color_temperature"]["value"] = "4000";
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("controlled lighting accepts partial and zero-illuminance declarations without "
          "manufacturing missing values",
          "[controlled-references]") {
    ControlledFixture fixture({}, [](Json& light, std::size_t) {
        light["illuminance"]["value"] = 0;
        light["color_temperature"] = nullptr;
    });
    const auto K_RESULT = checked(fixture.inspect());
    CHECK(K_RESULT.lighting()[0uz].m_illuminance_lux == 0.0);
    CHECK(!K_RESULT.lighting()[0uz].m_color_temperature_kelvin);
    CHECK(!K_RESULT.has_missing_lighting());
    ControlledFixture unmeasured({}, {}, {}, {}, false);
    const auto K_UNMEASURED = checked(unmeasured.inspect());
    CHECK(K_UNMEASURED.has_missing_lighting());
    CHECK(K_UNMEASURED.lighting().empty());
    CHECK(sha256(fixture.m_capture.inference().frames()[0uz].m_image) ==
          sha256(unmeasured.m_capture.inference().frames()[0uz].m_image));
}

TEST_CASE("controlled references reject label and measurement aliases into selected inference "
          "dependencies",
          "[controlled-references]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    ControlledFixture fixture({}, {}, [K_CASE](AssetDependencyGraph& graph, const auto& selected) {
        switch (K_CASE) {
        case 0:
            graph.m_products[9uz].m_recipe_sha256 = selected[0uz].m_product.m_content_sha256;
            break;
        case 1: {
            const auto& light = selected[0uz].m_pairs[0uz].m_baseline_lighting;
            if (!light) {
                throw std::logic_error("Expected fixture lighting");
            }
            graph.m_products[9uz].m_recipe_sha256 = light->m_content_sha256;
            break;
        }
        case 2:
            graph.m_products.back().m_dependencies.pop_back();
            break;
        case 3:
            graph.m_products[15uz].m_dependencies.clear();
            break;
        case 4:
            graph.m_products[9uz].m_recipe_sha256 = graph.m_products.back().m_recipe_sha256;
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect(K_CASE == 2 ? ControlledReferenceKind::CHANGED_LIGHT
                                       : ControlledReferenceKind::BACKSIDE));
}

TEST_CASE("controlled reference bounds and stale independent selections fail before adoption",
          "[controlled-references]") {
    ControlledFixture fixture;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    auto selected = fixture.m_selected[0uz];
    auto context = fixture.m_receipt.graph().m_context;
    switch (K_CASE) {
    case 0:
        selected.m_pairs.clear();
        break;
    case 1:
        selected.m_pairs.resize(33uz);
        break;
    case 2:
        selected.m_product.m_revision++;
        break;
    case 3:
        selected.m_pairs[0uz].m_reference.m_image.m_content_sha256.resize(65uz, 'a');
        break;
    case 4:
        selected.m_kind = ControlledReferenceKind::CHANGED_LIGHT;
        break;
    case 5: {
        auto& lighting = selected.m_pairs[0uz].m_baseline_lighting;
        if (!lighting) {
            throw std::logic_error("Expected fixture lighting");
        }
        lighting->m_content_sha256.resize(65uz, 'a');
        break;
    }
    case 6:
        selected.m_pairs.push_back(selected.m_pairs[0uz]);
        break;
    case 7:
        context.m_world_epoch = { 4 };
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!inspect_controlled_reference(
        selected, fixture.m_inventory, fixture.m_capture, fixture.m_receipt, context));
    CHECK(!inspect_controlled_reference(fixture.m_selected[0uz],
                                        checked(fixture.m_base.inspect()),
                                        fixture.m_capture,
                                        fixture.m_receipt,
                                        fixture.m_receipt.graph().m_context));
}

TEST_CASE("controlled reference and lighting bytes bound parsing and preserve exact whitespace",
          "[controlled-references]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    ControlledFixture::ByteMutation bytes;
    bytes.m_record = [K_CASE](std::string& text) {
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
            break;
        }
    };
    bytes.m_lighting = [K_CASE](std::string& text) {
        if (K_CASE == 5) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 6) {
            text.resize(64uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 7) {
            text.resize(64uz * 1024uz, ' ');
        }
    };
    ControlledFixture fixture({}, {}, {}, bytes);
    CHECK(fixture.inspect().has_value() == (K_CASE == 4 || K_CASE == 7));
}

TEST_CASE("controlled reference staged cancellation exact replay selected reload and source "
          "protection recover",
          "[controlled-references]") {
    ControlledFixture fixture;
    const auto K_RESULT = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_controlled_reference(K_RESULT,
                                                       fixture.m_receipt,
                                                       fixture.directory(),
                                                       { 51 },
                                                       0,
                                                       K_CONTEXT,
                                                       stop.get_token(),
                                                       [&](std::size_t) {
                                                           stop.request_stop();
                                                       });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_controlled_reference(
        K_RESULT, fixture.m_receipt, fixture.directory(), { 51 }, 0, K_CONTEXT));
    CHECK(checked(publish_controlled_reference(
                      K_RESULT, fixture.m_receipt, fixture.directory(), { 51 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    const auto K_READ = read_controlled_reference(fixture.directory(),
                                                  K_COMMIT,
                                                  fixture.m_selected[0uz],
                                                  fixture.m_inventory,
                                                  fixture.m_capture,
                                                  fixture.m_receipt,
                                                  K_CONTEXT);
    REQUIRE(K_READ);
    CHECK(K_READ->selection().m_pairs == K_RESULT.selection().m_pairs);
    CHECK(K_READ->lighting()[0uz].m_illuminance_lux == K_RESULT.lighting()[0uz].m_illuminance_lux);
    auto stale = K_COMMIT;
    stale.m_generation++;
    CHECK(!read_controlled_reference(fixture.directory(),
                                     stale,
                                     fixture.m_selected[0uz],
                                     fixture.m_inventory,
                                     fixture.m_capture,
                                     fixture.m_receipt,
                                     K_CONTEXT));
    CHECK(!publish_controlled_reference(
        K_RESULT, fixture.m_receipt, fixture.m_receipt.source_directory(), { 52 }, 0, K_CONTEXT));
    CHECK(!publish_controlled_reference(
        K_RESULT, fixture.m_receipt, fixture.directory(), { 52 }, 0, K_CONTEXT));
    auto wrong = fixture.m_selected[0uz];
    wrong.m_product.m_content_sha256 = hash("not committed");
    CHECK(!read_controlled_reference(fixture.directory(),
                                     K_COMMIT,
                                     wrong,
                                     fixture.m_inventory,
                                     fixture.m_capture,
                                     fixture.m_receipt,
                                     K_CONTEXT));
}

TEST_CASE("controlled reference owned values survive source destruction and cancellation preserves "
          "callers",
          "[controlled-references]") {
    const auto K_RESULT = [] {
        ControlledFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(ControlledReferenceKind::BACKSIDE, stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        const auto K_OWNED = checked(fixture.inspect());
        fixture.m_selected[0uz].m_pairs.clear();
        return K_OWNED;
    }();
    CHECK(K_RESULT.selection().m_pairs.size() == 1uz);
    CHECK(K_RESULT.lighting().size() == 2uz);
}

TEST_CASE("controlled reference pair IDs cannot relabel duplicate prop and view correspondences",
          "[controlled-references]") {
    ControlledFixture fixture([](Json& record, auto& selected) {
        auto pair = selected.m_pairs[0uz];
        pair.m_id = 2;
        selected.m_pairs.push_back(pair);
        record["pairs"].push_back(controlled_pair(pair));
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "corpus schema five requires each selected controlled kind and preserves earlier schema bytes",
    "[controlled-references][asset-corpus]") {
    const auto K_SCHEMA = GENERATE(1, 2, 3, 4);
    const bool K_MEASURED = GENERATE(false, true);
    ControlledFixture fixture { {}, {}, {}, {}, K_MEASURED };
    RecipeFixture recipe;
    const auto K_RECIPE = recipe.save();
    const auto K_REGISTRY = fixture.m_output.m_root / "capture";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_CAPTURE_COMMIT = checked(publish_capture_selection(
        fixture.m_capture, fixture.m_receipt, K_REGISTRY, { 71 }, 0, K_CONTEXT));
    const auto& capture = fixture.m_capture.record().m_selection;
    const auto& node = K_RECIPE.graph().m_products.back();
    AssetCorpusDefinition definition {
        "synthetic-controlled",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          capture.m_product,
          capture.m_selection,
          capture.m_revision,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::INVENTORY, fixture.m_inventory.selection().m_inventory },
           { AssetCorpusEvidence::VISIBILITY, fixture.m_inventory.selection().m_visibility },
           { AssetCorpusEvidence::BACKSIDE, fixture.m_selected[0uz].m_product },
           { AssetCorpusEvidence::EMPTY_BACKGROUND, fixture.m_selected[1uz].m_product },
           { AssetCorpusEvidence::CHANGED_LIGHT, fixture.m_selected[2uz].m_product } } } },
        { { "import",
          node.m_product,
          K_RECIPE.receipt_sha256(),
          node.m_recipe_sha256,
          recipe.m_context,
          recipe.m_host } } };
    definition.m_require_typed_scale = K_SCHEMA == 2;
    definition.m_require_typed_inventory = K_SCHEMA == 3;
    definition.m_require_typed_paths = K_SCHEMA == 4;
    std::vector<AssetCorpusCaptureInput> inputs {
        { "corner", fixture.m_capture, fixture.m_receipt, K_REGISTRY } };
    if (K_SCHEMA == 3) {
        inputs[0uz].m_inventory = fixture.m_inventory;
    }
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE } };
    const auto K_OLD = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_OLD.canonical_json()).at("schema_version") == K_SCHEMA);
    definition.m_require_typed_references = true;
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    for (auto kind : { ControlledReferenceKind::BACKSIDE,
                      ControlledReferenceKind::EMPTY_BACKGROUND,
                      ControlledReferenceKind::CHANGED_LIGHT }) {
        inputs[0uz].m_references.push_back(checked(fixture.inspect(kind)));
    }
    const auto K_CORPUS = checked(admit_asset_corpus(definition, inputs, K_RECIPES));
    CHECK(Json::parse(K_CORPUS.canonical_json()).at("schema_version") == 5);
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "LIGHTING_CALIBRATION_UNVERIFIED";
    }));
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "REFERENCE_CORRESPONDENCE_UNVERIFIED";
    }));
    CHECK(std::ranges::none_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "BACKSIDE_UNVERIFIED";
    }));
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
              return blocker.m_code == "LIGHTING_MEASUREMENT_MISSING";
          }) == !K_MEASURED);
    ControlledFixture foreign({}, [](Json& light, std::size_t) {
        light["illuminance"]["value"] = 99;
    });
    const auto K_ORIGINAL = inputs[0uz].m_references[0uz];
    inputs[0uz].m_references[0uz] = checked(foreign.inspect());
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_references[0uz] = K_ORIGINAL;
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, K_SEAL, { 72 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        auto doc = Json::parse(K_CORPUS.canonical_json());
        doc["typed_references"] = flag;
        const auto K_TEXT = doc.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   inputs,
                                   K_RECIPES));
    }
    inputs[0uz].m_references.pop_back();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), inputs, K_RECIPES));
    auto& evidence = definition.m_captures[0uz].m_evidence;
    evidence.erase(AssetCorpusEvidence::CHANGED_LIGHT);
    CHECK(admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_references.push_back(inputs[0uz].m_references[0uz]);
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    inputs[0uz].m_references.pop_back();
    evidence.erase(AssetCorpusEvidence::EMPTY_BACKGROUND);
    inputs[0uz].m_references.pop_back();
    CHECK(admit_asset_corpus(definition, inputs, K_RECIPES));
    evidence.erase(AssetCorpusEvidence::INVENTORY);
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
    evidence[AssetCorpusEvidence::INVENTORY] = fixture.m_inventory.selection().m_inventory;
    evidence.erase(AssetCorpusEvidence::BACKSIDE);
    inputs[0uz].m_references.clear();
    CHECK(admit_asset_corpus(definition, inputs, K_RECIPES));
    evidence[AssetCorpusEvidence::BACKSIDE] = fixture.m_selected[0uz].m_product;
    evidence[AssetCorpusEvidence::EMPTY_BACKGROUND] = fixture.m_selected[1uz].m_product;
    evidence[AssetCorpusEvidence::CHANGED_LIGHT] = fixture.m_selected[2uz].m_product;
    definition.m_require_typed_references = false;
    CHECK(checked(admit_asset_corpus(definition, inputs, K_RECIPES)).canonical_json() ==
          K_OLD.canonical_json());
    inputs[0uz].m_references.push_back(checked(fixture.inspect()));
    CHECK(!admit_asset_corpus(definition, inputs, K_RECIPES));
}
