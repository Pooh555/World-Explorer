#pragma once

#include "world_explorer/asset_catalog/dependency_receipt.hpp"
#include "world_explorer/reconstruction_api/map_records.hpp"
#include "world_explorer/reconstruction_api/pose_estimator.hpp"

#include <array>
#include <memory>

namespace world_explorer {

enum class ObjectMaskClass : std::uint8_t { VISIBLE, OCCLUDED, UNKNOWN, BACKGROUND };
enum class ObjectAssociationDecision : std::uint8_t { HYPOTHESIS, ACCEPTED, REJECTED };

struct ObjectImageGrid {
    std::uint64_t m_width {}, m_height {};
    // Integer centers: mask_pixel = scale * calibrated_image_pixel + offset.
    std::array<double, 4> m_image_to_mask { 1, 1, 0, 0 };
    auto operator<=>(const ObjectImageGrid&) const = default;
};

struct ObjectViewSelection {
    CaptureId m_capture;
    FrameId m_frame;
    CalibrationId m_calibration;
    MapRevisionId m_frame_pose_revision;
    AssetRevisionRef m_image, m_pose_asset;
    auto operator<=>(const ObjectViewSelection&) const = default;
};

struct ObjectObservationSelection {
    AssetRevisionRef m_product;
    ObjectObservationId m_observation;
    std::uint64_t m_revision {};
    ObjectViewSelection m_view;
    AssetRevisionRef m_mask;
    ObjectImageGrid m_grid;
    std::vector<AssetRevisionRef> m_evidence;
};

struct ObjectMaskSpan {
    std::uint64_t m_first {}, m_count {};
    ObjectMaskClass m_class;
};

struct ObjectClassHypothesis {
    std::string m_label;
    std::optional<double> m_confidence;
};

struct ObjectTrackAssociation {
    ObjectTrackId m_track;
    ObjectAssociationDecision m_decision;
    std::optional<double> m_confidence;
};

struct ObjectObservationEvidence {
    AssetRevisionRef m_product;
    bool m_accepted {};
    std::string m_reason;
};

struct ObjectObservationRecord {
    ObjectObservationSelection m_selection;
    AssetDependencyContext m_context;
    std::string m_recipe_sha256;
    std::vector<ObjectClassHypothesis> m_classes;
    std::vector<ObjectTrackAssociation> m_associations;
    std::vector<ObjectObservationEvidence> m_evidence;
    bool m_manual_ancestor {};
};

// Owned immutable declarations and validated pose/calibration bytes. Mask labels
// and associations are not independently observed truth or trusted manual acts.
class AdmittedObjectObservation {
  public:
    AdmittedObjectObservation(const AdmittedObjectObservation&) = default;
    AdmittedObjectObservation& operator=(const AdmittedObjectObservation&) = default;
    const ObjectObservationRecord& record() const;
    const std::vector<ObjectMaskSpan>& mask() const;
    const std::array<std::uint64_t, 4>& coverage() const;
    const CalibrationRecord& calibration() const;
    const PoseEstimate& pose() const;
    const SimilarityTransform& world_from_camera() const;
    const std::string& receipt_sha256() const;

  private:
    struct Implementation;
    explicit AdmittedObjectObservation(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedObjectObservation>
    inspect_object_observation(const AdmittedAssetReceipt&,
                               const ObjectObservationSelection&,
                               const AssetDependencyContext&,
                               const std::stop_token&);
};

// Observation <=256 KiB; mask <=4 MiB / 16,384 complete row-major runs / 16M
// pixels; pose asset <=16 MiB / 4096 frames / 64 calibrations and submaps.
// All dependencies are selected outside the record. No files or map commits.
Result<AdmittedObjectObservation>
inspect_object_observation(const AdmittedAssetReceipt& receipt,
                           const ObjectObservationSelection& selection,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop = {});

}  // namespace world_explorer
