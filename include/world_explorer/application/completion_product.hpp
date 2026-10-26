#pragma once

#include "world_explorer/application/structure_graph.hpp"

namespace world_explorer {

enum class CompletionSubject : std::uint8_t { OBJECT_GEOMETRY, SHELL_GEOMETRY };
enum class CompletionMethod : std::uint8_t { INFERRED, GENERATED, LIBRARY };
enum class CompletionRejection : std::uint8_t {
    NONE,
    ANCHOR_CHECK_FAILED,
    REGION_CHECK_FAILED,
    DECLARED_RANKED_ALTERNATIVE
};

struct CompletionMethodSelection {
    CompletionMethod m_method;
    std::uint64_t m_seed {};
    AssetRevisionRef m_configuration, m_algorithm, m_rights;
    std::optional<AssetRevisionRef> m_prompt, m_library;
};

struct CompletionCandidateSelection {
    std::uint64_t m_id {};
    AssetRevisionRef m_geometry;
    CompletionMethodSelection m_method;
};

struct CompletionProductSelection {
    AssetRevisionRef m_product;
    CompletionSubject m_subject;
    std::vector<StructureSurfaceId> m_surfaces;
    std::vector<CompletionCandidateSelection> m_candidates;
    std::uint64_t m_selected_candidate {};
    double m_max_region_extent_units {};
};

struct CompletionProtection {
    std::uint32_t m_source_face {}, m_result_face {}, m_rotation {};
};

struct CompletionRegion {
    std::uint64_t m_id {};
    std::vector<std::uint32_t> m_faces;
    AssetEvidenceClass m_class;
    std::optional<double> m_confidence;
    std::vector<StructureSurfaceId> m_surfaces;
    Bounds3d m_bounds;
};

struct CompletionChecks {
    bool m_observed_anchors_preserved {}, m_complete_region_partition {},
        m_region_extent_within_policy {};
};

struct CompletionCandidate {
    CompletionCandidateSelection m_selection;
    std::uint32_t m_rank {};
    CompletionRejection m_rejection;
    std::vector<CompletionProtection> m_protected;
    std::vector<CompletionRegion> m_regions;
    CompletionChecks m_checks;
};

struct CompletionProductRequest {
    std::optional<CompletionProductSelection> m_selection;
    std::optional<AdmittedGeometryProduct> m_source;
    std::optional<AdmittedStructureGraph> m_structure;
    std::optional<AdmittedCaptureSelection> m_capture;
    std::optional<AdmittedAssetReceipt> m_receipt;
    std::vector<AdmittedGeometryProduct> m_candidates;
    AssetDependencyContext m_expected;
};

// Owned geometry-bound completion declarations and recomputed consistency checks;
// no completion worker, human/rights authenticity, quality or asset-adoption token.
class AdmittedCompletionProduct {
  public:
    AdmittedCompletionProduct(const AdmittedCompletionProduct&) = default;
    AdmittedCompletionProduct& operator=(const AdmittedCompletionProduct&) = default;
    const CompletionProductSelection& selection() const;
    const std::vector<CompletionCandidate>& candidates() const;
    const AdmittedGeometryProduct& selected_geometry() const;
    const AssetRevisionRef& source_geometry() const;
    const AssetRevisionRef& structure_product() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool synthetic_source() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedCompletionProduct(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedCompletionProduct>
    inspect_completion_product(const CompletionProductRequest&, const std::stop_token&);
    friend Result<RecordCommit> publish_completion_product(const AdmittedCompletionProduct&,
                                                           const AdmittedAssetReceipt&,
                                                           const std::filesystem::path&,
                                                           OperationId,
                                                           std::uint64_t,
                                                           const AssetDependencyContext&,
                                                           const std::stop_token&,
                                                           const std::function<void(std::size_t)>&);
};

// Root <=1 MiB; 1..8 independently admitted candidates, <=8192 protection rows,
// <=32768 total region faces, <=32 regions/candidate. Owned batch <=8 MiB.
Result<AdmittedCompletionProduct>
inspect_completion_product(const CompletionProductRequest& request,
                           const std::stop_token& stop = {});
Result<RecordCommit>
publish_completion_product(const AdmittedCompletionProduct& completion,
                           const AdmittedAssetReceipt& receipt,
                           const std::filesystem::path& directory,
                           OperationId operation,
                           std::uint64_t base_generation,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop = {},
                           const std::function<void(std::size_t)>& observed = {});
Result<AdmittedCompletionProduct> read_completion_product(const std::filesystem::path& directory,
                                                          const RecordCommit& commit,
                                                          const CompletionProductRequest& request,
                                                          const std::stop_token& stop = {});

}  // namespace world_explorer
