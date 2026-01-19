#pragma once

#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace DirtSim {
namespace Client {

/**
 * @brief RAII wrapper for fork/exec/kill of server and UI subprocesses.
 */
class SubprocessManager {
public:
    struct ProcessOptions {
        std::string workingDirectory;
        std::vector<std::pair<std::string, std::string>> environmentOverrides;
    };

    SubprocessManager();
    ~SubprocessManager();

    bool launchServer(const std::string& serverPath, const std::string& args = "");
    bool launchServer(
        const std::string& serverPath, const std::string& args, const ProcessOptions& options);
    bool launchUI(const std::string& uiPath, const std::string& args = "");
    bool launchUI(
        const std::string& uiPath, const std::string& args, const ProcessOptions& options);
    bool waitForServerReady(const std::string& url, int timeoutSec = 5);
    bool waitForUIReady(const std::string& url, int timeoutSec = 5);
    void killServer();
    void killUI();
    bool isServerRunning();
    bool isUIRunning();

private:
    bool launchProcess(
        const std::string& path,
        const std::string& args,
        const ProcessOptions& options,
        pid_t& pidOut,
        const char* processLabel);
    pid_t serverPid_ = -1;
    pid_t uiPid_ = -1;
    bool tryConnect(const std::string& url);
};

} // namespace Client
} // namespace DirtSim
