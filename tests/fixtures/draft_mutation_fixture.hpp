#pragma once

#include "asset_region_fixture.hpp"
#include "world_explorer/application/draft_mutations.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace world_explorer::draft_mutation_test {

using namespace asset_region_test;

struct DraftFixture {
    RegionFixture m_before, m_after;
    RecipeFixture m_workspace;

  private:
    std::optional<AdmittedAssetReceipt> m_input, m_output;
    std::optional<AdmittedManualActionJournal> m_journal;

  public:
    DraftMetadataMutation m_mutation;
    std::filesystem::path m_root { m_workspace.m_root / "draft" };

    const AdmittedAssetReceipt& input() const {
        if (!m_input) {
            throw std::logic_error("Fixture input is not initialized");
        }
        return *m_input;
    }

    const AdmittedAssetReceipt& output() const {
        if (!m_output) {
            throw std::logic_error("Fixture output is not initialized");
        }
        return *m_output;
    }

    const AdmittedManualActionJournal& journal() const {
        if (!m_journal) {
            throw std::logic_error("Fixture journal is not initialized");
        }
        return *m_journal;
    }

    explicit DraftFixture(std::size_t extra_descendants = 0uz,
                          ManualActionKind kind = ManualActionKind::EDIT_GEOMETRY) {
        m_before.m_graph.m_products[3uz].m_product.m_content_sha256 =
            hash(m_before.m_record.dump());
        AssetRevisionRef parent = m_before.m_graph.m_products[3uz].m_product;
        for (std::size_t index { 0uz }; index < extra_descendants; ++index) {
            auto node = m_before.node(index + 2000uz,
                                      "synthetic initial descendant " + std::to_string(index));
            node.m_dependencies = { parent };
            parent = node.m_product;
            m_before.m_graph.m_products.push_back(node);
        }
        m_input = m_before.freeze();
        configure(kind, 1, 0);
    }

    void configure(ManualActionKind kind, std::uint64_t action, std::uint64_t base) {
        m_mutation = { { action }, kind, { action }, base, { input().graph().m_products[2uz].m_product } };
        const ManualActionRequest K_ACTION { { action },
                                            kind,
                                            "synthetic-actor",
                                            1000,
                                            500,
                                            200,
                                            m_mutation.m_targets,
                                            "synthetic draft replacement" };
        const auto K_COMMIT = append_manual_action(K_ACTION,
                                                   input(),
                                                   m_workspace.m_root / "journal",
                                                   { action },
                                                   action - 1,
                                                   m_before.m_graph.m_context);
        REQUIRE(K_COMMIT);
        const auto K_JOURNAL =
            read_manual_action_journal(m_workspace.m_root / "journal", *K_COMMIT);
        REQUIRE(K_JOURNAL);
        m_journal = *K_JOURNAL;
        m_after.m_graph = input().graph();
        m_after.m_data = m_before.m_data;
        m_after.m_record = m_before.m_record;
        m_after.m_record["manual_ancestor"] = true;
        const auto K_ACTION_STORE = read_immutable_records(journal().source_directory());
        REQUIRE(K_ACTION_STORE);
        const auto& raw = K_ACTION_STORE->m_records.at("manual-" + std::to_string(action));
        const std::string K_ACTION_TEXT(reinterpret_cast<const char*>(raw.data()), raw.size());
        const auto K_ACTION_SHA = hash(K_ACTION_TEXT);
        m_after.m_data[K_ACTION_SHA] = K_ACTION_TEXT;
        for (auto& node : m_after.m_graph.m_products) {
            if (node.m_product.m_product.m_value < 1000) {
                continue;
            }
            const std::string K_TEXT { "synthetic revised draft metadata " +
                                      std::to_string(node.m_product.m_product.m_value) +
                                      " action " + std::to_string(action) };
            node.m_product.m_content_sha256 = hash(K_TEXT);
            m_after.m_data[hash(K_TEXT)] = K_TEXT;
            ++node.m_product.m_revision;
            node.m_manual_ancestor = true;
            if (node.m_product.m_product == AssetProductId { 1000 }) {
                node.m_manual_action_sha256 = K_ACTION_SHA;
            }
        }
        save_after();
    }

    void advance() {
        m_before.m_graph = output().graph();
        m_before.m_data = m_after.m_data;
        m_before.m_record = m_after.m_record;
        std::filesystem::remove_all(m_before.m_directory.m_root / "blobs");
        std::filesystem::create_directory(m_before.m_directory.m_root / "blobs");
        m_input = m_before.freeze();
        configure(ManualActionKind::EDIT_GEOMETRY, 2, 2);
    }

    void save_after() {
        // Fixture-owned staging only; production commands never repair sources.
        std::filesystem::remove_all(m_after.m_directory.m_root / "blobs");
        std::filesystem::create_directory(m_after.m_directory.m_root / "blobs");
        const auto& target = m_after.m_graph.m_products[2uz].m_product;
        m_after.m_record["target"] = region_ref(target);
        const auto K_TEXT = m_after.m_record.dump();
        m_after.m_data[hash(K_TEXT)] = K_TEXT;
        m_after.m_graph.m_products[3uz].m_product.m_content_sha256 = hash(K_TEXT);
        std::map<AssetProductId, AssetRevisionRef> refs;
        for (const auto& node : m_after.m_graph.m_products) {
            refs.emplace(node.m_product.m_product, node.m_product);
        }
        for (auto& node : m_after.m_graph.m_products) {
            for (auto& ref : node.m_dependencies) {
                ref = refs.at(ref.m_product);
            }
        }
        m_output = m_after.freeze();
    }

    Result<RecordCommit> publish(const std::stop_token& stop = {},
                                 const DraftMutationObserver& observed = {}) const {
        return replace_draft_metadata({ m_mutation, m_root },
                                      input(),
                                      output(),
                                      journal(),
                                      m_before.m_graph.m_context,
                                      stop,
                                      observed);
    }

    Result<DraftMutationSnapshot> read(const RecordCommit& commit,
                                       const std::stop_token& stop = {}) const {
        return read_draft_metadata_mutation(m_mutation,
                                            input(),
                                            output(),
                                            journal(),
                                            m_root,
                                            commit,
                                            m_before.m_graph.m_context,
                                            stop);
    }
};

}  // namespace world_explorer::draft_mutation_test
