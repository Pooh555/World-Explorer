#include "world_explorer/application/native_structure.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>

namespace world_explorer {
namespace {

using namespace json_record_detail;

template <class T> T take(Result<T> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

template <class T> const T& selected(const std::optional<T>& value) {
    if (!value) {
        throw RecordFailure(ErrorCode::INVALID_ARGUMENT, "Native structure input missing");
    }
    return *value;
}

std::vector<std::byte> bytes(const std::string& text) {
    const auto K_VIEW = std::as_bytes(std::span(text));
    return { K_VIEW.begin(), K_VIEW.end() };
}

void context(const AssetDependencyContext& actual, const AssetDependencyContext& expected) {
    require(actual.m_world_epoch == expected.m_world_epoch &&
                actual.m_pose_revision == expected.m_pose_revision,
            "Native structure context differs",
            ErrorCode::RESYNC_REQUIRED);
}

template <class T>
std::vector<T> identities(const std::vector<T>& input, std::size_t limit, std::vector<T> expected) {
    require(input.size() <= limit,
            "Native structure identity budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    auto result = input;
    std::ranges::sort(result);
    std::ranges::sort(expected);
    require(result == expected && std::adjacent_find(result.begin(), result.end()) == result.end(),
            "Native structure identity set differs",
            ErrorCode::RESYNC_REQUIRED);
    return result;
}

template <class T> Json ids(const std::vector<T>& values) {
    auto result = Json::array();
    for (auto value : values) {
        result.push_back(value.m_value);
    }
    return result;
}

Json reference(const AssetRevisionRef& value) {
    return { { "product_id", value.m_product.m_value },
            { "revision", value.m_revision },
            { "content_sha256", value.m_content_sha256 } };
}

Json reference(const AssetDefinitionRef& value) {
    return { { "asset_id", value.m_asset.m_value },
            { "revision", value.m_revision },
            { "sha256", value.m_sha256 } };
}

SimilarityTransform compose(const SimilarityTransform& outer, const SimilarityTransform& inner) {
    const auto& a = outer.m_rotation;
    const auto& b = inner.m_rotation;
    return { { a.m_w * b.m_w - a.m_x * b.m_x - a.m_y * b.m_y - a.m_z * b.m_z,
             a.m_w * b.m_x + a.m_x * b.m_w + a.m_y * b.m_z - a.m_z * b.m_y,
             a.m_w * b.m_y - a.m_x * b.m_z + a.m_y * b.m_w + a.m_z * b.m_x,
             a.m_w * b.m_z + a.m_x * b.m_y - a.m_y * b.m_x + a.m_z * b.m_w },
            transform_point(outer, inner.m_translation),
            outer.m_scale * inner.m_scale };
}

Json transform_json(const SimilarityTransform& value) {
    require(valid(value), "Native structure frame overflows");
    return {
        { "rotation",
         { value.m_rotation.m_w, value.m_rotation.m_x, value.m_rotation.m_y, value.m_rotation.m_z } },
        { "translation",
         { value.m_translation.m_x, value.m_translation.m_y, value.m_translation.m_z } },
        { "scale", value.m_scale } };
}

std::string key(SceneId scene, SceneRevisionId revision) {
    return "native-structure-" + std::to_string(scene.m_value) + "-" +
           std::to_string(revision.m_value);
}

std::vector<ImmutableRecord> publication(const AdmittedNativeStructure& structure) {
    auto records = structure.records();
    const auto& instances = structure.assets().instances();
    records.reserve(records.size() + instances.definitions().size() + 1uz);
    for (const auto& definition : instances.definitions()) {
        const auto& ref = definition.reference();
        records.push_back(
            { "asset-" + std::to_string(ref.m_asset.m_value) + "-" + std::to_string(ref.m_revision),
             bytes(definition.canonical_json()) });
    }
    const auto& scene = instances.instances();
    records.push_back({ "scene-" + std::to_string(scene.m_scene.m_value) + "-" +
                           std::to_string(scene.m_revision.m_value),
                       bytes(instances.canonical_json()) });
    return records;
}

}  // namespace

struct AdmittedNativeStructure::Implementation {
    AdmittedNativeAssets m_assets;
    std::vector<NativeStructureSelection> m_structures;
    std::string m_json, m_sha256;
    bool m_manual {};
    std::vector<ImmutableRecord> m_records;
};

AdmittedNativeStructure::AdmittedNativeStructure(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const AdmittedNativeAssets& AdmittedNativeStructure::assets() const {
    return m_state->m_assets;
}

const std::vector<NativeStructureSelection>& AdmittedNativeStructure::structures() const {
    return m_state->m_structures;
}

const std::string& AdmittedNativeStructure::canonical_json() const {
    return m_state->m_json;
}

const std::string& AdmittedNativeStructure::sha256() const {
    return m_state->m_sha256;
}

bool AdmittedNativeStructure::manual_ancestor() const {
    return m_state->m_manual;
}

const std::vector<ImmutableRecord>& AdmittedNativeStructure::records() const {
    return m_state->m_records;
}

Result<AdmittedNativeStructure> inspect_native_structure(const NativeStructureRequest& request,
                                                         const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto& assets = selected(request.m_assets);
        const auto& receipt = selected(request.m_receipt);
        const auto& instances = assets.instances();
        context(instances.instances().m_context, request.m_expected);
        require(assets.receipt_sha256() == receipt.receipt_sha256(),
                "Native structure original receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        (void)take(validate_asset_dependencies(receipt.graph(), request.m_expected, stop));
        require(!request.m_structures.empty() && request.m_structures.size() <= 16uz,
                "Native structure graph budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::set<AssetId> structural, bound;
        for (const auto& definition : instances.definitions()) {
            if (definition.definition().m_kind == AssetKind::STRUCTURAL) {
                structural.insert(definition.reference().m_asset);
            }
        }
        bool manual = instances.instances().m_manual_ancestor;
        std::map<std::string, std::vector<std::byte>> graph_records;
        std::vector<Json> rows;
        rows.reserve(request.m_structures.size());
        std::vector<NativeStructureSelection> owned;
        owned.reserve(request.m_structures.size());
        for (const auto& input : request.m_structures) {
            check_stop(stop);
            const auto& graph = selected(input.m_graph);
            context(graph.context(), request.m_expected);
            const auto K_DEFINITION = std::ranges::find(
                instances.definitions(), input.m_definition, &AdmittedAssetDefinition::reference);
            require(K_DEFINITION != instances.definitions().end() &&
                        structural.contains(input.m_definition.m_asset) &&
                        bound.insert(input.m_definition.m_asset).second,
                    "Native structure definition missing duplicated or nonstructural",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_SOURCE = std::ranges::find_if(assets.sources(), [&](const auto& asset) {
                return asset.m_definition.m_asset == input.m_definition.m_asset;
            });
            require(K_SOURCE != assets.sources().end(), "Native structure source missing");
            const auto& geometry = selected(K_SOURCE->m_geometry);
            require(graph.receipt_sha256() == receipt.receipt_sha256() &&
                        graph.geometry_product() == geometry.selection().m_product,
                    "Native structure geometry/receipt differs",
                    ErrorCode::RESYNC_REQUIRED);
            NativeStructureSelection output { graph, K_DEFINITION->reference(), {}, {}, {}, {}, {} };
            std::vector<StructureSurfaceId> surfaces;
            surfaces.reserve(graph.surfaces().size());
            for (const auto& surface : graph.surfaces()) {
                surfaces.push_back(surface.m_id);
            }
            output.m_surfaces = identities(input.m_surfaces, 32uz, std::move(surfaces));
            std::vector<StructureOpeningId> openings;
            openings.reserve(graph.openings().size());
            for (const auto& opening : graph.openings()) {
                openings.push_back(opening.m_id);
            }
            output.m_openings = identities(input.m_openings, 64uz, std::move(openings));
            std::vector<StructureRoomId> rooms;
            rooms.reserve(graph.rooms().size());
            for (const auto& room : graph.rooms()) {
                rooms.push_back(room.m_id);
            }
            output.m_rooms = identities(input.m_rooms, 32uz, std::move(rooms));
            std::vector<NativeStructureRelation> relations;
            relations.reserve(graph.relations().size());
            for (const auto& relation : graph.relations()) {
                relations.push_back({ relation.m_kind, relation.m_source, relation.m_target });
            }
            output.m_relations = identities(input.m_relations, 128uz, std::move(relations));
            std::vector<AssetInstanceId> copies;
            copies.reserve(instances.instances().m_instances.size());
            for (const auto& instance : instances.instances().m_instances) {
                if (instance.m_definition == input.m_definition) {
                    require(instance.m_visible,
                            "Hidden structured instances unsupported",
                            ErrorCode::UNSUPPORTED_OPERATION);
                    copies.push_back(instance.m_instance);
                }
            }
            output.m_instances = identities(input.m_instances, 64uz, std::move(copies));
            require(!output.m_instances.empty(), "Native structural definition has no instances");
            auto placements = Json::array(), relation_keys = Json::array();
            for (auto id : output.m_instances) {
                const auto K_INSTANCE = std::ranges::find(
                    instances.instances().m_instances, id, &AssetInstance::m_instance);
                require(K_INSTANCE != instances.instances().m_instances.end(),
                        "Structured instance missing");
                const auto K_WORLD =
                    compose(K_INSTANCE->m_world_from_asset,
                            K_DEFINITION->definition().m_pivot.m_asset_from_geometry);
                for (auto point : geometry.mesh().m_positions) {
                    check_stop(stop);
                    require(finite(transform_point(K_WORLD, point)),
                            "Structured instance vertices overflow");
                }
                placements.push_back(
                    { { "instance_id", id.m_value }, { "world_from_graph", transform_json(K_WORLD) } });
            }
            for (const auto& relation : output.m_relations) {
                relation_keys.push_back(
                    { { "kind",
                      relation.m_kind == StructureRelationKind::CONTACT ? "CONTACT" : "SUPPORT" },
                     { "source", relation.m_source.m_value },
                     { "target", relation.m_target.m_value } });
            }
            const auto& ref = graph.selection().m_product;
            auto original = take(receipt.copy_blob(ref.m_content_sha256, 256uz * 1024uz, stop));
            require(sha256(original) == ref.m_content_sha256, "Native structure root hash differs");
            const auto K_KEY = "structure-" + std::to_string(ref.m_product.m_value) + "-" +
                               std::to_string(ref.m_revision);
            const auto K_PRIOR = graph_records.find(K_KEY);
            require(K_PRIOR == graph_records.end() || K_PRIOR->second == original,
                    "Structured graph semantic revision conflicts");
            graph_records.emplace(K_KEY, std::move(original));
            manual = manual || graph.manual_ancestor();
            rows.push_back(
                { { "definition", reference(output.m_definition) },
                 { "geometry", reference(graph.geometry_product()) },
                 { "graph", reference(ref) },
                 { "graph_record", K_KEY },
                 { "surface_ids", ids(output.m_surfaces) },
                 { "opening_ids", ids(output.m_openings) },
                 { "room_ids", ids(output.m_rooms) },
                 { "relations", relation_keys },
                 { "instances", placements },
                 { "plane_policy_units", graph.selection().m_max_plane_residual_units },
                 { "relation_policy_units", graph.selection().m_max_relation_residual_units },
                 { "manual_ancestor", graph.manual_ancestor() } });
            owned.push_back(std::move(output));
        }
        require(bound == structural,
                "Missing structural definition graph",
                ErrorCode::INVALID_ARGUMENT);
        std::ranges::sort(owned, {}, [](const auto& item) {
            return item.m_definition.m_asset;
        });
        std::ranges::sort(rows, {}, [](const Json& row) {
            return integer(row.at("definition").at("asset_id"));
        });
        const auto& scene = instances.instances();
        const Json K_DOC { { "schema_version", 1 },
                          { "kind", "NATIVE_STRUCTURE_BINDINGS" },
                          { "qualification", "DECLARED_PARTIAL_PATCHES" },
                          { "frame", "GEOMETRY_LOCAL_BEFORE_PIVOT" },
                          { "identity_scope", "SCENE_INSTANCE_GRAPH_ELEMENT" },
                          { "scene_id", scene.m_scene.m_value },
                          { "scene_revision", scene.m_revision.m_value },
                          { "source_instances_sha256", instances.sha256() },
                          { "receipt_sha256", receipt.receipt_sha256() },
                          { "world_epoch", request.m_expected.m_world_epoch.m_value },
                          { "pose_revision", request.m_expected.m_pose_revision.m_value },
                          { "structures", rows },
                          { "manual_ancestor", manual } };
        auto text = K_DOC.dump();
        require(text.size() <= 1024uz * 1024uz,
                "Native structure bindings exceed budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::vector<ImmutableRecord> records;
        records.reserve(graph_records.size() + 1uz);
        for (auto& [K_RECORD_KEY, data] : graph_records) {
            records.push_back({ K_RECORD_KEY, std::move(data) });
        }
        records.push_back({ key(scene.m_scene, scene.m_revision), bytes(text) });
        auto digest = sha256(std::as_bytes(std::span(text)));
        check_stop(stop);
        return AdmittedNativeStructure(
            std::make_shared<const AdmittedNativeStructure::Implementation>(
                AdmittedNativeStructure::Implementation { assets,
                                                         std::move(owned),
                                                         std::move(text),
                                                         std::move(digest),
                                                         manual,
                                                         std::move(records) }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native structure allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_native_structure(const AdmittedNativeStructure& structure,
                                              const AdmittedAssetReceipt& receipt,
                                              const std::filesystem::path& directory,
                                              OperationId operation,
                                              std::uint64_t base_generation,
                                              const AssetDependencyContext& expected,
                                              const std::stop_token& stop,
                                              const std::function<void(std::size_t)>& observed) {
    try {
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        context(structure.assets().instances().instances().m_context, expected);
        require(structure.assets().receipt_sha256() == receipt.receipt_sha256(),
                "Structured publication receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         publication(structure),
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Structured publication allocation failed");
    }
}

Result<AdmittedNativeStructure> read_native_structure(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const NativeStructureRequest& request,
                                                      const std::stop_token& stop) {
    try {
        auto structure = take(inspect_native_structure(request, stop));
        const auto K_SAVED = take(read_immutable_records(directory, stop));
        require(K_SAVED.m_commit.m_generation == commit.m_generation &&
                    K_SAVED.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Structured selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& record : publication(structure)) {
            check_stop(stop);
            const auto K_FOUND = K_SAVED.m_records.find(record.m_key);
            require(K_FOUND != K_SAVED.m_records.end() && K_FOUND->second == record.m_bytes,
                    "Structured committed metadata differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return structure;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Structured reload allocation failed");
    }
}

}  // namespace world_explorer
