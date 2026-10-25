#pragma once

#include "world_explorer/map_store/capture_selection.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

enum class EvaluationItemKind : std::uint8_t { PROP, FLOOR, WALL, CEILING, OPENING };
enum class EvaluationVisibility : std::uint8_t { VISIBLE, FULLY_OCCLUDED, OUT_OF_VIEW, UNKNOWN };

struct EvaluationInventoryItem {
    ObjectTrackId m_track;
    EvaluationItemKind m_kind;
    std::string m_label;
    bool m_required {};
    auto operator<=>(const EvaluationInventoryItem&) const = default;
};

struct EvaluationVisibilityCell {
    ObjectTrackId m_track;
    CaptureId m_capture;
    FrameId m_frame;
    EvaluationVisibility m_state;
    std::optional<AssetRevisionRef> m_annotation;
    auto operator<=>(const EvaluationVisibilityCell&) const = default;
};

struct EvaluationInventorySelection {
    AssetRevisionRef m_inventory, m_visibility;
    std::vector<EvaluationInventoryItem> m_items;
    std::vector<EvaluationVisibilityCell> m_cells;
};

// Coordinator-only declarations, not object truth, completeness or AP success.
class AdmittedEvaluationInventory {
  public:
    AdmittedEvaluationInventory(const AdmittedEvaluationInventory&) = default;
    AdmittedEvaluationInventory& operator=(const AdmittedEvaluationInventory&) = default;
    const EvaluationInventorySelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedEvaluationInventory(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedEvaluationInventory>
    inspect_evaluation_inventory(const EvaluationInventorySelection&,
                                 const AdmittedCaptureSelection&,
                                 const AdmittedAssetReceipt&,
                                 const AssetDependencyContext&,
                                 const std::stop_token&);
    friend Result<RecordCommit>
    publish_evaluation_inventory(const AdmittedEvaluationInventory&,
                                 const AdmittedAssetReceipt&,
                                 const std::filesystem::path&,
                                 OperationId,
                                 std::uint64_t,
                                 const AssetDependencyContext&,
                                 const std::stop_token&,
                                 const std::function<void(std::size_t)>&);
};

// <=64 KiB inventory / 256 KiB visibility, 1..64 items (>=1 required),
// <=64 evaluation views / 4096 cells. Exact complete Cartesian table required.
Result<AdmittedEvaluationInventory>
inspect_evaluation_inventory(const EvaluationInventorySelection& selected,
                             const AdmittedCaptureSelection& capture,
                             const AdmittedAssetReceipt& receipt,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop = {});
Result<RecordCommit>
publish_evaluation_inventory(const AdmittedEvaluationInventory& inventory,
                             const AdmittedAssetReceipt& receipt,
                             const std::filesystem::path& directory,
                             OperationId operation,
                             std::uint64_t base_generation,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop = {},
                             const std::function<void(std::size_t)>& observed = {});
Result<AdmittedEvaluationInventory>
read_evaluation_inventory(const std::filesystem::path& directory,
                          const RecordCommit& commit,
                          const EvaluationInventorySelection& selected,
                          const AdmittedCaptureSelection& capture,
                          const AdmittedAssetReceipt& receipt,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop = {});

}  // namespace world_explorer
