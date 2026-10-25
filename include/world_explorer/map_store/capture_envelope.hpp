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

enum class CaptureSceneMotion : std::uint8_t { UNASSESSED, STATIC, DYNAMIC };
enum class CaptureExposureControl : std::uint8_t { UNASSESSED, LOCKED, AUTOMATIC };

struct CaptureSourceEnvelope {
    CaptureSourceSelection m_source;
    std::optional<std::uint64_t> m_source_frame_count;
    std::string m_condition;
    CaptureSceneMotion m_scene_motion;
    CaptureExposureControl m_exposure_control;
    auto operator<=>(const CaptureSourceEnvelope&) const = default;
};

struct CaptureEnvelopeView {
    ObjectViewSelection m_view;
    CaptureFrameRole m_role;
    auto operator<=>(const CaptureEnvelopeView&) const = default;
};

struct CaptureEnvelopeSelection {
    AssetRevisionRef m_product;
    std::vector<CaptureSourceEnvelope> m_sources;
    std::vector<CaptureEnvelopeView> m_views;
};

// Coordinator-only evidence. Native camera binding does not prove consent,
// physical conditions, full-source coverage, image quality or measured scale.
class AdmittedCaptureEnvelope {
  public:
    AdmittedCaptureEnvelope(const AdmittedCaptureEnvelope&) = default;
    AdmittedCaptureEnvelope& operator=(const AdmittedCaptureEnvelope&) = default;
    const CaptureEnvelopeSelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool manual_ancestor() const;
    bool has_unknown_source_counts() const;
    bool has_unassessed_conditions() const;
    bool has_dynamic_sources() const;
    bool has_automatic_exposure() const;

  private:
    struct Implementation;
    explicit AdmittedCaptureEnvelope(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedCaptureEnvelope> inspect_capture_envelope(const CaptureEnvelopeSelection&,
                                                                    const AdmittedCaptureSelection&,
                                                                    const AdmittedAssetReceipt&,
                                                                    const AssetDependencyContext&,
                                                                    const std::stop_token&);
    friend Result<RecordCommit> publish_capture_envelope(const AdmittedCaptureEnvelope&,
                                                         const AdmittedAssetReceipt&,
                                                         const std::filesystem::path&,
                                                         OperationId,
                                                         std::uint64_t,
                                                         const AssetDependencyContext&,
                                                         const std::stop_token&,
                                                         const std::function<void(std::size_t)>&);
};

// <=256 KiB, 1..32 complete source rows, 1..64 complete selected view rows.
Result<AdmittedCaptureEnvelope> inspect_capture_envelope(const CaptureEnvelopeSelection& selected,
                                                         const AdmittedCaptureSelection& capture,
                                                         const AdmittedAssetReceipt& receipt,
                                                         const AssetDependencyContext& expected,
                                                         const std::stop_token& stop = {});
Result<RecordCommit>
publish_capture_envelope(const AdmittedCaptureEnvelope& envelope,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});
Result<AdmittedCaptureEnvelope> read_capture_envelope(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const CaptureEnvelopeSelection& selected,
                                                      const AdmittedCaptureSelection& capture,
                                                      const AdmittedAssetReceipt& receipt,
                                                      const AssetDependencyContext& expected,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
