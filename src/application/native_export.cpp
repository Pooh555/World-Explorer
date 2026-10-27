#include "../scene_store/gltf_internal.hpp"
#include "native_export_internal.hpp"
#include "world_explorer/core/asset_files.hpp"

namespace world_explorer {
namespace {

using namespace native_export_detail;

Json errors_json(const Errors& errors) {
    return { { "position_units", errors.m_position },
            { "normal", errors.m_normal },
            { "tangent", errors.m_tangent },
            { "uv", errors.m_uv },
            { "linear_color", errors.m_color },
            { "numeric", errors.m_numeric },
            { "normal_map", errors.m_normal_map } };
}

Json policy_json(const NativeExportPolicy& policy) {
    return { { "position_units", policy.m_position_units },
            { "normal", policy.m_normal },
            { "tangent", policy.m_tangent },
            { "uv", policy.m_uv },
            { "linear_color", policy.m_linear_color },
            { "numeric", policy.m_numeric },
            { "normal_map", policy.m_normal_map } };
}

std::vector<ExportPackageFile> build(const NativeExportRequest& request,
                                     const std::stop_token& stop) {
    check_stop(stop);
    const auto& source = selected(request.m_assets);
    const auto& receipt = selected(request.m_receipt);
    const auto& instances = source.instances();
    const auto& expected = request.m_expected;
    require(source.receipt_sha256() == receipt.receipt_sha256() &&
                instances.instances().m_context.m_world_epoch == expected.m_world_epoch &&
                instances.instances().m_context.m_pose_revision == expected.m_pose_revision,
            "Native export original receipt/context differs",
            ErrorCode::RESYNC_REQUIRED);
    (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
    if (request.m_structure) {
        const auto& structured = request.m_structure.value();
        require(structured.assets().receipt_sha256() == receipt.receipt_sha256() &&
                    structured.assets().instances().sha256() == instances.sha256(),
                "Native export structured source differs",
                ErrorCode::RESYNC_REQUIRED);
    }
    require(!instances.definitions().empty() && instances.definitions().size() <= 2uz &&
                instances.instances().m_instances.size() <= 64uz,
            "Native export definition/instance budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::size_t materials {};
    std::map<AssetId, std::size_t> lod_levels;
    if (request.m_lods) {
        const auto& selections = selected(request.m_lods);
        require(selections.size() == instances.definitions().size(),
                "Native LOD selection must cover every definition");
        for (const auto& selection : selections) {
            const auto K_DEFINITION =
                std::ranges::find_if(instances.definitions(), [&](const auto& item) {
                    return item.reference().m_asset == selection.m_asset;
                });
            require(K_DEFINITION != instances.definitions().end() &&
                        selection.m_level <= K_DEFINITION->definition().m_lods.size() &&
                        lod_levels.emplace(selection.m_asset, selection.m_level).second,
                    "Native LOD selection is foreign, duplicated or outside admitted levels");
        }
    }
    for (const auto& definition : instances.definitions()) {
        require(request.m_colliders || !definition.definition().m_collider,
                "Native collider export requires explicit companion selection",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(request.m_lods || definition.definition().m_lods.empty(),
                "Native LOD export requires explicit per-definition selection",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(definition.definition().m_materials.size() <= 4uz - materials,
                "Native export material budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        materials += definition.definition().m_materials.size();
    }
    const auto K_POLICY = policy_json(request.m_policy);
    for (const auto& value : K_POLICY) {
        const auto K_LIMIT = value.get<double>();
        require(std::isfinite(K_LIMIT) && K_LIMIT >= 0 && K_LIMIT <= 1,
                "Native export independent error limit invalid",
                ErrorCode::INVALID_ARGUMENT);
    }
    const auto K_RECHECKED = take(admit_asset_instances(
        instances.instances(), instances.definitions(), receipt, expected, stop));
    require(K_RECHECKED.sha256() == instances.sha256(),
            "Native export source instances differ",
            ErrorCode::RESYNC_REQUIRED);
    for (const auto& instance : instances.instances().m_instances) {
        require(instance.m_visible,
                "Hidden native export instances unsupported",
                ErrorCode::UNSUPPORTED_OPERATION);
        transform_profile(instance.m_world_from_asset);
        const auto K_ASSET = std::ranges::find_if(source.sources(), [&](const auto& item) {
            return item.m_definition.m_asset == instance.m_definition.m_asset;
        });
        require(K_ASSET != source.sources().end(), "Native export instance asset missing");
        const auto K_ORIGINAL = std::ranges::find(instances.instances().m_instances,
                                                  K_ASSET->m_source_instance,
                                                  &AssetInstance::m_instance);
        require(K_ORIGINAL != instances.instances().m_instances.end() &&
                    instance.m_material_variant == K_ORIGINAL->m_material_variant,
                "Native export per-instance material overrides unsupported",
                ErrorCode::UNSUPPORTED_OPERATION);
    }
    Files files;
    if (request.m_structure) {
        const auto& structured = request.m_structure.value();
        files.add("structure/bindings.json", bytes(structured.canonical_json()));
        for (const auto& record : structured.records()) {
            if (record.m_key.starts_with("structure-")) {
                files.add("structure/" + record.m_key.substr(10uz) + ".json", record.m_bytes);
            }
        }
    }
    auto assembly = document();
    auto catalog = Json::array(), mappings = Json::array(), lod_entries = Json::array();
    std::map<AssetId, std::size_t> meshes;
    const auto K_REGION = [&](const AssetRevisionRef& ref) {
        files.add("provenance/" + std::to_string(ref.m_product.m_value) + "-" +
                      std::to_string(ref.m_revision) + ".json",
                  take(receipt.copy_blob(ref.m_content_sha256, 256uz * 1024uz, stop)));
    };
    for (const auto& definition : instances.definitions()) {
        check_stop(stop);
        const auto K_SOURCE = std::ranges::find_if(source.sources(), [&](const auto& item) {
            return item.m_definition.m_asset == definition.reference().m_asset;
        });
        require(K_SOURCE != source.sources().end(), "Native export source asset missing");
        const auto K_CHECK_ERRORS = [&](CompiledAsset& result) {
            const auto K_ERRORS = errors_json(result.m_errors);
            for (auto value = K_ERRORS.begin(); value != K_ERRORS.end(); ++value) {
                require(value.value().get<double>() <= K_POLICY.at(value.key()).get<double>(),
                        "Native export conversion exceeds independently selected error limit",
                        ErrorCode::UNSUPPORTED_OPERATION);
            }
            result.m_mapping["errors"] = K_ERRORS;
        };
        CompiledAsset compiled {};
        if (request.m_lods) {
            auto levels = Json::array();
            for (std::size_t level { 0uz }; level <= K_SOURCE->m_lods.size(); ++level) {
                check_stop(stop);
                auto result =
                    compile_asset(definition,
                                  *K_SOURCE,
                                  instances,
                                  files,
                                  stop,
                                  level,
                                  request.m_colliders ? K_COLLIDER_PROFILE : std::string_view {});
                K_CHECK_ERRORS(result);
                auto& mapping = result.m_mapping;
                mapping["level"] = level;
                const auto& geometry = level == 0uz ? selected(K_SOURCE->m_geometry)
                                                    : K_SOURCE->m_lods[level - 1uz].candidate();
                const auto K_PROVENANCE =
                    std::ranges::find_if(K_SOURCE->m_provenance, [&](const auto& item) {
                        return item.m_region.m_target == geometry.selection().m_product;
                    });
                require(K_PROVENANCE != K_SOURCE->m_provenance.end(),
                        "Native LOD provenance missing");
                mapping["provenance"] = reference(K_PROVENANCE->m_product);
                if (level > 0uz) {
                    const auto& lod = K_SOURCE->m_lods[level - 1uz];
                    const auto K_PATH = "lods/" +
                                        std::to_string(definition.reference().m_asset.m_value) +
                                        "-" + std::to_string(level) + ".json";
                    files.add(K_PATH,
                              take(receipt.copy_blob(lod.selection().m_product.m_content_sha256,
                                                     1024uz * 1024uz,
                                                     stop)));
                    mapping["certificate"] = {
                        { "product", reference(lod.selection().m_product) },
                        { "path", K_PATH },
                        { "sha256", lod.selection().m_product.m_content_sha256 } };
                    mapping["base_source_faces"] = lod.source_faces();
                } else {
                    mapping["certificate"] = nullptr;
                    mapping["base_source_faces"] = Json::array();
                    for (std::size_t face { 0uz }; face < geometry.mesh().m_triangles.size();
                         ++face) {
                        mapping["base_source_faces"].push_back(Json::array({ face }));
                    }
                }
                levels.push_back(mapping);
                if (level == lod_levels.at(definition.reference().m_asset)) {
                    compiled = std::move(result);
                }
            }
            lod_entries.push_back(
                { { "definition", reference(definition.reference()) },
                 { "selected_level", lod_levels.at(definition.reference().m_asset) },
                 { "assembly_mesh_index", assembly["meshes"].size() },
                 { "levels", levels } });
            const auto K_ASSET_PATH =
                "assets/" + std::to_string(definition.reference().m_asset.m_value) + "/asset.gltf";
            files.json(K_ASSET_PATH, compiled.m_document);
            compiled.m_mapping["asset_path"] = K_ASSET_PATH;
            compiled.m_mapping["base_geometry"] = reference(definition.definition().m_geometry);
        } else {
            compiled =
                compile_asset(definition,
                              *K_SOURCE,
                              instances,
                              files,
                              stop,
                              {},
                              request.m_colliders ? K_COLLIDER_PROFILE : std::string_view {});
            K_CHECK_ERRORS(compiled);
        }
        compiled.m_mapping["mesh_index"] = assembly["meshes"].size();
        compiled.m_mapping["completion"] =
            K_SOURCE->m_completion ? reference(K_SOURCE->m_completion->selection().m_product)
                                   : Json(nullptr);
        meshes.emplace(definition.reference().m_asset, assembly["meshes"].size());
        const auto K_PREFIX =
            "assets/" + std::to_string(definition.reference().m_asset.m_value) + "/";
        append_resources(assembly, std::move(compiled.m_document), K_PREFIX);
        mappings.push_back(std::move(compiled.m_mapping));
        catalog.push_back(Json::parse(definition.canonical_json()));
        for (const auto& selection : K_SOURCE->m_provenance) {
            K_REGION(selection.m_product);
        }
        for (const auto& material : K_SOURCE->m_materials) {
            for (const auto& [K_ROLE, K_CHANNEL] : material.selection().m_channels) {
                K_REGION(K_CHANNEL.m_provenance);
            }
        }
        files.json("rights/" + std::to_string(definition.reference().m_asset.m_value) + ".json",
                   { { "product", reference(definition.definition().m_rights) },
                    { "status", "UNREVIEWED_BINDING_ONLY" },
                    { "opaque_payload_exported", false } });
    }
    auto nodes = Json::array(), node_map = Json::array();
    for (const auto& instance : instances.instances().m_instances) {
        const auto& transform = instance.m_world_from_asset;
        const auto& q = transform.m_rotation;
        const auto K_MESH = meshes.at(instance.m_definition.m_asset);
        const auto K_NODE = assembly["nodes"].size();
        assembly["nodes"].push_back(
            { { "mesh", K_MESH },
             { "translation", xyz(transform.m_translation) },
             { "rotation", { q.m_x, q.m_y, q.m_z, q.m_w } },
             { "scale", { transform.m_scale, transform.m_scale, transform.m_scale } } });
        nodes.push_back(K_NODE);
        node_map.push_back(
            { { "instance_id", std::to_string(instance.m_instance.m_value) },
             { "definition", reference(instance.m_definition) },
             { "selected_variant", std::to_string(instance.m_material_variant.m_value) },
             { "node_index", K_NODE },
             { "mesh_index", K_MESH } });
    }
    assembly["scenes"].push_back(
        { { "nodes", nodes },
         { "name",
          source.synthetic_source() ? "World Explorer synthetic diagnostic"
                                    : "World Explorer unqualified asset draft" } });
    assembly["extras"] = { { "profile", K_PROFILE },
                          { "synthetic_diagnostic", source.synthetic_source() },
                          { "qualification", "UNQUALIFIED_EXPORT" } };
    if (request.m_structure) {
        assembly["extras"]["profile"] = "WE_NATIVE_STRUCTURED_GLTF_V1";
        assembly["extras"]["manual_ancestor"] = request.m_structure->manual_ancestor();
    }
    if (request.m_lods) {
        assembly["extras"]["profile"] = K_LOD_PROFILE;
        files.json("lods.json",
                   { { "schema_version", 1 },
                    { "kind", "NATIVE_LOD_SELECTION" },
                    { "source_receipt_sha256", receipt.receipt_sha256() },
                    { "source_instances_sha256", instances.sha256() },
                    { "structure_geometry_scope", "LOD0" },
                    { "selection_scope", "EXPLICIT_PER_DEFINITION_ALL_INSTANCES" },
                    { "assets", lod_entries } });
    }
    Json colliders;
    if (request.m_colliders) {
        colliders = compile_colliders(request, files, stop);
        files.json("colliders.json", colliders);
        assembly["extras"]["profile"] = K_COLLIDER_PROFILE;
        for (auto& mapping : mappings) {
            const auto& entries = colliders.at("assets");
            const auto K_ENTRY = std::ranges::find_if(entries, [&](const auto& item) {
                return item.at("definition") == mapping.at("definition");
            });
            mapping["collider"] = K_ENTRY == entries.end() ? Json(nullptr) : *K_ENTRY;
            if (K_ENTRY != entries.end()) {
                mapping["collider"].erase("instances");
                mapping["collider"].erase("source_faces");
            }
        }
    }
    files.json("environment.gltf", assembly);
    files.json("catalog.json", catalog);
    files.add("scene.json", bytes(instances.canonical_json()));
    std::ranges::sort(files.m_files, {}, &ExportPackageFile::m_path);
    auto inventory = Json::array();
    for (const auto& file : files.m_files) {
        inventory.push_back({ { "path", file.m_path },
                             { "bytes", file.m_bytes.size() },
                             { "sha256", sha256(file.m_bytes) } });
    }
    const auto K_UNITS =
        Json { { "status", "ARBITRARY" }, { "nominal_metres_per_unit", 1 }, { "measured", false } };
    Json manifest { { "schema_version",
                    request.m_colliders   ? 4
                    : request.m_lods      ? 3
                    : request.m_structure ? 2
                                          : 1 },
                   { "profile",
                    request.m_colliders   ? K_COLLIDER_PROFILE
                    : request.m_lods      ? K_LOD_PROFILE
                    : request.m_structure ? std::string_view("WE_NATIVE_STRUCTURED_GLTF_V1")
                                          : K_PROFILE },
                   { "qualification", "UNQUALIFIED_EXPORT" },
                   { "source_receipt_sha256", receipt.receipt_sha256() },
                   { "source_instances_sha256", instances.sha256() },
                   { "scene_id", std::to_string(instances.instances().m_scene.m_value) },
                   { "scene_revision", std::to_string(instances.instances().m_revision.m_value) },
                   { "world_epoch", std::to_string(expected.m_world_epoch.m_value) },
                   { "pose_revision", std::to_string(expected.m_pose_revision.m_value) },
                   { "assets", mappings },
                   { "nodes", node_map },
                   { "files", inventory },
                   { "units", K_UNITS },
                   { "walking_enabled", false },
                   { "manual_ancestor",
                    request.m_structure ? request.m_structure->manual_ancestor()
                                        : instances.instances().m_manual_ancestor },
                   { "synthetic_diagnostic", source.synthetic_source() },
                   { "error_limits", K_POLICY },
                   { "losses",
                    { "FLOAT32_GEOMETRY_UV",
                     "NORMAL_TANGENT_NORMALIZED",
                     "PIVOT_BAKED",
                     "SRGB8_COLOR_EMISSION",
                     "LINEAR8_ORM_NORMAL",
                     "NORMAL_MAP_DECODE_NORMALIZED",
                     "PRIMITIVES_GROUPED_WITH_SOURCE_FACE_MAP",
                     "NOMINAL_METRE_MAPPING" } },
                   { "uv_origin", "TOP_LEFT_UNCHANGED" },
                   { "normal_basis", "TANGENT_W_POSITIVE_BITANGENT_UNCHANGED" },
                   { "mip_policy", "NONE" },
                   { "occlusion_scope", "UNQUALIFIED_SOURCE_CHANNEL" },
                   { "movement_occlusion_revalidation", "REQUIRED" },
                   { "source_images_added", false },
                   { "fit_lights_exported", false },
                   { "material_assignment", "SOURCE_FACE_GROUPS_WITHOUT_INSTANCE_OVERRIDES" },
                   { "validator", "NOT_RUN" },
                   { "importer", "NOT_RUN" } };
    if (request.m_structure) {
        manifest["structure"] = { { "path", "structure/bindings.json" },
                                 { "sha256", request.m_structure->sha256() },
                                 { "scope", "DECLARED_PARTIAL_PATCHES" } };
    }
    if (request.m_lods) {
        const auto K_LODS =
            std::ranges::find(files.m_files, std::string { "lods.json" }, &ExportPackageFile::m_path);
        require(K_LODS != files.m_files.end(), "Native LOD companion missing");
        manifest["lod_selection"] = { { "path", "lods.json" },
                                     { "sha256", sha256(K_LODS->m_bytes) },
                                     { "scope", "EXPLICIT_PER_DEFINITION_ALL_INSTANCES" },
                                     { "structure_geometry_scope", "LOD0" } };
    }
    if (request.m_colliders) {
        manifest["collider_companion"] = { { "path", "colliders.json" },
                                          { "sha256", sha256(bytes(colliders.dump())) },
                                          { "geometry_scope", "LOD0" },
                                          { "purpose", "SYNTHETIC_GAMEPLAY" },
                                          { "backend_shape", "NOT_COOKED" } };
        manifest["collider_limits"] = {
            { "world_position_units", selected(request.m_colliders).m_world_position_units } };
    }
    files.json("manifest.json", manifest);
    std::ranges::sort(files.m_files, {}, &ExportPackageFile::m_path);
    check_stop(stop);
    return std::move(files.m_files);
}

std::string record_key(const std::string& manifest, const std::string& path) {
    return sha256(bytes(manifest + ":" + path));
}

}  // namespace

Result<std::vector<ExportPackageFile>> prepare_native_export(const NativeExportRequest& request,
                                                             const std::stop_token& stop) {
    try {
        return build(request, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const gltf_detail::Cancelled&) {
        return fail(ErrorCode::CANCELLED, "Native PNG conversion cancelled");
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native export allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

struct AdmittedNativeExport::Implementation {
    std::vector<ExportPackageFile> m_files;
    std::string m_manifest, m_receipt;
    AssetDependencyContext m_context;
    std::filesystem::path m_source;
};

AdmittedNativeExport::AdmittedNativeExport(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const std::vector<ExportPackageFile>& AdmittedNativeExport::files() const {
    return m_state->m_files;
}

const std::string& AdmittedNativeExport::manifest_sha256() const {
    return m_state->m_manifest;
}

const std::string& AdmittedNativeExport::receipt_sha256() const {
    return m_state->m_receipt;
}

const AssetDependencyContext& AdmittedNativeExport::context() const {
    return m_state->m_context;
}

Result<AdmittedNativeExport> inspect_native_export(const NativeExportRequest& request,
                                                   const std::vector<ExportPackageFile>& files,
                                                   const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(files.size() <= 64uz,
                "Native export file count exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::size_t total {};
        std::map<std::string, std::size_t> selected_files;
        for (std::size_t index { 0uz }; index < files.size(); ++index) {
            const auto& file = files[index];
            require(file.m_path.size() <= 128uz && !file.m_bytes.empty() &&
                        file.m_bytes.size() <= 1024uz * 1024uz &&
                        file.m_bytes.size() <= K_PACKAGE_BYTES - total,
                    "Native export file budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            total += file.m_bytes.size();
            require(selected_files.emplace(file.m_path, index).second,
                    "Duplicate native export file");
        }
        auto expected = take(prepare_native_export(request, stop));
        require(files.size() == expected.size(), "Native export file set differs");
        std::string manifest;
        for (const auto& file : expected) {
            check_stop(stop);
            const auto K_FOUND = selected_files.find(file.m_path);
            require(K_FOUND != selected_files.end() &&
                        files[K_FOUND->second].m_bytes == file.m_bytes,
                    "Native export canonical bytes differ from independent inputs");
            if (file.m_path == "manifest.json") {
                manifest = sha256(file.m_bytes);
            }
        }
        const auto& receipt = selected(request.m_receipt);
        return AdmittedNativeExport(std::make_shared<const AdmittedNativeExport::Implementation>(
            AdmittedNativeExport::Implementation { std::move(expected),
                                                  std::move(manifest),
                                                  receipt.receipt_sha256(),
                                                  request.m_expected,
                                                  receipt.source_directory() }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native export admission allocation failed");
    }
}

Result<RecordCommit> publish_native_export(const AdmittedNativeExport& package,
                                           const AdmittedAssetReceipt& receipt,
                                           const std::filesystem::path& directory,
                                           OperationId operation,
                                           std::uint64_t base_generation,
                                           const AssetDependencyContext& expected,
                                           const std::stop_token& stop,
                                           const std::function<void(std::size_t)>& observed) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        return std::unexpected(K_GRAPH.error());
    }
    if (package.receipt_sha256() != receipt.receipt_sha256() ||
        package.context().m_world_epoch != expected.m_world_epoch ||
        package.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED,
                    "Native export publication receipt/context differs");
    }
    try {
        std::vector<ImmutableRecord> records;
        for (const auto& file : package.files()) {
            records.push_back({ record_key(package.manifest_sha256(), file.m_path), file.m_bytes });
        }
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         records,
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native export publication allocation failed");
    }
}

Result<AdmittedNativeExport>
read_native_export(const std::filesystem::path& directory,
                   const RecordCommit& commit,
                   const NativeExportRequest& request,
                   const std::vector<ExportPackageFile>& selected_files,
                   const std::stop_token& stop) {
    try {
        auto package = take(inspect_native_export(request, selected_files, stop));
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && commit.m_generation == K_SNAPSHOT.m_commit.m_generation &&
                    commit.m_manifest_sha256 == K_SNAPSHOT.m_commit.m_manifest_sha256,
                "Native export selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& file : package.files()) {
            const auto K_FOUND =
                K_SNAPSHOT.m_records.find(record_key(package.manifest_sha256(), file.m_path));
            require(K_FOUND != K_SNAPSHOT.m_records.end() && K_FOUND->second == file.m_bytes,
                    "Native export committed closure differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return package;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native export reload allocation failed");
    }
}

Result<void> write_native_export(const AdmittedNativeExport& package,
                                 const std::filesystem::path& directory,
                                 const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_TARGET =
            std::filesystem::weakly_canonical(std::filesystem::absolute(directory));
        const auto K_SOURCE = std::filesystem::weakly_canonical(package.m_state->m_source);
        const auto K_WITHIN = [](const auto& path, const auto& parent) {
            const auto K_RELATIVE = path.lexically_relative(parent);
            return !K_RELATIVE.empty() && *K_RELATIVE.begin() != "..";
        };
        require(!directory.empty() && !K_WITHIN(K_TARGET, K_SOURCE) &&
                    !K_WITHIN(K_SOURCE, K_TARGET) &&
                    !std::filesystem::exists(std::filesystem::symlink_status(K_TARGET)),
                "Choose a new native export directory outside inputs",
                ErrorCode::INVALID_ARGUMENT);
        require(std::filesystem::create_directory(K_TARGET),
                "Native export directory creation failed",
                ErrorCode::IO_ERROR);
        for (const auto& file : package.files()) {
            check_stop(stop);
            if (file.m_path == "manifest.json") {
                continue;
            }
            const auto K_PATH = K_TARGET / file.m_path;
            std::filesystem::create_directories(K_PATH.parent_path());
            (void)take(asset_files::publish_asset(K_PATH, file.m_bytes, stop));
        }
        const auto K_MANIFEST = std::ranges::find(
            package.files(), std::string("manifest.json"), &ExportPackageFile::m_path);
        require(K_MANIFEST != package.files().end(), "Native export terminal manifest missing");
        (void)take(
            asset_files::publish_asset(K_TARGET / "manifest.json", K_MANIFEST->m_bytes, stop));
        return {};
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native export write allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

}  // namespace world_explorer
