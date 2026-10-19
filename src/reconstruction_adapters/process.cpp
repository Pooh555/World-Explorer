#include "world_explorer/reconstruction_adapters/process.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
extern char** environ;
#endif

namespace world_explorer {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& value) {
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring output(static_cast<std::size_t>(count), 0);
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        output.data(),
                        count);
    return output;
}

std::wstring quote_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        quoted.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        quoted += character;
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L'"';
}

struct native_handle {
    HANDLE value {};

    ~native_handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};

struct pipe_pair {
    native_handle read, write;

    bool create() {
        static std::atomic<std::uint64_t> sequence {};
        const auto name = L"\\\\.\\pipe\\world_explorer_" + std::to_wstring(GetCurrentProcessId()) +
                          L"_" + std::to_wstring(sequence.fetch_add(1));
        read.value = CreateNamedPipeW(name.c_str(),
                                      PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                      PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                      1,
                                      65536,
                                      65536,
                                      0,
                                      nullptr);
        if (read.value == INVALID_HANDLE_VALUE)
            return false;
        SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        write.value =
            CreateFileW(name.c_str(), GENERIC_WRITE, 0, &security, OPEN_EXISTING, 0, nullptr);
        return write.value != INVALID_HANDLE_VALUE &&
               (ConnectNamedPipe(read.value, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED);
    }
};

struct input_channel {
    native_handle server, child, event;
    OVERLAPPED overlap {};
    std::vector<std::byte> pending;
    std::size_t offset {};
    bool writing {}, closed {};

    ~input_channel() {
        if (writing) {
            CancelIoEx(server.value, &overlap);
            DWORD transferred {};
            GetOverlappedResult(server.value, &overlap, &transferred, TRUE);
        }
    }

    bool create(bool duplex) {
        SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        if (!duplex) {
            child.value = CreateFileW(L"NUL",
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &security,
                                      OPEN_EXISTING,
                                      0,
                                      nullptr);
            return child.value != INVALID_HANDLE_VALUE;
        }
        static std::atomic<std::uint64_t> sequence {};
        const auto name = L"\\\\.\\pipe\\world_explorer_input_" +
                          std::to_wstring(GetCurrentProcessId()) + L"_" +
                          std::to_wstring(sequence.fetch_add(1));
        server.value = CreateNamedPipeW(name.c_str(),
                                        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED |
                                            FILE_FLAG_FIRST_PIPE_INSTANCE,
                                        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                        1,
                                        65536,
                                        65536,
                                        0,
                                        nullptr);
        if (server.value == INVALID_HANDLE_VALUE)
            return false;
        event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event.value)
            return false;
        overlap.hEvent = event.value;
        child.value =
            CreateFileW(name.c_str(), GENERIC_READ, 0, &security, OPEN_EXISTING, 0, nullptr);
        return child.value != INVALID_HANDLE_VALUE &&
               (ConnectNamedPipe(server.value, &overlap) || GetLastError() == ERROR_PIPE_CONNECTED);
    }

    Result<void> pump(const ProcessControl& control, bool cancelling) {
        if (!control.m_next_input || closed)
            return {};
        if (writing) {
            DWORD transferred {};
            if (!GetOverlappedResult(server.value, &overlap, &transferred, FALSE)) {
                const auto code = GetLastError();
                if (code == ERROR_IO_INCOMPLETE)
                    return {};
                writing = false;
                if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA) {
                    closed = true;
                    return {};
                }
                return fail(ErrorCode::IO_ERROR, "Unable to finish worker input write");
            }
            writing = false;
            offset += transferred;
            if (offset == pending.size())
                pending.clear();
        }
        if (pending.empty()) {
            auto next = control.m_next_input(cancelling);
            if (!next)
                return std::unexpected(next.error());
            if (next->size() > K_PROCESS_INPUT_LIMIT)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Process input exceeds frame budget");
            pending = std::move(*next);
            offset = 0;
        }
        if (pending.empty())
            return {};
        ResetEvent(event.value);
        overlap = {};
        overlap.hEvent = event.value;
        const auto bytes = static_cast<DWORD>(std::min(pending.size() - offset, 4096uz));
        if (!WriteFile(server.value, pending.data() + offset, bytes, nullptr, &overlap)) {
            const auto code = GetLastError();
            if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA) {
                closed = true;
                return {};
            }
            if (code != ERROR_IO_PENDING)
                return fail(ErrorCode::IO_ERROR, "Unable to write worker input");
        }
        writing = true;
        return {};
    }
};

}  // namespace

