#include "world_explorer/asset_catalog/recipe.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#include <map>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

std::string hash(const std::string& text) {
    return sha256(std::as_bytes(std::span(text)));
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    REQUIRE(output);
}

Json component(const std::string& role, const std::string& sha) {
    return { { "role", role },
            { "sha256", sha },
            { "archive_bytes", 10 },
            { "installed_bytes", 20 },
            { "rights",
             { { "terms_sha256", std::string(64uz, 'd') },
              { "review_sha256", std::string(64uz, 'e') },
              { "local_use", "DECLARED_PERMITTED" },
              { "distribution", "UNREVIEWED" } } } };
}

Json recipe(const std::string& input) {
    return { { "schema_version", 1 },
            { "stage", "synthetic_import" },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "inputs", Json::array() },
            { "configuration_sha256", std::string(64uz, 'f') },
            { "prompt_sha256", std::string(64uz, '1') },
            { "seed", 0 },
            { "components",
             Json::array({ component("CODE", std::string(64uz, 'a')),
                          component("ENVIRONMENT", std::string(64uz, 'b')),
                          component("INPUT", input) }) },
            { "budget",
             { { "device", "CPU" },
              { "host_ram_bytes", 100 },
              { "device_vram_bytes", 0 },
              { "scratch_bytes", 1000 },
              { "wall_time_ms", 1000 },
              { "cpu_threads", 2 },
              { "queued_jobs", 1 },
              { "batch_size", 1 },
              { "candidate_limit", 10 },
              { "width", 96 },
              { "height", 64 } } } };
}

struct RecipeFixture {
    std::filesystem::path m_root;
    AssetDependencyContext m_context { { 3 }, { 5 } };
    AssetRecipeHost m_host { true, 100, 0, 1090, 2, 1 };
    std::vector<std::string> m_sources { "synthetic source" };
    Json m_recipe = recipe(hash(m_sources[0uz]));
    std::string m_selected_sha;

    RecipeFixture() {
        static std::atomic<std::size_t> g_sequence {};
        for (std::size_t attempt { 0uz }; attempt < 1024uz; ++attempt) {
            const auto K_ROOT = std::filesystem::temp_directory_path() /
                                ("we_recipe_" + std::to_string(g_sequence.fetch_add(1uz)));
            if (std::filesystem::create_directory(K_ROOT)) {
                m_root = std::filesystem::canonical(K_ROOT);
                break;
            }
        }
        REQUIRE(!m_root.empty());
        std::filesystem::create_directory(m_root / "blobs");
        inputs();
    }

    RecipeFixture(const RecipeFixture&) = delete;
    RecipeFixture& operator=(const RecipeFixture&) = delete;

    ~RecipeFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    void inputs() {
        auto& components = m_recipe["components"];
        components = Json::array({ components[0uz], components[1uz] });
        m_recipe["inputs"] = Json::array();
        for (std::size_t index { 0uz }; index < m_sources.size(); ++index) {
            const auto K_SHA = hash(m_sources[index]);
            m_recipe["inputs"].push_back(
                { { "product_id", index + 1uz }, { "revision", 1 }, { "content_sha256", K_SHA } });
            components.push_back(component("INPUT", K_SHA));
        }
    }

