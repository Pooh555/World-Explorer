#include "../fixtures/asset_region_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>

using namespace world_explorer;
using namespace world_explorer::asset_region_test;
using world_explorer::asset_component_test::tree;

TEST_CASE("region provenance partitions faces and texels with independent evidence and confidence",
          "[asset-region]") {
    const auto K_TEXELS = GENERATE(false, true);
    RegionFixture fixture;
    if (K_TEXELS) {
        fixture.m_selection.m_domain = { AssetRegionKind::MATERIAL_TEXELS, 8, 4, 2 };
        fixture.m_record["domain"] = {
            { "kind", "MATERIAL_TEXELS" }, { "elements", 8 }, { "width", 4 }, { "height", 2 } };
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_directory.m_root);
    const auto K_FIRST = fixture.inspect(K_RECEIPT);
    REQUIRE(K_FIRST);
    CHECK(K_FIRST->m_coverage == std::array<std::uint64_t, 5> { 2, 2, 2, 2, 0 });
    CHECK(K_FIRST->m_manual_ancestor_elements == 0);
    CHECK(K_FIRST->m_record.m_spans[3uz].m_class == AssetEvidenceClass::GENERATED);
    CHECK(K_FIRST->m_record.m_spans[3uz].m_confidence == 1.0);
    CHECK(K_FIRST->m_record.m_domain == fixture.m_selection.m_domain);
    CHECK(tree(fixture.m_directory.m_root) == K_BEFORE);
    std::ranges::reverse(fixture.m_selection.m_sources);
    std::ranges::reverse(fixture.m_record["sources"]);
    const auto K_REORDERED_RECEIPT = fixture.freeze();
    const auto K_REORDERED = fixture.inspect(K_REORDERED_RECEIPT);
    REQUIRE(K_REORDERED);
    CHECK(K_REORDERED->m_record.m_sources == K_FIRST->m_record.m_sources);
    CHECK(K_REORDERED->m_coverage == K_FIRST->m_coverage);
    CHECK(K_REORDERED->m_provenance_sha256 != K_FIRST->m_provenance_sha256);
    // An owned sidecar remains inspectable after its original files disappear.
    std::filesystem::remove_all(fixture.m_directory.m_root / "blobs");
    const auto K_RETAINED = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RETAINED);
    CHECK(K_RETAINED->m_record.m_sources == K_FIRST->m_record.m_sources);
}

