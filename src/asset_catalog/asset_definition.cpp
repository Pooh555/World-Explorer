#include "world_explorer/asset_catalog/asset_definition.hpp"

#include "recipe_json.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <functional>
#include <new>

namespace world_explorer {
namespace {

using namespace asset_recipe_detail;

template <class Enum> std::string label(Enum value, const std::map<Enum, std::string>& names) {
    const auto K_FOUND = names.find(value);
    require(K_FOUND != names.end(),
            "Unsupported asset metadata enum",
            ErrorCode::UNSUPPORTED_OPERATION);
    return K_FOUND->second;
}

Json vector_json(Vec3d value) {
    return Json::array({ value.m_x, value.m_y, value.m_z });
}

Json transform_json(const SimilarityTransform& value) {
    return {
        { "rotation",
         { value.m_rotation.m_w, value.m_rotation.m_x, value.m_rotation.m_y, value.m_rotation.m_z } },
        { "translation", vector_json(value.m_translation) },
        { "scale", value.m_scale } };
}

}  // namespace

struct AdmittedAssetDefinition::Implementation {
    AssetDefinition m_definition;
    AssetDefinitionRef m_reference;
    std::string m_receipt_sha256, m_json;
    std::vector<AssetProvenanceCoverage> m_provenance;
};

AdmittedAssetDefinition::AdmittedAssetDefinition(
    std::shared_ptr<const Implementation> implementation)
    : m_impl(std::move(implementation)) { }

const AssetDefinition& AdmittedAssetDefinition::definition() const {
    return m_impl->m_definition;
}

const AssetDefinitionRef& AdmittedAssetDefinition::reference() const {
    return m_impl->m_reference;
}

const std::string& AdmittedAssetDefinition::receipt_sha256() const {
    return m_impl->m_receipt_sha256;
}

const std::string& AdmittedAssetDefinition::canonical_json() const {
    return m_impl->m_json;
}

const std::vector<AssetProvenanceCoverage>& AdmittedAssetDefinition::provenance() const {
    return m_impl->m_provenance;
}

Result<AdmittedAssetDefinition>
admit_asset_definition(const AssetDefinition& value,
                       const AdmittedAssetReceipt& receipt,
                       const std::vector<AssetProvenanceSelection>& selections,
                       const AssetDependencyContext& expected,
                       const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        require(value.m_schema_version == 1 || value.m_schema_version == 2,
                "Unsupported asset definition schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(value.m_context.m_world_epoch == expected.m_world_epoch &&
                    value.m_context.m_pose_revision == expected.m_pose_revision,
                "Stale asset definition context",
                ErrorCode::RESYNC_REQUIRED);
        require(value.m_asset.m_value && value.m_revision, "Missing asset identity/revision");
        const auto K_PROVENANCE_LIMIT = value.m_schema_version == 1 ? 26uz : 106uz;
        require(!value.m_sources.empty() && value.m_sources.size() <= 64uz &&
                    !value.m_materials.empty() && value.m_materials.size() <= 16uz &&
                    value.m_lods.size() <= 8uz && value.m_provenance.size() <= K_PROVENANCE_LIMIT &&
                    selections.size() <= K_PROVENANCE_LIMIT,
                "Asset definition count budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::map<AssetProductId, std::reference_wrapper<const AssetDependencyNode>> nodes;
        for (const auto& node : receipt.graph().m_products) {
            nodes.emplace(node.m_product.m_product, node);
        }
        bool manual {};
        const auto K_BIND = [&](const AssetRevisionRef& ref) -> Json {
            check_stop(stop);
            const auto K_FOUND = nodes.find(ref.m_product);
            require(K_FOUND != nodes.end() && K_FOUND->second.get().m_product == ref,
                    "Asset product reference is stale or missing",
                    ErrorCode::RESYNC_REQUIRED);
            manual = manual || K_FOUND->second.get().m_manual_ancestor;
            return { { "product_id", ref.m_product.m_value },
                    { "revision", ref.m_revision },
                    { "content_sha256", ref.m_content_sha256 } };
        };
        const auto K_REFS = [&](const std::vector<AssetRevisionRef>& refs) {
            auto result = Json::array();
            std::set<AssetProductId> seen;
            for (const auto& ref : refs) {
                require(seen.insert(ref.m_product).second, "Duplicate asset product role");
                result.push_back(K_BIND(ref));
            }
            return result;
        };
        K_REFS(value.m_sources);
        const auto K_GEOMETRY = K_BIND(value.m_geometry);
        std::map<AssetProductId, AssetRegionKind> targets {
            { value.m_geometry.m_product, AssetRegionKind::GEOMETRY_FACES } };
        std::vector<Json> materials;
        std::set<AssetMaterialVariantId> variants;
        std::set<AssetProductId> material_roots;
        std::map<AssetProductId, AssetLightingMode> target_lighting;
        std::map<AssetProductId, MaterialChannelRole> target_roles;
        std::map<AssetProductId, AssetRevisionRef> channel_provenance;
        static const std::map<AssetLightingMode, std::string> K_LIGHTING {
            { AssetLightingMode::CAPTURED_APPEARANCE, "CAPTURED_APPEARANCE" },
            { AssetLightingMode::ESTIMATED_PBR, "ESTIMATED_PBR" },
            { AssetLightingMode::GENERATED_PBR, "GENERATED_PBR" } };
        for (const auto& material : value.m_materials) {
            require(material.m_variant.m_value && variants.insert(material.m_variant).second,
                    "Invalid/duplicate material variant");
            require(material_roots.insert(material.m_product.m_product).second,
                    "Aliased asset output roles");
            Json row { { "variant_id", material.m_variant.m_value },
                      { "product", K_BIND(material.m_product) },
                      { "lighting", label(material.m_lighting, K_LIGHTING) } };
            if (value.m_schema_version == 1) {
                require(material.m_channels.empty(), "Schema 1 cannot carry channel bindings");
                require(
                    targets.emplace(material.m_product.m_product, AssetRegionKind::MATERIAL_TEXELS)
                        .second,
                    "Aliased asset output roles");
                target_lighting.emplace(material.m_product.m_product, material.m_lighting);
            } else {
                const bool K_CAPTURED =
                    material.m_lighting == AssetLightingMode::CAPTURED_APPEARANCE;
                require(material.m_channels.size() == (K_CAPTURED ? 1uz : 6uz),
                        "Schema 2 requires complete channel bindings");
                static const std::map<MaterialChannelRole, std::string> K_ROLES {
                    { MaterialChannelRole::CAPTURED_RGB, "CAPTURED_RGB" },
                    { MaterialChannelRole::BASE_COLOR, "BASE_COLOR" },
                    { MaterialChannelRole::METALLIC, "METALLIC" },
                    { MaterialChannelRole::ROUGHNESS, "ROUGHNESS" },
                    { MaterialChannelRole::NORMAL, "NORMAL" },
                    { MaterialChannelRole::OCCLUSION, "OCCLUSION" },
                    { MaterialChannelRole::EMISSION, "EMISSION" } };
                std::set<MaterialChannelRole> roles;
                std::set<AssetProductId> products;
                std::vector<std::reference_wrapper<const AssetMaterialChannelBinding>> channels;
                channels.reserve(material.m_channels.size());
                for (const auto& channel : material.m_channels) {
                    channels.emplace_back(channel);
                }
                std::ranges::sort(channels, {}, [](const auto& channel) {
                    return channel.get().m_role;
                });
                auto encoded = Json::array();
                const auto& dependencies =
                    nodes.at(material.m_product.m_product).get().m_dependencies;
                for (const auto& selected_channel : channels) {
                    const auto& channel = selected_channel.get();
                    const auto K_ROLE = label(channel.m_role, K_ROLES);
                    require(
                        roles.insert(channel.m_role).second &&
                            products.insert(channel.m_product.m_product).second &&
                            ((K_CAPTURED && channel.m_role == MaterialChannelRole::CAPTURED_RGB) ||
                             (!K_CAPTURED && channel.m_role != MaterialChannelRole::CAPTURED_RGB)),
                        "Invalid/aliased material channel roles");
                    auto product = K_BIND(channel.m_product);
                    auto provenance = K_BIND(channel.m_provenance);
                    require(std::ranges::find(dependencies, channel.m_product) !=
                                    dependencies.end() &&
                                std::ranges::find(dependencies, channel.m_provenance) !=
                                    dependencies.end(),
                            "Material channel/provenance is not a root dependency",
                            ErrorCode::RESYNC_REQUIRED);
                    const auto K_TARGET = targets.find(channel.m_product.m_product);
                    if (K_TARGET == targets.end()) {
                        targets.emplace(channel.m_product.m_product,
                                        AssetRegionKind::MATERIAL_TEXELS);
                        target_lighting.emplace(channel.m_product.m_product, material.m_lighting);
                        target_roles.emplace(channel.m_product.m_product, channel.m_role);
                        channel_provenance.emplace(channel.m_product.m_product,
                                                   channel.m_provenance);
                    } else {
                        require(K_TARGET->second == AssetRegionKind::MATERIAL_TEXELS &&
                                    target_lighting.at(K_TARGET->first) == material.m_lighting &&
                                    target_roles.at(K_TARGET->first) == channel.m_role &&
                                    channel_provenance.at(K_TARGET->first) == channel.m_provenance,
                                "Shared material channel bindings differ");
                    }
                    encoded.push_back(
                        { { "role", K_ROLE }, { "product", product }, { "provenance", provenance } });
                }
                row["channels"] = std::move(encoded);
            }
            materials.push_back(std::move(row));
        }
        if (value.m_schema_version == 2) {
            for (auto root : material_roots) {
                require(!targets.contains(root), "Material roots alias payload roles");
            }
        }
        const auto K_LODS = K_REFS(value.m_lods);
        for (const auto& lod : value.m_lods) {
            require(!material_roots.contains(lod.m_product) &&
                        targets.emplace(lod.m_product, AssetRegionKind::GEOMETRY_FACES).second,
                    "Aliased asset LOD");
        }
        Json collider = nullptr;
        if (value.m_collider.has_value()) {
            const auto& selected = value.m_collider.value();
            require(
                !material_roots.contains(selected.m_product.m_product) &&
                    targets.emplace(selected.m_product.m_product, AssetRegionKind::GEOMETRY_FACES)
                        .second,
                "Aliased asset collider");
            static const std::map<AssetColliderMode, std::string> K_MODES {
                { AssetColliderMode::UNVERIFIED, "UNVERIFIED" },
                { AssetColliderMode::SYNTHETIC_GAMEPLAY, "SYNTHETIC_GAMEPLAY" } };
            collider = { { "product", K_BIND(selected.m_product) },
                        { "mode", label(selected.m_mode, K_MODES) } };
        }
        Json measurement = nullptr;
        Json meters = nullptr;
        if (value.m_units.m_status == AssetUnitStatus::ARBITRARY) {
            require(!value.m_units.m_measurement.has_value() &&
                        !value.m_units.m_meters_per_unit.has_value(),
                    "Arbitrary units claim a metric conversion");
        } else {
            require(value.m_units.m_status == AssetUnitStatus::DECLARED_METRIC,
                    "Unsupported asset units",
                    ErrorCode::UNSUPPORTED_OPERATION);
            if (!value.m_units.m_measurement.has_value() ||
                !value.m_units.m_meters_per_unit.has_value()) {
                return fail(ErrorCode::INVALID_DATA, "Missing declared metric identity");
            }
            const auto K_METERS = value.m_units.m_meters_per_unit.value();
            require(std::isfinite(K_METERS) && K_METERS > 0, "Invalid declared metric conversion");
            measurement = K_BIND(value.m_units.m_measurement.value());
            meters = K_METERS;
        }
        require(valid(value.m_bounds) && (value.m_bounds.m_minimum != value.m_bounds.m_maximum),
                "Invalid asset bounds");
        require(valid(value.m_pivot.m_asset_from_geometry) &&
                    value.m_pivot.m_asset_from_geometry.m_scale == 1,
                "Asset pivot must be a finite rigid transform");
        Json action = nullptr;
        static const std::map<AssetPivotPolicy, std::string> K_POLICIES {
            { AssetPivotPolicy::SUPPORT_BASE_CENTER, "SUPPORT_BASE_CENTER" },
            { AssetPivotPolicy::ATTACHMENT_ORIGIN, "ATTACHMENT_ORIGIN" },
            { AssetPivotPolicy::STRUCTURAL_FRAME, "STRUCTURAL_FRAME" },
            { AssetPivotPolicy::OVERRIDE, "OVERRIDE" } };
        const auto K_POLICY = label(value.m_pivot.m_policy, K_POLICIES);
        if (value.m_pivot.m_policy == AssetPivotPolicy::OVERRIDE) {
            if (!value.m_pivot.m_override_action.has_value()) {
                return fail(ErrorCode::INVALID_DATA, "Pivot override has no declared action");
            }
            const auto& selected_action = value.m_pivot.m_override_action.value();
            action = K_BIND(selected_action);
            require(nodes.at(selected_action.m_product).get().m_manual_ancestor,
                    "Pivot override action has no declared manual ancestry");
        } else {
            require(!value.m_pivot.m_override_action.has_value(),
                    "Automatic pivot has an override action");
            require((value.m_kind == AssetKind::STRUCTURAL &&
                     value.m_pivot.m_policy == AssetPivotPolicy::STRUCTURAL_FRAME) ||
                        (value.m_kind == AssetKind::PROP &&
                         value.m_pivot.m_policy != AssetPivotPolicy::STRUCTURAL_FRAME),
                    "Asset pivot policy differs from kind");
        }
        static const std::map<AssetKind, std::string> K_KINDS {
            { AssetKind::PROP, "PROP" }, { AssetKind::STRUCTURAL, "STRUCTURAL" } };
        const auto K_KIND = label(value.m_kind, K_KINDS);
        const auto K_RIGHTS = K_BIND(value.m_rights);
        K_REFS(value.m_provenance);
        require(value.m_provenance.size() == targets.size() && selections.size() == targets.size(),
                "Missing/extra asset provenance selections");
        std::map<AssetProductId, std::size_t> selected;
        for (std::size_t index { 0uz }; index < selections.size(); ++index) {
            require(selected.emplace(selections[index].m_product.m_product, index).second,
                    "Duplicate independent provenance selection",
                    ErrorCode::INVALID_ARGUMENT);
        }
        std::set<AssetProductId> covered;
        std::vector<AssetProvenanceCoverage> coverage;
        for (const auto& ref : value.m_provenance) {
            const auto K_FOUND = selected.find(ref.m_product);
            require(K_FOUND != selected.end(),
                    "Unselected asset provenance",
                    ErrorCode::INVALID_ARGUMENT);
            const auto& selection = selections[K_FOUND->second];
            require(selection.m_product == ref,
                    "Stale provenance selection",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_TARGET = targets.find(selection.m_region.m_target.m_product);
            require(K_TARGET != targets.end() &&
                        K_TARGET->second == selection.m_region.m_domain.m_kind &&
                        covered.insert(K_TARGET->first).second,
                    "Missing/aliased provenance target or wrong domain");
            if (channel_provenance.contains(K_TARGET->first)) {
                require(channel_provenance.at(K_TARGET->first) == ref,
                        "Material channel provenance selection differs",
                        ErrorCode::RESYNC_REQUIRED);
            }
            const auto K_REGION = inspect_asset_region_provenance(
                receipt, ref.m_product, selection.m_region, expected, stop);
            if (!K_REGION) {
                return std::unexpected(K_REGION.error());
            }
            if (K_TARGET->second == AssetRegionKind::MATERIAL_TEXELS) {
                const auto K_LIGHTING_MODE = target_lighting.at(K_TARGET->first);
                const auto& counts = K_REGION->m_coverage;
                if (K_LIGHTING_MODE == AssetLightingMode::CAPTURED_APPEARANCE) {
                    require(counts[2uz] == 0 && counts[3uz] == 0 && counts[4uz] == 0,
                            "Captured appearance contains inferred/generated/edited texels");
                } else if (K_LIGHTING_MODE == AssetLightingMode::ESTIMATED_PBR) {
                    require(counts[3uz] == 0 && counts[4uz] == 0,
                            "Estimated PBR contains generated/edited texels");
                }
            }
            coverage.push_back({ ref, K_REGION->m_coverage, K_REGION->m_manual_ancestor_elements });
        }
        require(value.m_manual_ancestor == manual,
                "Asset definition clears or invents declared manual ancestry");
        // Every caller-owned string has been bound to a bounded admitted identity.
        auto owned = value;
        std::ranges::sort(owned.m_sources);
        std::ranges::sort(owned.m_provenance);
        std::ranges::sort(owned.m_materials, {}, &AssetMaterialBinding::m_variant);
        for (auto& material : owned.m_materials) {
            std::ranges::sort(material.m_channels, {}, &AssetMaterialChannelBinding::m_role);
        }
        std::ranges::sort(coverage, {}, [](const auto& item) {
            return item.m_product.m_product;
        });
        auto sorted_sources = K_REFS(owned.m_sources);
        auto sorted_provenance = K_REFS(owned.m_provenance);
        std::ranges::sort(materials, {}, [](const Json& item) {
            return integer(item.at("variant_id"));
        });
        const Json K_DOCUMENT {
            { "schema_version", owned.m_schema_version },
            { "asset_id", owned.m_asset.m_value },
            { "revision", owned.m_revision },
            { "world_epoch", expected.m_world_epoch.m_value },
            { "pose_revision", expected.m_pose_revision.m_value },
            { "receipt_sha256", receipt.receipt_sha256() },
            { "kind", K_KIND },
            { "sources", sorted_sources },
            { "geometry", K_GEOMETRY },
            { "materials", materials },
            { "lods", K_LODS },
            { "collider", collider },
            { "units",
             { { "status", meters.is_null() ? "ARBITRARY" : "DECLARED_METRIC" },
              { "meters_per_unit", meters },
              { "measurement", measurement } } },
            { "bounds",
             { { "minimum", vector_json(owned.m_bounds.m_minimum) },
              { "maximum", vector_json(owned.m_bounds.m_maximum) } } },
            { "pivot",
             { { "policy", K_POLICY },
              { "asset_from_geometry", transform_json(owned.m_pivot.m_asset_from_geometry) },
              { "orientation_ambiguous", owned.m_pivot.m_orientation_ambiguous },
              { "override_action", action } } },
            { "provenance", sorted_provenance },
            { "rights", K_RIGHTS },
            { "manual_ancestor", manual } };
        auto text = K_DOCUMENT.dump();
        require(text.size() <= 64uz * 1024uz,
                "Asset definition encoding exceeds budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        const AssetDefinitionRef K_REFERENCE {
            value.m_asset, value.m_revision, sha256(std::as_bytes(std::span(text))) };
        check_stop(stop);
        return AdmittedAssetDefinition(
            std::make_shared<const AdmittedAssetDefinition::Implementation>(
                AdmittedAssetDefinition::Implementation { std::move(owned),
                                                         K_REFERENCE,
                                                         receipt.receipt_sha256(),
                                                         std::move(text),
                                                         std::move(coverage) }));
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset definition allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