    AdmittedAssetReceipt save(const std::optional<std::string>& raw = {}) {
        std::map<std::string, std::string> blobs;
        AssetDependencyGraph graph;
        graph.m_context = m_context;
        auto products = Json::array();
        std::vector<AssetRevisionRef> parents;
        for (std::size_t index { 0uz }; index < m_sources.size(); ++index) {
            const auto K_SHA = hash(m_sources[index]);
            const auto K_ROOT_RECIPE = recipe(K_SHA).dump();
            blobs[K_SHA] = m_sources[index];
            blobs[hash(K_ROOT_RECIPE)] = K_ROOT_RECIPE;
            const AssetRevisionRef K_REF { { index + 1uz }, 1, K_SHA };
            parents.push_back(K_REF);
            graph.m_products.push_back({ K_REF, {}, hash(K_ROOT_RECIPE), {}, false });
        }
        const auto K_TEXT = raw.value_or(m_recipe.dump());
        m_selected_sha = hash(K_TEXT);
        blobs[m_selected_sha] = K_TEXT;
        blobs[hash("synthetic result")] = "synthetic result";
        graph.m_products.push_back(
            { { { 1000 }, 1, hash("synthetic result") }, parents, m_selected_sha, {}, false });
        const auto K_REF_JSON = [](const AssetRevisionRef& ref) {
            return Json { { "product_id", ref.m_product.m_value },
                         { "revision", ref.m_revision },
                         { "content_sha256", ref.m_content_sha256 } };
        };
        for (const auto& node : graph.m_products) {
            auto dependencies = Json::array();
            for (const auto& parent : node.m_dependencies) {
                dependencies.push_back(K_REF_JSON(parent));
            }
            products.push_back({ { "product", K_REF_JSON(node.m_product) },
                                { "dependencies", dependencies },
                                { "recipe_sha256", node.m_recipe_sha256 },
                                { "manual_action_sha256", nullptr },
                                { "manual_ancestor", false } });
        }
        auto files = Json::array();
        for (const auto& [K_SHA, K_BYTES] : blobs) {
            write(m_root / "blobs" / (K_SHA + ".bin"), K_BYTES);
            files.push_back({ { "file", "blobs/" + K_SHA + ".bin" },
                             { "sha256", K_SHA },
                             { "bytes", K_BYTES.size() } });
        }
        const auto K_GRAPH = validate_asset_dependencies(graph, m_context);
        REQUIRE(K_GRAPH);
        const Json K_DOCUMENT { { "schema_version", 1 },
                               { "operation_id", 1 },
                               { "graph_sha256", K_GRAPH->m_graph_sha256 },
                               { "graph",
                                { { "schema_version", 1 },
                                 { "world_epoch", 3 },
                                 { "pose_revision", 5 },
                                 { "products", products } } },
                               { "files", files } };
        const auto K_DOCUMENT_TEXT = K_DOCUMENT.dump();
        write(m_root / "receipt.json", K_DOCUMENT_TEXT);
        const auto K_ADMITTED = read_asset_receipt({ m_root, hash(K_DOCUMENT_TEXT) }, m_context);
        REQUIRE(K_ADMITTED);
        return *K_ADMITTED;
    }
};

bool blocked(const AssetRecipeInspection& result, AssetRecipeBlocker blocker) {
    return std::ranges::find(result.m_blockers, blocker) != result.m_blockers.end();
}

}  // namespace

