#pragma once

#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace world_explorer::adapter_detail {

inline bool valid_digest(const std::string& digest) {
    return digest.size() == 64 && digest.find_first_not_of("0123456789abcdef") == std::string::npos;
}

inline nlohmann::json read_json(const std::filesystem::path& path,
                                std::size_t maximum,
                                const std::string& expected_sha256 = {}) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) ||
        std::filesystem::file_size(path) > maximum)
        throw std::invalid_argument("Missing, linked or oversized artifact manifest");
    std::ifstream file(path, std::ios::binary);
    std::string bytes(maximum + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    if (file.bad() || !file.eof() || count > maximum)
        throw std::invalid_argument("Artifact manifest changed or failed to read");
    bytes.resize(count);
    if (!expected_sha256.empty() &&
        sha256(std::as_bytes(std::span(bytes.data(), bytes.size()))) != expected_sha256)
        throw std::invalid_argument("Artifact manifest checksum mismatch during read");
    return nlohmann::json::parse(
        bytes, [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
            if (depth > 32)
                throw std::invalid_argument("Artifact manifest nesting exceeds 32 levels");
            return true;
        });
}

inline std::filesystem::path contained_file(const std::filesystem::path& root,
                                            const std::string& name) {
    if (name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        throw std::invalid_argument("Invalid artifact path");
    const std::filesystem::path relative(std::u8string(name.begin(), name.end()));
    if (relative.is_absolute() || relative.has_root_name())
        throw std::invalid_argument("Artifact path must be relative");
    for (const auto& part : relative)
        if (part == "." || part == "..")
            throw std::invalid_argument("Unsafe artifact path");
    const auto path = std::filesystem::canonical(root / relative);
    if (path.lexically_relative(root) != relative ||
        !std::filesystem::is_regular_file(std::filesystem::symlink_status(root / relative)))
        throw std::invalid_argument("Artifact escapes its directory or uses a symlink");
    return path;
}

inline std::uint64_t unsigned_value(const nlohmann::json& value) {
    if (!value.is_number_unsigned())
        throw std::invalid_argument("Expected unsigned artifact metadata");
    return value.get<std::uint64_t>();
}

}  // namespace world_explorer::adapter_detail
