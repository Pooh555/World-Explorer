#include "../fixtures/corpus_inspection_fixture.hpp"
#include "world_explorer/application/commands.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace world_explorer;
using namespace world_explorer::corpus_inspection_test;

namespace {

void matches(InspectionFixture& fixture) {
    const auto K_SHA = fixture.save();
    const auto K_SOURCE = inspection_tree(fixture.m_input.m_receipt.source_directory());
    const auto K_RECIPE = inspection_tree(fixture.m_recipe_receipt.source_directory());
    const auto K_OUTPUT = inspection_tree(fixture.m_output.m_root);
    const auto K_RESULT = inspect_asset_corpus_request(fixture.request(), K_SHA);
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->canonical_json() == fixture.m_corpus.canonical_json());
    CHECK(K_RESULT->blockers() == fixture.m_corpus.blockers());
    CHECK(!K_RESULT->blockers().empty());
    CHECK(inspection_tree(fixture.m_input.m_receipt.source_directory()) == K_SOURCE);
    CHECK(inspection_tree(fixture.m_recipe_receipt.source_directory()) == K_RECIPE);
    CHECK(inspection_tree(fixture.m_output.m_root) == K_OUTPUT);
}

}  // namespace

TEST_CASE(
    "corpus inspection reopens independent receipts annotated capture and seal without writes",
    "[corpus-inspection]") {
    SelectionFixture views;
    const auto K_RECEIPT = views.freeze();
    const auto K_CAPTURE = checked(views.inspect(K_RECEIPT));
    InspectionFixture fixture({ "corner", K_CAPTURE, K_RECEIPT, {} });
    matches(fixture);
    // Paths are relative to the request, never the inspected corpus or cwd.
    fixture.m_request["captures"][0uz]["registry"] = "capture";
    matches(fixture);
}

TEST_CASE("corpus inspection loads independent typed inventory and controlled references",
          "[corpus-inspection]") {
    controlled_references_test::ControlledFixture source;
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_inventory = source.m_inventory;
    for (const auto& pin : source.m_selected) {
        input.m_references.push_back(
            checked(inspect_controlled_reference(pin,
                                                 source.m_inventory,
                                                 source.m_capture,
                                                 source.m_receipt,
                                                 source.m_receipt.graph().m_context)));
    }
    InspectionFixture fixture(input);
    matches(fixture);
    fixture.m_request["captures"][0uz]["inventory"] = nullptr;
    CHECK(!fixture.inspect());
}

TEST_CASE("corpus inspection loads typed temporal paths and capture envelope",
          "[corpus-inspection]") {
    capture_envelope_test::EnvelopeFixture source;
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_paths = checked(inspect_evaluation_paths(source.m_base.m_selected,
                                                     source.m_capture,
                                                     source.m_receipt,
                                                     source.m_receipt.graph().m_context));
    input.m_envelope = checked(source.inspect());
    InspectionFixture fixture(input);
    matches(fixture);
    fixture.m_request["captures"][0uz]["envelope"]["sources"][0uz]["source_frame_count"] = nullptr;
    CHECK(!fixture.inspect());
}

