#include "../fixtures/evaluation_corpus_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>

using namespace world_explorer;
using namespace world_explorer::asset_corpus_test;

namespace {

bool blocked(const AdmittedAssetCorpus& corpus, const std::string& code) {
    return std::ranges::any_of(corpus.blockers(), [&](const auto& blocker) {
        return blocker.m_code == code;
    });
}

auto tree(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            result.emplace(entry.path().lexically_relative(root).generic_string(),
                           checked(sha256_file(entry.path(), 64uz * 1024uz * 1024uz)));
        }
    }
    return result;
}

}  // namespace

TEST_CASE("corpus seals bind capture commits and recipe resources without approving evidence",
          "[asset-corpus]") {
    CorpusFixture fixture;
    const auto K_BEFORE = tree(fixture.m_capture_receipt.source_directory());
    const auto K_RECIPE_BEFORE = tree(fixture.m_recipe_receipt.source_directory());
    const auto K_CORPUS = fixture.admit();
    REQUIRE(K_CORPUS);
    CHECK(K_CORPUS->reference().m_sha256 == hash(K_CORPUS->canonical_json()));
    CHECK(blocked(*K_CORPUS, "SYNTHETIC_INPUTS"));
    CHECK(blocked(*K_CORPUS, "INDEPENDENT_SCALE_MISSING"));
    CHECK(blocked(*K_CORPUS, "EMPTY_BACKGROUND_MISSING"));
    CHECK(blocked(*K_CORPUS, "CHANGED_LIGHT_MISSING"));
    CHECK(blocked(*K_CORPUS, "SOURCE_RIGHTS_UNVERIFIED"));
    CHECK(blocked(*K_CORPUS, "CONTRACTS_ONLY_HOST"));
    CHECK(blocked(*K_CORPUS, "DISTRIBUTION_NOT_CLEARED"));
    CHECK(blocked(*K_CORPUS, "RUNTIME_CLOSURE_UNVERIFIED"));
    CHECK(K_CORPUS->recipes()[0uz].m_budget.m_candidate_limit == 10);
    const auto K_COMMIT = publish_asset_corpus(*K_CORPUS, fixture.seal(), { 11 });
    REQUIRE(K_COMMIT);
    CHECK(K_COMMIT->m_generation == 1);
    const auto K_SEALED = tree(fixture.seal());
    const auto K_REPLAY = publish_asset_corpus(*K_CORPUS, fixture.seal(), { 11 });
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_manifest_sha256 == K_COMMIT->m_manifest_sha256);
    CHECK(tree(fixture.seal()) == K_SEALED);
    const auto K_READ = read_asset_corpus(
        fixture.seal(), *K_COMMIT, K_CORPUS->reference(), fixture.captures(), fixture.recipes());
    REQUIRE(K_READ);
    CHECK(K_READ->canonical_json() == K_CORPUS->canonical_json());
    CHECK(K_READ->blockers() == K_CORPUS->blockers());
    CHECK(tree(fixture.m_capture_receipt.source_directory()) == K_BEFORE);
    CHECK(tree(fixture.m_recipe_receipt.source_directory()) == K_RECIPE_BEFORE);
}

TEST_CASE("present opaque corpus evidence stays unverified and scale must match selection",
          "[asset-corpus]") {
    CorpusFixture fixture(true);
    fixture.m_definition.m_track = AssetCorpusTrack::PILOT;
    const auto K_CORPUS = fixture.admit();
    REQUIRE(K_CORPUS);
    CHECK(!blocked(*K_CORPUS, "SYNTHETIC_INPUTS"));
    CHECK(!blocked(*K_CORPUS, "INDEPENDENT_SCALE_MISSING"));
    CHECK(blocked(*K_CORPUS, "INDEPENDENT_SCALE_UNVERIFIED"));
    CHECK(blocked(*K_CORPUS, "EMPTY_BACKGROUND_UNVERIFIED"));
    CHECK(blocked(*K_CORPUS, "CHANGED_LIGHT_UNVERIFIED"));
    CHECK(blocked(*K_CORPUS, "REAL_CAPTURE_AUTHENTICITY_UNVERIFIED"));
    CHECK(fixture.decode(K_CORPUS->canonical_json()));
    fixture.m_definition.m_captures[0uz].m_evidence.erase(AssetCorpusEvidence::INDEPENDENT_SCALE);
    CHECK(!fixture.admit());
}

