#include "../fixtures/evaluation_inventory_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::evaluation_inventory_test;

TEST_CASE("evaluation inventory binds declared structure and complete visibility without changing "
          "inference",
          "[evaluation-inventory]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedEvaluationInventory, InferenceCaptureInputs>);
    InventoryFixture fixture;
    const auto K_INPUT_HASH = sha256(fixture.m_capture.inference().frames()[0uz].m_image);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->selection().m_items.size() == 6uz);
    CHECK(K_RESULT->selection().m_cells.size() == 6uz);
    CHECK(K_RESULT->capture_product() == fixture.m_views.m_selection.m_product);
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(sha256(fixture.m_capture.inference().frames()[0uz].m_image) == K_INPUT_HASH);
    CHECK(fixture.m_capture.inference().frames().size() == 1uz);
    CHECK(!fixture.m_capture.validate_inference_dependency(
        fixture.m_receipt, fixture.m_selected.m_inventory, fixture.m_receipt.graph().m_context));
    const auto K_OTHER = [] {
        InventoryFixture other([](Json& inventory, auto& selected, Json& visibility) {
            inventory["items"][1uz]["label"] = "floor_revised";
            selected.m_items[1uz].m_label = "floor_revised";
            visibility["cells"][1uz]["state"] = "UNKNOWN";
            selected.m_cells[1uz].m_state = EvaluationVisibility::UNKNOWN;
        });
        REQUIRE(other.inspect());
        return other.m_capture.inference();
    }();
    CHECK(sha256(K_OTHER.frames()[0uz].m_image) == K_INPUT_HASH);
}

