#pragma once

#include "world_explorer/asset_catalog/static_payloads.hpp"
#include "world_explorer/core/immutable_records.hpp"
#include "world_explorer/scene_store/asset_instances.hpp"

namespace world_explorer {

struct StaticExportDefinition {
    AssetDefinitionRef m_definition;
    std::vector<AssetProvenanceSelection> m_provenance;
};

struct StaticExportSource {
    std::optional<AdmittedAssetInstances> m_scene;
    std::optional<AdmittedAssetReceipt> m_receipt;
    std::vector<StaticExportDefinition> m_definitions;
    AssetDependencyContext m_expected;
    double m_max_position_error_units { 1e-5 }, m_max_normal_error { 1e-5 };
};

enum class ExportAuditState : std::uint8_t { NOT_RUN, REPORTED_PASS, REPORTED_FAIL };

struct ExportToolIdentity {
    std::string m_name, m_version, m_executable_sha256, m_package_sha256;
};

struct ExportAudit {
    ExportAuditState m_state { ExportAuditState::NOT_RUN };
    std::optional<ExportToolIdentity> m_tool;
    std::optional<std::string> m_report_sha256;
    std::vector<std::byte> m_report;
    std::uint64_t m_errors {}, m_warnings {};
    std::optional<std::string> m_payload_sha256 {};
};

struct ExportPackageFile {
    std::string m_path;
    std::vector<std::byte> m_bytes;
};

struct StaticExportRequest {
    StaticExportSource m_source;
    // Explicit NOT_RUN is permitted; omission is not an audit outcome.
    std::optional<ExportAudit> m_validator, m_importer;
};

// Deterministic proposal, still requiring independent source-bound admission.
// New generated-constant GLB profile only; captured/textured profiles unchanged.
Result<std::vector<ExportPackageFile>> prepare_static_export(const StaticExportRequest& request,
                                                             const std::stop_token& stop = {});

class AdmittedExportManifest {
  public:
    AdmittedExportManifest(const AdmittedExportManifest&) = default;
    AdmittedExportManifest& operator=(const AdmittedExportManifest&) = default;
    const std::vector<ExportPackageFile>& files() const;
    const std::string& manifest_sha256() const;
    const std::string& receipt_sha256() const;
    const std::string& source_instances_sha256() const;
    const AssetDependencyContext& context() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedExportManifest(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedExportManifest> inspect_static_export(
        const StaticExportRequest&, const std::vector<ExportPackageFile>&, const std::stop_token&);
    friend Result<void> write_static_export(const AdmittedExportManifest&,
                                            const std::filesystem::path&,
                                            const std::stop_token&);
};

// <=16 generated definitions /64 visible instances /1 MiB GLB /8 MiB package.
// Constant occlusion must be 1; HDR emission, textures, LOD/collision, metric units
// and hidden nodes are unsupported. All receipts remain unqualified declarations.
Result<AdmittedExportManifest> inspect_static_export(const StaticExportRequest& request,
                                                     const std::vector<ExportPackageFile>& files,
                                                     const std::stop_token& stop = {});
Result<RecordCommit> publish_export_manifest(const AdmittedExportManifest& manifest,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop = {},
                                             const std::function<void(std::size_t)>& observed = {});
Result<AdmittedExportManifest>
read_export_manifest(const std::filesystem::path& directory,
                     const RecordCommit& commit,
                     const StaticExportRequest& request,
                     const std::vector<ExportPackageFile>& selected_files,
                     const std::stop_token& stop = {});
// New local directory; data files first, manifest.json last. Only directories
// with a complete terminal manifest are consumable after interruption.
Result<void> write_static_export(const AdmittedExportManifest& manifest,
                                 const std::filesystem::path& directory,
                                 const std::stop_token& stop = {});

}  // namespace world_explorer