TEST_CASE("asset recipe inspection binds owned source bytes dependencies context and frozen limits",
          "[asset-recipe]") {
    RecipeFixture fixture;
    const auto K_RECEIPT = fixture.save();
    const auto K_SELECTED = fixture.m_selected_sha;
    // All later inspection must use the admitted snapshot, with no filesystem reads.
    write(fixture.m_root / "blobs" / (K_SELECTED + ".bin"), "changed source recipe");
    const auto K_RESULT =
        inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_recipe_sha256 == K_SELECTED);
    CHECK(K_RESULT->m_receipt_sha256 == K_RECEIPT.receipt_sha256());
    CHECK(K_RESULT->m_installation_bytes == 90);
    CHECK(K_RESULT->m_reserved_disk_bytes == 1090);
    CHECK(K_RESULT->m_components.size() == 3uz);
    CHECK(K_RESULT->m_seed == 0);
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::CONTRACTS_ONLY_HOST));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    CHECK(!(blocked(*K_RESULT, AssetRecipeBlocker::HOST_CAPACITY_EXCEEDED)));
    REQUIRE(inspect_asset_recipe(K_RECEIPT, { 1 }, fixture.m_context, fixture.m_host));
    auto changed = fixture.m_context;
    ++changed.m_pose_revision.m_value;
    const auto K_STALE = inspect_asset_recipe(K_RECEIPT, { 1000 }, changed, fixture.m_host);
    REQUIRE(!(K_STALE));
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_UNKNOWN =
        inspect_asset_recipe(K_RECEIPT, { 999 }, fixture.m_context, fixture.m_host);
    REQUIRE(!(K_UNKNOWN));
    CHECK(K_UNKNOWN.error().m_code == ErrorCode::NOT_FOUND);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = inspect_asset_recipe(
        K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host, stop.get_token());
    REQUIRE(!(K_CANCELLED));
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("rehashing recipes cannot bypass exact schema identities budgets and selected inputs",
          "[asset-recipe]") {
    const auto K_FIELD = GENERATE(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
    CAPTURE(K_FIELD);
    RecipeFixture fixture;
    auto& value = fixture.m_recipe;
    if (K_FIELD == 0) {
        value["schema_version"] = 2;
    }
    if (K_FIELD == 1) {
        value["extra"] = true;
    }
    if (K_FIELD == 2) {
        value["seed"] = -1;
    }
    if (K_FIELD == 3) {
        value["seed"] = true;
    }
    if (K_FIELD == 4) {
        value["budget"]["width"] = 96.0;
    }
    if (K_FIELD == 5) {
        value["configuration_sha256"] = "invalid";
    }
    if (K_FIELD == 6) {
        value["components"][0uz]["role"] = "EXECUTABLE";
    }
    if (K_FIELD == 7) {
        value["components"].push_back(value["components"][0uz]);
    }
    if (K_FIELD == 8) {
        value["components"].erase(0uz);
    }
    if (K_FIELD == 9) {
        value["world_epoch"] = 4;
    }
    if (K_FIELD == 10) {
        value["pose_revision"] = 6;
    }
    if (K_FIELD == 11) {
        value["inputs"][0uz]["revision"] = 2;
    }
    if (K_FIELD == 12) {
        value["inputs"] = Json::array();
    }
    if (K_FIELD == 13) {
        value["components"][2uz]["sha256"] = std::string(64uz, 'c');
    }
    if (K_FIELD == 14) {
        value["budget"]["device_vram_bytes"] = 1;
    }
    if (K_FIELD == 15) {
        value["budget"]["device"] = "CUDA";
    }
    if (K_FIELD == 16) {
        value["budget"]["device"] = "REMOTE";
    }
    if (K_FIELD == 17) {
        value["prompt_sha256"] = std::string(64uz, 'A');
    }
    if (K_FIELD == 18) {
        value["stage"] = "path/to/script";
    }
    if (K_FIELD == 19) {
        value["budget"]["queued_jobs"] = 65;
    }
    if (K_FIELD == 20) {
        value["components"][0uz]["installed_bytes"] = 0;
    }
    if (K_FIELD == 21) {
        value["components"][0uz]["rights"]["review_sha256"] = nullptr;
    }
    if (K_FIELD == 22) {
        value["components"][0uz]["rights"]["local_use"] = "APPROVED";
    }
    if (K_FIELD == 23) {
        value["inputs"].push_back(value["inputs"][0uz]);
    }
    const auto K_RECEIPT = fixture.save();
    const auto K_BEFORE = sha256_file(fixture.m_root / "receipt.json", 1024uz * 1024uz).value();
    const auto K_REJECTED =
        inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(!(K_REJECTED));
    CHECK(sha256_file(fixture.m_root / "receipt.json", 1024uz * 1024uz).value() == K_BEFORE);
}

TEST_CASE("asset recipe JSON rejects duplicate NUL deep and oversized records at exact byte limits",
          "[asset-recipe]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    RecipeFixture fixture;
    auto text = fixture.m_recipe.dump();
    text.resize(64uz * 1024uz, ' ');
    const auto K_EXACT = fixture.save(text);
    REQUIRE(inspect_asset_recipe(K_EXACT, { 1000 }, fixture.m_context, fixture.m_host));
    if (K_FIELD == 0) {
        text += ' ';
    }
    if (K_FIELD == 1) {
        text.insert(1uz, "\"schema_version\":1,");
        text.resize(64uz * 1024uz);
    }
    if (K_FIELD == 2) {
        text[0uz] = '\0';
    }
    if (K_FIELD == 3) {
        text = "{\"deep\":" + std::string(20uz, '[') + "0" + std::string(20uz, ']') + "}";
    }
    if (K_FIELD == 4) {
        text = "{";
    }
    const auto K_RECEIPT = fixture.save(text);
    CHECK(!(inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host)));
}

TEST_CASE("asset recipe unknown costs and local restrictions remain explicit provisioning blockers",
          "[asset-recipe]") {
    RecipeFixture fixture;
    auto& item = fixture.m_recipe["components"][0uz];
    item["archive_bytes"] = nullptr;
    item["rights"]["local_use"] = "UNREVIEWED";
    item["rights"]["review_sha256"] = nullptr;
    const auto K_RECEIPT = fixture.save();
    const auto K_RESULT =
        inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_RESULT);
    CHECK(!(K_RESULT->m_installation_bytes));
    CHECK(!(K_RESULT->m_reserved_disk_bytes));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::UNKNOWN_INSTALLATION_SIZE));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::UNREVIEWED_LOCAL_RIGHTS));
    item["rights"]["local_use"] = "RESTRICTED";
    const auto K_RESTRICTED_RECEIPT = fixture.save();
    const auto K_RESTRICTED =
        inspect_asset_recipe(K_RESTRICTED_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_RESTRICTED);
    CHECK(blocked(*K_RESTRICTED, AssetRecipeBlocker::RESTRICTED_LOCAL_RIGHTS));
    CHECK(K_RESTRICTED->m_recipe_sha256 != K_RESULT->m_recipe_sha256);
}

