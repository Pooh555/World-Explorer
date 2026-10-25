#pragma once

#include "world_explorer/map_store/capture_selection.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace world_explorer {

struct CaptureMeasurementScene {
    SceneId m_scene;
    WorldEpoch m_epoch;
    SceneRevisionId m_revision;
    std::string m_sha256;
    auto operator<=>(const CaptureMeasurementScene&) const = default;
};

struct CaptureMeasurementSelection {
    AssetRevisionRef m_product;
    ObjectViewSelection m_view;
    CaptureMeasurementScene m_scene;
    AssetRevisionRef m_alignment;
    auto operator<=>(const CaptureMeasurementSelection&) const = default;
};

// Checked capture/scene/anchor declarations. No physical measurement, source
// correspondence, sensor authenticity, metric-map or walking authorization.
class AdmittedCaptureMeasurement {
  public:
    AdmittedCaptureMeasurement(const AdmittedCaptureMeasurement&) = default;
    AdmittedCaptureMeasurement& operator=(const AdmittedCaptureMeasurement&) = default;
    const CaptureMeasurementSelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    const WorldFrameAlignment& alignment() const;
    double metres() const;
    double uncertainty_metres() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedCaptureMeasurement(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedCaptureMeasurement>
    inspect_capture_measurement(const CaptureMeasurementSelection&,
                                const AdmittedCaptureSelection&,
                                const AdmittedAssetReceipt&,
                                const SceneState&,
                                const AssetDependencyContext&,
                                const std::stop_token&);
    friend Result<RecordCommit>
    publish_capture_measurement(const AdmittedCaptureMeasurement&,
                                const AdmittedAssetReceipt&,
                                const std::filesystem::path&,
                                OperationId,
                                std::uint64_t,
                                const AssetDependencyContext&,
                                const std::stop_token&,
                                const std::function<void(std::size_t)>&);
};

// <=16 KiB record + existing <=8192-byte WORLD_FRAME_ALIGNMENT_V1 document.
// Source: <=32 MiB untextured meshes, <=64 chunks, 262144 vertices/524288 faces.
// Requires an exact selected inference view and independent scene/content pins.
Result<AdmittedCaptureMeasurement>
inspect_capture_measurement(const CaptureMeasurementSelection& selected,
                            const AdmittedCaptureSelection& capture,
                            const AdmittedAssetReceipt& receipt,
                            const SceneState& source,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {});
Result<RecordCommit>
publish_capture_measurement(const AdmittedCaptureMeasurement& measurement,
                            const AdmittedAssetReceipt& receipt,
                            const std::filesystem::path& directory,
                            OperationId operation,
                            std::uint64_t base_generation,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {},
                            const std::function<void(std::size_t)>& observed = {});

// Exact committed metadata, followed by fresh independent source admission.
Result<AdmittedCaptureMeasurement>
read_capture_measurement(const std::filesystem::path& directory,
                         const RecordCommit& commit,
                         const CaptureMeasurementSelection& selected,
                         const AdmittedCaptureSelection& capture,
                         const AdmittedAssetReceipt& receipt,
                         const SceneState& source,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {});

}  // namespace world_explorer
