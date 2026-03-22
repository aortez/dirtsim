#include "ProcessRunner.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kPollSliceMs = 50;

std::string describeErrno(const std::string& prefix)
{
    return prefix + ": " + std::strerror(errno);
}

void writeBestEffort(int fd, const char* data, size_t size)
{
    while (size > 0) {
        const ssize_t bytesWritten = ::write(fd, data, size);
        if (bytesWritten > 0) {
            data += bytesWritten;
            size -= static_cast<size_t>(bytesWritten);
            continue;
        }
        if (bytesWritten < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

Result<std::monostate, std::string> setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return Result<std::monostate, std::string>::error(
            describeErrno("Failed to read file descriptor flags"));
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return Result<std::monostate, std::string>::error(
            describeErrno("Failed to set file descriptor flags"));
    }
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<bool, std::string> readAvailableOutput(int fd, std::string& output)
{
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t bytesRead = ::read(fd, buffer.data(), buffer.size());
        if (bytesRead > 0) {
            output.append(buffer.data(), static_cast<size_t>(bytesRead));
            continue;
        }
        if (bytesRead == 0) {
            return Result<bool, std::string>::okay(true);
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Result<bool, std::string>::okay(false);
        }
        return Result<bool, std::string>::error(describeErrno("Failed to read process output"));
    }
}

std::vector<char*> buildExecArgs(const std::vector<std::string>& argv)
{
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);
    return args;
}

} // namespace

Result<ProcessRunResult, std::string> runProcessCapture(
    const std::vector<std::string>& argv, int timeoutMs)
{
    if (argv.empty() || argv.front().empty()) {
        return Result<ProcessRunResult, std::string>::error("Process path is required");
    }

    int pipeFds[2] = { -1, -1 };
    if (::pipe(pipeFds) != 0) {
        return Result<ProcessRunResult, std::string>::error(
            describeErrno("Failed to create process pipe"));
    }

    const auto nonBlockingResult = setNonBlocking(pipeFds[0]);
    if (nonBlockingResult.isError()) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return Result<ProcessRunResult, std::string>::error(nonBlockingResult.errorValue());
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const std::string error = describeErrno("Failed to fork process");
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return Result<ProcessRunResult, std::string>::error(error);
    }

    if (pid == 0) {
        ::close(pipeFds[0]);
        if (::dup2(pipeFds[1], STDOUT_FILENO) < 0 || ::dup2(pipeFds[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        ::close(pipeFds[1]);

        auto execArgs = buildExecArgs(argv);
        ::execvp(execArgs[0], execArgs.data());

        const std::string error = describeErrno("Failed to exec process");
        writeBestEffort(STDERR_FILENO, error.c_str(), error.size());
        writeBestEffort(STDERR_FILENO, "\n", 1);
        _exit(errno == ENOENT ? 127 : 126);
    }

    ::close(pipeFds[1]);

    std::string output;
    int status = -1;
    bool childExited = false;
    bool outputClosed = false;
    const auto deadline = timeoutMs > 0
        ? std::optional(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs))
        : std::nullopt;

    while (!childExited || !outputClosed) {
        const auto readResult = readAvailableOutput(pipeFds[0], output);
        if (readResult.isError()) {
            ::close(pipeFds[0]);
            static_cast<void>(::kill(pid, SIGKILL));
            static_cast<void>(::waitpid(pid, &status, 0));
            return Result<ProcessRunResult, std::string>::error(readResult.errorValue());
        }
        outputClosed = readResult.value();

        if (!childExited) {
            const pid_t waitResult = ::waitpid(pid, &status, WNOHANG);
            if (waitResult == pid) {
                childExited = true;
            }
            else if (waitResult < 0 && errno != EINTR) {
                const std::string error = describeErrno("Failed to wait for process");
                ::close(pipeFds[0]);
                static_cast<void>(::kill(pid, SIGKILL));
                static_cast<void>(::waitpid(pid, &status, 0));
                return Result<ProcessRunResult, std::string>::error(error);
            }
        }

        if (childExited && outputClosed) {
            break;
        }

        if (!childExited && deadline.has_value()
            && std::chrono::steady_clock::now() >= deadline.value()) {
            static_cast<void>(::kill(pid, SIGKILL));
            static_cast<void>(::waitpid(pid, &status, 0));
            ::close(pipeFds[0]);
            return Result<ProcessRunResult, std::string>::error("Process timed out");
        }

        int pollTimeoutMs = childExited ? 0 : kPollSliceMs;
        if (!childExited && deadline.has_value()) {
            const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         deadline.value() - std::chrono::steady_clock::now())
                                         .count();
            pollTimeoutMs = static_cast<int>(
                std::max<int64_t>(0, std::min<int64_t>(remainingMs, kPollSliceMs)));
        }

        pollfd pfd{};
        pfd.fd = pipeFds[0];
        pfd.events = POLLIN | POLLHUP;
        const int pollResult = ::poll(&pfd, 1, pollTimeoutMs);
        if (pollResult < 0 && errno != EINTR) {
            const std::string error = describeErrno("Failed to poll process output");
            ::close(pipeFds[0]);
            static_cast<void>(::kill(pid, SIGKILL));
            static_cast<void>(::waitpid(pid, &status, 0));
            return Result<ProcessRunResult, std::string>::error(error);
        }
    }

    ::close(pipeFds[0]);

    if (!WIFEXITED(status)) {
        return Result<ProcessRunResult, std::string>::error("Process failed to exit cleanly");
    }

    return Result<ProcessRunResult, std::string>::okay(
        ProcessRunResult{ .exitCode = WEXITSTATUS(status), .output = std::move(output) });
}

} // namespace OsManager
} // namespace DirtSim
