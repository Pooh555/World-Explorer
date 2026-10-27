#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace world_explorer::test_support {

// Own the value so optional artifact paths do not borrow mutable CRT storage.
// Keep present-but-empty variables distinct from missing variables.
inline std::optional<std::string> probe_environment(const char* name) {
#if defined(_MSC_VER)
    char* value {};
    const auto K_ERROR = ::_dupenv_s(&value, nullptr, name);
    const std::unique_ptr<char, decltype(&std::free)> K_VALUE { value, &std::free };
    if (K_ERROR != 0) {
        throw std::runtime_error("Cannot read probe environment variable");
    }
    if (!K_VALUE) {
        return std::nullopt;
    }
    return std::string(K_VALUE.get());
#else
    const auto K_VALUE = std::getenv(name);
    if (!K_VALUE) {
        return std::nullopt;
    }
    return std::string(K_VALUE);
#endif
}

}  // namespace world_explorer::test_support
