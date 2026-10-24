#pragma once

#include "asset_recipe_fixture.hpp"
#include "world_explorer/asset_catalog/component_evidence.hpp"

#include <algorithm>

namespace world_explorer::asset_component_test {

using namespace asset_recipe_test;

struct ComponentCosts {
    std::uint64_t m_archive_bytes {};
    std::uint64_t m_installed_bytes {};
};

struct EvidenceFixture {
    RecipeFixture m_recipe;
    RecipeFixture m_evidence;
    std::map<std::string, std::string> m_payloads;
    std::vector<Json> m_manifests;
    std::vector<AssetReviewSelection> m_selected;
    std::string m_terms { "Synthetic terms; no actual permission grant." };
    std::string m_configuration { "{\"schema_version\":1,\"synthetic\":true}" };
    std::string m_prompt { "{\"prompt\":null}" };
    std::string m_local_use { "DECLARED_PERMITTED" };
    std::string m_distribution { "UNREVIEWED" };
    std::optional<std::string> m_raw_manifest;
    std::optional<std::string> m_raw_review;
    std::optional<std::string> m_omitted;
    bool m_omit_review {};
    std::function<void(Json&)> m_change_recipe;
    std::function<void(Json&)> m_change_review;

    std::string payload(const std::string& bytes) {
        const auto K_SHA = hash(bytes);
        m_payloads[K_SHA] = bytes;
        return K_SHA;
    }

    EvidenceFixture() {
        m_recipe.m_host.m_free_disk_bytes = 1000000;
        const auto K_CODE = payload("synthetic code bytes");
        const auto K_ENV = payload("synthetic environment bytes");
        const std::string K_ARCHIVE { "synthetic opaque archive; never unpacked" };
        const auto K_ARCHIVE_SHA = payload(K_ARCHIVE);
        m_manifests = { { { "schema_version", 1 },
                        { "role", "CODE" },
                        { "archive", { { "sha256", K_ARCHIVE_SHA }, { "bytes", K_ARCHIVE.size() } } },
                        { "installed_bytes", m_payloads.at(K_CODE).size() },
                        { "files",
                         Json::array({ { { "path", "src/code.txt" },
                                       { "sha256", K_CODE },
                                       { "bytes", m_payloads.at(K_CODE).size() } } }) } },
                       { { "schema_version", 1 },
                        { "role", "ENVIRONMENT" },
                        { "archive", nullptr },
                        { "installed_bytes", m_payloads.at(K_ENV).size() },
                        { "files",
                         Json::array({ { { "path", "lib/environment.bin" },
                                       { "sha256", K_ENV },
                                       { "bytes", m_payloads.at(K_ENV).size() } } }) } } };
    }

    std::pair<AdmittedAssetReceipt, AdmittedAssetReceipt> freeze() {
        auto data = m_payloads;
        data[hash(m_terms)] = m_terms;
        data[hash(m_configuration)] = m_configuration;
        data[hash(m_prompt)] = m_prompt;
        auto& value = m_recipe.m_recipe;
        value["configuration_sha256"] = hash(m_configuration);
        value["prompt_sha256"] = hash(m_prompt);
        value["components"] = Json::array();
        m_selected.clear();
        const auto K_COMPONENT = [&](const std::string& name,
                                     AssetRecipeRole component_role,
                                     const std::string& sha,
                                     const ComponentCosts& costs,
                                     bool first) {
            Json review { { "schema_version", 1 },
                         { "role", name },
                         { "component_sha256", sha },
                         { "terms_sha256", hash(m_terms) },
                         { "reviewer", "synthetic.test" },
                         { "local_use", m_local_use },
                         { "distribution", m_distribution } };
            if (first && m_change_review) {
                m_change_review(review);
            }
            const auto K_RAW = m_raw_review;
            const auto K_TEXT = first && K_RAW.has_value() ? K_RAW.value() : review.dump();
            data[hash(K_TEXT)] = K_TEXT;
            auto item = component(name, sha);
            item["archive_bytes"] = costs.m_archive_bytes;
            item["installed_bytes"] = costs.m_installed_bytes;
            item["rights"] = { { "terms_sha256", hash(m_terms) },
                              { "review_sha256", hash(K_TEXT) },
                              { "local_use", m_local_use },
                              { "distribution", m_distribution } };
            value["components"].push_back(item);
            m_selected.push_back({ component_role, sha, hash(K_TEXT), "synthetic.test" });
        };
        for (std::size_t index { 0uz }; index < m_manifests.size(); ++index) {
            const auto& manifest = m_manifests[index];
            const auto K_RAW = m_raw_manifest;
            const auto K_TEXT = index == 0uz && K_RAW.has_value() ? K_RAW.value() : manifest.dump();
            const auto K_SHA = hash(K_TEXT);
            data[K_SHA] = K_TEXT;
            K_COMPONENT(
                index == 0uz   ? "CODE"
                : index == 1uz ? "ENVIRONMENT"
                               : "MODEL",
                index == 0uz   ? AssetRecipeRole::CODE
                : index == 1uz ? AssetRecipeRole::ENVIRONMENT
                               : AssetRecipeRole::MODEL,
                K_SHA,
                ComponentCosts { .m_archive_bytes =
                                    manifest.at("archive").is_null()
                                        ? 0
                                        : manifest.at("archive").at("bytes").get<std::uint64_t>(),
                                .m_installed_bytes =
                                    manifest.at("installed_bytes").get<std::uint64_t>() },
                index == 0uz);
        }
        K_COMPONENT("INPUT",
                    AssetRecipeRole::INPUT,
                    hash(m_recipe.m_sources[0uz]),
                    ComponentCosts { .m_archive_bytes = 0,
                                    .m_installed_bytes = m_recipe.m_sources[0uz].size() },
                    false);
        if (m_change_recipe) {
            m_change_recipe(value);
        }
        const auto K_OMITTED = m_omitted;
        if (K_OMITTED.has_value()) {
            data.erase(K_OMITTED.value());
        }
        if (m_omit_review) {
            data.erase(m_selected[0uz].m_review_sha256);
        }
        m_evidence.m_sources.clear();
        for (const auto& [K_SHA, K_BYTES] : data) {
            static_cast<void>(K_SHA);
            m_evidence.m_sources.push_back(K_BYTES);
        }
        m_evidence.inputs();
        return { m_recipe.save(), m_evidence.save() };
    }

    Result<AssetRecipeEvidenceInspection>
    inspect(const std::pair<AdmittedAssetReceipt, AdmittedAssetReceipt>& receipts) const {
        return inspect_asset_recipe_evidence(receipts.first,
                                             { 1000 },
                                             receipts.second,
                                             m_recipe.m_context,
                                             m_recipe.m_host,
                                             m_selected);
    }
};

inline bool blocked(const AssetRecipeEvidenceInspection& result, AssetRecipeBlocker blocker) {
    return std::ranges::find(result.m_recipe.m_blockers, blocker) !=
           result.m_recipe.m_blockers.end();
}

inline std::map<std::string, std::string> tree(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            result.emplace(entry.path().lexically_relative(root).generic_string(),
                           sha256_file(entry.path(), 16uz * 1024uz * 1024uz).value());
        }
    }
    return result;
}

}  // namespace world_explorer::asset_component_test
