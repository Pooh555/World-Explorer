#ifndef WORLD_EXPLORER_CORE_RESULT_HPP
#define WORLD_EXPLORER_CORE_RESULT_HPP
#include <cstdint>
#include <expected>
#include <string>

namespace world_explorer {

enum class ErrorCode : std::uint8_t {
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

struct Error {
    ErrorCode m_code;
    std::string m_message;
};
template <class ValueType> using Result = std::expected<ValueType, Error>;

inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error { code, std::move(message) });
}

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_CORE_RESULT_HPP