TEST_CASE("asset recipe no-archive imports distinguish known zero costs from unknown costs",
          "[asset-recipe]") {
    RecipeFixture fixture;
    fixture.m_recipe["components"][0uz]["archive_bytes"] = 0;
    const auto K_KNOWN = fixture.save();
    const auto K_RESULT = inspect_asset_recipe(K_KNOWN, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_installation_bytes == 80);
    CHECK(K_RESULT->m_reserved_disk_bytes == 1080);
    CHECK(!blocked(*K_RESULT, AssetRecipeBlocker::UNKNOWN_INSTALLATION_SIZE));
    fixture.m_recipe["components"][0uz]["archive_bytes"] = nullptr;
    const auto K_UNKNOWN = fixture.save();
    const auto K_UNSIZED =
        inspect_asset_recipe(K_UNKNOWN, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_UNSIZED);
    CHECK(!K_UNSIZED->m_installation_bytes);
    CHECK(blocked(*K_UNSIZED, AssetRecipeBlocker::UNKNOWN_INSTALLATION_SIZE));
}

TEST_CASE("asset recipe capacity assessment includes exact installation scratch and CUDA requests",
          "[asset-recipe]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    RecipeFixture fixture;
    fixture.m_host.m_contracts_only = false;
    fixture.m_host.m_device_vram_bytes = 24;
    fixture.m_recipe["budget"]["device"] = "CUDA";
    fixture.m_recipe["budget"]["device_vram_bytes"] = 24;
    const auto K_RECEIPT = fixture.save();
    const auto K_FIT = inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_FIT);
    CHECK(!(blocked(*K_FIT, AssetRecipeBlocker::HOST_CAPACITY_EXCEEDED)));
    CHECK(blocked(*K_FIT, AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED));
    if (K_FIELD == 0) {
        --fixture.m_host.m_host_ram_bytes;
    }
    if (K_FIELD == 1) {
        --fixture.m_host.m_device_vram_bytes;
    }
    if (K_FIELD == 2) {
        --fixture.m_host.m_free_disk_bytes;
    }
    if (K_FIELD == 3) {
        --fixture.m_host.m_cpu_threads;
    }
    if (K_FIELD == 4) {
        --fixture.m_host.m_queued_jobs;
    }
    const auto K_EXCESS =
        inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_EXCESS);
    CHECK(blocked(*K_EXCESS, AssetRecipeBlocker::HOST_CAPACITY_EXCEEDED));
}

