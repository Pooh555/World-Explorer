#pragma once

#include "capture_selection_fixture.hpp"
#include "world_explorer/application/evaluation_corpus.hpp"

#include <span>
#include <stdexcept>

namespace world_explorer::asset_corpus_test {

using namespace capture_selection_test;

template <class T> T checked(Result<T> result) {
    if (!result) {
        throw std::runtime_error(result.error().m_message);
    }
    return *result;
}

struct CorpusFixture {
    SelectionFixture m_capture;
    RecipeFixture m_recipe, m_output;
    AdmittedAssetReceipt m_capture_receipt, m_recipe_receipt;
    AdmittedCaptureSelection m_selection;
    RecordCommit m_capture_commit;
    AssetCorpusDefinition m_definition;

    explicit CorpusFixture(bool with_evidence = false)
        : m_capture_receipt(stage_capture(with_evidence)), m_recipe_receipt(m_recipe.save()),
          m_selection(checked(m_capture.inspect(m_capture_receipt, false))),
          m_capture_commit(
              checked(publish_capture_selection(m_selection,
                                                m_capture_receipt,
                                                registry(),
                                                { 1 },
                                                0,
                                                m_capture_receipt.graph().m_context))) {
        const auto& selected = m_selection.record().m_selection;
        AssetCorpusCapture capture { "corner",
                                    selected.m_product,
                                    selected.m_selection,
                                    selected.m_revision,
                                    m_capture_receipt.receipt_sha256(),
                                    m_capture_commit,
                                    {} };
        if (with_evidence) {
            for (const auto& node : m_capture_receipt.graph().m_products) {
                if (node.m_product.m_product.m_value >= 7000 &&
                    node.m_product.m_product.m_value < 7010) {
                    capture.m_evidence.emplace(
                        static_cast<AssetCorpusEvidence>(node.m_product.m_product.m_value - 7000),
                        node.m_product);
                }
            }
        }
        const auto& recipe = m_recipe_receipt.graph().m_products.back();
        m_definition = { "synthetic-corpus",
                        1,
                        AssetCorpusTrack::SYNTHETIC,
                        { capture },
                        { { "import",
                          recipe.m_product,
                          m_recipe_receipt.receipt_sha256(),
                          recipe.m_recipe_sha256,
                          m_recipe.m_context,
                          m_recipe.m_host } } };
    }

    AdmittedAssetReceipt stage_capture(bool evidence) {
        if (evidence) {
            auto& region = m_capture.m_object.m_region;
            for (std::size_t index { 0uz }; index < 10uz; ++index) {
                auto node = region.node(7000uz + index,
                                        "synthetic evidence declaration " + std::to_string(index));
                if (index == 3uz) {
                    m_capture.m_selection.m_measurement = node.m_product;
                }
                region.m_graph.m_products.push_back(std::move(node));
            }
        }
        return m_capture.freeze(false);
    }

    std::filesystem::path registry() const {
        return m_output.m_root / "capture";
    }

    std::filesystem::path seal() const {
        return m_output.m_root / "seal";
    }

    std::vector<AssetCorpusCaptureInput> captures() const {
        return { { "corner", m_selection, m_capture_receipt, registry() } };
    }

    std::vector<AssetCorpusRecipeInput> recipes() const {
        return { { "import", m_recipe_receipt } };
    }

    Result<AdmittedAssetCorpus> admit(const std::stop_token& stop = {}) const {
        return admit_asset_corpus(m_definition, captures(), recipes(), stop);
    }

    Result<AdmittedAssetCorpus> decode(const std::string& text,
                                       const std::stop_token& stop = {}) const {
        return decode_asset_corpus(std::as_bytes(std::span(text)),
                                   { m_definition.m_corpus, m_definition.m_revision, hash(text) },
                                   captures(),
                                   recipes(),
                                   stop);
    }
};

}  // namespace world_explorer::asset_corpus_test
