#pragma once

#include "../core/json_records.hpp"
#include "world_explorer/asset_catalog/asset_definition.hpp"

#include <map>

namespace world_explorer::asset_metadata_detail {

using namespace json_record_detail;

inline AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

inline std::optional<AssetRevisionRef> optional_reference(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional { reference(value) };
}

inline AssetDefinitionRef definition_ref(const Json& value) {
    keys(value, { "asset_id", "revision", "sha256" });
    return {
        { integer(value.at("asset_id")) }, integer(value.at("revision")), digest(value.at("sha256")) };
}

template <class Enum> Enum named(const Json& value, const std::map<std::string, Enum>& names) {
    const auto K_FOUND = names.find(value.get<std::string>());
    require(K_FOUND != names.end(), "Unsupported metadata enum", ErrorCode::UNSUPPORTED_OPERATION);
    return K_FOUND->second;
}

inline AssetUnits units(const Json& value) {
    keys(value, { "status", "meters_per_unit", "measurement" });
    AssetUnits result;
    result.m_status =
        named<AssetUnitStatus>(value.at("status"),
                               { { "ARBITRARY", AssetUnitStatus::ARBITRARY },
                                { "DECLARED_METRIC", AssetUnitStatus::DECLARED_METRIC } });
    if (!value.at("meters_per_unit").is_null()) {
        result.m_meters_per_unit = number(value.at("meters_per_unit"));
    }
    result.m_measurement = optional_reference(value.at("measurement"));
    return result;
}

inline std::vector<std::byte> encoded(const std::string& value) {
    const auto K_BYTES = std::as_bytes(std::span(value));
    return { K_BYTES.begin(), K_BYTES.end() };
}

inline std::string definition_key(const AssetDefinitionRef& ref) {
    return "asset-" + std::to_string(ref.m_asset.m_value) + "-" + std::to_string(ref.m_revision);
}

}  // namespace world_explorer::asset_metadata_detail