TEST_CASE("asset recipe installation reservations reject overflow even with unknown costs",
          "[asset-recipe]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    RecipeFixture fixture;
    if (K_FIELD == 0 || K_FIELD == 1) {
        fixture.m_recipe["components"][0uz]["archive_bytes"] =
            std::numeric_limits<std::uint64_t>::max();
    }
    if (K_FIELD == 1) {
        fixture.m_recipe["components"][0uz]["installed_bytes"] = nullptr;
    }
    if (K_FIELD == 2) {
        fixture.m_recipe["budget"]["scratch_bytes"] = std::numeric_limits<std::uint64_t>::max();
    }
    const auto K_RECEIPT = fixture.save();
    const auto K_RESULT =
        inspect_asset_recipe(K_RECEIPT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(!(K_RESULT));
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("asset recipe component cardinality has exact model and total bounds", "[asset-recipe]") {
    RecipeFixture fixture;
    for (std::size_t index { 1uz }; index < 46uz; ++index) {
        fixture.m_sources.push_back("synthetic source " + std::to_string(index));
    }
    fixture.inputs();
    for (std::size_t index { 0uz }; index < 16uz; ++index) {
        fixture.m_recipe["components"].push_back(
            component("MODEL", hash("model " + std::to_string(index))));
    }
    const auto K_EXACT = fixture.save();
    const auto K_RESULT = inspect_asset_recipe(K_EXACT, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_components.size() == 64uz);
    fixture.m_sources.push_back("synthetic forty-seventh source");
    fixture.inputs();
    for (std::size_t index { 0uz }; index < 16uz; ++index) {
        fixture.m_recipe["components"].push_back(
            component("MODEL", hash("model " + std::to_string(index))));
    }
    const auto K_EXCESS = fixture.save();
    const auto K_TOO_MANY =
        inspect_asset_recipe(K_EXCESS, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(!(K_TOO_MANY));
    CHECK(K_TOO_MANY.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    fixture.m_sources.resize(1uz);
    fixture.inputs();
    for (std::size_t index { 0uz }; index < 17uz; ++index) {
        fixture.m_recipe["components"].push_back(
            component("MODEL", hash("model " + std::to_string(index))));
    }
    const auto K_MODEL_EXCESS = fixture.save();
    CHECK(!(inspect_asset_recipe(K_MODEL_EXCESS, { 1000 }, fixture.m_context, fixture.m_host)));
}

TEST_CASE("admitted asset blob copies are bounded independent owned values", "[asset-recipe]") {
    RecipeFixture fixture;
    const auto K_RECEIPT = fixture.save();
    auto bytes = K_RECEIPT.copy_blob(fixture.m_selected_sha, 64uz * 1024uz);
    REQUIRE(bytes);
    (*bytes)[0uz] = std::byte { 0 };
    const auto K_AGAIN = K_RECEIPT.copy_blob(fixture.m_selected_sha, 64uz * 1024uz);
    REQUIRE(K_AGAIN);
    CHECK(sha256(*K_AGAIN) == fixture.m_selected_sha);
    CHECK(!(K_RECEIPT.copy_blob(fixture.m_selected_sha, 1uz)));
    CHECK(!(K_RECEIPT.copy_blob(fixture.m_selected_sha, 0uz)));
    CHECK(!(K_RECEIPT.copy_blob("bad", 1uz)));
    CHECK(!(K_RECEIPT.copy_blob(std::string(64uz, '0'), 1uz)));
    CHECK(!(K_RECEIPT.copy_blob(fixture.m_selected_sha, 16uz * 1024uz * 1024uz + 1uz)));
    std::stop_source stop;
    stop.request_stop();
    CHECK(!(K_RECEIPT.copy_blob(fixture.m_selected_sha, 64uz * 1024uz, stop.get_token())));
}

TEST_CASE("asset recipe lock changes invalidate identity while retaining old admitted snapshots",
          "[asset-recipe]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    RecipeFixture fixture;
    const auto K_BEFORE = fixture.save();
    const auto K_OLD = inspect_asset_recipe(K_BEFORE, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_OLD);
    if (K_FIELD == 0) {
        fixture.m_recipe["seed"] = 1;
    }
    if (K_FIELD == 1) {
        fixture.m_recipe["configuration_sha256"] = std::string(64uz, '2');
    }
    if (K_FIELD == 2) {
        fixture.m_recipe["prompt_sha256"] = std::string(64uz, '2');
    }
    if (K_FIELD == 3) {
        fixture.m_recipe["components"][0uz]["sha256"] = std::string(64uz, '2');
    }
    if (K_FIELD == 4) {
        fixture.m_recipe["components"][1uz]["sha256"] = std::string(64uz, '2');
    }
    if (K_FIELD == 5) {
        fixture.m_recipe["components"].push_back(component("MODEL", std::string(64uz, '2')));
    }
    if (K_FIELD == 6) {
        fixture.m_recipe["components"][0uz]["rights"]["review_sha256"] = std::string(64uz, '2');
    }
    if (K_FIELD == 7) {
        fixture.m_recipe["budget"]["wall_time_ms"] = 2000;
    }
    const auto K_AFTER = fixture.save();
    const auto K_NEW = inspect_asset_recipe(K_AFTER, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_NEW);
    CHECK(K_NEW->m_recipe_sha256 != K_OLD->m_recipe_sha256);
    CHECK(K_NEW->m_graph_sha256 != K_OLD->m_graph_sha256);
    CHECK(K_NEW->m_receipt_sha256 != K_OLD->m_receipt_sha256);
    const auto K_AGAIN = inspect_asset_recipe(K_BEFORE, { 1000 }, fixture.m_context, fixture.m_host);
    REQUIRE(K_AGAIN);
    CHECK(K_AGAIN->m_recipe_sha256 == K_OLD->m_recipe_sha256);
}
