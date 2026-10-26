#pragma once

#include "asset_edit_fixture.hpp"
#include "world_explorer/application/static_export.hpp"

#include <span>

namespace world_explorer::static_export_test {

using namespace asset_edit_test;

template <class T> T checked(Result<T> result) {
    INFO((result ? "admitted" : result.error().m_message));
    REQUIRE(result);
    return *result;
}

struct StaticExportOptions {
    std::size_t m_assets { 1uz }, m_instances_per_asset { 2uz };
    std::function<void(Json&, Json&)> m_payload {};
    std::function<void(AssetDefinition&)> m_definition {};
    std::function<void(AssetInstanceSet&)> m_instances {};
};

struct StaticExportFixture {
    AssetEditFixture m_base;
    RecipeFixture m_output;
    AdmittedAssetReceipt m_receipt;
    std::vector<AdmittedAssetDefinition> m_definitions;
    AdmittedAssetInstances m_scene;

    explicit StaticExportFixture(const StaticExportOptions& options = {})
        : m_receipt(stage(options)), m_definitions(definitions(options)), m_scene(scene(options)) {
    }

    AdmittedAssetReceipt stage(const StaticExportOptions& options) {
        if (options.m_payload) {
            options.m_payload(m_base.m_geometry, m_base.m_material);
        }
        AssetDefinitionEditSelection selected;
        return m_base.stage(m_base.m_before, m_base.m_geometry, m_base.m_material, selected);
    }

    std::vector<AdmittedAssetDefinition> definitions(const StaticExportOptions& options) {
        auto value = m_base.m_before.m_definition;
        if (options.m_definition) {
            options.m_definition(value);
        }
        bool first = true;
        for (const auto& row : m_base.m_geometry["positions"]) {
            const auto K_POINT = transform_point(value.m_pivot.m_asset_from_geometry,
                                                 { row[0uz].template get<double>(),
                                                  row[1uz].template get<double>(),
                                                  row[2uz].template get<double>() });
            if (first) {
                value.m_bounds = { K_POINT, K_POINT };
                first = false;
            }
            value.m_bounds.m_minimum = { std::min(value.m_bounds.m_minimum.m_x, K_POINT.m_x),
                                        std::min(value.m_bounds.m_minimum.m_y, K_POINT.m_y),
                                        std::min(value.m_bounds.m_minimum.m_z, K_POINT.m_z) };
            value.m_bounds.m_maximum = { std::max(value.m_bounds.m_maximum.m_x, K_POINT.m_x),
                                        std::max(value.m_bounds.m_maximum.m_y, K_POINT.m_y),
                                        std::max(value.m_bounds.m_maximum.m_z, K_POINT.m_z) };
        }
        std::vector<AdmittedAssetDefinition> result;
        const auto K_FIRST = value.m_asset.m_value;
        for (std::size_t asset { 0uz }; asset < options.m_assets; ++asset) {
            value.m_asset = { K_FIRST + asset };
            result.push_back(checked(admit_asset_definition(
                value, m_receipt, m_base.m_before.m_provenance, m_receipt.graph().m_context)));
        }
        return result;
    }

    AdmittedAssetInstances scene(const StaticExportOptions& options) const {
        auto value = m_base.m_before.instances(m_definitions[0uz]);
        value.m_instances.clear();
        for (const auto& definition : m_definitions) {
            for (std::size_t instance { 0uz }; instance < options.m_instances_per_asset; ++instance) {
                value.m_instances.push_back(
                    { { value.m_instances.size() + 1uz },
                     definition.reference(),
                     { {}, { static_cast<double>(value.m_instances.size()) * 5, 0, 0 }, 1 },
                     { 7 },
                     true,
                     {},
                     {},
                     {},
                     definition.definition().m_manual_ancestor });
            }
        }
        if (options.m_instances) {
            options.m_instances(value);
        }
        return checked(admit_asset_instances(value, m_definitions, m_receipt, value.m_context));
    }

    StaticExportRequest request() const {
        std::vector<StaticExportDefinition> selected;
        for (const auto& definition : m_definitions) {
            selected.push_back({ definition.reference(), m_base.m_before.m_provenance });
        }
        return { { m_scene, m_receipt, selected, m_receipt.graph().m_context, 1e-5, 1e-5 },
                ExportAudit {},
                ExportAudit {} };
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "records";
    }
};

inline ExportPackageFile& file(std::vector<ExportPackageFile>& files, const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    if (K_FOUND == files.end()) {
        throw std::logic_error("Missing export fixture file");
    }
    return *K_FOUND;
}

inline Json document(const ExportPackageFile& file) {
    return Json::parse(
        std::string_view(reinterpret_cast<const char*>(file.m_bytes.data()), file.m_bytes.size()));
}

inline std::uint32_t word(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t result {};
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        result |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (8uz * index);
    }
    return result;
}

inline Json glb_document(const std::vector<std::byte>& bytes) {
    const auto K_LENGTH = word(bytes, 12uz);
    return Json::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()) + 20uz, K_LENGTH));
}

inline ExportAudit validator_audit(const std::vector<std::byte>& payload,
                                   const std::string& version = "synthetic-fixture",
                                   std::uint64_t errors = 0) {
    const auto K_TEXT = Json { { "validatorVersion", version },
                              { "issues", { { "numErrors", errors }, { "numWarnings", 0 } } } }
                            .dump();
    const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
    return { errors ? ExportAuditState::REPORTED_FAIL : ExportAuditState::REPORTED_PASS,
            ExportToolIdentity { "KHRONOS_GLTF_VALIDATOR",
                                version,
                                hash("synthetic executable"),
                                hash("synthetic package") },
            hash(K_TEXT),
            { K_BYTES.begin(), K_BYTES.end() },
            errors,
            0,
            sha256(payload) };
}

}  // namespace world_explorer::static_export_test
