#pragma once

#include "world_explorer/asset_catalog/asset_definition.hpp"
#include "world_explorer/core/immutable_records.hpp"

#include <cstddef>
#include <span>

namespace world_explorer {

Result<AdmittedAssetDefinition>
decode_asset_definition(std::span<const std::byte> bytes,
                        const AssetDefinitionRef& selected,
                        const AdmittedAssetReceipt& receipt,
                        const std::vector<AssetProvenanceSelection>& provenance,
                        const AssetDependencyContext& expected,
                        const std::stop_token& stop = {});
Result<RecordCommit>
publish_asset_definition(const AdmittedAssetDefinition& definition,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});

}  // namespace world_explorer