TEST_CASE("corpus admission refuses substituted independent pins and bounded tables",
          "[asset-corpus]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CorpusFixture fixture;
    auto& capture = fixture.m_definition.m_captures[0uz];
    auto& recipe = fixture.m_definition.m_recipes[0uz];
    switch (K_CASE) {
    case 0:
        capture.m_product.m_revision++;
        break;
    case 1:
        capture.m_selection.m_value++;
        break;
    case 2:
        capture.m_revision++;
        break;
    case 3:
        capture.m_receipt_sha256 = std::string(64uz, 'a');
        break;
    case 4:
        capture.m_commit.m_generation++;
        break;
    case 5:
        capture.m_commit.m_manifest_sha256 = std::string(64uz, 'a');
        break;
    case 6:
        recipe.m_product.m_revision++;
        break;
    case 7:
        recipe.m_recipe_sha256 = std::string(64uz, 'a');
        break;
    case 8:
        recipe.m_receipt_sha256 = std::string(64uz, 'a');
        break;
    case 9:
        recipe.m_context.m_pose_revision.m_value++;
        break;
    case 10:
        capture.m_label = std::string(65uz, 'a');
        break;
    case 11:
        fixture.m_definition.m_corpus = "../escape";
        break;
    case 12:
        fixture.m_definition.m_captures.resize(17uz, capture);
        break;
    case 13:
        fixture.m_definition.m_recipes.resize(17uz, recipe);
        break;
    case 14:
        capture.m_evidence[AssetCorpusEvidence::INDEPENDENT_SCALE] = capture.m_product;
        break;
    case 15:
        capture.m_evidence[AssetCorpusEvidence::EMPTY_BACKGROUND] = { { 9999 }, 1, hash("missing") };
        break;
    case 16:
        fixture.m_definition.m_track = static_cast<AssetCorpusTrack>(255);
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    CHECK(!fixture.admit());
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
    CHECK(!std::filesystem::exists(fixture.seal()));
}

TEST_CASE("corpus strict decoding rejects rehashed malformed and noncanonical documents",
          "[asset-corpus]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CorpusFixture fixture;
    const auto K_CORPUS = checked(fixture.admit());
    auto doc = Json::parse(K_CORPUS.canonical_json());
    auto raw = K_CORPUS.canonical_json();
    switch (K_CASE) {
    case 0:
        doc["extra"] = true;
        break;
    case 1:
        doc["schema_version"] = 2;
        break;
    case 2:
        doc["revision"] = 1.0;
        break;
    case 3:
        doc["recipes"][0uz]["host"]["contracts_only"] = 1;
        break;
    case 4:
        doc["captures"][0uz]["evidence"]["OTHER"] = nullptr;
        break;
    case 5:
        doc["captures"][0uz]["evidence"].erase("BACKSIDE");
        break;
    case 6:
        doc["track"] = "QUALIFIED";
        break;
    case 7:
        raw.insert(1uz, "\"schema_version\":1,");
        break;
    case 8:
        raw.push_back('\0');
        break;
    case 9:
        raw = doc.dump(2);
        break;
    case 10:
        raw.assign(256uz * 1024uz + 1uz, ' ');
        break;
    case 11:
        doc["captures"][0uz]["commit"]["generation"] = -1;
        break;
    case 12:
        doc["captures"].push_back(doc["captures"][0uz]);
        break;
    case 13:
        raw = std::string(18uz, '[') + "0" + std::string(18uz, ']');
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    if (K_CASE < 7 || K_CASE == 11 || K_CASE == 12) {
        raw = doc.dump();
    }
    CHECK(!fixture.decode(raw));
}

TEST_CASE("corpus publication rechecks capture registry and refuses changes to sealed identity",
          "[asset-corpus]") {
    CorpusFixture fixture;
    const auto K_ORIGINAL = checked(fixture.admit());
    const auto K_COMMIT = checked(publish_asset_corpus(K_ORIGINAL, fixture.seal(), { 21 }));
    const auto K_BEFORE = tree(fixture.seal());
    fixture.m_definition.m_recipes[0uz].m_host.m_host_ram_bytes++;
    const auto K_CHANGED = checked(fixture.admit());
    CHECK(K_CHANGED.reference().m_sha256 != K_ORIGINAL.reference().m_sha256);
    CHECK(!publish_asset_corpus(K_CHANGED, fixture.seal(), { 21 }));
    CHECK(!publish_asset_corpus(K_CHANGED, fixture.seal(), { 22 }));
    CHECK(tree(fixture.seal()) == K_BEFORE);
    auto wrong = K_COMMIT;
    wrong.m_manifest_sha256 = hash("wrong commit");
    CHECK(!read_asset_corpus(
        fixture.seal(), wrong, K_ORIGINAL.reference(), fixture.captures(), fixture.recipes()));
    auto selected = K_ORIGINAL.reference();
    selected.m_sha256 = hash("wrong body");
    CHECK(!read_asset_corpus(
        fixture.seal(), K_COMMIT, selected, fixture.captures(), fixture.recipes()));
    CHECK(publish_immutable_records(
        fixture.registry(), { 2 }, 1, { { "later-observation", { std::byte { 1 } } } }, {}));
    CHECK(!fixture.admit());
    CHECK(!publish_asset_corpus(K_ORIGINAL, fixture.m_output.m_root / "new-seal", { 23 }));
    CHECK(!std::filesystem::exists(fixture.m_output.m_root / "new-seal"));
    CHECK(tree(fixture.seal()) == K_BEFORE);
}

TEST_CASE("corpus cancellation and staged bytes cannot expose a seal before replay commits",
          "[asset-corpus]") {
    CorpusFixture fixture;
    const auto K_CORPUS = checked(fixture.admit());
    std::stop_source source;
    const auto K_RESULT = publish_asset_corpus(
        K_CORPUS, fixture.seal(), { 31 }, source.get_token(), [&](std::size_t count) {
            if (count == 1uz) {
                source.request_stop();
            }
        });
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    const auto K_PENDING = read_immutable_records(fixture.seal());
    REQUIRE(K_PENDING);
    CHECK(K_PENDING->m_commit.m_generation == 0);
    CHECK(K_PENDING->m_records.empty());
    CHECK(!read_asset_corpus(fixture.seal(),
                             { 1, hash("uncommitted") },
                             K_CORPUS.reference(),
                             fixture.captures(),
                             fixture.recipes()));
    const auto K_RECOVERED = checked(publish_asset_corpus(K_CORPUS, fixture.seal(), { 31 }));
    CHECK(read_asset_corpus(
        fixture.seal(), K_RECOVERED, K_CORPUS.reference(), fixture.captures(), fixture.recipes()));
}

TEST_CASE("corpus cancellation overlap and missing independent handles preserve caller data",
          "[asset-corpus]") {
    CorpusFixture fixture;
    const auto K_CORPUS = checked(fixture.admit());
    const auto K_BEFORE = tree(fixture.m_capture_receipt.source_directory());
    CHECK(!publish_asset_corpus(
        K_CORPUS, fixture.m_capture_receipt.source_directory() / "nested", { 41 }));
    CHECK(!publish_asset_corpus(K_CORPUS, fixture.registry() / "nested", { 42 }));
    CHECK(!admit_asset_corpus(fixture.m_definition, {}, fixture.recipes()));
    auto inputs = fixture.captures();
    inputs[0uz].m_label = "substitute";
    CHECK(!admit_asset_corpus(fixture.m_definition, inputs, fixture.recipes()));
    std::stop_source source;
    source.request_stop();
    CHECK(!fixture.admit(source.get_token()));
    CHECK(!fixture.decode(K_CORPUS.canonical_json(), source.get_token()));
    CHECK(!publish_asset_corpus(K_CORPUS, fixture.seal(), { 43 }, source.get_token()));
    CHECK(!std::filesystem::exists(fixture.seal()));
    CHECK(tree(fixture.m_capture_receipt.source_directory()) == K_BEFORE);
}

TEST_CASE("admitted corpus retains owned metadata after source and seal deletion",
          "[asset-corpus]") {
    const auto K_OWNED = [] {
        CorpusFixture fixture(true);
        const auto K_CORPUS = checked(fixture.admit());
        const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, fixture.seal(), { 51 }));
        return checked(read_asset_corpus(
            fixture.seal(), K_COMMIT, K_CORPUS.reference(), fixture.captures(), fixture.recipes()));
    }();
    CHECK(K_OWNED.definition().m_captures[0uz].m_evidence.size() == 10uz);
    CHECK(K_OWNED.reference().m_sha256 == hash(K_OWNED.canonical_json()));
    CHECK(K_OWNED.recipes()[0uz].m_components.size() == 3uz);
    CHECK(blocked(K_OWNED, "INDEPENDENT_SCALE_UNVERIFIED"));
}

