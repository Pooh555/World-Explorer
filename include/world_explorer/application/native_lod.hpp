#pragma once

#include "world_explorer/application/geometry_product.hpp"

namespace world_explorer {

struct NativeLodSelection {
    AssetRevisionRef m_product;
    double m_max_deviation_units {};
};

struct NativeLodRequest {
    std::optional<NativeLodSelection> m_selection;
    std::optional<AdmittedGeometryProduct> m_base, m_candidate;
    std::optional<AdmittedAssetReceipt> m_receipt;
    AssetDependencyContext m_expected;
};

// Exact affine three-face fan reduction; no producer, streaming or AP8 claim.
// Owned base/candidate handles retain original source correspondence and bytes.
class AdmittedNativeLod {
  public:
    AdmittedNativeLod(const AdmittedNativeLod&) = default;
    AdmittedNativeLod& operator=(const AdmittedNativeLod&) = default;
    const NativeLodSelection& selection() const;
    const AdmittedGeometryProduct& base() const;
    const AdmittedGeometryProduct& candidate() const;
    const std::vector<std::vector<std::uint32_t>>& source_faces() const;
    const std::string& receipt_sha256() const;

  private:
    struct Implementation;
    explicit AdmittedNativeLod(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedNativeLod> inspect_native_lod(const NativeLodRequest&,
                                                        const std::stop_token&);
};

// <=1 MiB certificate, <=8192 vertices/faces inherited from geometry admission.
// Independently pinned certificate and error ceiling; exact whole-surface,
// material and affine attribute preservation. Unsupported reductions refuse.
Result<AdmittedNativeLod> inspect_native_lod(const NativeLodRequest& request,
                                             const std::stop_token& stop = {});

}  // namespace world_explorer