TEST_CASE("region provenance rejects gaps overlaps missing tails and unsafe scalar values",
          "[asset-region]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    RegionFixture fixture;
    auto& spans = fixture.m_record["spans"];
    if (K_VARIANT == 0) {
        spans[1uz]["first"] = 3;
    }
    if (K_VARIANT == 1) {
        spans[1uz]["first"] = 1;
    }
    if (K_VARIANT == 2) {
        spans[1uz]["count"] = 0;
    }
    if (K_VARIANT == 3) {
        spans[3uz]["count"] = 1;
    }
    if (K_VARIANT == 4) {
        spans[3uz]["count"] = std::numeric_limits<std::uint64_t>::max();
    }
    if (K_VARIANT == 5) {
        spans[0uz]["first"] = -1;
    }
    if (K_VARIANT == 6) {
        spans[2uz]["count"] = 2.0;
    }
    if (K_VARIANT == 7) {
        spans[2uz]["confidence"] = -0.01;
    }
    if (K_VARIANT == 8) {
        spans[2uz]["confidence"] = 1.01;
    }
    if (K_VARIANT == 9) {
        spans[2uz]["confidence"] = "high";
    }
    if (K_VARIANT == 10) {
        spans[0uz]["confidence"] = 0.0;
    }
    if (K_VARIANT == 11) {
        spans[1uz]["support"] = Json::array();
    }
    if (K_VARIANT == 12) {
        spans[0uz]["support"] = spans[1uz]["support"];
    }
    if (K_VARIANT == 13) {
        spans[2uz]["class"] = "MEASURED";
    }
    if (K_VARIANT == 14) {
        spans[2uz]["manual_ancestor"] = 1;
    }
    if (K_VARIANT == 15) {
        spans = Json::array();
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_directory.m_root);
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_RESULT);
    CHECK(tree(fixture.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("region provenance binds exact independent target source and context identities",
          "[asset-region]") {
    const auto K_VARIANT =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    RegionFixture fixture;
    if (K_VARIANT == 0) {
        fixture.m_record["world_epoch"] = 4;
    }
    if (K_VARIANT == 1) {
        fixture.m_record["pose_revision"] = 6;
    }
    if (K_VARIANT == 2) {
        fixture.m_record["target"]["revision"] = 2;
    }
    if (K_VARIANT == 3) {
        fixture.m_record["recipe_sha256"] = hash("different recipe");
    }
    if (K_VARIANT == 4) {
        fixture.m_record["sources"][0uz]["calibration_id"] = 9;
    }
    if (K_VARIANT == 5) {
        fixture.m_record["sources"][0uz]["calibration_sha256"] = hash("different calibration");
    }
    if (K_VARIANT == 6) {
        fixture.m_record["sources"][0uz]["pose_sha256"] = hash("different pose");
    }
    if (K_VARIANT == 7) {
        fixture.m_record["sources"][0uz]["image_grid_sha256"] = hash("different crop/resize");
    }
    if (K_VARIANT == 8) {
        fixture.m_record["sources"][0uz]["image"]["content_sha256"] = hash("different image");
    }
    if (K_VARIANT == 9) {
        fixture.m_record["sources"][0uz]["view"]["frame_id"] = 99;
    }
    if (K_VARIANT == 10) {
        fixture.m_record["spans"][1uz]["support"][0uz]["capture_id"] = 99;
    }
    if (K_VARIANT == 11) {
        fixture.m_record["spans"][1uz]["parents"][0uz]["revision"] = 2;
    }
    const auto K_RECEIPT = fixture.freeze();
    if (K_VARIANT == 12) {
        fixture.m_selection.m_target.m_content_sha256 = hash("changed target");
    }
    if (K_VARIANT == 13) {
        fixture.m_selection.m_sources[0uz].m_image.m_revision = 2;
    }
    if (K_VARIANT == 14) {
        fixture.m_selection.m_sources[0uz].m_view.m_frame = {};
    }
    if (K_VARIANT == 15) {
        fixture.m_graph.m_context.m_world_epoch = { 4 };
    }
    if (K_VARIANT == 16) {
        fixture.m_selection.m_domain.m_elements = 9;
    }
    if (K_VARIANT == 17) {
        fixture.m_selection.m_sources[0uz].m_image = fixture.m_selection.m_target;
    }
    if (K_VARIANT == 18) {
        fixture.m_selection.m_sources[0uz].m_pose_sha256 = std::string(65536uz, 'a');
    }
    if (K_VARIANT == 19) {
        fixture.m_selection.m_sources[0uz].m_calibration_sha256 = std::string(64uz, 'A');
    }
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_RESULT);
}

TEST_CASE("region provenance rejects duplicated unused and unrelated source lineage",
          "[asset-region]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6);
    RegionFixture fixture;
    if (K_VARIANT == 0) {
        fixture.m_record["sources"].push_back(fixture.m_record["sources"][0uz]);
    }
    if (K_VARIANT == 1) {
        fixture.m_record["spans"][1uz]["support"].push_back(
            fixture.m_record["spans"][1uz]["support"][0uz]);
    }
    if (K_VARIANT == 2) {
        fixture.m_record["spans"][1uz]["parents"].push_back(
            fixture.m_record["spans"][1uz]["parents"][0uz]);
    }
    if (K_VARIANT == 3) {
        fixture.m_record["spans"][1uz]["support"].erase(1uz);
    }
    if (K_VARIANT == 4) {
        fixture.m_record["spans"][1uz]["parents"][0uz] =
            region_ref(fixture.m_graph.m_products[2uz].m_product);
    }
    if (K_VARIANT == 5) {
        fixture.m_graph.m_products[3uz].m_dependencies.push_back(
            fixture.m_graph.m_products[0uz].m_product);
    }
    const auto K_RECEIPT = fixture.freeze();
    if (K_VARIANT == 6) {
        fixture.m_selection.m_sources.push_back(fixture.m_selection.m_sources.front());
    }
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_RESULT);
}

TEST_CASE("region provenance retains inherited manual ancestry without changing evidence class",
          "[asset-region]") {
    RegionFixture fixture;
    fixture.manual(true);
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_manual_ancestor_elements == 4);
    CHECK(K_RESULT->m_coverage == std::array<std::uint64_t, 5> { 2, 2, 2, 2, 0 });
    CHECK(K_RESULT->m_record.m_spans[2uz].m_class == AssetEvidenceClass::INFERRED);
    CHECK(K_RESULT->m_record.m_spans[2uz].m_manual_ancestor);
    CHECK(!K_RESULT->m_record.m_spans[3uz].m_manual_ancestor);
    // Supporting a manually derived image also taints lineage without an explicit parent row.
    fixture.m_record["spans"][1uz]["parents"] = Json::array();
    const auto K_SUPPORT_RECEIPT = fixture.freeze();
    const auto K_SUPPORT = fixture.inspect(K_SUPPORT_RECEIPT);
    REQUIRE(K_SUPPORT);
    CHECK(K_SUPPORT->m_manual_ancestor_elements == 4);
}

