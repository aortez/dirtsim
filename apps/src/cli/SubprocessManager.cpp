#include "SubprocessManager.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/StatusGet.h"
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace DirtSim {
namespace Client {

SubprocessManager::SubprocessManager()
{}

SubprocessManager::~SubprocessManager()
{
    killUI();
    killServer();
}

bool SubprocessManager::launchServer(const std::string& serverPath, const std::string& args)
{
    // Fork process.
    serverPid_ = fork();

    if (serverPid_ < 0) {
        SLOG_ERROR("SubprocessManager: Failed to fork process");
        return false;
    }

    if (serverPid_ == 0) {
        // Child process - exec server.
        SLOG_DEBUG("SubprocessManager: Launching server: {} {}", serverPath, args);

        // No need to redirect stdout - benchmark logging config disables console output.
        // Stderr is kept for crash reporting (terminate/abort messages).

        // Parse arguments into vector.
        std::vector<std::string> argVec;
        argVec.push_back(serverPath); // argv[0] is the program name.

        if (!args.empty()) {
            std::istringstream iss(args);
            std::string arg;
            while (iss >> arg) {
                argVec.push_back(arg);
            }
        }

        // Convert to char* array for execv.
        std::vector<char*> execArgs;
        for (auto& arg : argVec) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);

        // Execute server.
        execv(serverPath.c_str(), execArgs.data());

        // If exec fails, exit child process.
        SLOG_ERROR("SubprocessManager: exec failed");
        exit(1);
    }

    // Parent process.
    SLOG_INFO("SubprocessManager: Launched server (PID: {})", serverPid_);
    return true;
}

bool SubprocessManager::waitForServerReady(const std::string& url, int timeoutSec)
{
    SLOG_INFO("SubprocessManager: Waiting for server to be ready at {}", url);

    auto startTime = std::chrono::steady_clock::now();

    while (true) {

        // Check if server process is still alive.
        if (!isServerRunning()) {
            SLOG_ERROR("SubprocessManager: Server process died");
            return false;
        }

        // Try connecting and check server state.
        Network::WebSocketService client;
        auto connectResult = client.connect(url, 1000);

        if (connectResult.isValue()) {
            // Connected - now check if server is in a ready state (not Startup).
            Api::StatusGet::Command statusCmd;
            auto statusResult = client.sendCommand<Api::StatusGet::Okay>(statusCmd, 1000);
            client.disconnect();

            if (statusResult.isValue() && statusResult.value().isValue()) {
                const auto& status = statusResult.value().value();
                if (status.state == "Idle" || status.state == "SimRunning") {
                    SLOG_INFO("SubprocessManager: Server is ready (state: {})", status.state);
                    return true;
                }
                // Server in Startup or other state - keep waiting.
                SLOG_DEBUG("SubprocessManager: Server not ready yet (state: {})", status.state);
            }
            else if (statusResult.isValue() && statusResult.value().isError()) {
                // Server returned error (e.g., command not supported in Startup).
                SLOG_DEBUG(
                    "SubprocessManager: Server error: {}",
                    statusResult.value().errorValue().message);
            }
        }
        else {
            // Connection failed - server still starting up.
            SLOG_DEBUG("SubprocessManager: Connection failed: {}", connectResult.errorValue());
        }

        // Check timeout.
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeoutSec) {
            SLOG_ERROR("SubprocessManager: Timeout waiting for server");
            return false;
        }

        // Wait a bit before retrying.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void SubprocessManager::killServer()
{
    if (serverPid_ > 0) {
        SLOG_INFO("SubprocessManager: Killing server (PID: {})", serverPid_);

        // Send SIGTERM for graceful shutdown.
        kill(serverPid_, SIGTERM);

        // Wait for process to exit (with timeout).
        int status;
        int waitResult = waitpid(serverPid_, &status, WNOHANG);

        if (waitResult == 0) {
            // Process still running, wait a bit.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            waitResult = waitpid(serverPid_, &status, WNOHANG);

            if (waitResult == 0) {
                // Still running, force kill.
                SLOG_WARN("SubprocessManager: Server didn't respond to SIGTERM, sending SIGKILL");
                kill(serverPid_, SIGKILL);
                waitpid(serverPid_, &status, 0);
            }
        }

        serverPid_ = -1;
        SLOG_INFO("SubprocessManager: Server killed");
    }
}

bool SubprocessManager::isServerRunning() const
{
    if (serverPid_ <= 0) {
        return false;
    }

    // Check if process has exited (non-blocking).
    // Note: Using kill(pid, 0) doesn't work because zombie processes still exist.
    int status;
    pid_t result = waitpid(serverPid_, &status, WNOHANG);

    if (result == serverPid_) {
        // Process has exited (reaps zombie).
        SLOG_INFO("SubprocessManager: Server process {} has exited", serverPid_);
        return false;
    }
    else if (result == 0) {
        // Process still running.
        return true;
    }
    else {
        // Error or process doesn't exist.
        return false;
    }
}

bool SubprocessManager::tryConnect(const std::string& url)
{
    try {
        Network::WebSocketService client;
        auto result = client.connect(url, 1000);
        if (result.isError()) {
            return false;
        }
        client.disconnect();
        return true;
    }
    catch (const std::exception& e) {
        SLOG_DEBUG("SubprocessManager: tryConnect exception: {}", e.what());
        return false;
    }
}

bool SubprocessManager::launchUI(const std::string& uiPath, const std::string& args)
{
    // Fork process.
    uiPid_ = fork();

    if (uiPid_ < 0) {
        SLOG_ERROR("SubprocessManager: Failed to fork UI process");
        return false;
    }

    if (uiPid_ == 0) {
        // Child process - exec UI.
        SLOG_DEBUG("SubprocessManager: Launching UI: {} {}", uiPath, args);

        // Parse arguments into vector.
        std::vector<std::string> argVec;
        argVec.push_back(uiPath);

        if (!args.empty()) {
            std::istringstream iss(args);
            std::string arg;
            while (iss >> arg) {
                argVec.push_back(arg);
            }
        }

        // Convert to char* array for execv.
        std::vector<char*> execArgs;
        for (auto& arg : argVec) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);

        // Execute UI.
        execv(uiPath.c_str(), execArgs.data());

        // If exec fails, exit child process.
        SLOG_ERROR("SubprocessManager: execv failed for UI");
        exit(1);
    }

    // Parent process.
    SLOG_INFO("SubprocessManager: Launched UI (PID: {})", uiPid_);
    return true;
}

bool SubprocessManager::waitForUIReady(const std::string& url, int timeoutSec)
{
    SLOG_INFO("SubprocessManager: Waiting for UI to be ready at {}", url);

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        // Check if UI process is still alive.
        if (!isUIRunning()) {
            SLOG_ERROR("SubprocessManager: UI process died");
            return false;
        }

        // Try connecting.
        if (tryConnect(url)) {
            SLOG_INFO("SubprocessManager: UI is ready");
            return true;
        }

        // Check timeout.
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeoutSec) {
            SLOG_ERROR("SubprocessManager: Timeout waiting for UI");
            return false;
        }

        // Wait a bit before retrying.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void SubprocessManager::killUI()
{
    if (uiPid_ > 0) {
        SLOG_INFO("SubprocessManager: Killing UI (PID: {})", uiPid_);

        // Send SIGTERM for graceful shutdown.
        kill(uiPid_, SIGTERM);

        // Wait for process to exit (with timeout).
        int status;
        int waitResult = waitpid(uiPid_, &status, WNOHANG);

        if (waitResult == 0) {
            // Process still running, wait a bit.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            waitResult = waitpid(uiPid_, &status, WNOHANG);

            if (waitResult == 0) {
                // Still running, force kill.
                SLOG_WARN("SubprocessManager: UI didn't respond to SIGTERM, sending SIGKILL");
                kill(uiPid_, SIGKILL);
                waitpid(uiPid_, &status, 0);
            }
        }

        uiPid_ = -1;
        SLOG_INFO("SubprocessManager: UI killed");
    }
}

bool SubprocessManager::isUIRunning() const
{
    if (uiPid_ <= 0) {
        return false;
    }

    // Check if process has exited (non-blocking).
    // Note: Using kill(pid, 0) doesn't work because zombie processes still exist.
    int status;
    pid_t result = waitpid(uiPid_, &status, WNOHANG);

    if (result == uiPid_) {
        // Process has exited (reaps zombie).
        SLOG_INFO("SubprocessManager: UI process {} has exited", uiPid_);
        return false;
    }
    else if (result == 0) {
        // Process still running.
        return true;
    }
    else {
        // Error or process doesn't exist.
        return false;
    }
}

} // namespace Client
} // namespace DirtSim
