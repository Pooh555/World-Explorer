#include "world_explorer/reconstruction_adapters/process.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace world_explorer {
#ifdef _WIN32
namespace {
std::wstring widen(const std::string& value) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring output(static_cast<std::size_t>(count), 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        output.data(), count);
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
    HANDLE value{};
    ~native_handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct pipe_pair {
    native_handle read, write;
    bool create() {
        static std::atomic<std::uint64_t> sequence{};
        const auto name = L"\\\\.\\pipe\\world_explorer_" + std::to_wstring(GetCurrentProcessId()) +
                          L"_" + std::to_wstring(sequence.fetch_add(1));
        read.value = CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
        if (read.value == INVALID_HANDLE_VALUE)
            return false;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        write.value =
            CreateFileW(name.c_str(), GENERIC_WRITE, 0, &security, OPEN_EXISTING, 0, nullptr);
        return write.value != INVALID_HANDLE_VALUE &&
               (ConnectNamedPipe(read.value, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED);
    }
};
struct input_channel {
    native_handle server, child, event;
    OVERLAPPED overlap{};
    std::vector<std::byte> pending;
    std::size_t offset{};
    bool writing{}, closed{};
    ~input_channel() {
        if (writing) {
            CancelIoEx(server.value, &overlap);
            DWORD transferred{};
            GetOverlappedResult(server.value, &overlap, &transferred, TRUE);
        }
    }
    bool create(bool duplex) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        if (!duplex) {
            child.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &security, OPEN_EXISTING, 0, nullptr);
            return child.value != INVALID_HANDLE_VALUE;
        }
        static std::atomic<std::uint64_t> sequence{};
        const auto name = L"\\\\.\\pipe\\world_explorer_input_" +
                          std::to_wstring(GetCurrentProcessId()) + L"_" +
                          std::to_wstring(sequence.fetch_add(1));
        server.value = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
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
    result<void> pump(const process_control& control, bool cancelling) {
        if (!control.next_input || closed)
            return {};
        if (writing) {
            DWORD transferred{};
            if (!GetOverlappedResult(server.value, &overlap, &transferred, FALSE)) {
                const auto code = GetLastError();
                if (code == ERROR_IO_INCOMPLETE)
                    return {};
                writing = false;
                if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA) {
                    closed = true;
                    return {};
                }
                return fail(error_code::IO_ERROR, "Unable to finish worker input write");
            }
            writing = false;
            offset += transferred;
            if (offset == pending.size())
                pending.clear();
        }
        if (pending.empty()) {
            auto next = control.next_input(cancelling);
            if (!next)
                return std::unexpected(next.error());
            if (next->size() > PROCESS_INPUT_LIMIT)
                return fail(error_code::RESOURCE_EXHAUSTED, "Process input exceeds frame budget");
            pending = std::move(*next);
            offset = 0;
        }
        if (pending.empty())
            return {};
        ResetEvent(event.value);
        overlap = {};
        overlap.hEvent = event.value;
        const auto bytes = static_cast<DWORD>(std::min(pending.size() - offset, std::size_t{4096}));
        if (!WriteFile(server.value, pending.data() + offset, bytes, nullptr, &overlap)) {
            const auto code = GetLastError();
            if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA) {
                closed = true;
                return {};
            }
            if (code != ERROR_IO_PENDING)
                return fail(error_code::IO_ERROR, "Unable to write worker input");
        }
        writing = true;
        return {};
    }
};
} // namespace
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic,
                            const process_control& control) {
    if (arguments.empty() || stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
    if (control.cancellation_grace < std::chrono::milliseconds(0) ||
        control.cancellation_grace > std::chrono::seconds(5))
        return fail(error_code::INVALID_ARGUMENT, "Invalid process cancellation deadline");
    std::wstring command;
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos || argument.size() > 32768)
            return fail(error_code::INVALID_ARGUMENT, "Invalid process argument");
        if (!command.empty())
            command += L' ';
        const auto wide = widen(argument);
        if (!argument.empty() && wide.empty())
            return fail(error_code::INVALID_ARGUMENT, "Invalid UTF-8 process argument");
        command += quote_argument(wide);
    }
    if (command.size() > 32766)
        return fail(error_code::INVALID_ARGUMENT, "Process command exceeds Windows limit");
    native_handle job{CreateJobObjectW(nullptr, nullptr)};
    pipe_pair stdout_pipe, stderr_pipe;
    input_channel input;
    if (!job.value || !stdout_pipe.create() || !stderr_pipe.create() ||
        !input.create(static_cast<bool>(control.next_input)))
        return fail(error_code::IO_ERROR, "Unable to create child pipes/job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        return fail(error_code::IO_ERROR, "Unable to bound child lifetime");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = stdout_pipe.write.value;
    startup.StartupInfo.hStdError = stderr_pipe.write.value;
    startup.StartupInfo.hStdInput = input.child.value;
    SIZE_T attribute_bytes{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<std::max_align_t> attributes((attribute_bytes + sizeof(std::max_align_t) - 1) /
                                             sizeof(std::max_align_t));
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        return fail(error_code::IO_ERROR, "Unable to initialize child handle list");
    struct attribute_owner {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~attribute_owner() { DeleteProcThreadAttributeList(list); }
    } attribute_cleanup{startup.lpAttributeList};
    std::array inherited{input.child.value, stdout_pipe.write.value, stderr_pipe.write.value};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited.data(), sizeof(inherited), nullptr, nullptr))
        return fail(error_code::IO_ERROR, "Unable to restrict inherited child handles");
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        nullptr, &startup.StartupInfo, &process))
        return fail(error_code::BACKEND_FAILURE, "Unable to start worker executable");
    native_handle process_handle{process.hProcess}, thread_handle{process.hThread};
    if (!AssignProcessToJobObject(job.value, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        return fail(error_code::IO_ERROR, "Unable to contain worker process");
    }
    struct child_owner {
        HANDLE job, process;
        ~child_owner() {
            TerminateJobObject(job, 130);
            WaitForSingleObject(process, INFINITE);
        }
    } owner{job.value, process.hProcess};
    ResumeThread(process.hThread);
    CloseHandle(input.child.value);
    input.child.value = nullptr;
    CloseHandle(stdout_pipe.write.value);
    stdout_pipe.write.value = nullptr;
    CloseHandle(stderr_pipe.write.value);
    stderr_pipe.write.value = nullptr;
    std::array<char, 4096> buffer{};
    bool ended = false, cancelling = false;
    auto cancelled_at = std::chrono::steady_clock::now();
    while (true) {
        if (stop.stop_requested() && !cancelling) {
            cancelling = true;
            cancelled_at = std::chrono::steady_clock::now();
        }
        const auto grace =
            control.next_input ? control.cancellation_grace : std::chrono::milliseconds(0);
        if (cancelling && std::chrono::steady_clock::now() - cancelled_at >= grace)
            TerminateJobObject(job.value, 130);
        const auto sent = input.pump(control, cancelling);
        if (!sent)
            return std::unexpected(sent.error());
        bool drained = false;
        const std::array streams{std::pair{stdout_pipe.read.value, &output},
                                 std::pair{stderr_pipe.read.value, &diagnostic}};
        for (const auto& [pipe, callback] : streams) {
            for (int block = 0; block < 16; ++block) {
                DWORD available{}, read{};
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available ||
                    !ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                              nullptr) ||
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
        return fail(error_code::CANCELLED, "Worker cancelled; committed stages retained");
    return static_cast<int>(code);
}
#else
namespace {
int reserve_descriptor(int descriptor) {
    if (descriptor < 0 || descriptor >= 3)
        return descriptor;
    const auto owned = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    close(descriptor);
    return owned;
}
// Blocking SIGPIPE only on this owner avoids changing the application's global
// signal policy. Consume a newly generated signal before restoring its mask.
ssize_t write_input(int descriptor, const std::byte* bytes, std::size_t size) {
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const auto masked = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (masked != 0) {
        errno = masked;
        return -1;
    }
    sigpending(&pending);
    const bool prior = sigismember(&pending, SIGPIPE) == 1;
    const auto written = write(descriptor, bytes, size);
    const auto code = errno;
    if (written < 0 && code == EPIPE && !prior) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1) {
            int signal{};
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = code;
    return written;
}
} // namespace
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic,
                            const process_control& control) {
    if (arguments.empty())
        return fail(error_code::INVALID_ARGUMENT, "Empty process command");
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
    if (control.cancellation_grace < std::chrono::milliseconds(0) ||
        control.cancellation_grace > std::chrono::seconds(5))
        return fail(error_code::INVALID_ARGUMENT, "Invalid process cancellation deadline");
    for (const auto& argument : arguments)
        if (argument.find('\0') != std::string::npos)
            return fail(error_code::INVALID_ARGUMENT, "NUL in process argument");
    struct descriptors {
        int read{-1}, write{-1};
        bool create() {
            int handles[2];
            if (pipe(handles) != 0)
                return false;
            read = reserve_descriptor(handles[0]);
            write = reserve_descriptor(handles[1]);
            return fcntl(read, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(write, F_SETFD, FD_CLOEXEC) >= 0 && fcntl(read, F_SETFL, O_NONBLOCK) >= 0;
        }
        ~descriptors() {
            if (read >= 0)
                close(read);
            if (write >= 0)
                close(write);
        }
    } stdout_pipe, stderr_pipe;
    if (!stdout_pipe.create() || !stderr_pipe.create())
        return fail(error_code::IO_ERROR, "Unable to create worker streams");
    struct input_channel {
        int child{-1}, parent{-1};
        ~input_channel() {
            if (child >= 0)
                close(child);
            if (parent >= 0)
                close(parent);
        }
        bool create(bool duplex) {
            if (!duplex) {
                child = reserve_descriptor(open("/dev/null", O_RDONLY | O_CLOEXEC));
                return child >= 0;
            }
            int handles[2];
            if (pipe(handles) != 0)
                return false;
            child = reserve_descriptor(handles[0]);
            parent = reserve_descriptor(handles[1]);

            return fcntl(child, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(parent, F_SETFD, FD_CLOEXEC) >= 0 &&
                   fcntl(parent, F_SETFL, O_NONBLOCK) >= 0;
        }
    } input;
    if (!input.create(static_cast<bool>(control.next_input)))
        return fail(error_code::IO_ERROR, "Unable to create worker input");
    std::vector<std::byte> pending_input;
    std::size_t input_offset{};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    struct spawn_cleanup {
        posix_spawn_file_actions_t* actions;
        posix_spawnattr_t* attributes;
        ~spawn_cleanup() {
            posix_spawn_file_actions_destroy(actions);
            posix_spawnattr_destroy(attributes);
        }
    } cleanup{&actions, &attributes};
    posix_spawn_file_actions_adddup2(&actions, input.child, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, input.child);
    if (input.parent >= 0)
        posix_spawn_file_actions_addclose(&actions, input.parent);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe.write, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe.write, STDERR_FILENO);
    for (const auto handle :
         {stdout_pipe.read, stdout_pipe.write, stderr_pipe.read, stderr_pipe.write})
        posix_spawn_file_actions_addclose(&actions, handle);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child{};
    const auto spawned = posix_spawnp(&child, argv[0], &actions, &attributes, argv.data(), environ);
    if (spawned != 0)
        return fail(error_code::BACKEND_FAILURE,
                    "Unable to start worker executable (code " + std::to_string(spawned) + ")");
    close(input.child);
    input.child = -1;
    close(stdout_pipe.write);
    stdout_pipe.write = -1;
    close(stderr_pipe.write);
    stderr_pipe.write = -1;
    struct child_owner {
        pid_t child;
        bool reaped{};
        ~child_owner() {
            if (!reaped) {
                kill(-child, SIGKILL);
                while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
                }
            }
        }
    } owner{child};
    bool terminating = false, signalled = false;
    auto cancelled_at = std::chrono::steady_clock::now();
    std::array<char, 4096> buffer{};
    int status{};
    while (true) {
        if (stop.stop_requested() && !terminating) {
            terminating = true;
            cancelled_at = std::chrono::steady_clock::now();
        }
        const auto grace =
            control.next_input ? control.cancellation_grace : std::chrono::milliseconds(0);
        if (terminating && !signalled && std::chrono::steady_clock::now() - cancelled_at >= grace) {
            kill(-child, SIGTERM);
            signalled = true;
        }
        if (terminating &&
            std::chrono::steady_clock::now() - cancelled_at >= grace + std::chrono::seconds(2))
            kill(-child, SIGKILL);
        if (input.parent >= 0) {
            if (pending_input.empty()) {
                auto next = control.next_input(terminating);
                if (!next)
                    return std::unexpected(next.error());
                if (next->size() > PROCESS_INPUT_LIMIT)
                    return fail(error_code::RESOURCE_EXHAUSTED,
                                "Process input exceeds frame budget");
                pending_input = std::move(*next);
                input_offset = 0;
            }
            if (!pending_input.empty()) {
                const auto count = write_input(input.parent, pending_input.data() + input_offset,
                                               pending_input.size() - input_offset);
                if (count > 0) {
                    input_offset += static_cast<std::size_t>(count);
                    if (input_offset == pending_input.size())
                        pending_input.clear();
                } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    if (errno != EPIPE && errno != ECONNRESET)
                        return fail(error_code::IO_ERROR, "Unable to write worker input (code " +
                                                              std::to_string(errno) + ")");
                    close(input.parent);
                    input.parent = -1;
                    pending_input.clear();
                }
            }
        }
        std::array<pollfd, 2> poll_descriptors{
            {{stdout_pipe.read, POLLIN, 0}, {stderr_pipe.read, POLLIN, 0}}};
        poll(poll_descriptors.data(), static_cast<nfds_t>(poll_descriptors.size()), 20);
        // Bound work each cycle so continuous log output cannot starve cancellation.
        const std::array streams{std::pair{stdout_pipe.read, &output},
                                 std::pair{stderr_pipe.read, &diagnostic}};
        for (const auto& [pipe, callback] : streams) {
            for (int block = 0; block < 16; ++block) {
                const auto count = read(pipe, buffer.data(), buffer.size());
                if (count <= 0)
                    break;
                if (*callback)
                    (*callback)(std::string(buffer.data(), static_cast<std::size_t>(count)));
            }
        }
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            owner.reaped = true;
            // Reap any grandchildren left behind by a failed coordinator.
            kill(-child, SIGKILL);
            for (const auto& [pipe, callback] : streams) {
                while (true) {
                    const auto count = read(pipe, buffer.data(), buffer.size());
                    if (count <= 0)
                        break;
                    if (*callback)
                        (*callback)(std::string(buffer.data(), static_cast<std::size_t>(count)));
                }
            }
            break;
        }
        if (waited < 0 && errno != EINTR)
            return fail(error_code::IO_ERROR, "Unable to reap worker process");
    }
    if (terminating)
        return fail(error_code::CANCELLED, "Worker cancelled; committed stages retained");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
#endif
} // namespace world_explorer
