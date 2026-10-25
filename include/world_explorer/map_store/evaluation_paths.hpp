#pragma once

#include "world_explorer/map_store/capture_selection.hpp"

namespace world_explorer {

// Declared evaluation confidence, never a covariance or measured accuracy.
enum class EvaluationPoseConfidence : std::uint8_t { UNASSESSED, UNCERTAIN, DECLARED_RELIABLE };

struct EvaluationTemporalBlock {
    std::uint64_t m_id {};
    CaptureId m_capture;
    CaptureFrameRole m_role;
    std::vector<FrameId> m_frames;
    auto operator<=>(const EvaluationTemporalBlock&) const = default;
};

struct EvaluationInspectionEntry {
    CaptureId m_capture;
    FrameId m_frame;
    EvaluationPoseConfidence m_confidence;
    auto operator<=>(const EvaluationInspectionEntry&) const = default;
};

struct EvaluationPathSelection {
    AssetRevisionRef m_temporal, m_inspection;
    std::vector<EvaluationTemporalBlock> m_blocks;
    std::vector<EvaluationInspectionEntry> m_path;
};

// Coordinator-only frozen metadata; no worker conversion or physical accuracy claim.
class AdmittedEvaluationPaths {
  public:
    AdmittedEvaluationPaths(const AdmittedEvaluationPaths&) = default;
    AdmittedEvaluationPaths& operator=(const AdmittedEvaluationPaths&) = default;
    const EvaluationPathSelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool manual_ancestor() const;
    bool has_unassessed_or_uncertain_poses() const;

  private:
    struct Implementation;
    explicit AdmittedEvaluationPaths(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedEvaluationPaths> inspect_evaluation_paths(const EvaluationPathSelection&,
                                                                    const AdmittedCaptureSelection&,
                                                                    const AdmittedAssetReceipt&,
                                                                    const AssetDependencyContext&,
                                                                    const std::stop_token&);
    friend Result<RecordCommit> publish_evaluation_paths(const AdmittedEvaluationPaths&,
                                                         const AdmittedAssetReceipt&,
                                                         const std::filesystem::path&,
                                                         OperationId,
                                                         std::uint64_t,
                                                         const AssetDependencyContext&,
                                                         const std::stop_token&,
                                                         const std::function<void(std::size_t)>&);
};

// <=256 KiB each, <=64 blocks / frames / path entries. Complete selected-frame
// partition, at least one training-source temporal holdout and independent path.
Result<AdmittedEvaluationPaths> inspect_evaluation_paths(const EvaluationPathSelection& selected,
                                                         const AdmittedCaptureSelection& capture,
                                                         const AdmittedAssetReceipt& receipt,
                                                         const AssetDependencyContext& expected,
                                                         const std::stop_token& stop = {});
Result<RecordCommit>
publish_evaluation_paths(const AdmittedEvaluationPaths& paths,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});
Result<AdmittedEvaluationPaths> read_evaluation_paths(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const EvaluationPathSelection& selected,
                                                      const AdmittedCaptureSelection& capture,
                                                      const AdmittedAssetReceipt& receipt,
                                                      const AssetDependencyContext& expected,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
