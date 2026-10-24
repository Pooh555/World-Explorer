#include "../fixtures/asset_component_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <array>

using namespace world_explorer;
using namespace world_explorer::asset_recipe_test;
using namespace world_explorer::asset_component_test;

TEST_CASE("component inventories distinguish exact empty-file identities from missing payloads",
          "[asset-component-evidence]") {
    EvidenceFixture fixture;
    auto& manifest = fixture.m_manifests[0uz];
    manifest["files"].push_back({ { "path", "empty.marker" },
                                 { "sha256", sha256(std::span<const std::byte> {}) },
                                 { "bytes", 0 } });
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_components[0uz].m_file_count == 2uz);
    CHECK(K_RESULT->m_components[0uz].m_installed_bytes ==
          manifest["files"][0uz]["bytes"].get<std::uint64_t>());
    manifest["files"][1uz]["sha256"] = std::string(64uz, '0');
    const auto K_BAD = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_BAD);
    REQUIRE(!K_REJECTED);
}

TEST_CASE("component evidence JSON budgets admit exact records and reject rehashed malformed bytes",
          "[asset-component-evidence]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    EvidenceFixture fixture;
    fixture.m_terms = std::string(64uz * 1024uz, 't');
    auto text = fixture.m_manifests[0uz].dump();
    text.resize(256uz * 1024uz, ' ');
    fixture.m_raw_manifest = text;
    const auto K_FIRST = fixture.freeze();
    const auto K_REVIEW_BYTES =
        K_FIRST.second.copy_blob(fixture.m_selected[0uz].m_review_sha256, 64uz * 1024uz);
    REQUIRE(K_REVIEW_BYTES);
    std::string review(reinterpret_cast<const char*>(K_REVIEW_BYTES->data()),
                       K_REVIEW_BYTES->size());
    review.resize(64uz * 1024uz, ' ');
    fixture.m_raw_review = review;
    const auto K_EXACT = fixture.freeze();
    const auto K_BASELINE = fixture.inspect(K_EXACT);
    REQUIRE(K_BASELINE);
    if (K_FIELD == 0) {
        fixture.m_raw_manifest = text + ' ';
    }
    if (K_FIELD == 1) {
        text.insert(1uz, "\"schema_version\":1,");
        text.resize(256uz * 1024uz);
        fixture.m_raw_manifest = text;
    }
    if (K_FIELD == 2) {
        text[0uz] = '\0';
        fixture.m_raw_manifest = text;
    }
    if (K_FIELD == 3) {
        fixture.m_raw_manifest =
            "{\"deep\":" + std::string(20uz, '[') + "0" + std::string(20uz, ']') + "}";
    }
    if (K_FIELD == 4) {
        fixture.m_terms += 't';
    }
    if (K_FIELD == 5) {
        fixture.m_raw_review = review + ' ';
    }
    if (K_FIELD != 5) {
        fixture.m_raw_review.reset();
    }
    const auto K_BAD = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_BAD);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code ==
          (K_FIELD == 1 || K_FIELD == 2 ? ErrorCode::INVALID_DATA : ErrorCode::RESOURCE_EXHAUSTED));
}

TEST_CASE("component evidence total file-entry budget includes every inventory and source input",
          "[asset-component-evidence]") {
    EvidenceFixture fixture;
    for (std::size_t index { 0uz }; index < 2uz; ++index) {
        auto model = fixture.m_manifests[1uz];
        model["role"] = "MODEL";
        fixture.m_manifests.push_back(model);
    }
    for (std::size_t index { 0uz }; index < fixture.m_manifests.size(); ++index) {
        auto& manifest = fixture.m_manifests[index];
        const auto K_FILE = manifest["files"][0uz];
        manifest["files"] = Json::array();
        const auto K_COUNT = index == 3uz ? 1023uz : 1024uz;
        for (std::size_t file_index { 0uz }; file_index < K_COUNT; ++file_index) {
            auto file = K_FILE;
            file["path"] = "f" + std::to_string(file_index);
            manifest["files"].push_back(file);
        }
        manifest["installed_bytes"] = K_FILE["bytes"].get<std::uint64_t>() * K_COUNT;
    }
    const auto K_EXACT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_EXACT);
    REQUIRE(K_RESULT);
    std::size_t total {};
    for (const auto& component_value : K_RESULT->m_components) {
        total += component_value.m_file_count;
    }
    CHECK(total == 4096uz);
    auto& last = fixture.m_manifests.back();
    auto file = last["files"][0uz];
    file["path"] = "excess";
    last["files"].push_back(file);
    last["installed_bytes"] = file["bytes"].get<std::uint64_t>() * 1024uz;
    const auto K_BAD = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_BAD);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("component evidence verifies immutable inventories and independently selected reviews",
          "[asset-component-evidence]") {
    EvidenceFixture fixture;
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_BEFORE_RECIPE = tree(fixture.m_recipe.m_root);
    const auto K_BEFORE_EVIDENCE = tree(fixture.m_evidence.m_root);
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_components.size() == 3uz);
    CHECK(K_RESULT->m_evidence_receipt_sha256 == K_RECEIPTS.second.receipt_sha256());
    for (const auto& item : K_RESULT->m_components) {
        CHECK(item.m_review_selected);
        CHECK(item.m_file_count == 1uz);
    }
    CHECK(!blocked(*K_RESULT, AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::CONTRACTS_ONLY_HOST));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    CHECK(!blocked(*K_RESULT, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
    CHECK(tree(fixture.m_recipe.m_root) == K_BEFORE_RECIPE);
    CHECK(tree(fixture.m_evidence.m_root) == K_BEFORE_EVIDENCE);
    std::filesystem::remove_all(fixture.m_evidence.m_root / "blobs");
    const auto K_REOPENED = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_REOPENED);
}

