#pragma once

#include "world_explorer/core/math.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>

namespace world_explorer::json_record_detail {

using Json = nlohmann::json;

struct RecordFailure : std::runtime_error {
    ErrorCode m_code;

    RecordFailure(ErrorCode code, const std::string& message)
        : std::runtime_error(message), m_code(code) { }
};

inline void
require(bool condition, const std::string& message, ErrorCode code = ErrorCode::INVALID_DATA) {
    if (!condition) {
        throw RecordFailure(code, message);
    }
}

inline void check_stop(const std::stop_token& stop) {
    require(!stop.stop_requested(), "Metadata operation cancelled", ErrorCode::CANCELLED);
}

inline void keys(const Json& value, std::initializer_list<std::string_view> names) {
    require(value.is_object() && value.size() == names.size(), "Unexpected metadata fields");
    for (auto name : names) {
        require(value.contains(name), "Missing metadata field");
    }
}

inline std::uint64_t integer(const Json& value) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Expected unsigned metadata integer");
    return value.get<std::uint64_t>();
}

inline std::string digest(const Json& value) {
    auto result = value.get<std::string>();
    require(result.size() == 64uz && result.find_first_not_of("0123456789abcdef") == result.npos,
            "Invalid metadata digest");
    return result;
}

inline void digest_text(const std::string& value) {
    require(value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == value.npos,
            "Invalid selected metadata digest",
            ErrorCode::INVALID_ARGUMENT);
}

inline void array(const Json& value, std::size_t maximum) {
    require(value.is_array(), "Expected metadata array");
    require(
        value.size() <= maximum, "Metadata array budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
}

inline double number(const Json& value) {
    require(value.is_number(), "Expected numeric metadata value");
    const auto K_VALUE = value.get<double>();
    require(std::isfinite(K_VALUE), "Nonfinite metadata number");
    return K_VALUE;
}

inline Vec3d vector3(const Json& value) {
    array(value, 3uz);
    require(value.size() == 3uz, "Invalid metadata vector");
    return { number(value[0uz]), number(value[1uz]), number(value[2uz]) };
}

inline SimilarityTransform transform(const Json& value) {
    keys(value, { "rotation", "translation", "scale" });
    const auto& q = value.at("rotation");
    array(q, 4uz);
    require(q.size() == 4uz, "Invalid metadata quaternion");
    return { { number(q[0uz]), number(q[1uz]), number(q[2uz]), number(q[3uz]) },
            vector3(value.at("translation")),
            number(value.at("scale")) };
}

inline Json
parse(std::span<const std::byte> bytes, std::size_t maximum, const std::stop_token& stop) {
    check_stop(stop);
    require(!bytes.empty(), "Empty metadata document");
    require(
        bytes.size() <= maximum, "Metadata byte budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
    const std::string_view K_TEXT(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(K_TEXT.find('\0') == K_TEXT.npos, "NUL in metadata document");
    std::vector<std::set<std::string>> seen;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        check_stop(stop);
        require(depth <= 16, "Metadata nesting budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            seen.resize(K_DEPTH + 1uz);
            seen[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(seen.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    "Duplicate metadata key");
        }
        return true;
    });
}

inline bool same_bytes(std::span<const std::byte> bytes, std::string_view text) {
    return bytes.size() == text.size() &&
           std::equal(bytes.begin(), bytes.end(), std::as_bytes(std::span(text)).begin());
}

}  // namespace world_explorer::json_record_detail
