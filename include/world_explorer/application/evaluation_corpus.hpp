#pragma once

#include "world_explorer/application/capture_measurement.hpp"
#include "world_explorer/asset_catalog/recipe.hpp"
#include "world_explorer/map_store/capture_selection.hpp"
#include "world_explorer/map_store/evaluation_inventory.hpp"
#include "world_explorer/map_store/evaluation_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

enum class AssetCorpusTrack : std::uint8_t { SYNTHETIC, PILOT, RELEASE };
enum class AssetCorpusEvidence : std::uint8_t {
    CAPTURE_ENVELOPE,
    INVENTORY,
    VISIBILITY,
    INDEPENDENT_SCALE,
    TEMPORAL_BLOCKS,
    INSPECTION_PATH,
    BACKSIDE,
    EMPTY_BACKGROUND,
    CHANGED_LIGHT,
    EVALUATION_PROTOCOL
};

struct AssetCorpusCapture {
    std::string m_label;
    AssetRevisionRef m_product;
    CaptureSelectionId m_selection;
    std::uint64_t m_revision {};
    std::string m_receipt_sha256;
    RecordCommit m_commit;
    std::map<AssetCorpusEvidence, AssetRevisionRef> m_evidence;
};

struct AssetCorpusRecipe {
    std::string m_label;
    AssetRevisionRef m_product;
    std::string m_receipt_sha256, m_recipe_sha256;
    AssetDependencyContext m_context;
    AssetRecipeHost m_host;
};

struct AssetCorpusDefinition {
    std::string m_corpus;
    std::uint64_t m_revision {};
    AssetCorpusTrack m_track { AssetCorpusTrack::SYNTHETIC };
    std::vector<AssetCorpusCapture> m_captures;
    std::vector<AssetCorpusRecipe> m_recipes;
    bool m_require_typed_scale {};
    bool m_require_typed_inventory {};
    bool m_require_typed_paths {};
};

struct AssetCorpusRef {
    std::string m_corpus;
    std::uint64_t m_revision {};
    std::string m_sha256;
};

// Caller-selected owned handles and registry paths, never obtained from a manifest.
struct AssetCorpusCaptureInput {
    std::string m_label;
    AdmittedCaptureSelection m_selection;
    AdmittedAssetReceipt m_receipt;
    std::filesystem::path m_registry;
    std::optional<AdmittedCaptureMeasurement> m_measurement {};
    std::optional<AdmittedEvaluationInventory> m_inventory {};
    std::optional<AdmittedEvaluationPaths> m_paths {};
};

struct AssetCorpusRecipeInput {
    std::string m_label;
    AdmittedAssetReceipt m_receipt;
};

struct AssetCorpusBlocker {
    std::string m_scope, m_code;
    auto operator<=>(const AssetCorpusBlocker&) const = default;
};

// Coordinator-only frozen metadata. No execution, rights, measurement or AP pass token.
class AdmittedAssetCorpus {
  public:
    AdmittedAssetCorpus(const AdmittedAssetCorpus&) = default;
    AdmittedAssetCorpus& operator=(const AdmittedAssetCorpus&) = default;
    const AssetCorpusDefinition& definition() const;
    const AssetCorpusRef& reference() const;
    const std::string& canonical_json() const;
    const std::vector<AssetCorpusBlocker>& blockers() const;
    const std::vector<AssetRecipeInspection>& recipes() const;

  private:
    struct Implementation;
    explicit AdmittedAssetCorpus(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedAssetCorpus>
    admit_asset_corpus(const AssetCorpusDefinition&,
                       const std::vector<AssetCorpusCaptureInput>&,
                       const std::vector<AssetCorpusRecipeInput>&,
                       const std::stop_token&);
    friend Result<RecordCommit> publish_asset_corpus(const AdmittedAssetCorpus&,
                                                     const std::filesystem::path&,
                                                     OperationId,
                                                     const std::stop_token&,
                                                     const std::function<void(std::size_t)>&);
};

// <=256 KiB canonical document, 16 captures, 256 selected frames, 16 recipes,
// ten optional evidence refs/capture. Missing/opaque evidence yields blockers.
Result<AdmittedAssetCorpus> admit_asset_corpus(const AssetCorpusDefinition& definition,
                                               const std::vector<AssetCorpusCaptureInput>& captures,
                                               const std::vector<AssetCorpusRecipeInput>& recipes,
                                               const std::stop_token& stop = {});
Result<AdmittedAssetCorpus>
decode_asset_corpus(std::span<const std::byte> bytes,
                    const AssetCorpusRef& selected,
                    const std::vector<AssetCorpusCaptureInput>& captures,
                    const std::vector<AssetCorpusRecipeInput>& recipes,
                    const std::stop_token& stop = {});

// One seal per owned directory (base zero); exact operation replay is idempotent.
Result<RecordCommit> publish_asset_corpus(const AdmittedAssetCorpus& corpus,
                                          const std::filesystem::path& directory,
                                          OperationId operation,
                                          const std::stop_token& stop = {},
                                          const std::function<void(std::size_t)>& observed = {});
Result<AdmittedAssetCorpus> read_asset_corpus(const std::filesystem::path& directory,
                                              const RecordCommit& commit,
                                              const AssetCorpusRef& selected,
                                              const std::vector<AssetCorpusCaptureInput>& captures,
                                              const std::vector<AssetCorpusRecipeInput>& recipes,
                                              const std::stop_token& stop = {});

}  // namespace world_explorer
