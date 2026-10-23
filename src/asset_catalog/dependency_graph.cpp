#include "world_explorer/asset_catalog/dependency_graph.hpp"

#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <new>
#include <set>

namespace world_explorer {
namespace {

bool digest_valid(const std::string& value) {
    return value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

bool reference_valid(const AssetRevisionRef& reference) {
    return reference.m_product.m_value && reference.m_revision &&
           digest_valid(reference.m_content_sha256);
}

nlohmann::json reference_json(const AssetRevisionRef& reference) {
    return { { "product_id", reference.m_product.m_value },
            { "revision", reference.m_revision },
            { "content_sha256", reference.m_content_sha256 } };
}

}  // namespace

Result<AdmittedAssetDependencies>
validate_asset_dependencies(const AssetDependencyGraph& graph,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Asset dependency admission cancelled");
        }
        if (!expected.m_pose_revision.m_value) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Expected asset pose revision must be selected");
        }
        if (graph.m_schema_version != 1) {
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported asset dependency schema");
        }
        if (graph.m_context.m_world_epoch != expected.m_world_epoch ||
            graph.m_context.m_pose_revision != expected.m_pose_revision) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Asset dependency context is stale");
        }
        if (graph.m_products.empty()) {
            return fail(ErrorCode::INVALID_DATA, "Asset dependency graph is empty");
        }
        if (graph.m_products.size() > 256uz) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset dependency product budget exceeded");
        }
        std::map<AssetProductId, std::size_t> selected;
        std::size_t edges {};
        for (std::size_t index { 0uz }; index < graph.m_products.size(); ++index) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Asset dependency admission cancelled");
            }
            const auto& node = graph.m_products[index];
            if (node.m_dependencies.size() > 64uz || node.m_dependencies.size() > 4096uz - edges) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset dependency edge budget exceeded");
            }
            edges += node.m_dependencies.size();
            if (!reference_valid(node.m_product) || !digest_valid(node.m_recipe_sha256) ||
                (node.m_manual_action_sha256 && !digest_valid(*node.m_manual_action_sha256)) ||
                !selected.emplace(node.m_product.m_product, index).second) {
                return fail(ErrorCode::INVALID_DATA,
                            "Invalid or duplicate asset dependency product");
            }
        }
        std::vector<std::vector<std::size_t>> dependents(graph.m_products.size());
        std::vector<std::size_t> remaining(graph.m_products.size());
        std::set<AssetProductId> ready;
        for (const auto& [K_ID, K_INDEX] : selected) {
            const auto& node = graph.m_products[K_INDEX];
            std::set<AssetProductId> parents;
            for (const auto& dependency : node.m_dependencies) {
                if (!reference_valid(dependency)) {
                    return fail(ErrorCode::INVALID_DATA, "Malformed asset dependency reference");
                }
                const auto K_PARENT = selected.find(dependency.m_product);
                if (K_PARENT == selected.end() || dependency.m_product == K_ID ||
                    !parents.insert(dependency.m_product).second) {
                    return fail(ErrorCode::INVALID_DATA, "Missing or duplicate asset dependency");
                }
                if (dependency != graph.m_products[K_PARENT->second].m_product) {
                    return fail(ErrorCode::RESYNC_REQUIRED,
                                "Selected asset dependency revision changed");
                }
                dependents[K_PARENT->second].push_back(K_INDEX);
            }
            remaining[K_INDEX] = parents.size();
            if (parents.empty()) {
                ready.insert(K_ID);
            }
        }
        AdmittedAssetDependencies result;
        while (!ready.empty()) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Asset dependency admission cancelled");
            }
            const auto K_ID = *ready.begin();
            ready.erase(ready.begin());
            const auto K_INDEX = selected.at(K_ID);
            const auto& node = graph.m_products[K_INDEX];
            const auto K_MANUAL =
                node.m_manual_action_sha256.has_value() ||
                std::ranges::any_of(node.m_dependencies, [&](const auto& parent) {
                    return graph.m_products[selected.at(parent.m_product)].m_manual_ancestor;
                });
            if (node.m_manual_ancestor != K_MANUAL) {
                return fail(ErrorCode::INVALID_DATA,
                            "Asset dependency manual ancestry is inconsistent");
            }
            result.m_topological_order.push_back(K_ID);
            for (const auto K_CHILD : dependents[K_INDEX]) {
                if (--remaining[K_CHILD] == 0uz) {
                    ready.insert(graph.m_products[K_CHILD].m_product.m_product);
                }
            }
        }
        if (result.m_topological_order.size() != graph.m_products.size()) {
            return fail(ErrorCode::INVALID_DATA, "Asset dependency graph contains a cycle");
        }
        auto products = nlohmann::json::array();
        for (const auto& [K_ID, K_INDEX] : selected) {
            (void)K_ID;
            const auto& node = graph.m_products[K_INDEX];
            auto dependencies = node.m_dependencies;
            std::ranges::sort(dependencies);
            auto parents = nlohmann::json::array();
            for (const auto& dependency : dependencies) {
                parents.push_back(reference_json(dependency));
            }
            products.push_back(
                { { "product", reference_json(node.m_product) },
                 { "dependencies", std::move(parents) },
                 { "recipe_sha256", node.m_recipe_sha256 },
                 { "manual_ancestor", node.m_manual_ancestor },
                 { "manual_action_sha256",
                  node.m_manual_action_sha256 ? nlohmann::json(*node.m_manual_action_sha256)
                                              : nlohmann::json(nullptr) } });
        }
        const nlohmann::json K_CANONICAL {
            { "schema_version", graph.m_schema_version },
            { "world_epoch", graph.m_context.m_world_epoch.m_value },
            { "pose_revision", graph.m_context.m_pose_revision.m_value },
            { "products", std::move(products) } };
        const auto K_BYTES = K_CANONICAL.dump();
        result.m_graph_sha256 = sha256(std::as_bytes(std::span(K_BYTES)));
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Asset dependency admission cancelled");
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset dependency admission allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    "Asset dependency admission failed: " + std::string(exception.what()));
    }
}

}  // namespace world_explorer
