#include "world_explorer/scene_store/asset_instances.hpp"

#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {

using Json = nlohmann::json;

struct InstanceFailure : std::runtime_error {
    ErrorCode m_code;

    InstanceFailure(ErrorCode code, const std::string& message)
        : std::runtime_error(message), m_code(code) { }
};

void need(bool value, const std::string& message, ErrorCode code = ErrorCode::INVALID_DATA) {
    if (!value) {
        throw InstanceFailure(code, message);
    }
}

void stopped(const std::stop_token& stop) {
    need(!stop.stop_requested(), "Asset instance admission cancelled", ErrorCode::CANCELLED);
}

Json vector_json(Vec3d value) {
    return Json::array({ value.m_x, value.m_y, value.m_z });
}

}  // namespace

struct AdmittedAssetInstances::Implementation {
    AssetInstanceSet m_instances;
    std::vector<AdmittedAssetDefinition> m_definitions;
    std::string m_json, m_sha256;
};

AdmittedAssetInstances::AdmittedAssetInstances(std::shared_ptr<const Implementation> implementation)
    : m_impl(std::move(implementation)) { }

const AssetInstanceSet& AdmittedAssetInstances::instances() const {
    return m_impl->m_instances;
}

const std::vector<AdmittedAssetDefinition>& AdmittedAssetInstances::definitions() const {
    return m_impl->m_definitions;
}

const std::string& AdmittedAssetInstances::canonical_json() const {
    return m_impl->m_json;
}

const std::string& AdmittedAssetInstances::sha256() const {
    return m_impl->m_sha256;
}

