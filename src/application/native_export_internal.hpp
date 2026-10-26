#pragma once

#include "../core/json_records.hpp"
#include "world_explorer/application/native_export.hpp"
#include "world_explorer/core/hash.hpp"

#include <span>

namespace world_explorer::native_export_detail {

using namespace json_record_detail;
inline constexpr auto K_PACKAGE_BYTES = 8uz * 1024uz * 1024uz;
inline constexpr std::string_view K_PROFILE { "WE_NATIVE_TEXTURED_GLTF_V1" };

template <class T> T take(Result<T> value) {
    if (!value) {
        throw RecordFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

template <class T> const T& selected(const std::optional<T>& value) {
    if (!value) {
        throw RecordFailure(ErrorCode::INVALID_ARGUMENT, "Independent native export input missing");
    }
    return *value;
}

inline std::vector<std::byte> bytes(const std::string& text) {
    const auto K_VIEW = std::as_bytes(std::span(text));
    return { K_VIEW.begin(), K_VIEW.end() };
}

inline Json xyz(Vec3d value) {
    return { value.m_x, value.m_y, value.m_z };
}

inline Json reference(const AssetRevisionRef& ref) {
    return { { "product_id", std::to_string(ref.m_product.m_value) },
            { "revision", std::to_string(ref.m_revision) },
            { "sha256", ref.m_content_sha256 } };
}

inline Json reference(const AssetDefinitionRef& ref) {
    return { { "asset_id", std::to_string(ref.m_asset.m_value) },
            { "revision", std::to_string(ref.m_revision) },
            { "sha256", ref.m_sha256 } };
}

struct Files {
    std::vector<ExportPackageFile> m_files;
    std::size_t m_bytes {};

    void add(std::string path, std::vector<std::byte> data) {
        const auto K_FOUND = std::ranges::find(m_files, path, &ExportPackageFile::m_path);
        if (K_FOUND != m_files.end()) {
            require(K_FOUND->m_bytes == data, "Conflicting native export file");
            return;
        }
        require(!data.empty() && data.size() <= 1024uz * 1024uz && m_files.size() < 64uz &&
                    data.size() <= K_PACKAGE_BYTES - m_bytes,
                "Native export package budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        m_bytes += data.size();
        m_files.push_back({ std::move(path), std::move(data) });
    }

    void json(std::string path, const Json& document) {
        add(std::move(path), bytes(document.dump()));
    }
};

struct Errors {
    double m_position {}, m_normal {}, m_tangent {}, m_uv {}, m_color {}, m_numeric {},
        m_normal_map {};
};

struct CompiledAsset {
    Json m_document, m_mapping;
    Errors m_errors;
};

Json document();
void transform_profile(const SimilarityTransform& transform);
CompiledAsset compile_asset(const AdmittedAssetDefinition& definition,
                            const NativeAssetSelection& source,
                            const AdmittedAssetInstances& instances,
                            Files& files,
                            const std::stop_token& stop);
void append_resources(Json& assembly, Json asset, const std::string& prefix);

}  // namespace world_explorer::native_export_detail
