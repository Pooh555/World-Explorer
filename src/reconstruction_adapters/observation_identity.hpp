#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_OBSERVATION_IDENTITY_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_OBSERVATION_IDENTITY_HPP
#include "world_explorer/core/hash.hpp"
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace world_explorer::adapter_detail {
inline std::uint64_t identity(const std::string& source, std::string_view kind,
                              std::string_view external) {
    const auto input = "world_explorer.observation.v1/" + source + "/" + std::string(kind) + "/" +
                       std::string(external);
    const auto digest = sha256(std::as_bytes(std::span(input.data(), input.size())));
    std::uint64_t value{};
    const auto parsed = std::from_chars(digest.data(), digest.data() + 16, value, 16);
    if (parsed.ec != std::errc{} || value == 0)
        throw std::invalid_argument("Invalid observation identity");
    return value;
}
} // namespace world_explorer::adapter_detail
#endif
