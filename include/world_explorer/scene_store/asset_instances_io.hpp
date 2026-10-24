#pragma once

#include "world_explorer/core/immutable_records.hpp"
#include "world_explorer/scene_store/asset_instances.hpp"

#include <cstddef>
#include <span>

namespace world_explorer {

Result<AdmittedAssetInstances>
decode_asset_instances(std::span<const std::byte> bytes,
                       SceneId scene,
                       SceneRevisionId revision,
                       const std::string& selected_sha256,
                       const std::vector<AdmittedAssetDefinition>& definitions,
                       const AdmittedAssetReceipt& receipt,
                       const AssetDependencyContext& expected,
                       const std::stop_token& stop = {});
// Publishes all selected definitions and the instance set in one metadata commit.
Result<RecordCommit> publish_asset_instances(const AdmittedAssetInstances& instances,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop = {},
                                             const std::function<void(std::size_t)>& observed = {});

}  // namespace world_explorer