TEST_CASE("region provenance cannot clear declared manual edits or ancestry", "[asset-region]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6);
    RegionFixture fixture;
    fixture.manual(K_VARIANT >= 3);
    if (K_VARIANT == 0) {
        fixture.m_record["spans"][3uz]["manual_ancestor"] = false;
    }
    if (K_VARIANT == 1) {
        fixture.m_record["spans"][3uz]["class"] = "GENERATED";
    }
    if (K_VARIANT == 2) {
        fixture.m_record["manual_ancestor"] = false;
    }
    if (K_VARIANT == 3) {
        fixture.m_record["spans"][2uz]["manual_ancestor"] = false;
    }
    if (K_VARIANT == 4) {
        fixture.m_record["spans"][0uz]["manual_ancestor"] = true;
    }
    if (K_VARIANT == 5) {
        fixture.m_record["spans"][3uz]["class"] = "MANUALLY_EDITED";
    }
    if (K_VARIANT == 6) {
        fixture.m_record["spans"][1uz]["manual_ancestor"] = false;
        fixture.m_record["spans"][2uz]["manual_ancestor"] = false;
        fixture.m_record["spans"][1uz]["support"] =
            Json::array({ source_view_id(fixture.m_selection.m_sources[1uz].m_view) });
        fixture.m_record["sources"].erase(0uz);
        fixture.m_selection.m_sources.erase(fixture.m_selection.m_sources.begin());
        fixture.m_record["spans"][1uz]["parents"] = Json::array();
        fixture.m_record["spans"][2uz]["parents"] = Json::array();
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_RESULT);
}

TEST_CASE("region provenance admits local declared edits but does not authenticate their actions",
          "[asset-region]") {
    RegionFixture fixture;
    fixture.manual(false);
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_coverage == std::array<std::uint64_t, 5> { 2, 2, 2, 0, 2 });
    CHECK(K_RESULT->m_manual_ancestor_elements == 2);
}

TEST_CASE("region provenance bounds element arithmetic dimensions and encoded records",
          "[asset-region]") {
    RegionFixture fixture;
    fixture.m_selection.m_sources.clear();
    fixture.m_record["sources"] = Json::array();
    fixture.m_selection.m_domain = { AssetRegionKind::MATERIAL_TEXELS, 16777216, 4096, 4096 };
    fixture.m_record["domain"] = {
        { "kind", "MATERIAL_TEXELS" }, { "elements", 16777216 }, { "width", 4096 }, { "height", 4096 } };
    auto row = fixture.m_record["spans"][0uz];
    row["count"] = 16777216;
    fixture.m_record["spans"] = Json::array({ row });
    auto text = fixture.m_record.dump();
    text.resize(4uz * 1024uz * 1024uz, ' ');
    const auto K_EXACT_RECEIPT = fixture.freeze(text);
    const auto K_EXACT = fixture.inspect(K_EXACT_RECEIPT);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->m_coverage[0uz] == 16777216);
    const auto K_EXCESS_RECEIPT = fixture.freeze(text + ' ');
    const auto K_EXCESS = fixture.inspect(K_EXCESS_RECEIPT);
    REQUIRE(!K_EXCESS);
    CHECK(K_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4);
    if (K_VARIANT == 0) {
        fixture.m_record["domain"]["elements"] = 16777217;
    }
    if (K_VARIANT == 1) {
        fixture.m_record["domain"]["height"] = std::numeric_limits<std::uint64_t>::max();
    }
    if (K_VARIANT == 2) {
        fixture.m_record["domain"]["width"] = 4097;
    }
    if (K_VARIANT == 3) {
        fixture.m_record["domain"]["elements"] = 0;
    }
    if (K_VARIANT == 4) {
        fixture.m_record["domain"]["kind"] = "VERTICES";
    }
    const auto K_BAD_RECEIPT = fixture.freeze();
    const auto K_BAD = fixture.inspect(K_BAD_RECEIPT);
    REQUIRE(!K_BAD);
}

