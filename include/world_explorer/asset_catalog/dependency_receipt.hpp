#pragma once

#include "world_explorer/asset_catalog/dependency_graph.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>

namespace world_explorer {

struct AssetReceiptSelection {
    std::filesystem::path m_directory;
    std::string m_receipt_sha256;
};

class AdmittedAssetReceipt {
  public:
    AdmittedAssetReceipt(const AdmittedAssetReceipt&) = default;
    AdmittedAssetReceipt& operator=(const AdmittedAssetReceipt&) = default;
    const AssetDependencyGraph& graph() const;
    const std::string& receipt_sha256() const;
    OperationId operation() const;
    std::size_t file_count() const;
    // Independent owned copy; no filesystem access after initial admission.
    Result<std::vector<std::byte>> copy_blob(const std::string& sha256,
                                             std::size_t max_bytes,
                                             const std::stop_token& stop = {}) const;

  private:
    struct Implementation;
    explicit AdmittedAssetReceipt(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedAssetReceipt> read_asset_receipt(const AssetReceiptSelection&,
                                                           const AssetDependencyContext&,
                                                           const std::stop_token&);
    friend Result<AssetReceiptSelection>
    publish_asset_receipt(const AdmittedAssetReceipt&,
                          const std::filesystem::path&,
                          const AssetDependencyContext&,
                          const std::stop_token&,
                          const std::function<void(std::size_t)>&);
};

// Read-only, privately owned byte snapshot: <=1 MiB JSON, <=768 nonempty files,
// <=16 MiB/file and <=64 MiB aggregate. Only blobs/<sha256>.bin paths are allowed.
// Recipe/action contents are hash-bound opaque bytes, not semantic/rights approval.
Result<AdmittedAssetReceipt> read_asset_receipt(const AssetReceiptSelection& selection,
                                                const AssetDependencyContext& expected,
                                                const std::stop_token& stop = {});

// Single-operation output directory; completion is immutable receipt.json.
// Same exact selection retries idempotently; corrupt/foreign output is preserved
// and rejected. Parent must exist; source/output trees must be disjoint.
// Observer runs after each verified/installed blob, before completion publication.
Result<AssetReceiptSelection>
publish_asset_receipt(const AdmittedAssetReceipt& receipt,
                      const std::filesystem::path& directory,
                      const AssetDependencyContext& expected,
                      const std::stop_token& stop = {},
                      const std::function<void(std::size_t)>& observed = {});

}  // namespace world_explorer
