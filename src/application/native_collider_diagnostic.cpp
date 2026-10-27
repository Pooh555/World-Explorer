#include "world_explorer/application/native_collider_diagnostic.hpp"

#include "native_export_internal.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <charconv>
#include <map>

namespace world_explorer {
namespace {

using namespace native_export_detail;
constexpr auto K_REQUEST_BYTES = 1024uz * 1024uz;

Json placement(const SimilarityTransform& frame) {
    const auto& q = frame.m_rotation;
    return { { "rotation", { q.m_w, q.m_x, q.m_y, q.m_z } },
            { "translation", xyz(frame.m_translation) },
            { "scale", frame.m_scale } };
}

SimilarityTransform compose(const SimilarityTransform& a, const SimilarityTransform& b) {
    const auto& q = a.m_rotation;
    const auto& r = b.m_rotation;
    return { { q.m_w * r.m_w - q.m_x * r.m_x - q.m_y * r.m_y - q.m_z * r.m_z,
             q.m_w * r.m_x + q.m_x * r.m_w + q.m_y * r.m_z - q.m_z * r.m_y,
             q.m_w * r.m_y - q.m_x * r.m_z + q.m_y * r.m_w + q.m_z * r.m_x,
             q.m_w * r.m_z + q.m_x * r.m_y - q.m_y * r.m_x + q.m_z * r.m_w },
            transform_point(a, b.m_translation),
            a.m_scale * b.m_scale };
}

std::uint64_t id(const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    std::uint64_t result {};
    const auto K_PARSE = std::from_chars(K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), result);
    require(K_PARSE.ec == std::errc {} && K_PARSE.ptr == K_TEXT.data() + K_TEXT.size() && result &&
                std::to_string(result) == K_TEXT,
            "Invalid canonical collider identity");
    return result;
}

AssetRevisionRef product(const Json& value) {
    keys(value, { "product_id", "revision", "sha256" });
    return { { id(value.at("product_id")) }, id(value.at("revision")), digest(value.at("sha256")) };
}

const std::vector<std::byte>& file(const std::vector<ExportPackageFile>& files,
                                   const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    require(K_FOUND != files.end(), "Diagnostic package file missing");
    return K_FOUND->m_bytes;
}

Json document(const std::vector<ExportPackageFile>& files,
              const std::string& path,
              const std::stop_token& stop) {
    return parse(file(files, path), K_REQUEST_BYTES, stop);
}

void portable_path(const std::string& value) {
    require(!value.empty() && value.size() <= 128uz && !value.starts_with('/') &&
                !value.ends_with('/') && value.find("..") == value.npos &&
                value.find("//") == value.npos &&
                value.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") ==
                    value.npos,
            "Invalid diagnostic package path");
    for (const auto& part : std::filesystem::path(value)) {
        require(part != "." && part != "..", "Invalid diagnostic package path component");
    }
}

std::vector<ExportPackageFile> read_package(const std::filesystem::path& root,
                                            const std::string& expected,
                                            const std::stop_token& stop) {
    require(std::filesystem::is_directory(std::filesystem::symlink_status(root)),
            "Diagnostic package root must be a nonsymlink directory");
    Files files;
    files.add("manifest.json",
              take(asset_files::read_bytes(root / "manifest.json", K_REQUEST_BYTES, stop)));
    require(sha256(file(files.m_files, "manifest.json")) == expected,
            "Diagnostic selected package manifest differs",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_MANIFEST = document(files.m_files, "manifest.json", stop);
    const auto& inventory = K_MANIFEST.at("files");
    array(inventory, 63uz);
    std::set<std::string> names { "manifest.json" }, directories;
    for (const auto& row : inventory) {
        check_stop(stop);
        keys(row, { "path", "sha256", "bytes" });
        const auto K_PATH = row.at("path").get<std::string>();
        portable_path(K_PATH);
        require(names.insert(K_PATH).second, "Duplicate diagnostic package file");
        auto parent = std::filesystem::path(K_PATH).parent_path();
        while (!parent.empty()) {
            require(std::filesystem::is_directory(std::filesystem::symlink_status(root / parent)),
                    "Diagnostic package directory must not be a symlink");
            directories.insert(parent.generic_string());
            parent = parent.parent_path();
        }
        const auto K_SIZE = integer(row.at("bytes"));
        require(K_SIZE && K_SIZE <= K_REQUEST_BYTES && K_SIZE <= K_PACKAGE_BYTES - files.m_bytes,
                "Diagnostic package byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        auto data =
            take(asset_files::read_bytes(root / K_PATH, static_cast<std::size_t>(K_SIZE), stop));
        require(data.size() == K_SIZE && sha256(data) == digest(row.at("sha256")),
                "Diagnostic package inventory checksum differs",
                ErrorCode::RESYNC_REQUIRED);
        files.add(K_PATH, std::move(data));
    }
    auto pending = std::vector<std::filesystem::path> { {} };
    std::size_t entries {};
    while (!pending.empty()) {
        const auto K_RELATIVE = pending.back();
        pending.pop_back();
        for (const auto& entry : std::filesystem::directory_iterator(root / K_RELATIVE)) {
            check_stop(stop);
            require(++entries <= names.size() + directories.size(),
                    "Unlisted diagnostic package entry");
            const auto K_PATH = K_RELATIVE / entry.path().filename();
            const auto K_STATUS = entry.symlink_status();
            if (std::filesystem::is_directory(K_STATUS)) {
                require(directories.contains(K_PATH.generic_string()),
                        "Unlisted diagnostic package directory");
                pending.push_back(K_PATH);
            } else {
                require(std::filesystem::is_regular_file(K_STATUS) &&
                            names.contains(K_PATH.generic_string()),
                        "Unlisted or nonregular diagnostic package file");
            }
        }
    }
    require(entries == names.size() + directories.size(), "Diagnostic package closure changed");
    return std::move(files.m_files);
}

Json selection(const AdmittedNativeAssets& assets,
               const AdmittedNativeExport& package,
               const AssetDependencyContext& expected,
               const std::vector<NativeColliderQuery>& queries,
               const NativeColliderDiagnosticPolicy& policy,
               const std::stop_token& stop) {
    const auto& instances = assets.instances();
    require(package.receipt_sha256() == assets.receipt_sha256() &&
                package.context().m_world_epoch == expected.m_world_epoch &&
                package.context().m_pose_revision == expected.m_pose_revision &&
                instances.instances().m_context.m_world_epoch == expected.m_world_epoch &&
                instances.instances().m_context.m_pose_revision == expected.m_pose_revision,
            "Diagnostic current asset receipt/context differs",
            ErrorCode::RESYNC_REQUIRED);
    Json catalog = Json::array(), rows = Json::array(), rays = Json::array();
    for (const auto& definition : instances.definitions()) {
        check_stop(stop);
        catalog.push_back(parse(bytes(definition.canonical_json()), K_REQUEST_BYTES, stop));
        const auto K_SOURCE = std::ranges::find_if(assets.sources(), [&](const auto& source) {
            return source.m_definition.m_asset == definition.reference().m_asset;
        });
        require(K_SOURCE != assets.sources().end(), "Diagnostic current definition missing");
        if (!K_SOURCE->m_collider) {
            continue;
        }
        const auto& collider = selected(K_SOURCE->m_collider);
        const auto K_INPUT = take(prepare_native_collider_input(collider, expected, stop));
        require(collider.receipt_sha256() == assets.receipt_sha256(),
                "Diagnostic collider receipt differs");
        auto placements = Json::array();
        const auto& pivot = definition.definition().m_pivot.m_asset_from_geometry;
        for (const auto& instance : instances.instances().m_instances) {
            if (instance.m_definition != definition.reference()) {
                continue;
            }
            require(instance.m_visible, "Hidden diagnostic collider instance unsupported");
            placements.push_back(
                { { "instance_id", std::to_string(instance.m_instance.m_value) },
                 { "world_from_asset", placement(instance.m_world_from_asset) },
                 { "world_from_collider", placement(compose(instance.m_world_from_asset, pivot)) } });
        }
        rows.push_back({ { "definition", reference(definition.reference()) },
                        { "product", reference(K_INPUT.binding().m_collider) },
                        { "base_geometry", reference(K_INPUT.binding().m_base) },
                        { "cook_sha256", K_INPUT.binding().m_canonical_sha256 },
                        { "asset_from_collider", placement(pivot) },
                        { "instances", placements } });
    }
    for (const auto& query : queries) {
        rays.push_back({ { "query_id", query.m_query_id },
                        { "instance_id", std::to_string(query.m_instance.m_value) },
                        { "start", xyz(query.m_start) },
                        { "end", xyz(query.m_end) } });
    }
    return { { "package_manifest_sha256", package.manifest_sha256() },
            { "source_receipt_sha256", assets.receipt_sha256() },
            { "source_instances_sha256", instances.sha256() },
            { "catalog_sha256", sha256(bytes(catalog.dump())) },
            { "world_epoch", expected.m_world_epoch.m_value },
            { "pose_revision", expected.m_pose_revision.m_value },
            { "policy",
             { { "local_vertex_units", policy.m_shape.m_max_local_vertex_error_units },
              { "world_endpoint_units", policy.m_shape.m_max_world_ray_endpoint_error_units },
              { "world_position_units", policy.m_world_position_units } } },
            { "assets", rows },
            { "queries", rays } };
}

std::string run(const Json& selected,
                const std::vector<ExportPackageFile>& files,
                const std::stop_token& stop) {
    keys(selected,
         { "package_manifest_sha256",
          "source_receipt_sha256",
          "source_instances_sha256",
          "catalog_sha256",
          "world_epoch",
          "pose_revision",
          "policy",
          "assets",
          "queries" });
    const AssetDependencyContext K_CONTEXT { { integer(selected.at("world_epoch")) },
                                            { integer(selected.at("pose_revision")) } };
    const auto K_RECEIPT = digest(selected.at("source_receipt_sha256"));
    require(
        sha256(file(files, "manifest.json")) == digest(selected.at("package_manifest_sha256")) &&
            sha256(file(files, "scene.json")) == digest(selected.at("source_instances_sha256")) &&
            sha256(file(files, "catalog.json")) == digest(selected.at("catalog_sha256")),
        "Diagnostic current package/catalog/instance selection differs",
        ErrorCode::RESYNC_REQUIRED);
    const auto K_MANIFEST = document(files, "manifest.json", stop),
               K_COMPANION = document(files, "colliders.json", stop),
               K_SCENE = document(files, "scene.json", stop),
               K_CATALOG = document(files, "catalog.json", stop);
    array(K_CATALOG, 2uz);
    array(K_SCENE.at("instances"), 64uz);
    require(
        integer(K_MANIFEST.at("schema_version")) == 4 &&
            K_MANIFEST.at("profile").get<std::string>() == "WE_NATIVE_COLLIDER_GLTF_V1" &&
            K_MANIFEST.at("walking_enabled") == false &&
            K_MANIFEST.at("source_receipt_sha256").get<std::string>() == K_RECEIPT &&
            K_MANIFEST.at("source_instances_sha256") == selected.at("source_instances_sha256") &&
            id(K_MANIFEST.at("world_epoch")) == K_CONTEXT.m_world_epoch.m_value &&
            id(K_MANIFEST.at("pose_revision")) == K_CONTEXT.m_pose_revision.m_value &&
            K_MANIFEST.at("collider_companion").at("sha256").get<std::string>() ==
                sha256(file(files, "colliders.json")) &&
            K_COMPANION.at("source_instances_sha256") == selected.at("source_instances_sha256") &&
            K_COMPANION.at("source_receipt_sha256").get<std::string>() == K_RECEIPT &&
            K_COMPANION.at("walking_enabled") == false,
        "Diagnostic package companion context/profile differs",
        ErrorCode::RESYNC_REQUIRED);
    const auto& policy = selected.at("policy");
    keys(policy, { "local_vertex_units", "world_endpoint_units", "world_position_units" });
    for (const auto& value : policy) {
        const auto K_VALUE = number(value);
        require(K_VALUE >= 0 && K_VALUE <= 1, "Invalid diagnostic independent loss policy");
    }
    const NativeShapePolicy K_POLICY { number(policy.at("local_vertex_units")),
                                      number(policy.at("world_endpoint_units")) };
    const auto K_PLACEMENT_LIMIT = number(policy.at("world_position_units"));
    const auto& rows = selected.at("assets");
    array(rows, 2uz);
    require(!rows.empty(), "Diagnostic selection has no collider assets");
    array(K_COMPANION.at("assets"), 2uz);
    require(rows.size() == K_COMPANION.at("assets").size(), "Diagnostic collider coverage differs");
    std::map<std::uint64_t,
             std::pair<std::shared_ptr<const NativeStaticCollision>, SimilarityTransform>>
        instances;
    std::set<std::uint64_t> definitions;
    Json shapes = Json::array(), placements = Json::array(), results = Json::array();
    for (const auto& row : rows) {
        check_stop(stop);
        keys(row,
             { "definition",
              "product",
              "base_geometry",
              "cook_sha256",
              "asset_from_collider",
              "instances" });
        const auto& definition = row.at("definition");
        keys(definition, { "asset_id", "revision", "sha256" });
        const auto K_ASSET = id(definition.at("asset_id"));
        require(id(definition.at("revision")) && definitions.insert(K_ASSET).second,
                "Duplicate diagnostic definition");
        (void)digest(definition.at("sha256"));
        const auto K_DEFINITION = std::ranges::find_if(K_CATALOG, [&](const auto& item) {
            return integer(item.at("asset_id")) == K_ASSET;
        });
        require(K_DEFINITION != K_CATALOG.end() &&
                    sha256(bytes(K_DEFINITION->dump())) == digest(definition.at("sha256")) &&
                    integer(K_DEFINITION->at("revision")) == id(definition.at("revision")) &&
                    K_DEFINITION->at("pivot").at("asset_from_geometry") ==
                        row.at("asset_from_collider"),
                "Diagnostic canonical definition/pivot differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_ROW = std::ranges::find_if(K_COMPANION.at("assets"), [&](const auto& item) {
            return item.at("definition") == definition;
        });
        require(K_ROW != K_COMPANION.at("assets").end() &&
                    K_ROW->at("product") == row.at("product") &&
                    K_ROW->at("base_geometry") == row.at("base_geometry") &&
                    K_ROW->at("asset_from_collider") == row.at("asset_from_collider") &&
                    K_ROW->at("cook").at("sha256") == row.at("cook_sha256"),
                "Diagnostic selected collider/pivot differs",
                ErrorCode::RESYNC_REQUIRED);
        const ColliderInputBinding K_BINDING { K_CONTEXT,
                                              product(row.at("product")),
                                              product(row.at("base_geometry")),
                                              K_RECEIPT,
                                              digest(row.at("cook_sha256")) };
        const auto K_INPUT = take(decode_native_collider_input(
            file(files, "colliders/" + std::to_string(K_ASSET) + "/triangles.bin"),
            K_BINDING,
            stop));
        const auto K_SHAPE =
            take(build_native_static_collision(K_INPUT, K_BINDING, K_POLICY, stop));
        const auto K_PIVOT = transform(row.at("asset_from_collider"));
        array(row.at("instances"), 64uz);
        require(!row.at("instances").empty() &&
                    row.at("instances").size() == K_ROW->at("instances").size(),
                "Diagnostic instance coverage differs");
        for (const auto& instance : row.at("instances")) {
            check_stop(stop);
            keys(instance, { "instance_id", "world_from_asset", "world_from_collider" });
            const auto K_ID = id(instance.at("instance_id"));
            const auto K_CURRENT =
                std::ranges::find_if(K_SCENE.at("instances"), [&](const auto& item) {
                    return integer(item.at("instance_id")) == K_ID;
                });
            require(K_CURRENT != K_SCENE.at("instances").end() &&
                        K_CURRENT->at("world_from_asset") == instance.at("world_from_asset") &&
                        K_CURRENT->at("visible") == true &&
                        integer(K_CURRENT->at("definition").at("asset_id")) == K_ASSET &&
                        integer(K_CURRENT->at("definition").at("revision")) ==
                            id(definition.at("revision")) &&
                        K_CURRENT->at("definition").at("sha256") == definition.at("sha256"),
                    "Diagnostic canonical current instance differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_ORIGINAL =
                std::ranges::find_if(K_ROW->at("instances"), [&](const auto& item) {
                    return item.at("instance_id") == instance.at("instance_id");
                });
            require(K_ORIGINAL != K_ROW->at("instances").end() &&
                        K_ORIGINAL->at("world_from_collider") == instance.at("world_from_collider"),
                    "Diagnostic current instance placement differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_WORLD = transform(instance.at("world_from_collider")),
                       K_ASSET_FRAME = transform(instance.at("world_from_asset"));
            require(valid(K_PIVOT) && K_PIVOT.m_scale == 1 && valid(K_WORLD) &&
                        valid(K_ASSET_FRAME),
                    "Invalid diagnostic instance/pivot frame");
            double error {};
            const auto K_PROBE = [&](Vec3d vertex) {
                const auto K_DELTA =
                    transform_point(K_WORLD, vertex) -
                    transform_point(K_ASSET_FRAME, transform_point(K_PIVOT, vertex));
                const auto K_ERROR = std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z);
                require(std::isfinite(K_ERROR), "Diagnostic placement residual overflow");
                error = std::max(error, K_ERROR);
            };
            for (auto vertex : { Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 } }) {
                K_PROBE(vertex);
            }
            for (auto vertex : K_INPUT.mesh().m_positions) {
                check_stop(stop);
                K_PROBE(vertex);
            }
            require(error <= K_PLACEMENT_LIMIT,
                    "Diagnostic independently selected placement loss exceeded");
            require(instances.size() < 64uz &&
                        instances.emplace(K_ID, std::pair { K_SHAPE, K_WORLD }).second,
                    "Duplicate or excessive diagnostic instance");
            placements.push_back({ { "instance_id", K_ID },
                                  { "definition", definition },
                                  { "world_from_collider", placement(K_WORLD) },
                                  { "world_position_error_units", error } });
        }
        const auto& report = K_SHAPE->report();
        Json triangles = Json::array();
        for (const auto& triangle : K_SHAPE->backend_triangles()) {
            check_stop(stop);
            triangles.push_back({ { "source_face", triangle.m_source_face },
                                 { "positions",
                                  { xyz(triangle.m_vertices[0uz]),
                                   xyz(triangle.m_vertices[1uz]),
                                   xyz(triangle.m_vertices[2uz]) } } });
        }
        shapes.push_back({ { "definition", definition },
                          { "backend", report.m_backend },
                          { "cook_sha256", K_BINDING.m_canonical_sha256 },
                          { "adapter_source_sha256", report.m_adapter_source_sha256 },
                          { "build_sha256", report.m_build_sha256 },
                          { "geometry_sha256", report.m_geometry_sha256 },
                          { "local_vertex_error_units", report.m_local_vertex_error_units },
                          { "triangles", triangles } });
    }
    const auto& queries = selected.at("queries");
    array(queries, 256uz);
    require(!queries.empty(), "Diagnostic needs explicit queries");
    std::set<std::uint64_t> query_ids;
    for (const auto& query : queries) {
        check_stop(stop);
        keys(query, { "query_id", "instance_id", "start", "end" });
        const auto K_QUERY = integer(query.at("query_id")),
                   K_INSTANCE = id(query.at("instance_id"));
        require(K_QUERY && query_ids.insert(K_QUERY).second,
                "Duplicate or invalid diagnostic query ID");
        const auto K_FOUND = instances.find(K_INSTANCE);
        require(K_FOUND != instances.end(), "Diagnostic query instance missing or render-only");
        const auto& [K_SHAPE, K_FRAME] = K_FOUND->second;
        const auto K_HIT = take(K_SHAPE->ray_cast(K_SHAPE->binding(),
                                                  K_FRAME,
                                                  vector3(query.at("start")),
                                                  vector3(query.at("end")),
                                                  stop));
        Json hit = nullptr;
        if (K_HIT.m_hit) {
            const auto& value = *K_HIT.m_hit;
            hit = { { "fraction", value.m_fraction },
                   { "source_face", value.m_source_face },
                   { "world_position", xyz(value.m_world_position) } };
        }
        results.push_back({ { "query_id", K_QUERY },
                           { "instance_id", K_INSTANCE },
                           { "start", query.at("start") },
                           { "end", query.at("end") },
                           { "world_endpoint_error_units", K_HIT.m_world_endpoint_error_units },
                           { "world_surface_error_units", K_HIT.m_world_surface_error_units },
                           { "hit", hit } });
    }
    const Json K_REPORT { { "schema_version", 1 },
                         { "kind", "SYNTHETIC_GAMEPLAY_COLLIDER_DIAGNOSTIC" },
                         { "units", "ARBITRARY" },
                         { "walking_enabled", false },
                         { "body_installed", false },
                         { "selection_sha256", sha256(bytes(selected.dump())) },
                         { "package_manifest_sha256", selected.at("package_manifest_sha256") },
                         { "source_receipt_sha256", K_RECEIPT },
                         { "source_instances_sha256", selected.at("source_instances_sha256") },
                         { "world_epoch", K_CONTEXT.m_world_epoch.m_value },
                         { "pose_revision", K_CONTEXT.m_pose_revision.m_value },
                         { "policy", policy },
                         { "shapes", shapes },
                         { "instances", placements },
                         { "queries", results } };
    auto result = K_REPORT.dump();
    require(result.size() <= 8uz * 1024uz * 1024uz,
            "Diagnostic report byte budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    check_stop(stop);
    return result;
}

}  // namespace

Result<NativeColliderDiagnosticOutput>
inspect_native_colliders(const AdmittedNativeAssets& assets,
                         const AdmittedNativeExport& package,
                         const AssetDependencyContext& expected,
                         const std::vector<NativeColliderQuery>& queries,
                         const std::filesystem::path& replay_package_directory,
                         const NativeColliderDiagnosticPolicy& policy,
                         const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_PATH = replay_package_directory.generic_string();
        require(!K_PATH.empty() && K_PATH.size() <= 4096uz && K_PATH.find('\0') == K_PATH.npos,
                "Invalid diagnostic replay package path");
        require(queries.size() <= 256uz,
                "Diagnostic query budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_SELECTION = selection(assets, package, expected, queries, policy, stop);
        const Json K_REQUEST { { "schema_version", 1 },
                              { "kind", "NATIVE_COLLIDER_DIAGNOSTIC_REQUEST" },
                              { "package_directory", K_PATH },
                              { "selection", K_SELECTION } };
        auto request = K_REQUEST.dump();
        require(request.size() <= K_REQUEST_BYTES, "Diagnostic request byte budget exceeded");
        return NativeColliderDiagnosticOutput { std::move(request),
                                               run(K_SELECTION, package.files(), stop) };
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Diagnostic allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Collider diagnostic failed: ") + error.what());
    }
}

Result<std::string> inspect_native_colliders_request(const std::filesystem::path& request,
                                                     const std::string& expected_sha256,
                                                     const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(expected_sha256);
        const auto K_BYTES = take(asset_files::read_bytes(request, K_REQUEST_BYTES, stop));
        require(sha256(K_BYTES) == expected_sha256,
                "Diagnostic request checksum differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_REQUEST = parse(K_BYTES, K_REQUEST_BYTES, stop);
        keys(K_REQUEST, { "schema_version", "kind", "package_directory", "selection" });
        require(integer(K_REQUEST.at("schema_version")) == 1 &&
                    K_REQUEST.at("kind").get<std::string>() == "NATIVE_COLLIDER_DIAGNOSTIC_REQUEST",
                "Unsupported diagnostic request");
        const auto K_PATH = K_REQUEST.at("package_directory").get<std::string>();
        require(!K_PATH.empty() && K_PATH.size() <= 4096uz && K_PATH.find('\0') == K_PATH.npos,
                "Invalid diagnostic package directory");
        const auto& selected = K_REQUEST.at("selection");
        const auto K_FILES = read_package(
            request.parent_path() / K_PATH, digest(selected.at("package_manifest_sha256")), stop);
        return run(selected, K_FILES, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Diagnostic allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Collider diagnostic replay failed: ") + error.what());
    }
}

}  // namespace world_explorer