TEST_CASE("region provenance rejects rehashed malformed JSON and preserves cancellation",
          "[asset-region]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6);
    RegionFixture fixture;
    auto text = fixture.m_record.dump();
    if (K_VARIANT == 0) {
        text.insert(1uz, "\"schema_version\":1,");
    }
    if (K_VARIANT == 1) {
        text += std::string(1uz, '\0');
    }
    if (K_VARIANT == 2) {
        text = std::string(17uz, '[') + "0" + std::string(17uz, ']');
    }
    if (K_VARIANT == 3) {
        fixture.m_record["schema_version"] = 2;
    }
    if (K_VARIANT == 4) {
        fixture.m_record["extra"] = true;
    }
    if (K_VARIANT == 5) {
        fixture.m_record["spans"][2uz]["confidence"] = true;
    }
    if (K_VARIANT == 6) {
        fixture.m_record["spans"][2uz]["confidence"] = Json::object();
    }
    const auto K_RECEIPT = fixture.freeze(K_VARIANT < 3 ? std::optional { text } : std::nullopt);
    const auto K_BEFORE = tree(fixture.m_directory.m_root);
    const auto K_BAD = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_BAD);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = fixture.inspect(K_RECEIPT, stop.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(tree(fixture.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("region provenance preserves distinct view identities with shared image bytes at its "
          "view limit",
          "[asset-region]") {
    RegionFixture fixture;
    auto source = fixture.m_selection.m_sources.front();
    fixture.m_selection.m_sources.clear();
    fixture.m_record["sources"] = Json::array();
    auto row = fixture.m_record["spans"][1uz];
    row["first"] = 0;
    row["count"] = 8;
    row["support"] = Json::array();
    for (std::size_t index { 0uz }; index < 64uz; ++index) {
        source.m_view.m_frame = { index + 1uz };
        fixture.m_selection.m_sources.push_back(source);
        fixture.m_record["sources"].push_back(region_source(source));
        row["support"].push_back(source_view_id(source.m_view));
    }
    fixture.m_record["spans"] = Json::array({ row });
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = fixture.m_selection.m_sources;
    const auto K_EXACT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->m_record.m_sources.size() == 64uz);
    CHECK(K_EXACT->m_record.m_spans[0uz].m_support.size() == 64uz);
    CHECK(K_EXACT->m_coverage[1uz] == 8);
    CHECK(fixture.m_selection.m_sources == K_BEFORE);
    source.m_view.m_frame = { 65 };
    fixture.m_selection.m_sources.push_back(source);
    const auto K_SELECTION_EXCESS = fixture.inspect(K_RECEIPT);
    REQUIRE(!K_SELECTION_EXCESS);
    CHECK(K_SELECTION_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    fixture.m_selection.m_sources.pop_back();
    fixture.m_record["sources"].push_back(region_source(source));
    const auto K_RECORD_RECEIPT = fixture.freeze();
    const auto K_RECORD_EXCESS = fixture.inspect(K_RECORD_RECEIPT);
    REQUIRE(!K_RECORD_EXCESS);
    CHECK(K_RECORD_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("region provenance admits the exact span budget and rejects excess", "[asset-region]") {
    RegionFixture fixture;
    fixture.m_selection.m_sources.clear();
    fixture.m_record["sources"] = Json::array();
    auto row = fixture.m_record["spans"][0uz];
    row["count"] = 1;
    fixture.m_record["spans"] = Json::array();
    fixture.m_selection.m_domain.m_elements = 16384;
    fixture.m_record["domain"]["elements"] = 16384;
    for (std::size_t index { 0uz }; index < 16384uz; ++index) {
        row["first"] = index;
        fixture.m_record["spans"].push_back(row);
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_EXACT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->m_record.m_spans.size() == 16384uz);
    CHECK(K_EXACT->m_coverage[0uz] == 16384);
    row["first"] = 16384;
    fixture.m_record["spans"].push_back(row);
    fixture.m_record["domain"]["elements"] = 16385;
    fixture.m_selection.m_domain.m_elements = 16385;
    const auto K_EXCESS_RECEIPT = fixture.freeze();
    const auto K_EXCESS = fixture.inspect(K_EXCESS_RECEIPT);
    REQUIRE(!K_EXCESS);
    CHECK(K_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("region provenance bounds aggregate support and parent references together",
          "[asset-region]") {
    RegionFixture fixture;
    auto source = fixture.m_selection.m_sources.front();
    fixture.m_selection.m_sources.clear();
    fixture.m_record["sources"] = Json::array();
    auto row = fixture.m_record["spans"][3uz];
    row["count"] = 1;
    row["parents"] = Json::array();
    row["support"] = Json::array();
    for (std::size_t index { 0uz }; index < 64uz; ++index) {
        source.m_view.m_frame = { index + 1uz };
        fixture.m_selection.m_sources.push_back(source);
        fixture.m_record["sources"].push_back(region_source(source));
        row["support"].push_back(source_view_id(source.m_view));
    }
    fixture.m_record["spans"] = Json::array();
    fixture.m_selection.m_domain.m_elements = 1024;
    fixture.m_record["domain"]["elements"] = 1024;
    for (std::size_t index { 0uz }; index < 1024uz; ++index) {
        row["first"] = index;
        fixture.m_record["spans"].push_back(row);
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_EXACT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->m_coverage[3uz] == 1024);
    fixture.m_record["spans"][0uz]["parents"].push_back(
        region_ref(fixture.m_graph.m_products[0uz].m_product));
    const auto K_EXCESS_RECEIPT = fixture.freeze();
    const auto K_EXCESS = fixture.inspect(K_EXCESS_RECEIPT);
    REQUIRE(!K_EXCESS);
    CHECK(K_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}
