#include "world_explorer/application/commands.hpp"
#include <array>
#include <chrono>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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
result<int> run_worker(const worker_configuration& configuration,
                       const std::vector<std::string>& arguments, const std::stop_token& stop,
                       const log_callback& log) {
    if (configuration.script.empty() || !std::filesystem::is_regular_file(configuration.script))
        return fail(error_code::NOT_FOUND, "Worker script not found; configure --worker-script");
    std::vector<std::string> command{configuration.python, "-u", configuration.script.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return run_process(command, stop, log);
}

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
} // namespace
result<int> run_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                        const log_callback& log) {
    if (arguments.empty() || stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
    std::wstring command;
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos || argument.size() > 32768)
            return fail(error_code::INVALID_ARGUMENT, "Invalid process argument");
        if (!command.empty())
            command += L' ';
        command += quote_argument(widen(argument));
    }
    if (command.size() > 32766)
        return fail(error_code::INVALID_ARGUMENT, "Process command exceeds Windows limit");
    native_handle read_pipe, write_pipe, job{CreateJobObjectW(nullptr, nullptr)};
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!job.value || !CreatePipe(&read_pipe.value, &write_pipe.value, &security, 0) ||
        !SetHandleInformation(read_pipe.value, HANDLE_FLAG_INHERIT, 0))
        return fail(error_code::IO_ERROR, "Unable to create child pipes/job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        return fail(error_code::IO_ERROR, "Unable to bound child lifetime");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = write_pipe.value;
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
    CloseHandle(write_pipe.value);
    write_pipe.value = nullptr;
    std::array<char, 4096> buffer{};
    bool ended = false;
    while (true) {
        if (stop.stop_requested())
            TerminateJobObject(job.value, 130);
        DWORD available = 0;
        if (PeekNamedPipe(read_pipe.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD read = 0;
            if (ReadFile(read_pipe.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                         nullptr) &&
                read && log)
                log(std::string(buffer.data(), read));
        } else if (ended)
            break;
        ended = WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0;
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Worker cancelled; committed stages retained");
    return static_cast<int>(code);
}
#else
result<int> run_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                        const log_callback& log) {
    if (arguments.empty())
        return fail(error_code::INVALID_ARGUMENT, "Empty process command");
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Process cancelled before launch");
    for (const auto& argument : arguments)
        if (argument.find('\0') != std::string::npos)
            return fail(error_code::INVALID_ARGUMENT, "NUL in process argument");
    int pipe_handles[2];
    if (pipe(pipe_handles) != 0)
        return fail(error_code::IO_ERROR, "Unable to create worker output pipe");
    struct descriptors {
        int read, write;
        ~descriptors() {
            if (read >= 0)
                close(read);
            if (write >= 0)
                close(write);
        }
    } handles{pipe_handles[0], pipe_handles[1]};
    fcntl(handles.read, F_SETFD, FD_CLOEXEC);
    fcntl(handles.write, F_SETFD, FD_CLOEXEC);
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
    posix_spawn_file_actions_adddup2(&actions, handles.write, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, handles.write, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, handles.read);
    posix_spawn_file_actions_addclose(&actions, handles.write);
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
    close(handles.write);
    handles.write = -1;
    fcntl(handles.read, F_SETFL, O_NONBLOCK);
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
        pollfd descriptor{handles.read, POLLIN, 0};
        poll(&descriptor, 1, 20);
        // Bound work each cycle so continuous log output cannot starve cancellation.
        for (int block = 0; block < 16; ++block) {
            const auto count = read(handles.read, buffer.data(), buffer.size());
            if (count <= 0)
                break;
            if (log)
                log(std::string(buffer.data(), static_cast<std::size_t>(count)));
        }
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            owner.reaped = true;
            // Reap any grandchildren left behind by a failed coordinator.
            kill(-child, SIGKILL);
            while (true) {
                const auto count = read(handles.read, buffer.data(), buffer.size());
                if (count <= 0)
                    break;
                if (log)
                    log(std::string(buffer.data(), static_cast<std::size_t>(count)));
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
