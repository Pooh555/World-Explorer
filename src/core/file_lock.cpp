#include "world_explorer/core/file_lock.hpp"
#include <cerrno>
#include <new>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace world_explorer {
struct file_lock::implementation {
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~implementation() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int descriptor{-1};
    ~implementation() {
        if (descriptor >= 0)
            close(descriptor);
    }
#endif
};
file_lock::file_lock(std::unique_ptr<implementation> state) : state_(std::move(state)) {}
file_lock::~file_lock() = default;
result<std::unique_ptr<file_lock>> file_lock::acquire(const std::filesystem::path& path) {
    try {
        if (path.empty() || path.native().find(std::filesystem::path::value_type{}) !=
                                std::filesystem::path::string_type::npos)
            return fail(error_code::INVALID_ARGUMENT, "Invalid writer lock path");
        auto state = std::make_unique<implementation>();
#ifdef _WIN32
        state->handle =
            CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (state->handle == INVALID_HANDLE_VALUE)
            return fail(GetLastError() == ERROR_SHARING_VIOLATION ? error_code::WOULD_BLOCK
                                                                  : error_code::IO_ERROR,
                        "Cannot acquire writer lock");
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(state->handle, &information) ||
            (information.dwFileAttributes &
             (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            information.nNumberOfLinks != 1 || information.nFileSizeHigh ||
            information.nFileSizeLow > 65536)
            return fail(error_code::INVALID_DATA, "Writer lock must be a small regular file");
#else
        state->descriptor =
            ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (state->descriptor < 0)
            return fail(error_code::IO_ERROR, "Cannot open writer lock");
        struct stat information{};
        if (fstat(state->descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
            information.st_nlink != 1 || information.st_size > 65536)
            return fail(error_code::INVALID_DATA, "Writer lock must be a small regular file");
        if (flock(state->descriptor, LOCK_EX | LOCK_NB) != 0)
            return fail(errno == EWOULDBLOCK || errno == EAGAIN ? error_code::WOULD_BLOCK
                                                                : error_code::IO_ERROR,
                        "Writer already active or locking failed");
#endif
        return std::unique_ptr<file_lock>(new file_lock(std::move(state)));
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Writer lock allocation failed");
    }
}
} // namespace world_explorer
