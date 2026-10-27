#include "world_explorer/application/native_assets.hpp"

#include "../core/json_records.hpp"

#include <limits>

namespace world_explorer {
namespace {

using namespace json_record_detail;

template <class Value> Value take(Result<Value> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

template <class Value> const Value& selected(const std::optional<Value>& value) {
    if (!value) {
        throw RecordFailure(ErrorCode::INVALID_ARGUMENT, "Native asset selection missing");
    }
    return *value;
}

void context(const AssetDependencyContext& actual, const AssetDependencyContext& expected) {
    require(actual.m_world_epoch == expected.m_world_epoch &&
                actual.m_pose_revision == expected.m_pose_revision,
            "Native asset context differs",
            ErrorCode::RESYNC_REQUIRED);
}

AssetRegionInspection region(const NativeAssetSelection& asset,
                             const AssetRevisionRef& target,
                             const AdmittedAssetReceipt& receipt,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop) {
    const auto K_FOUND = std::ranges::find_if(asset.m_provenance, [&](const auto& item) {
        return item.m_region.m_target == target;
    });
    require(K_FOUND != asset.m_provenance.end(), "Native asset provenance target missing");
    return take(inspect_asset_region_provenance(
        receipt, K_FOUND->m_product.m_product, K_FOUND->m_region, expected, stop));
}

bool same_span(const AssetRegionSpan& a, const AssetRegionSpan& b) {
    return a.m_first == b.m_first && a.m_count == b.m_count && a.m_class == b.m_class &&
           a.m_confidence == b.m_confidence && a.m_parents == b.m_parents &&
           a.m_support == b.m_support && a.m_manual_ancestor == b.m_manual_ancestor;
}

void face_provenance(const NativeAssetSelection& asset,
                     const AdmittedGeometryProduct& geometry,
                     const AdmittedAssetReceipt& receipt,
                     const AssetDependencyContext& expected,
                     const std::stop_token& stop) {
    const auto K_REGION = region(asset, geometry.selection().m_product, receipt, expected, stop);
    const auto& record = K_REGION.m_record;
    require(record.m_domain == AssetRegionDomain { AssetRegionKind::GEOMETRY_FACES,
                                                  geometry.mesh().m_triangles.size(),
                                                  0,
                                                  0 } &&
                K_REGION.m_coverage == geometry.coverage(),
            "Native asset face domain or coverage differs");
    // Source calibration/grid SHA declarations remain metadata, as in native
    // material admission; image/calibration/pose identities bind actual views.
    require(record.m_sources.size() == geometry.selection().m_views.size(),
            "Native asset face source table differs");
    for (const auto& source : record.m_sources) {
        require(std::ranges::any_of(
                    geometry.selection().m_views,
                    [&](const auto& view) {
                        return source.m_view == AssetSourceViewId { view.m_capture, view.m_frame } &&
                               source.m_calibration == view.m_calibration &&
                               source.m_image == view.m_image &&
                               source.m_pose_sha256 == view.m_pose_asset.m_content_sha256;
                    }),
                "Native asset face source differs",
                ErrorCode::RESYNC_REQUIRED);
    }
    for (const auto& span : record.m_spans) {
        check_stop(stop);
        require(!span.m_confidence && span.m_parents == std::vector { geometry.selection().m_source },
                "Native face confidence or parent binding unsupported",
                ErrorCode::UNSUPPORTED_OPERATION);
        for (auto face = span.m_first; face < span.m_first + span.m_count; ++face) {
            const auto& support = geometry.support().at(static_cast<std::size_t>(face));
            auto views = support.m_views;
            std::ranges::sort(views);
            require(span.m_class == support.m_class && span.m_support == views,
                    "Native asset face provenance differs from correspondence");
        }
    }
}

void materials(const NativeAssetSelection& asset,
               const AdmittedAssetReceipt& receipt,
               const AssetDependencyContext& expected,
               const std::stop_token& stop) {
    const auto& definition = asset.m_definition;
    const auto& geometry = selected(asset.m_geometry);
    require(!asset.m_materials.empty() && asset.m_materials.size() <= 16uz &&
                asset.m_materials.size() == definition.m_materials.size(),
            "Native asset material count differs",
            ErrorCode::INVALID_ARGUMENT);
    std::set<AssetMaterialVariantId> variants;
    for (const auto& material : asset.m_materials) {
        check_stop(stop);
        const auto& selected = material.selection();
        context(material.context(), expected);
        require(material.receipt_sha256() == receipt.receipt_sha256() &&
                    material.geometry_product() == geometry.selection().m_product,
                "Native asset material geometry/receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_BINDING = std::ranges::find(
            definition.m_materials, selected.m_variant, &AssetMaterialBinding::m_variant);
        require(variants.insert(selected.m_variant).second &&
                    K_BINDING != definition.m_materials.end() &&
                    K_BINDING->m_product == selected.m_product &&
                    K_BINDING->m_lighting == selected.m_lighting,
                "Native asset material variant/product/lighting differs",
                ErrorCode::RESYNC_REQUIRED);
        if (definition.m_schema_version == 2) {
            require(K_BINDING->m_channels.size() == selected.m_channels.size(),
                    "Native material channel table differs",
                    ErrorCode::RESYNC_REQUIRED);
            for (const auto& channel : K_BINDING->m_channels) {
                const auto K_PIN = selected.m_channels.find(channel.m_role);
                require(K_PIN != selected.m_channels.end() &&
                            channel.m_product == K_PIN->second.m_product &&
                            channel.m_provenance == K_PIN->second.m_provenance,
                        "Native material channel identity differs",
                        ErrorCode::RESYNC_REQUIRED);
                const auto K_REGION = region(asset, channel.m_product, receipt, expected, stop);
                const auto& original = material.channels().at(channel.m_role).m_provenance.m_record;
                const auto& actual = K_REGION.m_record;
                require(actual.m_target == original.m_target &&
                            actual.m_domain == original.m_domain &&
                            actual.m_sources == original.m_sources &&
                            actual.m_manual_ancestor == original.m_manual_ancestor &&
                            actual.m_spans.size() == original.m_spans.size() &&
                            std::ranges::equal(actual.m_spans, original.m_spans, same_span),
                        "Native material channel provenance differs",
                        ErrorCode::RESYNC_REQUIRED);
            }
        } else {
            const auto K_AGGREGATE = region(asset, selected.m_product, receipt, expected, stop);
            for (const auto& [K_ROLE, K_CHANNEL] : material.channels()) {
                const auto& original = K_CHANNEL.m_provenance.m_record;
                const auto& summary = K_AGGREGATE.m_record;
                require(summary.m_domain == original.m_domain &&
                            summary.m_sources == original.m_sources &&
                            summary.m_manual_ancestor == original.m_manual_ancestor &&
                            summary.m_spans.size() == original.m_spans.size() &&
                            std::ranges::equal(summary.m_spans, original.m_spans, same_span),
                        "Native material summary differs from channel provenance",
                        ErrorCode::UNSUPPORTED_OPERATION);
            }
        }
    }
    const std::set<AssetMaterialVariantId> K_GROUPS(geometry.mesh().m_groups.begin(),
                                                    geometry.mesh().m_groups.end());
    require(K_GROUPS == variants, "Native asset has missing or unused material groups");
}

void bounds(const NativeAssetSelection& asset, const std::stop_token& stop) {
    const auto& definition = asset.m_definition;
    const auto& pivot = definition.m_pivot.m_asset_from_geometry;
    const auto& q = pivot.m_rotation;
    require(std::abs(q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z - 1) <= 1e-12,
            "Native asset pivot quaternion precision unsupported",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_INFINITY = std::numeric_limits<double>::infinity();
    Bounds3d computed { { K_INFINITY, K_INFINITY, K_INFINITY },
                       { -K_INFINITY, -K_INFINITY, -K_INFINITY } };
    for (auto position : selected(asset.m_geometry).mesh().m_positions) {
        check_stop(stop);
        position = transform_point(pivot, position);
        require(finite(position), "Native asset pivot overflows");
        computed.m_minimum = { std::min(computed.m_minimum.m_x, position.m_x),
                              std::min(computed.m_minimum.m_y, position.m_y),
                              std::min(computed.m_minimum.m_z, position.m_z) };
        computed.m_maximum = { std::max(computed.m_maximum.m_x, position.m_x),
                              std::max(computed.m_maximum.m_y, position.m_y),
                              std::max(computed.m_maximum.m_z, position.m_z) };
    }
    require(computed.m_minimum == definition.m_bounds.m_minimum &&
                computed.m_maximum == definition.m_bounds.m_maximum,
            "Native asset bounds differ from pivoted vertices");
}

void source_placement(const NativeAssetSelection& asset,
                      const AdmittedAssetInstances& instances,
                      const std::stop_token& stop) {
    const auto K_INSTANCE = std::ranges::find(
        instances.instances().m_instances, asset.m_source_instance, &AssetInstance::m_instance);
    require(
        asset.m_source_instance.m_value && K_INSTANCE != instances.instances().m_instances.end() &&
            K_INSTANCE->m_definition.m_asset == asset.m_definition.m_asset && K_INSTANCE->m_visible,
        "Native asset original instance missing or foreign",
        ErrorCode::INVALID_ARGUMENT);
    const auto& geometry = selected(asset.m_geometry);
    const auto& placement = K_INSTANCE->m_world_from_asset;
    const auto& pivot = asset.m_definition.m_pivot.m_asset_from_geometry;
    const auto& source = geometry.world_from_geometry();
    const auto K_PRECISE = [](Quatd q) {
        return std::abs(q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z - 1) <= 1e-12;
    };
    require(K_PRECISE(placement.m_rotation) && K_PRECISE(source.m_rotation) &&
                std::abs(placement.m_scale - source.m_scale) <= 1e-12 * source.m_scale,
            "Native asset original rotation precision or scale differs",
            ErrorCode::RESYNC_REQUIRED);
    for (auto axis : { Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 } }) {
        const auto K_DIFFERENCE = rotate(placement.m_rotation, rotate(pivot.m_rotation, axis)) -
                                  rotate(source.m_rotation, axis);
        require(std::abs(K_DIFFERENCE.m_x) <= 1e-10 && std::abs(K_DIFFERENCE.m_y) <= 1e-10 &&
                    std::abs(K_DIFFERENCE.m_z) <= 1e-10,
                "Native asset pivot changes original orientation",
                ErrorCode::RESYNC_REQUIRED);
    }
    const auto K_CLOSE = [](Vec3d a, Vec3d b) {
        const auto K_LIMIT =
            1e-10 * std::max({ 1.0, std::abs(b.m_x), std::abs(b.m_y), std::abs(b.m_z) });
        return finite(a) && finite(b) && std::abs(a.m_x - b.m_x) <= K_LIMIT &&
               std::abs(a.m_y - b.m_y) <= K_LIMIT && std::abs(a.m_z - b.m_z) <= K_LIMIT;
    };
    // Frame probes constrain orientation/scale even for planar meshes. Include
    // every vertex to detect overflow and verify the actual preserved placement.
    for (auto position : { Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 } }) {
        require(K_CLOSE(transform_point(placement, transform_point(pivot, position)),
                        transform_point(source, position)),
                "Native asset pivot changes original world frame",
                ErrorCode::RESYNC_REQUIRED);
    }
    for (auto position : geometry.mesh().m_positions) {
        check_stop(stop);
        require(K_CLOSE(transform_point(placement, transform_point(pivot, position)),
                        transform_point(source, position)),
                "Native asset pivot changes original vertices",
                ErrorCode::RESYNC_REQUIRED);
    }
}

std::string definition_key(const AssetDefinitionRef& ref) {
    return "asset-" + std::to_string(ref.m_asset.m_value) + "-" + std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedNativeAssets::Implementation {
    AdmittedAssetInstances m_instances;
    std::vector<NativeAssetSelection> m_sources;
    std::string m_receipt;
    bool m_synthetic {};
};

AdmittedNativeAssets::AdmittedNativeAssets(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const AdmittedAssetInstances& AdmittedNativeAssets::instances() const {
    return m_state->m_instances;
}

const std::vector<NativeAssetSelection>& AdmittedNativeAssets::sources() const {
    return m_state->m_sources;
}

const std::string& AdmittedNativeAssets::receipt_sha256() const {
    return m_state->m_receipt;
}

bool AdmittedNativeAssets::synthetic_source() const {
    return m_state->m_synthetic;
}

Result<AdmittedNativeAssets> inspect_native_assets(const NativeAssetsRequest& request,
                                                   const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(!request.m_assets.empty() && request.m_assets.size() <= 16uz &&
                    !request.m_instances.m_instances.empty() &&
                    request.m_instances.m_instances.size() <= 64uz,
                "Native asset/instance budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const auto& receipt = selected(request.m_receipt);
        const auto& expected = request.m_expected;
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        std::vector<AdmittedAssetDefinition> definitions;
        bool synthetic {};
        for (const auto& asset : request.m_assets) {
            check_stop(stop);
            const auto& geometry = selected(asset.m_geometry);
            const auto& definition = asset.m_definition;
            context(geometry.context(), expected);
            require(geometry.receipt_sha256() == receipt.receipt_sha256() &&
                        definition.m_geometry == geometry.selection().m_product,
                    "Native asset geometry/receipt differs",
                    ErrorCode::RESYNC_REQUIRED);
            require(definition.m_units.m_status == AssetUnitStatus::ARBITRARY &&
                        !definition.m_collider,
                    "Native asset units/collider profile unsupported",
                    ErrorCode::UNSUPPORTED_OPERATION);
            std::vector<AssetRevisionRef> sources { geometry.capture_product(),
                                                   geometry.selection().m_source };
            require(asset.m_lods.size() == definition.m_lods.size() && asset.m_lods.size() <= 8uz,
                    "Native asset independently admitted LOD bindings missing");
            auto previous_faces = geometry.mesh().m_triangles.size();
            for (std::size_t level { 0uz }; level < asset.m_lods.size(); ++level) {
                check_stop(stop);
                const auto& lod = asset.m_lods[level];
                require(lod.receipt_sha256() == receipt.receipt_sha256() &&
                            lod.base().selection().m_product == geometry.selection().m_product &&
                            lod.candidate().selection().m_product == definition.m_lods[level] &&
                            lod.candidate().mesh().m_triangles.size() < previous_faces,
                        "Native asset LOD order, base or receipt differs",
                        ErrorCode::RESYNC_REQUIRED);
                previous_faces = lod.candidate().mesh().m_triangles.size();
                sources.push_back(lod.selection().m_product);
                face_provenance(asset, lod.candidate(), receipt, expected, stop);
            }
            if (asset.m_completion) {
                const auto& completion = selected(asset.m_completion);
                context(completion.context(), expected);
                require(
                    completion.receipt_sha256() == receipt.receipt_sha256() &&
                        completion.selected_geometry().selection().m_product ==
                            geometry.selection().m_product &&
                        ((completion.selection().m_subject == CompletionSubject::OBJECT_GEOMETRY &&
                          definition.m_kind == AssetKind::PROP) ||
                         (completion.selection().m_subject == CompletionSubject::SHELL_GEOMETRY &&
                          definition.m_kind == AssetKind::STRUCTURAL)),
                    "Native asset completion selection/subject differs",
                    ErrorCode::RESYNC_REQUIRED);
                sources.push_back(completion.selection().m_product);
            }
            require(definition.m_sources.size() == sources.size() &&
                        std::ranges::is_permutation(definition.m_sources, sources),
                    "Native asset source/completion/LOD refs differ",
                    ErrorCode::RESYNC_REQUIRED);
            auto admitted = take(
                admit_asset_definition(definition, receipt, asset.m_provenance, expected, stop));
            face_provenance(asset, geometry, receipt, expected, stop);
            materials(asset, receipt, expected, stop);
            bounds(asset, stop);
            synthetic = synthetic || geometry.synthetic_source();
            definitions.push_back(std::move(admitted));
        }
        auto instances =
            take(admit_asset_instances(request.m_instances, definitions, receipt, expected, stop));
        for (const auto& item : instances.instances().m_instances) {
            require(!item.m_room && !item.m_support && item.m_lineage.empty(),
                    "Native room/support/lineage adoption unsupported",
                    ErrorCode::UNSUPPORTED_OPERATION);
        }
        for (const auto& asset : request.m_assets) {
            source_placement(asset, instances, stop);
        }
        check_stop(stop);
        return AdmittedNativeAssets(std::make_shared<const AdmittedNativeAssets::Implementation>(
            AdmittedNativeAssets::Implementation {
                instances, request.m_assets, receipt.receipt_sha256(), synthetic }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native assets allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_native_assets(const AdmittedNativeAssets& assets,
                                           const AdmittedAssetReceipt& receipt,
                                           const std::filesystem::path& directory,
                                           OperationId operation,
                                           std::uint64_t base_generation,
                                           const AssetDependencyContext& expected,
                                           const std::stop_token& stop,
                                           const std::function<void(std::size_t)>& observed) {
    if (assets.receipt_sha256() != receipt.receipt_sha256()) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Native asset publication receipt differs");
    }
    return publish_asset_instances(assets.instances(),
                                   receipt,
                                   directory,
                                   operation,
                                   base_generation,
                                   expected,
                                   stop,
                                   observed);
}

Result<AdmittedNativeAssets> read_native_assets(const std::filesystem::path& directory,
                                                const RecordCommit& commit,
                                                const NativeAssetsRequest& request,
                                                const std::stop_token& stop) {
    try {
        auto admitted = take(inspect_native_assets(request, stop));
        const auto K_SAVED = take(read_immutable_records(directory, stop));
        require(K_SAVED.m_commit.m_generation == commit.m_generation &&
                    K_SAVED.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Native asset selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_MATCH = [&](const std::string& key, const std::string& json) {
            const auto K_FOUND = K_SAVED.m_records.find(key);
            require(K_FOUND != K_SAVED.m_records.end() && same_bytes(K_FOUND->second, json),
                    "Native asset committed metadata differs",
                    ErrorCode::RESYNC_REQUIRED);
        };
        for (const auto& definition : admitted.instances().definitions()) {
            K_MATCH(definition_key(definition.reference()), definition.canonical_json());
        }
        const auto& instances = admitted.instances().instances();
        K_MATCH("scene-" + std::to_string(instances.m_scene.m_value) + "-" +
                    std::to_string(instances.m_revision.m_value),
                admitted.instances().canonical_json());
        check_stop(stop);
        return admitted;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native assets reload allocation failed");
    }
}

}  // namespace world_explorer
