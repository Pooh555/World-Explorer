#pragma once

#include "world_explorer/core/asset_ids.hpp"
#include "world_explorer/core/result.hpp"

#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

// Semantic product identity, immutable selected revision and content identity
// remain distinct. This contract validates declarations, not file bytes/rights.
struct AssetRevisionRef {
    AssetProductId m_product;
    std::uint64_t m_revision {};
    std::string m_content_sha256;
    auto operator<=>(const AssetRevisionRef&) const = default;
};

struct AssetDependencyNode {
    AssetRevisionRef m_product;
    std::vector<AssetRevisionRef> m_dependencies;
    std::string m_recipe_sha256;
    std::optional<std::string> m_manual_action_sha256;
    bool m_manual_ancestor {};
    auto operator<=>(const AssetDependencyNode&) const = default;
};

struct AssetDependencyContext {
    WorldEpoch m_world_epoch;
    MapRevisionId m_pose_revision;
};

struct AssetDependencyGraph {
    std::uint32_t m_schema_version { 1 };
    AssetDependencyContext m_context;
    std::vector<AssetDependencyNode> m_products;
};

struct AdmittedAssetDependencies {
    std::vector<AssetProductId> m_topological_order;
    std::string m_graph_sha256;
};

// Closed DAG: <=256 selected products, <=64 dependencies/product, <=4096 edges.
// Canonical order is independent of input order. Stale context/revisions require
// resync. Manual ancestry is exact transitive closure of declared manual actions.
// No caller mutation, file admission, publication or algorithm/model execution.
Result<AdmittedAssetDependencies>
validate_asset_dependencies(const AssetDependencyGraph& graph,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {});

}  // namespace world_explorer
