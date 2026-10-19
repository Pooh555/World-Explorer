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

struct FileLock::Implementation {
#ifdef _WIN32
    HANDLE handle { INVALID_HANDLE_VALUE };

    ~Implementation() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int m_descriptor { -1 };

    ~Implementation() {
        if (m_descriptor >= 0) {
            close(m_descriptor);
        }
    }
#endif
};

FileLock::FileLock(std::unique_ptr<Implementation> state) : m_state(std::move(state)) { }

FileLock::~FileLock() = default;

Result<std::unique_ptr<FileLock>> FileLock::acquire(const std::filesystem::path& path) {
    try {
        if (path.empty() || path.native().find(std::filesystem::path::value_type {}) !=
                                std::filesystem::path::string_type::npos) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid writer lock path");
        }
        auto state = std::make_unique<Implementation>();
#ifdef _WIN32
        state->handle = CreateFileW(path.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                    nullptr);
        if (state->handle == INVALID_HANDLE_VALUE)
            return fail(GetLastError() == ERROR_SHARING_VIOLATION ? ErrorCode::WOULD_BLOCK
                                                                  : ErrorCode::IO_ERROR,
                        "Cannot acquire writer lock");
        BY_HANDLE_FILE_INFORMATION information {};
        if (!GetFileInformationByHandle(state->handle, &information) ||
            (information.dwFileAttributes &
             (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            information.nNumberOfLinks != 1 || information.nFileSizeHigh ||
            information.nFileSizeLow > 65536)
            return fail(ErrorCode::INVALID_DATA, "Writer lock must be a small regular file");
#else
        state->m_descriptor =
            ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (state->m_descriptor < 0) {
            return fail(ErrorCode::IO_ERROR, "Cannot open writer lock");
        }
        struct stat information {};
        if (fstat(state->m_descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
            information.st_nlink != 1 || information.st_size > 65536) {
            return fail(ErrorCode::INVALID_DATA, "Writer lock must be a small regular file");
        }
        if (flock(state->m_descriptor, LOCK_EX | LOCK_NB) != 0) {
            return fail(errno == EWOULDBLOCK || errno == EAGAIN ? ErrorCode::WOULD_BLOCK
                                                                : ErrorCode::IO_ERROR,
                        "Writer already active or locking failed");
        }
#endif
        return std::unique_ptr<FileLock>(new FileLock(std::move(state)));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Writer lock allocation failed");
    }
}

}  // namespace world_explorer