TEST_CASE(
    "evaluation inventory rejects rehashed identity schema and incomplete or stale view tables",
    "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    InventoryFixture fixture([K_CASE](Json& inventory, auto&, Json& visibility) {
        switch (K_CASE) {
        case 0:
            inventory["extra"] = true;
            break;
        case 1:
            inventory["schema_version"] = 2;
            break;
        case 2:
            inventory["items"][0uz]["track_id"] = 42.0;
            break;
        case 3:
            inventory["items"][0uz]["kind"] = "DYNAMIC";
            break;
        case 4:
            inventory["items"][0uz]["required"] = 1;
            break;
        case 5:
            inventory["manual_ancestor"] = true;
            break;
        case 6:
            visibility["world_epoch"] = 4;
            break;
        case 7:
            visibility["views"][0uz]["calibration_id"] = 9;
            break;
        case 8:
            visibility["views"][0uz]["frame_id"] = 110;
            break;
        case 9:
            visibility["views"].clear();
            break;
        case 10:
            visibility["views"].push_back(visibility["views"][0uz]);
            break;
        case 11:
            visibility["cells"].erase(1uz);
            break;
        case 12:
            visibility["cells"].push_back(visibility["cells"][0uz]);
            break;
        case 13:
            visibility["cells"][0uz]["annotation"]["revision"] = 2;
            break;
        case 14:
            visibility["cells"][0uz]["state"] = "ASSUMED_VISIBLE";
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation visibility checks accepted identities labels mask states and all annotations",
          "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    InventoryFixture fixture([K_CASE](Json& inventory, auto& selected, Json& visibility) {
        switch (K_CASE) {
        case 0:
            visibility["cells"][0uz]["state"] = "FULLY_OCCLUDED";
            selected.m_cells[0uz].m_state = EvaluationVisibility::FULLY_OCCLUDED;
            break;
        case 1:
            visibility["cells"][0uz]["state"] = "UNKNOWN";
            selected.m_cells[0uz].m_state = EvaluationVisibility::UNKNOWN;
            break;
        case 2:
            visibility["cells"][0uz]["state"] = "UNKNOWN";
            visibility["cells"][0uz]["annotation"] = nullptr;
            selected.m_cells[0uz].m_state = EvaluationVisibility::UNKNOWN;
            selected.m_cells[0uz].m_annotation.reset();
            break;
        case 3:
            inventory["items"][0uz]["label"] = "wrong_class";
            selected.m_items[0uz].m_label = "wrong_class";
            break;
        case 4:
            visibility["cells"][0uz]["annotation"] = nullptr;
            selected.m_cells[0uz].m_annotation.reset();
            break;
        case 5: {
            // Associate the selected chair annotation with the rejected floor track.
            visibility["cells"][0uz]["state"] = "UNKNOWN";
            visibility["cells"][0uz]["annotation"] = nullptr;
            selected.m_cells[0uz].m_state = EvaluationVisibility::UNKNOWN;
            const auto K_REF = selected.m_cells[0uz].m_annotation;
            if (!K_REF) {
                throw std::logic_error("Expected fixture annotation");
            }
            selected.m_cells[0uz].m_annotation.reset();
            visibility["cells"][1uz]["state"] = "VISIBLE";
            visibility["cells"][1uz]["annotation"] = region_ref(K_REF.value());
            selected.m_cells[1uz].m_state = EvaluationVisibility::VISIBLE;
            selected.m_cells[1uz].m_annotation = K_REF;
            break;
        }
        case 6:
            inventory["items"].push_back(inventory["items"][0uz]);
            selected.m_items.push_back(selected.m_items[0uz]);
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation records reject recipe and content aliases into selected inference closures",
          "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    InventoryFixture fixture({}, [K_CASE](AssetDependencyGraph& graph, const auto& selected) {
        auto& input = graph.m_products[9uz];
        switch (K_CASE) {
        case 0:
            input.m_recipe_sha256 = selected.m_inventory.m_content_sha256;
            break;
        case 1:
            input.m_recipe_sha256 = selected.m_visibility.m_content_sha256;
            break;
        case 2:
            input.m_recipe_sha256 = graph.m_products.back().m_recipe_sha256;
            break;
        case 3:
            graph.m_products.back().m_dependencies.pop_back();
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation inventory admission bounds bytes counts and independent selections",
          "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    InventoryFixture fixture;
    switch (K_CASE) {
    case 0:
        fixture.m_selected.m_inventory.m_revision++;
        break;
    case 1:
        fixture.m_selected.m_visibility.m_revision++;
        break;
    case 2:
        fixture.m_selected.m_items[0uz].m_label.assign(65uz, 'a');
        break;
    case 3:
        fixture.m_selected.m_items.resize(65uz, fixture.m_selected.m_items[0uz]);
        break;
    case 4:
        fixture.m_selected.m_cells.resize(4097uz, fixture.m_selected.m_cells[0uz]);
        break;
    case 5:
        fixture.m_selected.m_items.clear();
        break;
    case 6:
        for (auto& item : fixture.m_selected.m_items) {
            item.m_required = false;
        }
        break;
    case 7:
        fixture.m_selected.m_cells[0uz].m_capture = { 17 };
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation inventory parses strict bounded records and retains exact byte ceilings",
          "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    InventoryFixture::ByteMutation bytes;
    bytes.m_inventory = [K_CASE](std::string& text) {
        if (K_CASE == 0) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 1) {
            text.push_back('\0');
        }
        if (K_CASE == 2) {
            text.assign(64uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 3) {
            text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
        }
        if (K_CASE == 6) {
            text.insert(0uz, 64uz * 1024uz - text.size(), ' ');
        }
    };
    bytes.m_visibility = [K_CASE](std::string& text) {
        if (K_CASE == 4) {
            text.assign(256uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 5) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 6) {
            text.insert(0uz, 256uz * 1024uz - text.size(), ' ');
        }
    };
    InventoryFixture fixture({}, {}, bytes);
    CHECK(fixture.inspect().has_value() == (K_CASE == 6));
}

TEST_CASE(
    "evaluation inventory publication hides staged bytes and exact replay owns committed metadata",
    "[evaluation-inventory]") {
    const auto K_AT = GENERATE(1uz, 2uz);
    InventoryFixture fixture;
    const auto K_INVENTORY = checked(fixture.inspect());
    const auto K_ROOT = fixture.m_output.m_root / "inventory";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source source;
    const auto K_CANCELLED = publish_evaluation_inventory(K_INVENTORY,
                                                          fixture.m_receipt,
                                                          K_ROOT,
                                                          { 101 },
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
    CHECK(checked(read_immutable_records(K_ROOT)).m_records.empty());
    CHECK(!read_evaluation_inventory(K_ROOT,
                                     { 1, hash("pending") },
                                     fixture.m_selected,
                                     fixture.m_capture,
                                     fixture.m_receipt,
                                     K_CONTEXT));
    const auto K_COMMIT = checked(
        publish_evaluation_inventory(K_INVENTORY, fixture.m_receipt, K_ROOT, { 101 }, 0, K_CONTEXT));
    const auto K_READ = checked(read_evaluation_inventory(
        K_ROOT, K_COMMIT, fixture.m_selected, fixture.m_capture, fixture.m_receipt, K_CONTEXT));
    CHECK(K_READ.selection().m_items == K_INVENTORY.selection().m_items);
    const auto K_SNAPSHOT = checked(read_immutable_records(K_ROOT));
    CHECK(sha256(K_SNAPSHOT.m_records.at("inventory-9000-1")) ==
          fixture.m_selected.m_inventory.m_content_sha256);
    CHECK(sha256(K_SNAPSHOT.m_records.at("visibility-9001-1")) ==
          fixture.m_selected.m_visibility.m_content_sha256);
    CHECK(checked(publish_evaluation_inventory(
                      K_INVENTORY, fixture.m_receipt, K_ROOT, { 101 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(!publish_evaluation_inventory(K_INVENTORY,
                                        fixture.m_receipt,
                                        fixture.m_receipt.source_directory() / "nested",
                                        { 102 },
                                        0,
                                        K_CONTEXT));
    auto wrong = K_COMMIT;
    wrong.m_generation++;
    CHECK(!read_evaluation_inventory(
        K_ROOT, wrong, fixture.m_selected, fixture.m_capture, fixture.m_receipt, K_CONTEXT));
}

TEST_CASE("evaluation inventory cancellation and owned lifetime preserve caller metadata",
          "[evaluation-inventory]") {
    const auto K_OWNED = [] {
        InventoryFixture fixture;
        const auto K_ITEMS = fixture.m_selected.m_items;
        std::stop_source source;
        source.request_stop();
        CHECK(!fixture.inspect(source.get_token()));
        const auto K_ADMITTED = checked(fixture.inspect());
        fixture.m_selected.m_items[0uz].m_label = "changed";
        CHECK(K_ADMITTED.selection().m_items == K_ITEMS);
        return K_ADMITTED;
    }();
    CHECK(K_OWNED.selection().m_items[0uz].m_label == "chair");
    CHECK(K_OWNED.selection().m_cells.size() == 6uz);
}

TEST_CASE("evaluation inventory accepts consistent fully occluded masks and canonical table order",
          "[evaluation-inventory]") {
    InventoryFixture fixture({}, {}, {}, true);
    const auto K_INVENTORY = checked(fixture.inspect());
    CHECK(K_INVENTORY.selection().m_cells[0uz].m_state == EvaluationVisibility::FULLY_OCCLUDED);
    std::ranges::reverse(fixture.m_selected.m_items);
    std::ranges::reverse(fixture.m_selected.m_cells);
    const auto K_REORDERED = checked(fixture.inspect());
    CHECK(K_REORDERED.selection().m_items == K_INVENTORY.selection().m_items);
    CHECK(K_REORDERED.selection().m_cells == K_INVENTORY.selection().m_cells);
}

TEST_CASE("corpus schema three requires exact typed inventory and preserves earlier schema bytes",
          "[evaluation-inventory]") {
    const auto K_SCALE = GENERATE(false, true);
    InventoryFixture fixture;
    RecipeFixture recipe;
    const auto K_RECIPE_RECEIPT = recipe.save();
    const auto& recipe_node = K_RECIPE_RECEIPT.graph().m_products.back();
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_ROOT = fixture.m_output.m_root / "capture";
    const auto K_CAPTURE_COMMIT = checked(
        publish_capture_selection(fixture.m_capture, fixture.m_receipt, K_ROOT, { 1 }, 0, K_CONTEXT));
    AssetCorpusDefinition definition {
        "inventory-fixture",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          fixture.m_views.m_selection.m_product,
          fixture.m_views.m_selection.m_selection,
          1,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::INVENTORY, fixture.m_selected.m_inventory },
           { AssetCorpusEvidence::VISIBILITY, fixture.m_selected.m_visibility } } } },
        { { "import",
          recipe_node.m_product,
          K_RECIPE_RECEIPT.receipt_sha256(),
          recipe_node.m_recipe_sha256,
          recipe.m_context,
          recipe.m_host } } };
    definition.m_require_typed_scale = K_SCALE;
    std::vector<AssetCorpusCaptureInput> captures {
        { "corner", fixture.m_capture, fixture.m_receipt, K_ROOT } };
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE_RECEIPT } };
    const auto K_OLD = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(Json::parse(K_OLD.canonical_json()).at("schema_version") == (K_SCALE ? 2 : 1));
    definition.m_require_typed_inventory = true;
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_inventory = checked(fixture.inspect());
    const auto K_CORPUS = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(Json::parse(K_CORPUS.canonical_json()).at("schema_version") == 3);
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "INVENTORY_COMPLETENESS_UNVERIFIED";
    }));
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "VISIBILITY_CORRESPONDENCE_UNVERIFIED";
    }));
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, K_SEAL, { 111 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), captures, K_RECIPES));
    InventoryFixture other([](Json& inventory, auto& selected, Json&) {
        inventory["items"][1uz]["label"] = "other_floor";
        selected.m_items[1uz].m_label = "other_floor";
    });
    captures[0uz].m_inventory = checked(other.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_inventory = checked(fixture.inspect());
    auto malformed = Json::parse(K_CORPUS.canonical_json());
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        malformed["typed_inventory"] = flag;
        const auto K_TEXT = malformed.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   captures,
                                   K_RECIPES));
    }
    captures[0uz].m_inventory.reset();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), captures, K_RECIPES));
    definition.m_require_typed_inventory = false;
    CHECK(checked(admit_asset_corpus(definition, captures, K_RECIPES)).canonical_json() ==
          K_OLD.canonical_json());
    captures[0uz].m_inventory = checked(fixture.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    definition.m_require_typed_inventory = true;
    auto& evidence = definition.m_captures[0uz].m_evidence;
    evidence.erase(AssetCorpusEvidence::VISIBILITY);
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    evidence.erase(AssetCorpusEvidence::INVENTORY);
    captures[0uz].m_inventory.reset();
    const auto K_MISSING = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(std::ranges::any_of(K_MISSING.blockers(), [](const auto& blocker) {
        return blocker.m_code == "INVENTORY_MISSING";
    }));
}

