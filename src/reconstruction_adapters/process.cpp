#include "world_explorer/reconstruction_adapters/process.hpp"
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
} // namespace
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic) {
    if (arguments.empty() || stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
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
    if (!job.value || !stdout_pipe.create() || !stderr_pipe.create())
        return fail(error_code::IO_ERROR, "Unable to create child pipes/job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        return fail(error_code::IO_ERROR, "Unable to bound child lifetime");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdout_pipe.write.value;
    startup.hStdError = stderr_pipe.write.value;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process))
        return fail(error_code::BACKEND_FAILURE, "Unable to start worker executable");
    native_handle process_handle{process.hProcess}, thread_handle{process.hThread};
    if (!AssignProcessToJobObject(job.value, process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        return fail(error_code::IO_ERROR, "Unable to contain worker process");
    }
    ResumeThread(process.hThread);
    CloseHandle(stdout_pipe.write.value);
    stdout_pipe.write.value = nullptr;
    CloseHandle(stderr_pipe.write.value);
    stderr_pipe.write.value = nullptr;
    std::array<char, 4096> buffer{};
    bool ended = false;
    while (true) {
        if (stop.stop_requested())
            TerminateJobObject(job.value, 130);
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
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic) {
    if (arguments.empty())
        return fail(error_code::INVALID_ARGUMENT, "Empty process command");
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
    for (const auto& argument : arguments)
        if (argument.find('\0') != std::string::npos)
            return fail(error_code::INVALID_ARGUMENT, "NUL in process argument");
    struct descriptors {
        int read{-1}, write{-1};
        bool create() {
            int handles[2];
            if (pipe(handles) != 0)
                return false;
            read = handles[0];
            write = handles[1];
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
    bool terminating = false;
    auto cancelled_at = std::chrono::steady_clock::now();
    std::array<char, 4096> buffer{};
    int status{};
    while (true) {
        if (stop.stop_requested() && !terminating) {
            kill(-child, SIGTERM);
            terminating = true;
            cancelled_at = std::chrono::steady_clock::now();
        }
        if (terminating &&
            std::chrono::steady_clock::now() - cancelled_at > std::chrono::seconds(2))
            kill(-child, SIGKILL);
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