TEST_CASE("corpus inspection loads typed protocol masks configs lights and preserves blockers",
          "[corpus-inspection]") {
    evaluation_protocol_test::ProtocolFixture source;
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_protocol = checked(source.inspect());
    input.m_paths = checked(inspect_evaluation_paths(source.m_base.m_selected,
                                                     source.m_capture,
                                                     source.m_receipt,
                                                     source.m_receipt.graph().m_context));
    InspectionFixture fixture(input);
    matches(fixture);
    fixture.m_request["captures"][0uz]["protocol"] = nullptr;
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("corpus inspection loads independently pinned measurement scene with bounded bundle",
          "[corpus-inspection]") {
    capture_measurement_test::MeasurementFixture source;
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_measurement = checked(source.inspect());
    InspectionFixture fixture(input);
    const auto K_BUNDLE = fixture.m_output.m_root / "source";
    REQUIRE(write_bundle(K_BUNDLE, std::make_shared<const SceneState>(source.m_scene)));
    const auto K_BYTES =
        checked(asset_files::read_bytes(K_BUNDLE / "manifest.json", 1024uz * 1024uz, {}));
    fixture.m_request["captures"][0uz]["measurement"]["manifest_sha256"] = sha256(K_BYTES);
    matches(fixture);
    fixture.m_request["captures"][0uz]["measurement"]["selection"]["scene"]["revision"] = 99;
    CHECK(!fixture.inspect());
}

TEST_CASE("corpus inspection refuses changed selections stale commits and fallback from the seal",
          "[corpus-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CorpusFixture source;
    InspectionFixture fixture(source.captures()[0uz]);
    auto& row = fixture.m_request["captures"][0uz];
    switch (K_CASE) {
    case 0:
        fixture.m_request["corpus"]["commit"]["generation"] = 99;
        break;
    case 1:
        fixture.m_request["corpus"]["commit"]["manifest_sha256"] = hash("foreign seal");
        break;
    case 2:
        fixture.m_request["corpus"]["selected"]["sha256"] = hash("foreign corpus");
        break;
    case 3:
        fixture.m_request["corpus"]["selected"]["revision"] = 99;
        break;
    case 4:
        row["commit"]["generation"] = 99;
        break;
    case 5:
        row["commit"]["manifest_sha256"] = hash("foreign capture commit");
        break;
    case 6:
        row["receipt"]["sha256"] = hash("foreign receipt");
        break;
    case 7:
        row["context"]["pose_revision"] = 99;
        break;
    case 8:
        row["selection"]["product"]["content_sha256"] = hash("foreign capture");
        break;
    case 9:
        row["selection"]["frames"][0uz]["view"]["frame_pose_revision"] = 99;
        break;
    case 10:
        row["selection"] = nullptr;
        break;
    case 11:
        row["receipt"] = nullptr;
        break;
    case 12:
        fixture.m_request["recipes"][0uz]["receipt"]["sha256"] = hash("foreign recipe receipt");
        break;
    case 13:
        fixture.m_request["recipes"][0uz]["label"] = "other";
        break;
    case 14:
        row["label"] = "other";
        break;
    case 15:
        fixture.m_request["corpus"]["directory"] = "missing-directory";
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = inspection_tree(fixture.m_output.m_root);
    CHECK(!inspect_asset_corpus_request(fixture.request(), K_SHA));
    CHECK(inspection_tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("corpus inspection bounds request schema tables enums integers and paths",
          "[corpus-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CorpusFixture source;
    InspectionFixture fixture(source.captures()[0uz]);
    auto& row = fixture.m_request["captures"][0uz];
    switch (K_CASE) {
    case 0:
        fixture.m_request["unknown"] = true;
        break;
    case 1:
        fixture.m_request["schema_version"] = 2;
        break;
    case 2:
        fixture.m_request["captures"] = Json::array();
        break;
    case 3:
        fixture.m_request["captures"] = std::vector<Json>(17uz, row);
        break;
    case 4:
        fixture.m_request["recipes"] = std::vector<Json>(17uz, fixture.m_request["recipes"][0uz]);
        break;
    case 5:
        row["selection"]["frames"] = std::vector<Json>(65uz, row["selection"]["frames"][0uz]);
        break;
    case 6:
        row["selection"]["frames"][0uz]["role"] = "UNKNOWN";
        break;
    case 7:
        row["selection"]["selection_id"] = -1;
        break;
    case 8:
        row["commit"]["generation"] = 1.0;
        break;
    case 9:
        row["registry"] = std::string("a\0b", 3uz);
        break;
    case 10:
        row["registry"] = std::string(4097uz, 'x');
        break;
    case 11:
        row["references"] = std::vector<Json>(4uz, nullptr);
        break;
    case 12:
        row["annotations"] = std::vector<Json>(129uz, nullptr);
        break;
    case 13:
        row["selection"]["sources"][0uz]["source_sha256"] = "bad";
        break;
    case 14:
        row["context"]["world_epoch"] = true;
        break;
    case 15:
        fixture.m_request.erase("recipes");
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    CHECK(!fixture.inspect());
}

TEST_CASE("corpus inspection bounds typed selections before admission", "[corpus-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    evaluation_protocol_test::ProtocolFixture source;
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_protocol = checked(source.inspect());
    InspectionFixture fixture(input);
    auto& pin = fixture.m_request["captures"][0uz]["protocol"];
    switch (K_CASE) {
    case 0:
        pin["uv_resolution"] = std::numeric_limits<std::uint64_t>::max();
        break;
    case 1:
        pin["masks"][0uz]["track"] = "INFERENCE";
        break;
    case 2:
        pin["masks"] = std::vector<Json>(33uz, pin["masks"][0uz]);
        break;
    case 3:
        pin["lights"] = std::vector<Json>(9uz, pin["lights"][0uz]);
        break;
    case 4:
        pin["lights"][0uz]["near_light"] = 1;
        break;
    case 5:
        pin["artifacts"]["OTHER"] = pin["artifacts"]["RENDERER"];
        break;
    case 6:
        pin["geometry_seed"] = 999;
        break;
    case 7:
        pin["product"]["content_sha256"] = hash("unselected protocol");
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "corpus inspection refuses raw duplicate nul excessive nesting bytes and wrong request hash",
    "[corpus-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    CorpusFixture source;
    InspectionFixture fixture(source.captures()[0uz]);
    auto raw = fixture.m_request.dump();
    switch (K_CASE) {
    case 0:
        raw.insert(1uz, "\"schema_version\":1,");
        break;
    case 1:
        raw.push_back('\0');
        break;
    case 2:
        raw = std::string(18uz, '[') + "0" + std::string(18uz, ']');
        break;
    case 3:
        raw.resize(2uz * 1024uz * 1024uz + 1uz, ' ');
        break;
    case 4:
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    write(fixture.request(), raw);
    const auto K_RESULT =
        inspect_asset_corpus_request(fixture.request(), K_CASE == 4 ? hash("wrong") : hash(raw));
    REQUIRE(!K_RESULT);
    if (K_CASE == 4) {
        CHECK(K_RESULT.error().m_code == ErrorCode::RESYNC_REQUIRED);
    }
}

TEST_CASE(
    "corpus inspection refuses missing prerequisites before resolving input paths and cancels",
    "[corpus-inspection]") {
    RecipeFixture output;
    const auto K_REQUEST = output.m_root / "missing.json";
    const std::string K_TEXT { R"({"schema_version":1,"corpus":null,"captures":[],"recipes":[]})" };
    write(K_REQUEST, K_TEXT);
    const auto K_RESULT = inspect_asset_corpus_request(K_REQUEST, hash(K_TEXT));
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(K_RESULT.error().m_message.find("Independent corpus/seal pin") != std::string::npos);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCEL =
        inspect_asset_corpus_request(output.m_root / "absent", "bad", stop.get_token());
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("native corpus inspection CLI reports synthetic blockers refuses incomplete request and "
          "preserves data",
          "[corpus-inspection][corpus-cli]") {
    CorpusFixture source;
    InspectionFixture fixture(source.captures()[0uz]);
    const auto K_SHA = fixture.save();
    const auto K_SOURCE = inspection_tree(fixture.m_input.m_receipt.source_directory());
    const auto K_OUTPUT = inspection_tree(fixture.m_output.m_root);
    std::string output;
    const auto K_LOG = [&](const std::string& line) {
        output += line;
    };
    const auto K_EXIT = run_process(
        { WE_TEST_PROCESS, "inspect-corpus", fixture.request().string(), K_SHA }, {}, K_LOG);
    INFO(output);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 0);
    CHECK(output.find("Track: SYNTHETIC") != output.npos);
    for (const auto& blocker : fixture.m_corpus.blockers()) {
        CHECK(output.find(blocker.m_scope + ": " + blocker.m_code) != output.npos);
    }
    CHECK(inspection_tree(fixture.m_input.m_receipt.source_directory()) == K_SOURCE);
    CHECK(inspection_tree(fixture.m_output.m_root) == K_OUTPUT);
    output.clear();
    fixture.m_request["corpus"] = nullptr;
    const auto K_REFUSED = run_process(
        { WE_TEST_PROCESS, "inspect-corpus", fixture.request().string(), fixture.save() }, {}, K_LOG);
    REQUIRE(K_REFUSED);
    CHECK(*K_REFUSED == 2);
    CHECK(output.find("Independent corpus/seal pin") != output.npos);
    output.clear();
    CHECK(checked(run_process({ WE_TEST_PROCESS, "inspect-corpus" }, {}, K_LOG)) == 2);
    CHECK(output.find("Usage:") != output.npos);
}

TEST_CASE("corpus v2 independently selects mixed profiles and reserves exact receipt totals",
          "[corpus-inspection][corpus-v2]") {
    CorpusFixture source;
    InspectionFixture fixture(source.captures()[0uz]);
    const auto K_TOTAL =
        fixture.m_input.m_receipt.payload_bytes() + fixture.m_recipe_receipt.payload_bytes();
    fixture.select_v2(K_TOTAL);
    matches(fixture);
    const auto K_RECIPE = fixture.m_recipe_receipt.source_directory();
    std::filesystem::rename(K_RECIPE / "blobs", K_RECIPE / "withheld");
    fixture.m_request["receipt_budget_bytes"] = K_TOTAL - 1uz;
    const auto K_BEFORE = inspection_tree(K_RECIPE);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(inspection_tree(K_RECIPE) == K_BEFORE);
    std::filesystem::rename(K_RECIPE / "withheld", K_RECIPE / "blobs");
    fixture.m_request["receipt_budget_bytes"] = K_TOTAL;
    matches(fixture);
    auto repeated = fixture.m_request["recipes"][0uz];
    repeated["label"] = "second_import";
    fixture.m_request["recipes"].push_back(repeated);
    const auto K_REPEAT = fixture.inspect();
    REQUIRE(!K_REPEAT);
    CHECK(K_REPEAT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("corpus v2 replays all twelve native grid evaluation views through API and CLI",
          "[corpus-inspection][corpus-v2][corpus-cli]") {
    evaluation_protocol_test::ProtocolFixture source(
        {},
        {},
        {},
        {},
        [](SelectionFixture& views) {
            evaluation_protocol_test::configure_v2_native_views(views, 3840, 2160);
        },
        2);
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_protocol = checked(source.inspect());
    REQUIRE(input.m_protocol->masks().size() == 24uz);
    InspectionFixture fixture(input);
    fixture.select_v2(fixture.m_input.m_receipt.payload_bytes() +
                      fixture.m_recipe_receipt.payload_bytes());
    matches(fixture);
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = inspection_tree(fixture.m_output.m_root);
    std::string output;
    const auto K_EXIT =
        run_process({ WE_TEST_PROCESS, "inspect-corpus", fixture.request().string(), K_SHA },
                    {},
                    [&](const std::string& line) {
                        output += line;
                    });
    INFO(output);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 0);
    CHECK(output.find("Track: SYNTHETIC") != output.npos);
    for (const auto& blocker : fixture.m_corpus.blockers()) {
        CHECK(output.find(blocker.m_scope + ": " + blocker.m_code) != output.npos);
    }
    CHECK(inspection_tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE(
    "corpus v2 rejects missing foreign and malformed independently selected versions and limits",
    "[corpus-inspection][corpus-v2]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    evaluation_protocol_test::ProtocolFixture source(
        {},
        {},
        {},
        {},
        [](SelectionFixture& views) {
            evaluation_protocol_test::configure_v2_native_views(views, 3840, 2160);
        },
        2);
    AssetCorpusCaptureInput input { "corner", source.m_capture, source.m_receipt, {} };
    input.m_protocol = checked(source.inspect());
    InspectionFixture fixture(input);
    fixture.select_v2(1024uz * 1024uz);
    auto& row = fixture.m_request["captures"][0uz];
    switch (K_CASE) {
    case 0:
        fixture.m_request.erase("receipt_budget_bytes");
        break;
    case 1:
        fixture.m_request["receipt_budget_bytes"] = 0;
        break;
    case 2:
        fixture.m_request["receipt_budget_bytes"] = 1024uz * 1024uz * 1024uz + 1uz;
        break;
    case 3:
        fixture.m_request["receipt_budget_bytes"] = true;
        break;
    case 4:
        row["receipt"].erase("schema_version");
        break;
    case 5:
        row["receipt"]["schema_version"] = 1;
        break;
    case 6:
        row["receipt"]["schema_version"] = 3;
        break;
    case 7:
        row["selection"].erase("schema_version");
        break;
    case 8:
        row["selection"]["schema_version"] = 1;
        break;
    case 9:
        row["selection"]["schema_version"] = 2.0;
        break;
    case 10:
        row["protocol"].erase("schema_version");
        break;
    case 11:
        row["protocol"]["schema_version"] = 1;
        break;
    case 12:
        row["protocol"]["schema_version"] = std::numeric_limits<std::uint64_t>::max();
        break;
    case 13:
        fixture.m_request["recipes"][0uz]["receipt"].erase("schema_version");
        break;
    case 14:
        fixture.m_request["schema_version"] = 1;
        break;
    case 15:
        fixture.m_request["receipt_budget_bytes"] = -1;
        break;
    case 16:
        fixture.m_request["receipt_budget_bytes"] = 1.0;
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    const auto K_BEFORE = inspection_tree(fixture.m_output.m_root / "capture");
    CHECK(!fixture.inspect());
    CHECK(inspection_tree(fixture.m_output.m_root / "capture") == K_BEFORE);
}
