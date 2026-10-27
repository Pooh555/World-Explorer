#pragma once

#include "world_explorer/application/native_assets.hpp"
#include "world_explorer/application/native_structure.hpp"
#include "world_explorer/application/static_export.hpp"

namespace world_explorer {

struct NativeExportPolicy {
    double m_position_units { 1e-5 }, m_normal { 1e-5 }, m_tangent { 1e-5 }, m_uv { 1e-5 };
    double m_linear_color { .005 }, m_numeric { .002 }, m_normal_map { .01 };
};

struct NativeExportLodSelection {
    AssetId m_asset;
    std::size_t m_level {};
};

struct NativeExportRequest {
    std::optional<AdmittedNativeAssets> m_assets;
    std::optional<AdmittedAssetReceipt> m_receipt;
    AssetDependencyContext m_expected;
    NativeExportPolicy m_policy;
    std::optional<AdmittedNativeStructure> m_structure {};
    // Explicit opt-in: exactly one selected level per definition, shared by all
    // its instances. All admitted levels/certificates remain in the package.
    std::optional<std::vector<NativeExportLodSelection>> m_lods {};
};

// A separate native textured PBR profile: <=2 definitions, <=4 total materials,
// <=64 visible instances, <=64 files /8 MiB. Core sRGB8/linear8 RGBA PNGs,
// arbitrary units, no captured-unlit conversion, HDR emission, instance material
// overrides or collider. LODs require the explicit additive selection profile.
Result<std::vector<ExportPackageFile>> prepare_native_export(const NativeExportRequest& request,
                                                             const std::stop_token& stop = {});

class AdmittedNativeExport {
  public:
    AdmittedNativeExport(const AdmittedNativeExport&) = default;
    AdmittedNativeExport& operator=(const AdmittedNativeExport&) = default;
    const std::vector<ExportPackageFile>& files() const;
    const std::string& manifest_sha256() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;

  private:
    struct Implementation;
    explicit AdmittedNativeExport(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedNativeExport> inspect_native_export(const NativeExportRequest&,
                                                              const std::vector<ExportPackageFile>&,
                                                              const std::stop_token&);
    friend Result<void> write_native_export(const AdmittedNativeExport&,
                                            const std::filesystem::path&,
                                            const std::stop_token&);
};

// Rebuilds every canonical file from independent private inputs. No general
// third-party importer, engine runner, relighting or AP qualification token.
Result<AdmittedNativeExport> inspect_native_export(const NativeExportRequest& request,
                                                   const std::vector<ExportPackageFile>& files,
                                                   const std::stop_token& stop = {});
Result<RecordCommit> publish_native_export(const AdmittedNativeExport& package,
                                           const AdmittedAssetReceipt& receipt,
                                           const std::filesystem::path& directory,
                                           OperationId operation,
                                           std::uint64_t base_generation,
                                           const AssetDependencyContext& expected,
                                           const std::stop_token& stop = {},
                                           const std::function<void(std::size_t)>& observed = {});
Result<AdmittedNativeExport> read_native_export(const std::filesystem::path& directory,
                                                const RecordCommit& commit,
                                                const NativeExportRequest& request,
                                                const std::vector<ExportPackageFile>& selected,
                                                const std::stop_token& stop = {});
Result<void> write_native_export(const AdmittedNativeExport& package,
                                 const std::filesystem::path& directory,
                                 const std::stop_token& stop = {});

}  // namespace world_explorer