TEST_CASE("corpus canonical recipe order and duplicate submission checks are deterministic",
          "[asset-corpus]") {
    CorpusFixture fixture;
    auto recipe = fixture.m_definition.m_recipes[0uz];
    recipe.m_label = "alternate";
    fixture.m_definition.m_recipes.push_back(recipe);
    auto inputs = fixture.recipes();
    inputs.push_back({ "alternate", fixture.m_recipe_receipt });
    const auto K_FIRST =
        checked(admit_asset_corpus(fixture.m_definition, fixture.captures(), inputs));
    std::ranges::reverse(fixture.m_definition.m_recipes);
    std::ranges::reverse(inputs);
    const auto K_SECOND =
        checked(admit_asset_corpus(fixture.m_definition, fixture.captures(), inputs));
    CHECK(K_FIRST.canonical_json() == K_SECOND.canonical_json());
    CHECK(K_FIRST.blockers() == K_SECOND.blockers());
    fixture.m_definition.m_recipes[0uz].m_label = fixture.m_definition.m_recipes[1uz].m_label;
    CHECK(!admit_asset_corpus(fixture.m_definition, fixture.captures(), inputs));
    fixture.m_definition.m_recipes.pop_back();
    auto capture = fixture.m_definition.m_captures[0uz];
    capture.m_label = "duplicate-source";
    fixture.m_definition.m_captures.push_back(capture);
    auto captures = fixture.captures();
    captures.push_back(
        { "duplicate-source", fixture.m_selection, fixture.m_capture_receipt, fixture.registry() });
    CHECK(!admit_asset_corpus(fixture.m_definition, captures, fixture.recipes()));
    fixture.m_definition.m_captures[1uz].m_label = "corner";
    CHECK(!admit_asset_corpus(fixture.m_definition, captures, fixture.recipes()));
}
