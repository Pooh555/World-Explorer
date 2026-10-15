#ifndef WORLD_EXPLORER_CORE_RESULT_HPP
#define WORLD_EXPLORER_CORE_RESULT_HPP
#include <cstdint>

#include <expected>
#include <string>

namespace world_explorer {
enum class error_code : std::uint8_t {
    INVALID_ARGUMENT,
    INVALID_DATA,
    UNSUPPORTED_OPERATION,
    WOULD_BLOCK,
    CANCELLED,
    IO_ERROR,
    RESOURCE_EXHAUSTED,
    WRONG_BASE,
    WRONG_EPOCH,
    RESYNC_REQUIRED,
    DUPLICATE_OPERATION,
    NOT_FOUND,
    BACKEND_FAILURE,
    DEVICE_LOST
};
struct error {
    error_code code;
    std::string message;
};
template <class value_type> using result = std::expected<value_type, error>;
inline std::unexpected<error> fail(error_code code, std::string message) {
    return std::unexpected(error{code, std::move(message)});
}
} // namespace world_explorer

#endif // WORLD_EXPLORER_CORE_RESULT_HPP