TEST_CASE("inventory visibility covers every item across both selected evaluation views",
          "[evaluation-inventory]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    InventoryFixture fixture(
        [K_CASE](Json&, auto& selected, Json& visibility) {
            if (K_CASE == 1) {
                visibility["views"].erase(1uz);
            }
            if (K_CASE == 2) {
                visibility["cells"].erase(6uz);
                selected.m_cells.erase(selected.m_cells.begin() + 6z);
            }
            if (K_CASE == 3) {
                visibility["cells"].back()["frame_id"] = 999;
                selected.m_cells.back().m_frame = { 999 };
            }
        },
        {},
        {},
        false,
        true);
    const auto K_RESULT = fixture.inspect();
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->selection().m_cells.size() == 12uz);
    } else {
        CHECK(!K_RESULT);
    }
}

TEST_CASE("evaluation separation bounds explicit roots and binds receipt context before traversal",
          "[evaluation-inventory]") {
    InventoryFixture fixture;
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    CHECK(fixture.m_capture.validate_evaluation_separation(
        fixture.m_receipt,
        { fixture.m_selected.m_inventory, fixture.m_selected.m_visibility },
        K_CONTEXT));
    CHECK(!fixture.m_capture.validate_evaluation_separation(fixture.m_receipt, {}, K_CONTEXT));
    CHECK(!fixture.m_capture.validate_evaluation_separation(
        fixture.m_receipt,
        { fixture.m_selected.m_inventory, fixture.m_selected.m_inventory },
        K_CONTEXT));
    auto stale = fixture.m_selected.m_inventory;
    stale.m_revision++;
    CHECK(!fixture.m_capture.validate_evaluation_separation(fixture.m_receipt, { stale }, K_CONTEXT));
    auto context = K_CONTEXT;
    context.m_world_epoch = { 4 };
    CHECK(!fixture.m_capture.validate_evaluation_separation(
        fixture.m_receipt, { fixture.m_selected.m_inventory }, context));
    CHECK(!fixture.m_capture.validate_evaluation_separation(
        fixture.m_receipt, { fixture.m_views.m_selection.m_frames[0uz].m_view.m_image }, K_CONTEXT));
    std::stop_source stop;
    stop.request_stop();
    CHECK(!fixture.m_capture.validate_evaluation_separation(
        fixture.m_receipt, { fixture.m_selected.m_inventory }, K_CONTEXT, stop.get_token()));
}
