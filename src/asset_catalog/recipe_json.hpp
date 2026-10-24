#pragma once

#include "world_explorer/asset_catalog/recipe.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>

namespace world_explorer::asset_recipe_detail {

using Json = nlohmann::json;

struct RecipeFailure : std::runtime_error {
    ErrorCode m_code;

    RecipeFailure(ErrorCode code, const std::string& message)
        : std::runtime_error(message), m_code(code) { }
};

inline void
require(bool condition, const std::string& message, ErrorCode code = ErrorCode::INVALID_DATA) {
    if (!condition) {
        throw RecipeFailure(code, message);
    }
}

inline void check_stop(const std::stop_token& stop) {
    require(!stop.stop_requested(), "Recipe inspection cancelled", ErrorCode::CANCELLED);
}

inline void keys(const Json& value, std::initializer_list<std::string_view> names) {
    require(value.is_object() && value.size() == names.size(), "Unexpected recipe fields");
    for (const auto K_NAME : names) {
        require(value.contains(K_NAME), "Missing recipe field");
    }
}

inline std::uint64_t integer(const Json& value) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Recipe requires an unsigned integer");
    return value.get<std::uint64_t>();
}

inline std::uint64_t count(const Json& value, std::uint64_t maximum) {
    const auto K_COUNT = integer(value);
    require(K_COUNT > 0 && K_COUNT <= maximum, "Recipe count exceeds schema limits");
    return K_COUNT;
}

inline std::string digest(const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    require(K_TEXT.size() == 64uz &&
                K_TEXT.find_first_not_of("0123456789abcdef") == std::string::npos,
            "Invalid recipe SHA-256");
    return K_TEXT;
}

inline std::optional<std::string> optional_digest(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional { digest(value) };
}

inline std::optional<std::uint64_t> size(const Json& value, bool allow_zero = false) {
    if (value.is_null()) {
        return std::nullopt;
    }
    const auto K_BYTES = integer(value);
    require(allow_zero || K_BYTES > 0, "Recipe installed payload must be nonempty");
    return K_BYTES;
}

template <class Enum> Enum named(const Json& value, const std::map<std::string, Enum>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = names.find(K_TEXT);
    require(K_FOUND != names.end(), "Unsupported recipe enum", ErrorCode::UNSUPPORTED_OPERATION);
    return K_FOUND->second;
}

inline AssetRecipeRights rights(const Json& value) {
    static const std::map<std::string, AssetRecipeRights> K_NAMES {
        { "UNREVIEWED", AssetRecipeRights::UNREVIEWED },
        { "DECLARED_PERMITTED", AssetRecipeRights::DECLARED_PERMITTED },
        { "RESTRICTED", AssetRecipeRights::RESTRICTED } };
    return named(value, K_NAMES);
}

inline AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

inline Json parse(std::span<const std::byte> bytes) {
    const std::string_view K_TEXT(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(K_TEXT.find('\0') == K_TEXT.npos, "NUL in recipe JSON");
    std::vector<std::set<std::string>> object_keys;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 16, "Recipe JSON nesting exceeds budget", ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            object_keys.resize(K_DEPTH + 1uz);
            object_keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(object_keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    "Duplicate recipe JSON key");
        }
        return true;
    });
}

inline std::uint64_t add(std::uint64_t first, std::uint64_t second) {
    require(second <= std::numeric_limits<std::uint64_t>::max() - first,
            "Recipe byte reservation overflows",
            ErrorCode::RESOURCE_EXHAUSTED);
    return first + second;
}

}  // namespace world_explorer::asset_recipe_detail