TEST_CASE("component reviews cannot select themselves or replace independent reviewer identities",
          "[asset-component-evidence]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    EvidenceFixture fixture;
    const auto K_RECEIPTS = fixture.freeze();
    if (K_FIELD == 0) {
        fixture.m_selected.clear();
    }
    if (K_FIELD == 1) {
        fixture.m_selected[0uz].m_review_sha256 = std::string(64uz, '0');
    }
    if (K_FIELD == 2) {
        fixture.m_selected[0uz].m_reviewer = "different.reviewer";
    }
    if (K_FIELD == 3) {
        fixture.m_selected.push_back(fixture.m_selected[0uz]);
    }
    if (K_FIELD == 4) {
        fixture.m_selected[0uz].m_component_sha256 = std::string(64uz, '0');
    }
    if (K_FIELD == 5) {
        fixture.m_selected[0uz].m_reviewer = "invalid/reviewer";
    }
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    if (K_FIELD == 0) {
        REQUIRE(K_RESULT);
        CHECK(blocked(*K_RESULT, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
        CHECK(std::ranges::none_of(K_RESULT->m_components,
                                   &AssetComponentEvidence::m_review_selected));
    } else {
        REQUIRE(!K_RESULT);
    }
}

TEST_CASE("rehashed component records cannot bypass payload costs roles terms and strict schemas",
          "[asset-component-evidence]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(K_FIELD);
    EvidenceFixture fixture;
    auto& manifest = fixture.m_manifests[0uz];
    if (K_FIELD == 0) {
        manifest["schema_version"] = 2;
    }
    if (K_FIELD == 1) {
        manifest["role"] = "MODEL";
    }
    if (K_FIELD == 2) {
        manifest["extra"] = true;
    }
    if (K_FIELD == 3) {
        manifest["files"][0uz]["sha256"] = std::string(64uz, '0');
    }
    if (K_FIELD == 4) {
        manifest["files"][0uz]["bytes"] = 1;
    }
    if (K_FIELD == 5) {
        manifest["installed_bytes"] = 1;
    }
    if (K_FIELD == 6) {
        manifest["archive"]["bytes"] = 1;
    }
    if (K_FIELD == 7) {
        manifest["files"] = Json::array();
    }
    if (K_FIELD == 8) {
        fixture.m_change_review = [](Json& value) {
            value["terms_sha256"] = std::string(64uz, '0');
        };
    }
    if (K_FIELD == 9) {
        fixture.m_change_review = [](Json& value) {
            value["component_sha256"] = std::string(64uz, '0');
        };
    }
    if (K_FIELD == 10) {
        fixture.m_change_review = [](Json& value) {
            value["local_use"] = "RESTRICTED";
        };
    }
    if (K_FIELD == 11) {
        fixture.m_change_review = [](Json& value) {
            value["schema_version"] = 2;
        };
    }
    if (K_FIELD == 12) {
        fixture.m_change_review = [](Json& value) {
            value["extra"] = 0;
        };
    }
    if (K_FIELD == 13) {
        fixture.m_change_recipe = [](Json& value) {
            value["components"][0uz]["archive_bytes"] = nullptr;
        };
    }
    if (K_FIELD == 14) {
        fixture.m_change_recipe = [](Json& value) {
            value["components"][0uz]["installed_bytes"] = nullptr;
        };
    }
    if (K_FIELD == 15) {
        fixture.m_change_recipe = [](Json& value) {
            value["components"][0uz]["rights"]["review_sha256"] = nullptr;
            value["components"][0uz]["rights"]["local_use"] = "UNREVIEWED";
        };
    }
    if (K_FIELD == 16) {
        manifest["files"][0uz]["bytes"] = true;
    }
    if (K_FIELD == 17) {
        fixture.m_change_recipe = [](Json& value) {
            value["components"][2uz]["installed_bytes"] = 1;
        };
    }
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_evidence.m_root);
    const auto K_REJECTED = fixture.inspect(K_RECEIPTS);
    CHECK(!K_REJECTED);
    CHECK(tree(fixture.m_evidence.m_root) == K_BEFORE);
}

TEST_CASE("component inventory paths reject traversal aliases parents and device names",
          "[asset-component-evidence]") {
    const auto K_PATH = GENERATE("../outside",
                                 "/absolute",
                                 "C:/absolute",
                                 "a\\b",
                                 "a//b",
                                 "a/./b",
                                 "a/../b",
                                 "a/",
                                 "con.txt",
                                 "a/NUL",
                                 "COM1.bin",
                                 "lpt9",
                                 "a.");
    EvidenceFixture fixture;
    fixture.m_manifests[0uz]["files"][0uz]["path"] = K_PATH;
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_RECEIPTS);
    CHECK(!K_REJECTED);
}

