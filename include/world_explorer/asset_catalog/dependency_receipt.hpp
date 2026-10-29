#pragma once

#include "world_explorer/asset_catalog/dependency_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace world_explorer {

enum class AssetReceiptProfile : std::uint8_t { SMALL_V1 = 1, CAPTURE_V2 = 2 };

struct AssetReceiptSelection {
    std::filesystem::path m_directory;
    std::string m_receipt_sha256;
    AssetReceiptProfile m_profile { AssetReceiptProfile::SMALL_V1 };
    // Caller allocation allowance, checked against the complete inventory before
    // payload reads. Cannot enlarge the selected profile's hard ceiling.
    std::size_t m_payload_budget { 768uz * 1024uz * 1024uz };
};

// Owns exactly one immutable blob, with no receipt, graph or other blob access.
class AdmittedAssetBlob {
  public:
    std::span<const std::byte> bytes() const;
    const std::byte* data() const;
    std::size_t size() const;
    bool empty() const;

    auto begin() const {
        return bytes().begin();
    }

    auto end() const {
        return bytes().end();
    }

  private:
    explicit AdmittedAssetBlob(std::shared_ptr<const std::vector<std::byte>> bytes);
    std::shared_ptr<const std::vector<std::byte>> m_bytes;
    friend class AdmittedAssetReceipt;
};

class AdmittedAssetReceipt {
  public:
    AdmittedAssetReceipt(const AdmittedAssetReceipt&) = default;
    AdmittedAssetReceipt& operator=(const AdmittedAssetReceipt&) = default;
    const AssetDependencyGraph& graph() const;
    const std::string& receipt_sha256() const;
    OperationId operation() const;
    const std::filesystem::path& source_directory() const;
    std::size_t file_count() const;
    std::size_t payload_bytes() const;
    AssetReceiptProfile profile() const;
    Result<AdmittedAssetBlob> share_blob(const std::string& sha256,
                                         std::size_t max_bytes,
                                         const std::stop_token& stop = {}) const;
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
// Explicit CAPTURE_V2 selects schema 2 and <=768 MiB aggregate; all other bounds
// stay fixed. Shared blobs own no other products. This is a storage profile, not
// a resource-fit, capture, recipe or rights qualification.
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