Result<int> execute_process(const std::vector<std::string>& arguments,
                            const std::stop_token& stop,
                            const ProcessOutputCallback& output,
                            const ProcessOutputCallback& diagnostic,
                            const ProcessControl& control) {
    if (arguments.empty() || stop.stop_requested())
        return fail(ErrorCode::CANCELLED, "Process cancelled before launch");
    if (control.m_cancellation_grace < std::chrono::milliseconds(0) ||
        control.m_cancellation_grace > std::chrono::seconds(5))
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid process cancellation deadline");
    std::wstring command;
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos || argument.size() > 32768)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid process argument");
        if (!command.empty())
            command += L' ';
        const auto wide = widen(argument);
        if (!argument.empty() && wide.empty())
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid UTF-8 process argument");
        command += quote_argument(wide);
    }
    if (command.size() > 32766)
        return fail(ErrorCode::INVALID_ARGUMENT, "Process command exceeds Windows limit");
    native_handle job { CreateJobObjectW(nullptr, nullptr) };
    pipe_pair stdout_pipe, stderr_pipe;
    input_channel input;
    if (!job.value || !stdout_pipe.create() || !stderr_pipe.create() ||
        !input.create(static_cast<bool>(control.m_next_input)))
        return fail(ErrorCode::IO_ERROR, "Unable to create child pipes/job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return fail(ErrorCode::IO_ERROR, "Unable to bound child lifetime");
    STARTUPINFOEXW startup {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = stdout_pipe.write.value;
    startup.StartupInfo.hStdError = stderr_pipe.write.value;
    startup.StartupInfo.hStdInput = input.child.value;
    SIZE_T attribute_bytes {};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<std::max_align_t> attributes((attribute_bytes + sizeof(std::max_align_t) - 1) /
                                             sizeof(std::max_align_t));
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        return fail(ErrorCode::IO_ERROR, "Unable to initialize child handle list");

    struct attribute_owner {
        LPPROC_THREAD_ATTRIBUTE_LIST list;

        ~attribute_owner() {
            DeleteProcThreadAttributeList(list);
        }
    } attribute_cleanup { startup.lpAttributeList };

    std::array inherited { input.child.value, stdout_pipe.write.value, stderr_pipe.write.value };
    if (!UpdateProcThreadAttribute(startup.lpAttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited.data(),
                                   sizeof(inherited),
                                   nullptr,
                                   nullptr))
        return fail(ErrorCode::IO_ERROR, "Unable to restrict inherited child handles");
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(nullptr,
                        command.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr,
                        nullptr,
                        &startup.StartupInfo,
                        &process))
        return fail(ErrorCode::BACKEND_FAILURE, "Unable to start worker executable");
    native_handle process_handle { process.hProcess }, thread_handle { process.hThread };
    if (!AssignProcessToJobObject(job.value, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        return fail(ErrorCode::IO_ERROR, "Unable to contain worker process");
    }

    struct child_owner {
        HANDLE job, process;

        ~child_owner() {
            TerminateJobObject(job, 130);
            WaitForSingleObject(process, INFINITE);
        }
    } owner { job.value, process.hProcess };

    ResumeThread(process.hThread);
    CloseHandle(input.child.value);
    input.child.value = nullptr;
    CloseHandle(stdout_pipe.write.value);
    stdout_pipe.write.value = nullptr;
    CloseHandle(stderr_pipe.write.value);
    stderr_pipe.write.value = nullptr;
    std::array<char, 4096> buffer {};
    bool ended = false, cancelling = false;
    auto cancelled_at = std::chrono::steady_clock::now();
    while (true) {
        if (stop.stop_requested() && !cancelling) {
            cancelling = true;
            cancelled_at = std::chrono::steady_clock::now();
        }
        const auto grace =
            control.m_next_input ? control.m_cancellation_grace : std::chrono::milliseconds(0);
        if (cancelling && std::chrono::steady_clock::now() - cancelled_at >= grace)
            TerminateJobObject(job.value, 130);
        const auto sent = input.pump(control, cancelling);
        if (!sent)
            return std::unexpected(sent.error());
        bool drained = false;
        const std::array streams { std::pair { stdout_pipe.read.value, &output },
                                  std::pair { stderr_pipe.read.value, &diagnostic } };
        for (const auto& [pipe, callback] : streams) {
            for (int block { 0 }; block < 16; ++block) {
                DWORD available {}, read {};
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available ||
                    !ReadFile(
                        pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) ||
                    !read)
                    break;
                drained = true;
                if (*callback)
                    (*callback)(std::string(buffer.data(), read));
            }
        }
        if (ended && !drained)
            break;
        ended = WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0;
        if (ended)
            TerminateJobObject(job.value, 130);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    if (stop.stop_requested())
        return fail(ErrorCode::CANCELLED, "Worker cancelled; committed stages retained");
    return static_cast<int>(code);
}
#else
namespace {

int reserve_descriptor(int descriptor) {
    if (descriptor < 0 || descriptor >= 3) {
        return descriptor;
    }
    const auto K_OWNED = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    close(descriptor);
    return K_OWNED;
}

// Blocking SIGPIPE only on this owner avoids changing the application's global
// signal policy. Consume a newly generated signal before restoring its mask.
ssize_t write_input(int descriptor, const std::byte* bytes, std::size_t size) {
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const auto K_MASKED = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (K_MASKED != 0) {
        errno = K_MASKED;
        return -1;
    }
    sigpending(&pending);
    const bool K_PRIOR = sigismember(&pending, SIGPIPE) == 1;
    const auto K_WRITTEN = write(descriptor, bytes, size);
    const auto K_CODE = errno;
    if (K_WRITTEN < 0 && K_CODE == EPIPE && !K_PRIOR) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1) {
            int signal {};
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = K_CODE;
    return K_WRITTEN;
}

}  // namespace

Result<int> execute_process(const std::vector<std::string>& arguments,
                            const std::stop_token& stop,
                            const ProcessOutputCallback& output,
                            const ProcessOutputCallback& diagnostic,
                            const ProcessControl& control) {
    if (arguments.empty()) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Empty process command");
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Process cancelled before launch");
    }
    if (control.m_cancellation_grace < std::chrono::milliseconds(0) ||
        control.m_cancellation_grace > std::chrono::seconds(5)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid process cancellation deadline");
    }
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos) {
            return fail(ErrorCode::INVALID_ARGUMENT, "NUL in process argument");
        }
    }

    struct Descriptors {
        int m_read { -1 }, m_write { -1 };

        bool create() {
            int handles[2];
            if (pipe(handles) != 0) {
                return false;
            }
            m_read = reserve_descriptor(handles[0]);
            m_write = reserve_descriptor(handles[1]);
            return fcntl(m_read, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(m_write, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(m_read, F_SETFL, O_NONBLOCK) >= 0;
        }

        ~Descriptors() {
            if (m_read >= 0) {
                close(m_read);
            }
            if (m_write >= 0) {
                close(m_write);
            }
        }
    } stdout_pipe, stderr_pipe;

    if (!stdout_pipe.create() || !stderr_pipe.create()) {
        return fail(ErrorCode::IO_ERROR, "Unable to create worker streams");
    }

    struct InputChannel {
        int m_child { -1 }, m_parent { -1 };

        ~InputChannel() {
            if (m_child >= 0) {
                close(m_child);
            }
            if (m_parent >= 0) {
                close(m_parent);
            }
        }

        bool create(bool duplex) {
            if (!duplex) {
                m_child = reserve_descriptor(open("/dev/null", O_RDONLY | O_CLOEXEC));
                return m_child >= 0;
            }
            int handles[2];
            if (pipe(handles) != 0) {
                return false;
            }
            m_child = reserve_descriptor(handles[0]);
            m_parent = reserve_descriptor(handles[1]);

            return fcntl(m_child, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(m_parent, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(m_parent, F_SETFL, O_NONBLOCK) >= 0;
        }
    } input;

    if (!input.create(static_cast<bool>(control.m_next_input))) {
        return fail(ErrorCode::IO_ERROR, "Unable to create worker input");
    }
    std::vector<std::byte> pending_input;
    std::size_t input_offset {};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);

    struct SpawnCleanup {
        posix_spawn_file_actions_t* m_actions;
        posix_spawnattr_t* m_attributes;

        ~SpawnCleanup() {
            posix_spawn_file_actions_destroy(m_actions);
            posix_spawnattr_destroy(m_attributes);
        }
    } cleanup { &actions, &attributes };

    posix_spawn_file_actions_adddup2(&actions, input.m_child, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, input.m_child);
    if (input.m_parent >= 0) {
        posix_spawn_file_actions_addclose(&actions, input.m_parent);
    }
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe.m_write, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe.m_write, STDERR_FILENO);
    for (const auto K_HANDLE :
         { stdout_pipe.m_read, stdout_pipe.m_write, stderr_pipe.m_read, stderr_pipe.m_write }) {
        posix_spawn_file_actions_addclose(&actions, K_HANDLE);
    }
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child {};
    const auto K_SPAWNED =
        posix_spawnp(&child, argv[0], &actions, &attributes, argv.data(), environ);
    if (K_SPAWNED != 0) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    "Unable to start worker executable (code " + std::to_string(K_SPAWNED) + ")");
    }
    close(input.m_child);
    input.m_child = -1;
    close(stdout_pipe.m_write);
    stdout_pipe.m_write = -1;
    close(stderr_pipe.m_write);
    stderr_pipe.m_write = -1;

    struct ChildOwner {
        pid_t m_child;
        bool m_reaped {};

        ~ChildOwner() {
            if (!m_reaped) {
                kill(-m_child, SIGKILL);
                while (waitpid(m_child, nullptr, 0) < 0 && errno == EINTR) { }
            }
        }
    } owner { child };

    bool terminating = false, signalled = false;
    auto cancelled_at = std::chrono::steady_clock::now();
    std::array<char, 4096> buffer {};
    int status {};
    while (true) {
        if (stop.stop_requested() && !terminating) {
            terminating = true;
            cancelled_at = std::chrono::steady_clock::now();
        }
        const auto K_GRACE =
            control.m_next_input ? control.m_cancellation_grace : std::chrono::milliseconds(0);
        if (terminating && !signalled &&
            std::chrono::steady_clock::now() - cancelled_at >= K_GRACE) {
            kill(-child, SIGTERM);
            signalled = true;
        }
        if (terminating &&
            std::chrono::steady_clock::now() - cancelled_at >= K_GRACE + std::chrono::seconds(2)) {
            kill(-child, SIGKILL);
        }
        if (input.m_parent >= 0) {
            if (pending_input.empty()) {
                auto next = control.m_next_input(terminating);
                if (!next) {
                    return std::unexpected(next.error());
                }
                if (next->size() > K_PROCESS_INPUT_LIMIT) {
                    return fail(ErrorCode::RESOURCE_EXHAUSTED,
                                "Process input exceeds frame budget");
                }
                pending_input = std::move(*next);
                input_offset = 0;
            }
            if (!pending_input.empty()) {
                const auto K_COUNT = write_input(input.m_parent,
                                                 pending_input.data() + input_offset,
                                                 pending_input.size() - input_offset);
                if (K_COUNT > 0) {
                    input_offset += static_cast<std::size_t>(K_COUNT);
                    if (input_offset == pending_input.size()) {
                        pending_input.clear();
                    }
                } else if (K_COUNT < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    if (errno != EPIPE && errno != ECONNRESET) {
                        return fail(ErrorCode::IO_ERROR,
                                    "Unable to write worker input (code " + std::to_string(errno) +
                                        ")");
                    }
                    close(input.m_parent);
                    input.m_parent = -1;
                    pending_input.clear();
                }
            }
        }
        std::array<pollfd, 2> poll_descriptors {
            { { stdout_pipe.m_read, POLLIN, 0 }, { stderr_pipe.m_read, POLLIN, 0 } } };
        poll(poll_descriptors.data(), static_cast<nfds_t>(poll_descriptors.size()), 20);
        // Bound work each cycle so continuous log output cannot starve cancellation.
        const std::array K_STREAMS { std::pair { stdout_pipe.m_read, &output },
                                    std::pair { stderr_pipe.m_read, &diagnostic } };
        for (const auto& [K_PIPE, K_CALLBACK] : K_STREAMS) {
            for (int block { 0 }; block < 16; ++block) {
                const auto K_COUNT = read(K_PIPE, buffer.data(), buffer.size());
                if (K_COUNT <= 0) {
                    break;
                }
                if (*K_CALLBACK) {
                    (*K_CALLBACK)(std::string(buffer.data(), static_cast<std::size_t>(K_COUNT)));
                }
            }
        }
        const auto K_WAITED = waitpid(child, &status, WNOHANG);
        if (K_WAITED == child) {
            owner.m_reaped = true;
            // Reap any grandchildren left behind by a failed coordinator.
            kill(-child, SIGKILL);
            for (const auto& [K_PIPE, K_CALLBACK] : K_STREAMS) {
                while (true) {
                    const auto K_COUNT = read(K_PIPE, buffer.data(), buffer.size());
                    if (K_COUNT <= 0) {
                        break;
                    }
                    if (*K_CALLBACK) {
                        (*K_CALLBACK)(
                            std::string(buffer.data(), static_cast<std::size_t>(K_COUNT)));
                    }
                }
            }
            break;
        }
        if (K_WAITED < 0 && errno != EINTR) {
            return fail(ErrorCode::IO_ERROR, "Unable to reap worker process");
        }
    }
    if (terminating) {
        return fail(ErrorCode::CANCELLED, "Worker cancelled; committed stages retained");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
#endif

}  // namespace world_explorer