TEST_CASE("component inventory rejects duplicate case aliases and file-directory collisions",
          "[asset-component-evidence]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    EvidenceFixture fixture;
    auto& manifest = fixture.m_manifests[0uz];
    auto file = manifest["files"][0uz];
    manifest["files"][0uz]["path"] = "a";
    file["path"] = K_FIELD == 0 ? "a" : K_FIELD == 1 ? "A" : "A/child";
    manifest["files"].push_back(file);
    manifest["installed_bytes"] = file["bytes"].get<std::uint64_t>() * 2uz;
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_RECEIPTS);
    CHECK(!K_REJECTED);
}

TEST_CASE("component evidence rejects missing config prompt terms archive file and malformed "
          "review bytes",
          "[asset-component-evidence]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    EvidenceFixture fixture;
    if (K_FIELD == 0) {
        fixture.m_omitted = hash(fixture.m_configuration);
    }
    if (K_FIELD == 1) {
        fixture.m_omitted = hash(fixture.m_prompt);
    }
    if (K_FIELD == 2) {
        fixture.m_omitted = hash(fixture.m_terms);
    }
    if (K_FIELD == 3) {
        fixture.m_omitted = fixture.m_manifests[0uz]["archive"]["sha256"].get<std::string>();
    }
    if (K_FIELD == 4) {
        fixture.m_omitted = fixture.m_manifests[0uz]["files"][0uz]["sha256"].get<std::string>();
    }
    if (K_FIELD == 5) {
        fixture.m_raw_review = "{\"schema_version\":1,\"schema_version\":1}";
    }
    if (K_FIELD == 6) {
        fixture.m_omit_review = true;
    }
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_RECEIPTS);
    CHECK(!K_REJECTED);
}

TEST_CASE("component evidence keeps unreviewed restricted and distribution decisions separate",
          "[asset-component-evidence]") {
    const auto K_RESTRICTED = GENERATE(false, true);
    EvidenceFixture fixture;
    fixture.m_local_use = K_RESTRICTED ? "RESTRICTED" : "UNREVIEWED";
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_RESULT);
    CHECK(blocked(*K_RESULT,
                  K_RESTRICTED ? AssetRecipeBlocker::RESTRICTED_LOCAL_RIGHTS
                               : AssetRecipeBlocker::UNREVIEWED_LOCAL_RIGHTS));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED));
}

TEST_CASE("component inventory admits the exact file count and rejects excess before loading",
          "[asset-component-evidence]") {
    EvidenceFixture fixture;
    auto& manifest = fixture.m_manifests[0uz];
    const auto K_FILE = manifest["files"][0uz];
    manifest["files"] = Json::array();
    for (std::size_t index { 0uz }; index < 1024uz; ++index) {
        auto file = K_FILE;
        file["path"] = "f" + std::to_string(index);
        manifest["files"].push_back(file);
    }
    manifest["installed_bytes"] = K_FILE["bytes"].get<std::uint64_t>() * 1024uz;
    const auto K_EXACT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_EXACT);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_components[0uz].m_file_count == 1024uz);
    auto file = K_FILE;
    file["path"] = "excess";
    manifest["files"].push_back(file);
    manifest["installed_bytes"] = K_FILE["bytes"].get<std::uint64_t>() * 1025uz;
    const auto K_EXCESS = fixture.freeze();
    const auto K_REJECTED = fixture.inspect(K_EXCESS);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("component evidence cancellation and stale contexts preserve admitted receipts",
          "[asset-component-evidence]") {
    EvidenceFixture fixture;
    const auto K_RECEIPTS = fixture.freeze();
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = inspect_asset_recipe_evidence(K_RECEIPTS.first,
                                                           { 1000 },
                                                           K_RECEIPTS.second,
                                                           fixture.m_recipe.m_context,
                                                           fixture.m_recipe.m_host,
                                                           fixture.m_selected,
                                                           stop.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    auto changed = fixture.m_recipe.m_context;
    ++changed.m_world_epoch.m_value;
    const auto K_STALE = inspect_asset_recipe_evidence(K_RECEIPTS.first,
                                                       { 1000 },
                                                       K_RECEIPTS.second,
                                                       changed,
                                                       fixture.m_recipe.m_host,
                                                       fixture.m_selected);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_REOPENED = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_REOPENED);
}