Result<AdmittedAssetInstances>
admit_asset_instances(const AssetInstanceSet& value,
                      const std::vector<AdmittedAssetDefinition>& definitions,
                      const AdmittedAssetReceipt& receipt,
                      const AssetDependencyContext& expected,
                      const std::stop_token& stop) {
    try {
        stopped(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        need(value.m_schema_version == 1,
             "Unsupported instance-set schema",
             ErrorCode::UNSUPPORTED_OPERATION);
        need(value.m_scene.m_value && value.m_revision.m_value, "Missing instance-set identity");
        need(value.m_context.m_world_epoch == expected.m_world_epoch &&
                 value.m_context.m_pose_revision == expected.m_pose_revision,
             "Stale instance-set context",
             ErrorCode::RESYNC_REQUIRED);
        need(definitions.size() <= 64uz && value.m_instances.size() <= 4096uz,
             "Instance-set count budget exceeded",
             ErrorCode::RESOURCE_EXHAUSTED);
        std::map<AssetProductId, std::reference_wrapper<const AssetDependencyNode>> products;
        for (const auto& node : receipt.graph().m_products) {
            products.emplace(node.m_product.m_product, node);
        }
        bool referenced_manual {};
        const auto K_BIND = [&](const AssetRevisionRef& ref) -> Json {
            stopped(stop);
            const auto K_FOUND = products.find(ref.m_product);
            need(K_FOUND != products.end() && K_FOUND->second.get().m_product == ref,
                 "Stale instance product reference",
                 ErrorCode::RESYNC_REQUIRED);
            referenced_manual = referenced_manual || K_FOUND->second.get().m_manual_ancestor;
            return { { "product_id", ref.m_product.m_value },
                    { "revision", ref.m_revision },
                    { "content_sha256", ref.m_content_sha256 } };
        };
        const auto K_OPTIONAL = [&](const std::optional<AssetRevisionRef>& ref) -> Json {
            if (!ref.has_value()) {
                return nullptr;
            }
            return K_BIND(ref.value());
        };
        Json measurement = nullptr;
        Json meters = nullptr;
        if (value.m_units.m_status == AssetUnitStatus::ARBITRARY) {
            need(!value.m_units.m_measurement.has_value() &&
                     !value.m_units.m_meters_per_unit.has_value(),
                 "Arbitrary instance units claim metric conversion");
        } else {
            need(value.m_units.m_status == AssetUnitStatus::DECLARED_METRIC,
                 "Unsupported instance units",
                 ErrorCode::UNSUPPORTED_OPERATION);
            if (!value.m_units.m_measurement.has_value() ||
                !value.m_units.m_meters_per_unit.has_value()) {
                return fail(ErrorCode::INVALID_DATA, "Missing declared instance units");
            }
            const auto K_METERS = value.m_units.m_meters_per_unit.value();
            need(std::isfinite(K_METERS) && K_METERS > 0, "Invalid declared instance conversion");
            measurement = K_BIND(value.m_units.m_measurement.value());
            meters = K_METERS;
        }
        Json action = nullptr;
        if (value.m_manual_action.has_value()) {
            action = K_BIND(value.m_manual_action.value());
            need(products.at(value.m_manual_action.value().m_product).get().m_manual_ancestor,
                 "Instance-set action lacks declared manual ancestry");
        }
        const auto K_SHARED_MANUAL = referenced_manual;
        std::map<AssetId, std::size_t> selected;
        for (std::size_t index { 0uz }; index < definitions.size(); ++index) {
            stopped(stop);
            const auto& definition = definitions[index];
            need(definition.receipt_sha256() == receipt.receipt_sha256(),
                 "Definition uses a different input snapshot",
                 ErrorCode::RESYNC_REQUIRED);
            need(selected.emplace(definition.reference().m_asset, index).second,
                 "Duplicate selected asset ID");
            need(definition.definition().m_units == value.m_units,
                 "Mixed asset/instance unit frames",
                 ErrorCode::RESYNC_REQUIRED);
        }
        std::set<AssetInstanceId> ids;
        std::set<AssetId> used;
        std::size_t lineage_entries {};
        bool manual = K_SHARED_MANUAL;
        for (const auto& instance : value.m_instances) {
            stopped(stop);
            need(instance.m_instance.m_value && ids.insert(instance.m_instance).second,
                 "Invalid/duplicate instance ID");
            const auto K_FOUND = selected.find(instance.m_definition.m_asset);
            need(K_FOUND != selected.end(),
                 "Instance definition is not selected",
                 ErrorCode::RESYNC_REQUIRED);
            const auto& definition = definitions[K_FOUND->second];
            need(definition.reference() == instance.m_definition,
                 "Stale instance definition revision/hash",
                 ErrorCode::RESYNC_REQUIRED);
            const auto& record = definition.definition();
            need(std::ranges::any_of(record.m_materials,
                                     [&](const auto& material) {
                                         return material.m_variant == instance.m_material_variant;
                                     }),
                 "Instance selects an unpermitted material variant");
            need(valid(instance.m_world_from_asset), "Invalid instance transform");
            for (std::size_t corner { 0uz }; corner < 8uz; ++corner) {
                const Vec3d K_POINT {
                    corner & 1uz ? record.m_bounds.m_maximum.m_x : record.m_bounds.m_minimum.m_x,
                    corner & 2uz ? record.m_bounds.m_maximum.m_y : record.m_bounds.m_minimum.m_y,
                    corner & 4uz ? record.m_bounds.m_maximum.m_z : record.m_bounds.m_minimum.m_z };
                need(finite(transform_point(instance.m_world_from_asset, K_POINT)),
                     "Instance transform overflows asset bounds");
            }
            need(instance.m_lineage.size() <= 16uz &&
                     instance.m_lineage.size() <= 16384uz - lineage_entries,
                 "Instance lineage budget exceeded",
                 ErrorCode::RESOURCE_EXHAUSTED);
            lineage_entries += instance.m_lineage.size();
            referenced_manual = K_SHARED_MANUAL || record.m_manual_ancestor;
            K_OPTIONAL(instance.m_room);
            K_OPTIONAL(instance.m_support);
            std::set<std::pair<AssetProductId, AssetInstanceId>> lineage;
            for (const auto& parent : instance.m_lineage) {
                need(parent.m_instance.m_value &&
                         lineage.emplace(parent.m_product.m_product, parent.m_instance).second,
                     "Invalid/duplicate instance lineage");
                K_BIND(parent.m_product);
            }
            need(instance.m_manual_ancestor == referenced_manual,
                 "Instance clears or invents declared manual ancestry");
            manual = manual || referenced_manual;
            used.insert(instance.m_definition.m_asset);
        }
        need(used.size() == definitions.size(), "Unused selected definition");
        need(value.m_manual_ancestor == manual, "Instance-set manual ancestry disagrees");
        auto owned = value;
        auto owned_definitions = definitions;
        std::ranges::sort(owned.m_instances, {}, &AssetInstance::m_instance);
        std::ranges::sort(owned_definitions, {}, [](const auto& item) {
            return item.reference().m_asset;
        });
        auto rows = Json::array();
        const auto K_REF_JSON = [](const AssetDefinitionRef& ref) -> Json {
            return { { "asset_id", ref.m_asset.m_value },
                    { "revision", ref.m_revision },
                    { "sha256", ref.m_sha256 } };
        };
        for (auto& instance : owned.m_instances) {
            stopped(stop);
            std::ranges::sort(instance.m_lineage, {}, [](const auto& item) {
                return std::pair { item.m_product.m_product, item.m_instance };
            });
            auto lineage = Json::array();
            for (const auto& parent : instance.m_lineage) {
                lineage.push_back({ { "instance_id", parent.m_instance.m_value },
                                   { "product", K_BIND(parent.m_product) } });
            }
            const auto& transform = instance.m_world_from_asset;
            rows.push_back({ { "instance_id", instance.m_instance.m_value },
                            { "definition", K_REF_JSON(instance.m_definition) },
                            { "world_from_asset",
                             { { "rotation",
                               { transform.m_rotation.m_w,
                                transform.m_rotation.m_x,
                                transform.m_rotation.m_y,
                                transform.m_rotation.m_z } },
                              { "translation", vector_json(transform.m_translation) },
                              { "scale", transform.m_scale } } },
                            { "material_variant_id", instance.m_material_variant.m_value },
                            { "visible", instance.m_visible },
                            { "room", K_OPTIONAL(instance.m_room) },
                            { "support", K_OPTIONAL(instance.m_support) },
                            { "lineage", lineage },
                            { "manual_ancestor", instance.m_manual_ancestor } });
        }
        auto refs = Json::array();
        for (const auto& definition : owned_definitions) {
            refs.push_back(K_REF_JSON(definition.reference()));
        }
        const Json K_DOCUMENT { { "schema_version", 1 },
                               { "scene_id", value.m_scene.m_value },
                               { "revision", value.m_revision.m_value },
                               { "world_epoch", expected.m_world_epoch.m_value },
                               { "pose_revision", expected.m_pose_revision.m_value },
                               { "receipt_sha256", receipt.receipt_sha256() },
                               { "definitions", refs },
                               { "instances", rows },
                               { "units",
                                { { "status", meters.is_null() ? "ARBITRARY" : "DECLARED_METRIC" },
                                 { "meters_per_unit", meters },
                                 { "measurement", measurement } } },
                               { "manual_action", action },
                               { "manual_ancestor", manual } };
        auto text = K_DOCUMENT.dump();
        need(text.size() <= 4uz * 1024uz * 1024uz,
             "Instance-set encoding exceeds budget",
             ErrorCode::RESOURCE_EXHAUSTED);
        auto sha = world_explorer::sha256(std::as_bytes(std::span(text)));
        stopped(stop);
        return AdmittedAssetInstances(
            std::make_shared<const AdmittedAssetInstances::Implementation>(
                AdmittedAssetInstances::Implementation { std::move(owned),
                                                        std::move(owned_definitions),
                                                        std::move(text),
                                                        std::move(sha) }));
    } catch (const InstanceFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Instance metadata allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
