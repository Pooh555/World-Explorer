#include "world_explorer/asset_catalog/recipe.hpp"

#include "recipe_json.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>

namespace world_explorer {
namespace {

using namespace asset_recipe_detail;

AssetRecipeBudget budget(const Json& value) {
    keys(value,
         { "device",
          "host_ram_bytes",
          "device_vram_bytes",
          "scratch_bytes",
          "wall_time_ms",
          "cpu_threads",
          "queued_jobs",
          "batch_size",
          "candidate_limit",
          "width",
          "height" });
    static const std::map<std::string, AssetRecipeDevice> K_DEVICES {
        { "CPU", AssetRecipeDevice::CPU }, { "CUDA", AssetRecipeDevice::CUDA } };
    constexpr auto K_MAX = std::numeric_limits<std::uint64_t>::max();
    AssetRecipeBudget result { named(value.at("device"), K_DEVICES),
                              count(value.at("host_ram_bytes"), K_MAX),
                              integer(value.at("device_vram_bytes")),
                              count(value.at("scratch_bytes"), K_MAX),
                              count(value.at("wall_time_ms"), 604800000),
                              count(value.at("cpu_threads"), 1024),
                              count(value.at("queued_jobs"), 64),
                              count(value.at("batch_size"), 4096),
                              count(value.at("candidate_limit"), 1000000),
                              count(value.at("width"), 32768),
                              count(value.at("height"), 32768) };
    require((result.m_device == AssetRecipeDevice::CPU) == (result.m_device_vram_bytes == 0),
            "Recipe device and VRAM declaration disagree");
    return result;
}

}  // namespace

Result<AssetRecipeInspection> inspect_asset_recipe(const AdmittedAssetReceipt& receipt,
                                                   AssetProductId product,
                                                   const AssetDependencyContext& expected,
                                                   const AssetRecipeHost& host,
                                                   const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        require(product.m_value != 0, "Recipe product is unselected", ErrorCode::INVALID_ARGUMENT);
        const auto K_NODE =
            std::ranges::find(receipt.graph().m_products, product, [](const auto& node) {
                return node.m_product.m_product;
            });
        require(K_NODE != receipt.graph().m_products.end(),
                "Unknown recipe product",
                ErrorCode::NOT_FOUND);
        const auto K_BYTES = receipt.copy_blob(K_NODE->m_recipe_sha256, 64uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_RECIPE = parse(*K_BYTES);
        keys(K_RECIPE,
             { "schema_version",
              "stage",
              "world_epoch",
              "pose_revision",
              "inputs",
              "configuration_sha256",
              "prompt_sha256",
              "seed",
              "components",
              "budget" });
        require(integer(K_RECIPE.at("schema_version")) == 1,
                "Unsupported recipe schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_RECIPE.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_RECIPE.at("pose_revision")) == expected.m_pose_revision.m_value,
                "Recipe context is stale",
                ErrorCode::RESYNC_REQUIRED);
        const auto& inputs = K_RECIPE.at("inputs");
        require(inputs.is_array() && inputs.size() <= 64uz, "Invalid recipe input count");
        std::vector<AssetRevisionRef> dependencies;
        for (const auto& input : inputs) {
            dependencies.push_back(reference(input));
        }
        auto selected = K_NODE->m_dependencies;
        std::ranges::sort(selected);
        std::ranges::sort(dependencies);
        require(dependencies == selected,
                "Recipe inputs differ from selected dependencies",
                ErrorCode::RESYNC_REQUIRED);
        AssetRecipeInspection result;
        result.m_product = product;
        result.m_context = expected;
        result.m_receipt_sha256 = receipt.receipt_sha256();
        result.m_graph_sha256 = K_GRAPH->m_graph_sha256;
        result.m_recipe_sha256 = K_NODE->m_recipe_sha256;
        result.m_stage = K_RECIPE.at("stage").get<std::string>();
        require(!result.m_stage.empty() && result.m_stage.size() <= 64uz &&
                    result.m_stage.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") ==
                        std::string::npos,
                "Invalid recipe stage name");
        result.m_configuration_sha256 = digest(K_RECIPE.at("configuration_sha256"));
        result.m_prompt_sha256 = digest(K_RECIPE.at("prompt_sha256"));
        result.m_seed = integer(K_RECIPE.at("seed"));
        result.m_budget = budget(K_RECIPE.at("budget"));
        const auto& components = K_RECIPE.at("components");
        require(components.is_array(), "Recipe components must be an array");
        require(components.size() <= 64uz,
                "Recipe component budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        static const std::map<std::string, AssetRecipeRole> K_ROLES {
            { "CODE", AssetRecipeRole::CODE },
            { "ENVIRONMENT", AssetRecipeRole::ENVIRONMENT },
            { "MODEL", AssetRecipeRole::MODEL },
            { "INPUT", AssetRecipeRole::INPUT } };
        std::map<AssetRecipeRole, std::size_t> counts;
        std::set<std::pair<AssetRecipeRole, std::string>> identities;
        std::set<std::string> input_hashes;
        std::set<AssetRecipeBlocker> blockers { AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED };
        std::uint64_t installation {};
        bool known { true };
        for (const auto& item : components) {
            check_stop(stop);
            keys(item, { "role", "sha256", "archive_bytes", "installed_bytes", "rights" });
            const auto& declaration = item.at("rights");
            keys(declaration, { "terms_sha256", "review_sha256", "local_use", "distribution" });
            AssetRecipeComponent component { named(item.at("role"), K_ROLES),
                                            digest(item.at("sha256")),
                                            size(item.at("archive_bytes"), true),
                                            size(item.at("installed_bytes")),
                                            optional_digest(declaration.at("terms_sha256")),
                                            optional_digest(declaration.at("review_sha256")),
                                            rights(declaration.at("local_use")),
                                            rights(declaration.at("distribution")) };
            require(identities.emplace(component.m_role, component.m_sha256).second,
                    "Duplicate recipe component");
            ++counts[component.m_role];
            if (component.m_role == AssetRecipeRole::INPUT) {
                input_hashes.insert(component.m_sha256);
            }
            require(
                (component.m_local_use != AssetRecipeRights::DECLARED_PERMITTED &&
                 component.m_distribution != AssetRecipeRights::DECLARED_PERMITTED) ||
                    (component.m_terms_sha256.has_value() && component.m_review_sha256.has_value()),
                "Permitted rights declaration lacks terms/review identity");
            if (component.m_local_use == AssetRecipeRights::UNREVIEWED) {
                blockers.insert(AssetRecipeBlocker::UNREVIEWED_LOCAL_RIGHTS);
            }
            if (component.m_local_use == AssetRecipeRights::RESTRICTED) {
                blockers.insert(AssetRecipeBlocker::RESTRICTED_LOCAL_RIGHTS);
            }
            if (component.m_distribution != AssetRecipeRights::DECLARED_PERMITTED) {
                blockers.insert(AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED);
            }
            known = known && component.m_archive_bytes.has_value() &&
                    component.m_installed_bytes.has_value();
            installation = add(installation, component.m_archive_bytes.value_or(0));
            installation = add(installation, component.m_installed_bytes.value_or(0));
            result.m_components.push_back(std::move(component));
        }
        require(counts[AssetRecipeRole::CODE] == 1uz &&
                    counts[AssetRecipeRole::ENVIRONMENT] == 1uz &&
                    counts[AssetRecipeRole::INPUT] >= 1uz && counts[AssetRecipeRole::MODEL] <= 16uz,
                "Recipe requires one code/environment, inputs and at most 16 models");
        std::set<std::string> expected_inputs;
        if (selected.empty()) {
            expected_inputs.insert(K_NODE->m_product.m_content_sha256);
        }
        for (const auto& dependency : selected) {
            expected_inputs.insert(dependency.m_content_sha256);
        }
        require(input_hashes == expected_inputs,
                "Recipe input component hashes differ from admitted source products",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_DISK = add(installation, result.m_budget.m_scratch_bytes);
        if (known) {
            result.m_installation_bytes = installation;
            result.m_reserved_disk_bytes = K_DISK;
        } else {
            blockers.insert(AssetRecipeBlocker::UNKNOWN_INSTALLATION_SIZE);
        }
        if (host.m_contracts_only) {
            blockers.insert(AssetRecipeBlocker::CONTRACTS_ONLY_HOST);
        }
        const auto& limits = result.m_budget;
        if (limits.m_host_ram_bytes > host.m_host_ram_bytes ||
            limits.m_device_vram_bytes > host.m_device_vram_bytes ||
            K_DISK > host.m_free_disk_bytes || limits.m_cpu_threads > host.m_cpu_threads ||
            limits.m_queued_jobs > host.m_queued_jobs) {
            blockers.insert(AssetRecipeBlocker::HOST_CAPACITY_EXCEEDED);
        }
        result.m_blockers.assign(blockers.begin(), blockers.end());
        check_stop(stop);
        return result;
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Recipe inspection allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
