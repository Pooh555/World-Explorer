#include "world_explorer/application/static_export.hpp"

#include "static_export_glb.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <map>
#include <set>
#include <span>

namespace world_explorer {
namespace {

using namespace static_export_detail;
constexpr auto K_PACKAGE_BYTES = 8uz * 1024uz * 1024uz;

template <class T> T take(Result<T> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return *value;
}

std::vector<std::byte> text_bytes(const std::string& text) {
    const auto K_BYTES = std::as_bytes(std::span(text));
    return { K_BYTES.begin(), K_BYTES.end() };
}

struct Package {
    std::vector<ExportPackageFile> m_files;
    std::size_t m_bytes {};

    void add(std::string path, std::vector<std::byte> bytes) {
        require(bytes.size() <= 1024uz * 1024uz && bytes.size() <= K_PACKAGE_BYTES - m_bytes &&
                    m_files.size() < 56uz,
                "Static export package budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(std::ranges::none_of(m_files,
                                     [&](const auto& item) {
                                         return item.m_path == path;
                                     }),
                "Repeated static export path");
        m_bytes += bytes.size();
        m_files.push_back({ std::move(path), std::move(bytes) });
    }

    void json(std::string path, const Json& doc, std::size_t maximum) {
        auto text = doc.dump();
        require(text.size() <= maximum,
                "Static export JSON budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        add(std::move(path), text_bytes(text));
    }
};

void token(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") ==
                    value.npos,
            "Invalid export tool identity token");
}

Json audit(const ExportAudit& selected,
           bool validator,
           const std::string& glb_sha,
           const EncodedStaticGlb& encoded,
           Package& package,
           const std::stop_token& stop) {
    constexpr std::array K_STATES { "NOT_RUN", "REPORTED_PASS", "REPORTED_FAIL" };
    const auto K_STATE = static_cast<std::size_t>(selected.m_state);
    require(K_STATE < K_STATES.size(),
            "Unsupported export audit state",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_ROLE = validator ? "validator" : "importer";
    Json tool = nullptr, report = nullptr;
    if (selected.m_state == ExportAuditState::NOT_RUN) {
        require(!selected.m_tool && !selected.m_report_sha256 && !selected.m_payload_sha256 &&
                    selected.m_report.empty() && selected.m_errors == 0 && selected.m_warnings == 0,
                "NOT_RUN export audit cannot claim tool/report/results");
    } else {
        if (!selected.m_tool || !selected.m_report_sha256 || !selected.m_payload_sha256) {
            throw RecordFailure(ErrorCode::INVALID_ARGUMENT,
                                "Export reported audit requires independent tool and report pins");
        }
        const auto& identity = selected.m_tool.value();
        digest_text(selected.m_payload_sha256.value());
        require(selected.m_payload_sha256.value() == glb_sha,
                "Export audit target payload differs",
                ErrorCode::RESYNC_REQUIRED);
        token(identity.m_name);
        token(identity.m_version);
        digest_text(identity.m_executable_sha256);
        digest_text(identity.m_package_sha256);
        digest_text(selected.m_report_sha256.value());
        require(identity.m_name == (validator ? "KHRONOS_GLTF_VALIDATOR" : "GODOT"),
                "Unsupported export audit tool",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(!selected.m_report.empty() && selected.m_report.size() <= 64uz * 1024uz &&
                    sha256(selected.m_report) == selected.m_report_sha256.value(),
                "Export audit report bytes differ",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_DOC = parse(selected.m_report, 64uz * 1024uz, stop);
        if (validator) {
            require(K_DOC.at("validatorVersion").get<std::string>() == identity.m_version &&
                        integer(K_DOC.at("issues").at("numErrors")) == selected.m_errors &&
                        integer(K_DOC.at("issues").at("numWarnings")) == selected.m_warnings,
                    "Validator report version/counts differ");
        } else {
            keys(K_DOC,
                 { "schema_version",
                  "kind",
                  "glb_sha256",
                  "errors",
                  "warnings",
                  "mesh_count",
                  "node_count",
                  "node_mesh_indices" });
            require(integer(K_DOC.at("schema_version")) == 1 &&
                        K_DOC.at("kind").get<std::string>() == "GODOT_IMPORT_OBSERVATION" &&
                        digest(K_DOC.at("glb_sha256")) == glb_sha &&
                        integer(K_DOC.at("errors")) == selected.m_errors &&
                        integer(K_DOC.at("warnings")) == selected.m_warnings,
                    "Importer observation pins/counts differ");
            array(K_DOC.at("node_mesh_indices"), 64uz);
            require(integer(K_DOC.at("mesh_count")) <= 16 && integer(K_DOC.at("node_count")) <= 64,
                    "Importer observation exceeds profile budget");
            if (selected.m_state == ExportAuditState::REPORTED_PASS) {
                require(integer(K_DOC.at("mesh_count")) == encoded.m_assets.size() &&
                            integer(K_DOC.at("node_count")) == encoded.m_nodes.size() &&
                            K_DOC.at("node_mesh_indices").size() == encoded.m_nodes.size(),
                        "Importer resource/node counts differ");
                for (std::size_t node { 0uz }; node < encoded.m_nodes.size(); ++node) {
                    require(integer(K_DOC.at("node_mesh_indices")[node]) ==
                                integer(encoded.m_nodes[node].at("mesh_index")),
                            "Importer did not report canonical resource sharing");
                }
            }
        }
        require((selected.m_state == ExportAuditState::REPORTED_PASS) == (selected.m_errors == 0),
                "Export reported outcome differs from error count");
        tool = { { "name", identity.m_name },
                { "version", identity.m_version },
                { "executable_sha256", identity.m_executable_sha256 },
                { "package_sha256", identity.m_package_sha256 } };
        const auto K_PATH = std::string("reports/") + K_ROLE + ".json";
        report = { { "path", K_PATH },
                  { "sha256", selected.m_report_sha256.value() },
                  { "bytes", selected.m_report.size() } };
        package.add(K_PATH, selected.m_report);
    }
    return {
        { "schema_version", 1 },
        { "role", K_ROLE },
        { "state", K_STATES[K_STATE] },
        { "glb_sha256", glb_sha },
        { "tool", tool },
        { "report", report },
        { "errors",
         selected.m_state == ExportAuditState::NOT_RUN ? Json(nullptr) : Json(selected.m_errors) },
        { "warnings",
         selected.m_state == ExportAuditState::NOT_RUN ? Json(nullptr) : Json(selected.m_warnings) },
        { "execution_authenticity", "UNVERIFIED" } };
}

std::vector<ExportPackageFile> build(const StaticExportRequest& request,
                                     const std::stop_token& stop) {
    check_stop(stop);
    if (!request.m_source.m_scene || !request.m_source.m_receipt || !request.m_validator ||
        !request.m_importer) {
        throw RecordFailure(ErrorCode::INVALID_ARGUMENT,
                            "Independent source scene/receipt and explicit validator/importer "
                            "outcomes are required");
    }
    const auto& source = request.m_source.m_scene.value();
    const auto& receipt = request.m_source.m_receipt.value();
    const auto& expected = request.m_source.m_expected;
    require(!source.definitions().empty() && source.definitions().size() <= 16uz &&
                !source.instances().m_instances.empty() &&
                source.instances().m_instances.size() <= 64uz &&
                request.m_source.m_definitions.size() == source.definitions().size(),
            "Static export source count budget differs",
            ErrorCode::RESOURCE_EXHAUSTED);
    require(std::isfinite(request.m_source.m_max_position_error_units) &&
                request.m_source.m_max_position_error_units >= 0 &&
                request.m_source.m_max_position_error_units <= 1 &&
                std::isfinite(request.m_source.m_max_normal_error) &&
                request.m_source.m_max_normal_error >= 0 &&
                request.m_source.m_max_normal_error <= 1,
            "Invalid independent static export conversion policy",
            ErrorCode::INVALID_ARGUMENT);
    const auto K_FRESH = take(
        admit_asset_instances(source.instances(), source.definitions(), receipt, expected, stop));
    require(K_FRESH.sha256() == source.sha256(),
            "Static export source receipt differs",
            ErrorCode::RESYNC_REQUIRED);
    require(source.instances().m_units.m_status == AssetUnitStatus::ARBITRARY,
            "Measured static GLB export is unsupported",
            ErrorCode::UNSUPPORTED_OPERATION);
    std::set<AssetId> selections;
    for (const auto& item : request.m_source.m_definitions) {
        require(selections.insert(item.m_definition.m_asset).second,
                "Repeated export definition selection");
    }
    Package package;
    std::vector<StaticAssetPayloads> payloads;
    Json catalog = Json::array();
    std::set<AssetRevisionRef> regions;
    for (const auto& definition : source.definitions()) {
        check_stop(stop);
        const auto K_SELECTED = std::ranges::find(request.m_source.m_definitions,
                                                  definition.reference(),
                                                  &StaticExportDefinition::m_definition);
        require(K_SELECTED != request.m_source.m_definitions.end() &&
                    K_SELECTED->m_provenance.size() == 2uz,
                "Static export requires exact independent definition/provenance selection",
                ErrorCode::RESYNC_REQUIRED);
        payloads.push_back(take(inspect_static_asset_payloads(
            definition, receipt, K_SELECTED->m_provenance, expected, stop)));
        catalog.push_back(parse(text_bytes(definition.canonical_json()), 256uz * 1024uz, stop));
        for (const auto& selected : K_SELECTED->m_provenance) {
            if (regions.insert(selected.m_product).second) {
                auto bytes = take(
                    receipt.copy_blob(selected.m_product.m_content_sha256, 64uz * 1024uz, stop));
                package.add("provenance/" + std::to_string(selected.m_product.m_product.m_value) +
                                "-" + std::to_string(selected.m_product.m_revision) + ".json",
                            std::move(bytes));
            }
        }
        package.json("rights/" + std::to_string(definition.reference().m_asset.m_value) + ".json",
                     { { "product", product_ref(definition.definition().m_rights) },
                      { "assessment", "UNREVIEWED_BINDING_ONLY" },
                      { "opaque_payload_exported", false } },
                     2048uz);
    }
    auto encoded = encode_static_glb(source, payloads, request.m_source, stop);
    const auto K_GLB_SHA = sha256(encoded.m_bytes);
    package.add("scene.glb", std::move(encoded.m_bytes));
    package.json(
        "catalog.json",
        { { "schema_version", 1 }, { "kind", "EXACT_DEFINITION_SET" }, { "definitions", catalog } },
        256uz * 1024uz);
    package.add("scene.json", text_bytes(source.canonical_json()));
    require(package.m_files.back().m_bytes.size() <= 256uz * 1024uz,
            "Static export scene sidecar exceeds profile budget",
            ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_VALIDATOR =
        audit(request.m_validator.value(), true, K_GLB_SHA, encoded, package, stop);
    const auto K_IMPORTER =
        audit(request.m_importer.value(), false, K_GLB_SHA, encoded, package, stop);
    package.json("audit-validator.json", K_VALIDATOR, 16uz * 1024uz);
    package.json("audit-importer.json", K_IMPORTER, 16uz * 1024uz);
    std::ranges::sort(package.m_files, {}, &ExportPackageFile::m_path);
    Json files = Json::array();
    std::string catalog_sha;
    for (const auto& file : package.m_files) {
        files.push_back({ { "path", file.m_path },
                         { "bytes", file.m_bytes.size() },
                         { "sha256", sha256(file.m_bytes) } });
        if (file.m_path == "catalog.json") {
            catalog_sha = sha256(file.m_bytes);
        }
    }
    package.json("manifest.json",
                 { { "schema_version", 1 },
                  { "kind", "EXPORT_MANIFEST" },
                  { "profile", "WE_GENERATED_CONSTANT_GLB_V1" },
                  { "qualification", "UNQUALIFIED_EXPORT" },
                  { "world_epoch", expected.m_world_epoch.m_value },
                  { "pose_revision", expected.m_pose_revision.m_value },
                  { "receipt_sha256", receipt.receipt_sha256() },
                  { "source_instances_sha256", source.sha256() },
                  { "source_scene",
                   { { "id", std::to_string(source.instances().m_scene.m_value) },
                    { "revision", std::to_string(source.instances().m_revision.m_value) } } },
                  { "catalog_scope", "EXACT_DEFINITION_SET_ONLY" },
                  { "catalog_sha256", catalog_sha },
                  { "assets", encoded.m_assets },
                  { "nodes", encoded.m_nodes },
                  { "files", files },
                  { "losses",
                   { "FLOAT32_GEOMETRY",
                    "NORMALS_NORMALIZED",
                    "PIVOT_BAKED_INTO_VERTICES",
                    "NOMINAL_METRE_MAPPING" } },
                  { "conversion",
                   { { "max_position_error_units", encoded.m_position_error },
                    { "max_normal_error", encoded.m_normal_error },
                    { "position_error_limit_units", request.m_source.m_max_position_error_units },
                    { "normal_error_limit", request.m_source.m_max_normal_error } } },
                  { "units", "ARBITRARY_ONE_UNIT_TO_NOMINAL_GLTF_METRE" },
                  { "manual_ancestor", source.instances().m_manual_ancestor },
                  { "validator", K_VALIDATOR },
                  { "importer", K_IMPORTER },
                  { "source_images_added", false },
                  { "selected_reports", "UNINSPECTED_IF_PRESENT" },
                  { "opaque_rights_exported", false },
                  { "walking_enabled", false } },
                 256uz * 1024uz);
    std::ranges::sort(package.m_files, {}, &ExportPackageFile::m_path);
    check_stop(stop);
    return package.m_files;
}

std::string record_key(const std::string& manifest, const std::string& path) {
    return sha256(text_bytes(manifest + ":" + path));
}

}  // namespace

Result<std::vector<ExportPackageFile>> prepare_static_export(const StaticExportRequest& request,
                                                             const std::stop_token& stop) {
    try {
        return build(request, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Static export allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

struct AdmittedExportManifest::Implementation {
    std::vector<ExportPackageFile> m_files;
    std::string m_manifest, m_receipt, m_source;
    AssetDependencyContext m_context;
    bool m_manual {};
    std::filesystem::path m_protected_root;
};

AdmittedExportManifest::AdmittedExportManifest(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const std::vector<ExportPackageFile>& AdmittedExportManifest::files() const {
    return m_state->m_files;
}

const std::string& AdmittedExportManifest::manifest_sha256() const {
    return m_state->m_manifest;
}

const std::string& AdmittedExportManifest::receipt_sha256() const {
    return m_state->m_receipt;
}

const std::string& AdmittedExportManifest::source_instances_sha256() const {
    return m_state->m_source;
}

const AssetDependencyContext& AdmittedExportManifest::context() const {
    return m_state->m_context;
}

bool AdmittedExportManifest::manual_ancestor() const {
    return m_state->m_manual;
}

Result<AdmittedExportManifest> inspect_static_export(const StaticExportRequest& request,
                                                     const std::vector<ExportPackageFile>& files,
                                                     const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(files.size() <= 56uz,
                "Static export selected file budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::set<std::string> names;
        std::size_t bytes {};
        for (const auto& file : files) {
            require(!file.m_path.empty() && file.m_path.size() <= 128uz &&
                        file.m_path.find_first_not_of(
                            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") ==
                            file.m_path.npos &&
                        !file.m_path.starts_with('/') &&
                        file.m_path.find("..") == file.m_path.npos &&
                        names.insert(file.m_path).second,
                    "Invalid/repeated export relative path");
            require(file.m_bytes.size() <= 1024uz * 1024uz &&
                        file.m_bytes.size() <= K_PACKAGE_BYTES - bytes,
                    "Static export selected bytes exceed budget",
                    ErrorCode::RESOURCE_EXHAUSTED);
            bytes += file.m_bytes.size();
        }
        auto expected = take(prepare_static_export(request, stop));
        require(files.size() == expected.size(), "Export package file set incomplete");
        for (const auto& file : expected) {
            check_stop(stop);
            const auto K_FOUND = std::ranges::find(files, file.m_path, &ExportPackageFile::m_path);
            require(K_FOUND != files.end() && K_FOUND->m_bytes == file.m_bytes,
                    "Export payload/manifest/sidecar differs from independent source",
                    ErrorCode::RESYNC_REQUIRED);
        }
        if (!request.m_source.m_receipt || !request.m_source.m_scene) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Export source prerequisites unavailable");
        }
        const auto K_MANIFEST =
            std::ranges::find(expected, std::string("manifest.json"), &ExportPackageFile::m_path);
        require(K_MANIFEST != expected.end(), "Missing canonical export manifest");
        const auto K_MANIFEST_SHA = sha256(K_MANIFEST->m_bytes);
        return AdmittedExportManifest(
            std::make_shared<const AdmittedExportManifest::Implementation>(
                AdmittedExportManifest::Implementation {
                    std::move(expected),
                    K_MANIFEST_SHA,
                    request.m_source.m_receipt->receipt_sha256(),
                    request.m_source.m_scene->sha256(),
                    request.m_source.m_expected,
                    request.m_source.m_scene->instances().m_manual_ancestor,
                    request.m_source.m_receipt->source_directory() }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Export manifest allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_export_manifest(const AdmittedExportManifest& manifest,
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
    if (manifest.receipt_sha256() != receipt.receipt_sha256() ||
        manifest.context().m_world_epoch != expected.m_world_epoch ||
        manifest.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Export publication receipt/context differs");
    }
    try {
        std::vector<ImmutableRecord> records;
        records.reserve(manifest.files().size());
        for (const auto& file : manifest.files()) {
            records.push_back({ record_key(manifest.manifest_sha256(), file.m_path), file.m_bytes });
        }
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         records,
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Export publication allocation failed");
    }
}

Result<AdmittedExportManifest>
read_export_manifest(const std::filesystem::path& directory,
                     const RecordCommit& commit,
                     const StaticExportRequest& request,
                     const std::vector<ExportPackageFile>& selected_files,
                     const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(commit.m_manifest_sha256);
        auto admitted = take(inspect_static_export(request, selected_files, stop));
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && commit.m_generation == K_SNAPSHOT.m_commit.m_generation &&
                    commit.m_manifest_sha256 == K_SNAPSHOT.m_commit.m_manifest_sha256,
                "Export selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& file : admitted.files()) {
            const auto K_FOUND =
                K_SNAPSHOT.m_records.find(record_key(admitted.manifest_sha256(), file.m_path));
            require(K_FOUND != K_SNAPSHOT.m_records.end() &&
                        sha256(K_FOUND->second) == sha256(file.m_bytes),
                    "Selected export closure is not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return admitted;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Export reload allocation failed");
    }
}

Result<void> write_static_export(const AdmittedExportManifest& manifest,
                                 const std::filesystem::path& directory,
                                 const std::stop_token& stop) {
    try {
        check_stop(stop);
        // scene.json is an owned canonical source sidecar; no source tree is opened here.
        const auto K_TARGET =
            std::filesystem::weakly_canonical(std::filesystem::absolute(directory));
        const auto K_SOURCE = std::filesystem::weakly_canonical(manifest.m_state->m_protected_root);
        const auto K_WITHIN = [](const auto& path, const auto& parent) {
            const auto K_RELATIVE = path.lexically_relative(parent);
            return !K_RELATIVE.empty() && *K_RELATIVE.begin() != "..";
        };
        require(!K_WITHIN(K_TARGET, K_SOURCE) && !K_WITHIN(K_SOURCE, K_TARGET),
                "Static export overlaps protected source tree",
                ErrorCode::INVALID_ARGUMENT);
        require(!directory.empty() &&
                    !std::filesystem::exists(std::filesystem::symlink_status(K_TARGET)),
                "Choose a new static export directory",
                ErrorCode::INVALID_ARGUMENT);
        require(std::filesystem::create_directory(K_TARGET),
                "Static export directory creation failed",
                ErrorCode::IO_ERROR);
        for (const auto& file : manifest.files()) {
            check_stop(stop);
            if (file.m_path == "manifest.json") {
                continue;
            }
            const auto K_FILE = K_TARGET / file.m_path;
            std::filesystem::create_directories(K_FILE.parent_path());
            (void)take(asset_files::publish_asset(K_FILE, file.m_bytes, stop));
        }
        const auto K_MANIFEST = std::ranges::find(
            manifest.files(), std::string("manifest.json"), &ExportPackageFile::m_path);
        require(K_MANIFEST != manifest.files().end(), "Missing owned terminal export manifest");
        (void)take(
            asset_files::publish_asset(K_TARGET / "manifest.json", K_MANIFEST->m_bytes, stop));
        return {};
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Static export write allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

}  // namespace world_explorer
