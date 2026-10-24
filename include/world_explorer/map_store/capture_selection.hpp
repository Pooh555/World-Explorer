#pragma once

#include "world_explorer/core/immutable_records.hpp"
#include "world_explorer/map_store/object_observation.hpp"

namespace world_explorer {

enum class CaptureFrameRole : std::uint8_t { INFERENCE, HELD_OUT, EVALUATION_ONLY };

struct CaptureSourceSelection {
    CaptureId m_capture;
    std::string m_source_sha256;
    AssetRevisionRef m_rights;
    auto operator<=>(const CaptureSourceSelection&) const = default;
};

struct CaptureFrameSelection {
    ObjectViewSelection m_view;
    CaptureFrameRole m_role;
    std::vector<AssetRevisionRef> m_annotations;
    auto operator<=>(const CaptureFrameSelection&) const = default;
};

struct CaptureSelection {
    AssetRevisionRef m_product;
    CaptureSelectionId m_selection;
    std::uint64_t m_revision {};
    std::vector<CaptureSourceSelection> m_sources;
    std::vector<CaptureFrameSelection> m_frames;
    std::optional<AssetRevisionRef> m_measurement;
};

struct CaptureSelectionRecord {
    CaptureSelection m_selection;
    AssetDependencyContext m_context;
    bool m_manual_ancestor {};
};

struct CaptureFrameInputs {
    ObjectViewSelection m_view;
    AssetDependencyContext m_context;
    ObservationRecord m_observation;
    std::vector<std::byte> m_image;
    CalibrationRecord m_calibration;
    PoseEstimate m_pose;
    SimilarityTransform m_world_from_camera;
    bool m_manual_ancestor {};
    ScaleStatus m_scale { ScaleStatus::ARBITRARY };
};

struct CaptureEvaluationFrame {
    CaptureFrameRole m_role;
    CaptureFrameInputs m_inputs;
    std::vector<AdmittedObjectObservation> m_annotations;
};

// Only selected image bytes and camera values. No receipt, complete pose asset,
// evaluation frames, masks, labels, annotation handles or coordinator manifest.
class InferenceCaptureInputs {
  public:
    InferenceCaptureInputs(const InferenceCaptureInputs&) = default;
    InferenceCaptureInputs& operator=(const InferenceCaptureInputs&) = default;
    const std::vector<CaptureFrameInputs>& frames() const;

  private:
    explicit InferenceCaptureInputs(std::shared_ptr<const std::vector<CaptureFrameInputs>> frames);
    std::shared_ptr<const std::vector<CaptureFrameInputs>> m_frames;
    friend class AdmittedCaptureSelection;
};

class AdmittedCaptureSelection {
  public:
    AdmittedCaptureSelection(const AdmittedCaptureSelection&) = default;
    AdmittedCaptureSelection& operator=(const AdmittedCaptureSelection&) = default;
    const CaptureSelectionRecord& record() const;
    InferenceCaptureInputs inference() const;
    const std::vector<CaptureEvaluationFrame>& evaluation() const;
    const std::string& receipt_sha256() const;
    const std::vector<std::byte>& encoded_bytes() const;

  private:
    struct Implementation;
    explicit AdmittedCaptureSelection(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedCaptureSelection>
    inspect_capture_selection(const AdmittedAssetReceipt&,
                              const CaptureSelection&,
                              const std::vector<AdmittedObjectObservation>&,
                              const AssetDependencyContext&,
                              const std::stop_token&);
};

// Strict <=256 KiB record, <=64 frames / 32 sources / 128 annotations (16/frame),
// <=32 MiB images per role group; pose assets <=16 MiB / 4096 frames / 64 groups.
// Requires inference and non-inference frames. No claim of a sealed real corpus,
// source rights, independent measurement or unknown upstream fit history.
Result<AdmittedCaptureSelection>
inspect_capture_selection(const AdmittedAssetReceipt& receipt,
                          const CaptureSelection& selection,
                          const std::vector<AdmittedObjectObservation>& annotations,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop = {});
Result<RecordCommit>
publish_capture_selection(const AdmittedCaptureSelection& selection,
                          const AdmittedAssetReceipt& receipt,
                          const std::filesystem::path& directory,
                          OperationId operation,
                          std::uint64_t base_generation,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop = {},
                          const std::function<void(std::size_t)>& observed = {});

}  // namespace world_explorer
