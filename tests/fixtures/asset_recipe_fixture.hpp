#pragma once

#include "world_explorer/asset_catalog/recipe.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <map>

namespace world_explorer::asset_recipe_test {

using Json = nlohmann::json;

inline std::string hash(const std::string& text) {
    return sha256(std::as_bytes(std::span(text)));
}

inline void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    REQUIRE(output);
}

inline Json component(const std::string& role, const std::string& sha) {
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

inline Json recipe(const std::string& input) {
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

}  // namespace world_explorer::asset_recipe_test
