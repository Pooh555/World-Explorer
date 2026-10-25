#pragma once

#include "world_explorer/map_store/evaluation_inventory.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

enum class ControlledReferenceKind : std::uint8_t { BACKSIDE, EMPTY_BACKGROUND, CHANGED_LIGHT };
enum class ControlledSubjectState : std::uint8_t { PRESENT, REMOVED };

struct ControlledReferencePair {
    std::uint64_t m_id {};
    ObjectTrackId m_subject;
    ObjectViewSelection m_baseline, m_reference;
    std::string m_baseline_condition, m_reference_condition;
    ControlledSubjectState m_reference_state;
    std::optional<AssetRevisionRef> m_baseline_lighting, m_reference_lighting;
    auto operator<=>(const ControlledReferencePair&) const = default;
};

struct ControlledReferenceSelection {
    AssetRevisionRef m_product;
    ControlledReferenceKind m_kind;
    std::vector<ControlledReferencePair> m_pairs;
};

struct ControlledLightingObservation {
    AssetRevisionRef m_product;
    ObjectViewSelection m_view;
    std::optional<double> m_illuminance_lux, m_color_temperature_kelvin;
};

// Coordinator-only declarations; no physical correspondence, relighting or AP pass.
class AdmittedControlledReference {
  public:
    AdmittedControlledReference(const AdmittedControlledReference&) = default;
    AdmittedControlledReference& operator=(const AdmittedControlledReference&) = default;
    const ControlledReferenceSelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const AssetRevisionRef& inventory_product() const;
    const AssetRevisionRef& visibility_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    const std::vector<ControlledLightingObservation>& lighting() const;
    bool manual_ancestor() const;
    bool has_missing_lighting() const;

  private:
    struct Implementation;
    explicit AdmittedControlledReference(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedControlledReference>
    inspect_controlled_reference(const ControlledReferenceSelection&,
                                 const AdmittedEvaluationInventory&,
                                 const AdmittedCaptureSelection&,
                                 const AdmittedAssetReceipt&,
                                 const AssetDependencyContext&,
                                 const std::stop_token&);
    friend Result<RecordCommit>
    publish_controlled_reference(const AdmittedControlledReference&,
                                 const AdmittedAssetReceipt&,
                                 const std::filesystem::path&,
                                 OperationId,
                                 std::uint64_t,
                                 const AssetDependencyContext&,
                                 const std::stop_token&,
                                 const std::function<void(std::size_t)>&);
};

// <=256 KiB record, 1..32 unique prop/view pairs, <=64 lighting records of
// <=64 KiB each / <=1 MiB aggregate. Exact admitted inventory and capture required.
Result<AdmittedControlledReference>
inspect_controlled_reference(const ControlledReferenceSelection& selected,
                             const AdmittedEvaluationInventory& inventory,
                             const AdmittedCaptureSelection& capture,
                             const AdmittedAssetReceipt& receipt,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop = {});
Result<RecordCommit>
publish_controlled_reference(const AdmittedControlledReference& reference,
                             const AdmittedAssetReceipt& receipt,
                             const std::filesystem::path& directory,
                             OperationId operation,
                             std::uint64_t base_generation,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop = {},
                             const std::function<void(std::size_t)>& observed = {});
Result<AdmittedControlledReference>
read_controlled_reference(const std::filesystem::path& directory,
                          const RecordCommit& commit,
                          const ControlledReferenceSelection& selected,
                          const AdmittedEvaluationInventory& inventory,
                          const AdmittedCaptureSelection& capture,
                          const AdmittedAssetReceipt& receipt,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop = {});

}  // namespace world_explorer
