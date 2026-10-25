#pragma once

#include "world_explorer/map_store/capture_selection.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

enum class EvaluationTrack : std::uint8_t { OBSERVED, COMPLETED };
enum class EvaluationArtifact : std::uint8_t {
    GEOMETRY_SAMPLER,
    UV_SAMPLER,
    RENDERER,
    TONE_MAPPER,
    METRICS,
    REVIEW_RUBRIC
};

struct EvaluationMaskPin {
    EvaluationTrack m_track;
    ObjectViewSelection m_view;
    AssetRevisionRef m_product;
    auto operator<=>(const EvaluationMaskPin&) const = default;
};

struct EvaluationLightPin {
    std::string m_label;
    AssetRevisionRef m_product;
    std::uint64_t m_seed {};
    bool m_near_light {};
    auto operator<=>(const EvaluationLightPin&) const = default;
};

struct EvaluationProtocolSelection {
    AssetRevisionRef m_product;
    std::map<EvaluationArtifact, AssetRevisionRef> m_artifacts;
    std::vector<EvaluationMaskPin> m_masks;
    std::vector<EvaluationLightPin> m_lights;
    std::uint64_t m_geometry_samples {}, m_geometry_seed {};
    std::uint32_t m_uv_resolution {}, m_reviewers_min {};
};

struct EvaluationMaskSummary {
    EvaluationMaskPin m_pin;
    std::uint64_t m_pixels {};
    // UNKNOWN / OBSERVED / COMPLETED declared surface classes. UNKNOWN is never eligible.
    std::array<std::uint64_t, 3> m_eligible {};
};

// Frozen coordinator metadata, not a metric runner or recipe/product approval.
class AdmittedEvaluationProtocol {
  public:
    AdmittedEvaluationProtocol(const AdmittedEvaluationProtocol&) = default;
    AdmittedEvaluationProtocol& operator=(const AdmittedEvaluationProtocol&) = default;
    const EvaluationProtocolSelection& selection() const;
    const AssetRevisionRef& capture_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    const std::vector<EvaluationMaskSummary>& masks() const;
    bool manual_ancestor() const;
    bool has_empty_masks() const;

  private:
    struct Implementation;
    explicit AdmittedEvaluationProtocol(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedEvaluationProtocol>
    inspect_evaluation_protocol(const EvaluationProtocolSelection&,
                                const AdmittedCaptureSelection&,
                                const AdmittedAssetReceipt&,
                                const AssetDependencyContext&,
                                const std::stop_token&);
    friend Result<RecordCommit>
    publish_evaluation_protocol(const AdmittedEvaluationProtocol&,
                                const AdmittedAssetReceipt&,
                                const std::filesystem::path&,
                                OperationId,
                                std::uint64_t,
                                const AssetDependencyContext&,
                                const std::stop_token&,
                                const std::function<void(std::size_t)>&);
};

// <=256 KiB root; 1..16 evaluation views x two tracks; six <=64 KiB config
// artifacts; 3..8 <=64 KiB light configs (>=one declared near light).
// Masks: <=256 KiB / 16,384 runs each, <=4 MiB / 65,536 runs / 64 Mi pixels total.
Result<AdmittedEvaluationProtocol>
inspect_evaluation_protocol(const EvaluationProtocolSelection& selected,
                            const AdmittedCaptureSelection& capture,
                            const AdmittedAssetReceipt& receipt,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {});
Result<RecordCommit>
publish_evaluation_protocol(const AdmittedEvaluationProtocol& protocol,
                            const AdmittedAssetReceipt& receipt,
                            const std::filesystem::path& directory,
                            OperationId operation,
                            std::uint64_t base_generation,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {},
                            const std::function<void(std::size_t)>& observed = {});
Result<AdmittedEvaluationProtocol>
read_evaluation_protocol(const std::filesystem::path& directory,
                         const RecordCommit& commit,
                         const EvaluationProtocolSelection& selected,
                         const AdmittedCaptureSelection& capture,
                         const AdmittedAssetReceipt& receipt,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {});

}  // namespace world_explorer
