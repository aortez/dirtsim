#include "WifiManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <exception>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace {

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd()
    {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const { return fd_; }

    void reset(int fd = -1)
    {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_;
};

std::string toLower(const std::string& input)
{
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string toUpper(const std::string& input)
{
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string trim(const std::string& text)
{
    const size_t start = text.find_first_not_of(" \n\r\t");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = text.find_last_not_of(" \n\r\t");
    return text.substr(start, end - start + 1);
}

std::vector<std::string> splitEscaped(const std::string& line, char separator)
{
    std::vector<std::string> fields;
    std::string current;
    bool escape = false;

    for (char c : line) {
        if (escape) {
            current.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == separator) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    if (escape) {
        current.push_back('\\');
    }

    fields.push_back(current);
    return fields;
}

bool isWifiType(const std::string& type)
{
    const std::string upper = toUpper(type);
    return upper == "802-11-WIRELESS" || upper == "WIFI";
}

std::string normalizeSecurity(const std::string& raw)
{
    if (raw.empty()) {
        return "open";
    }

    const std::string upper = toUpper(raw);
    if (upper.find("SAE") != std::string::npos || upper.find("WPA3") != std::string::npos) {
        return "wpa3";
    }
    if (upper.find("WPA2") != std::string::npos) {
        return "wpa2";
    }
    if (upper.find("WPA") != std::string::npos) {
        return "wpa2";
    }
    if (upper.find("WEP") != std::string::npos) {
        return "wep";
    }
    if (upper.find("NONE") != std::string::npos || upper.find("OPEN") != std::string::npos
        || upper == "--") {
        return "open";
    }

    return toLower(raw);
}

std::optional<std::string> formatDate(int64_t timestamp)
{
    if (timestamp <= 0) {
        return std::nullopt;
    }

    const std::time_t timeValue = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    if (localtime_r(&timeValue, &localTime) == nullptr) {
        return std::nullopt;
    }

    char buffer[16];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &localTime) == 0) {
        return std::nullopt;
    }

    return std::string(buffer);
}

std::string formatRelative(int64_t timestamp)
{
    if (timestamp <= 0) {
        return "never";
    }

    const auto now = std::chrono::system_clock::now();
    const auto then = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(timestamp));

    auto delta = now - then;
    if (delta < std::chrono::seconds(0)) {
        delta = std::chrono::seconds(0);
    }

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(delta).count();

    if (seconds < 60) {
        return "just now";
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + "m ago";
    }
    if (seconds < 86400) {
        return std::to_string(seconds / 3600) + "h ago";
    }
    return std::to_string(seconds / 86400) + "d ago";
}

struct CommandError {
    std::string message;
};

Result<std::string, CommandError> runCommand(const std::vector<std::string>& args)
{
    if (args.empty()) {
        return Result<std::string, CommandError>::error(CommandError{ "No command provided" });
    }

    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        return Result<std::string, CommandError>::error(
            CommandError{ std::string("pipe failed: ") + std::strerror(errno) });
    }

    UniqueFd readFd(pipeFds[0]);
    UniqueFd writeFd(pipeFds[1]);

    const pid_t pid = fork();
    if (pid < 0) {
        return Result<std::string, CommandError>::error(
            CommandError{ std::string("fork failed: ") + std::strerror(errno) });
    }

    if (pid == 0) {
        if (dup2(writeFd.get(), STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (dup2(writeFd.get(), STDERR_FILENO) < 0) {
            _exit(127);
        }

        readFd.reset();
        writeFd.reset();

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    writeFd.reset();

    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t readCount = read(readFd.get(), buffer.data(), buffer.size());
        if (readCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (readCount == 0) {
            break;
        }
        output.append(buffer.data(), static_cast<size_t>(readCount));
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return Result<std::string, CommandError>::error(
            CommandError{ std::string("waitpid failed: ") + std::strerror(errno) });
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        std::string message = trim(output);
        if (message.empty()) {
            message = args[0] + " failed";
        }
        return Result<std::string, CommandError>::error(
            CommandError{ message + " (exit " + std::to_string(exitCode) + ")" });
    }

    return Result<std::string, CommandError>::okay(output);
}

struct SavedConnection {
    std::string ssid;
    std::string connectionId;
    std::string security;
    int64_t timestamp = 0;
    bool active = false;
};

struct ScanNetwork {
    std::string ssid;
    int signalDbm = 0;
    std::string security;
};

} // namespace

namespace DirtSim {
namespace Network {

Result<WifiStatus, std::string> WifiManager::getStatus() const
{
    const auto output =
        runCommand({ "nmcli", "-t", "-f", "NAME,TYPE", "connection", "show", "--active" });
    if (output.isError()) {
        return Result<WifiStatus, std::string>::error(output.errorValue().message);
    }

    WifiStatus status;
    const std::string text = output.value();
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        const std::string line =
            (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);
        start = (end == std::string::npos) ? text.size() : end + 1;

        if (line.empty()) {
            continue;
        }

        const auto fields = splitEscaped(line, ':');
        if (fields.size() < 2) {
            continue;
        }

        if (!isWifiType(fields[1])) {
            continue;
        }

        status.connected = true;
        status.ssid = fields[0];
        break;
    }

    return Result<WifiStatus, std::string>::okay(status);
}

Result<std::vector<WifiNetworkInfo>, std::string> WifiManager::listNetworks() const
{
    const auto savedOutput =
        runCommand({ "nmcli", "-t", "-f", "NAME,TYPE,DEVICE,TIMESTAMP", "connection", "show" });
    if (savedOutput.isError()) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error(
            savedOutput.errorValue().message);
    }

    std::vector<SavedConnection> savedConnections;
    std::unordered_map<std::string, size_t> savedIndexBySsid;

    const std::string savedText = savedOutput.value();
    size_t start = 0;
    while (start < savedText.size()) {
        const size_t end = savedText.find('\n', start);
        const std::string line = (end == std::string::npos) ? savedText.substr(start)
                                                            : savedText.substr(start, end - start);
        start = (end == std::string::npos) ? savedText.size() : end + 1;

        if (line.empty()) {
            continue;
        }

        const auto fields = splitEscaped(line, ':');
        if (fields.size() < 4) {
            continue;
        }

        const std::string& connectionId = fields[0];
        const std::string& type = fields[1];
        const std::string& device = fields[2];
        const std::string& timestampRaw = fields[3];
        const std::string& ssid = connectionId;

        if (!isWifiType(type)) {
            continue;
        }
        if (ssid.empty()) {
            continue;
        }

        int64_t timestamp = 0;
        if (!timestampRaw.empty() && timestampRaw != "--") {
            try {
                timestamp = std::stoll(timestampRaw);
            }
            catch (const std::exception&) {
                timestamp = 0;
            }
        }

        SavedConnection connection{
            .ssid = ssid,
            .connectionId = connectionId,
            .security = "unknown",
            .timestamp = timestamp,
            .active = (!device.empty() && device != "--"),
        };

        auto it = savedIndexBySsid.find(ssid);
        if (it == savedIndexBySsid.end()) {
            savedIndexBySsid.emplace(ssid, savedConnections.size());
            savedConnections.push_back(connection);
            continue;
        }

        SavedConnection& existing = savedConnections[it->second];
        const bool replace = (connection.active && !existing.active)
            || (connection.active == existing.active && connection.timestamp > existing.timestamp);
        if (replace) {
            existing = connection;
        }
    }

    std::vector<ScanNetwork> scanNetworks;
    std::unordered_map<std::string, size_t> openIndexBySsid;
    std::optional<std::string> scanError;

    const auto scanOutput =
        runCommand({ "nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "device", "wifi", "list" });
    if (scanOutput.isError()) {
        scanError = scanOutput.errorValue().message;
    }
    else {
        const std::string scanText = scanOutput.value();
        size_t scanStart = 0;
        while (scanStart < scanText.size()) {
            const size_t scanEnd = scanText.find('\n', scanStart);
            const std::string line = (scanEnd == std::string::npos)
                ? scanText.substr(scanStart)
                : scanText.substr(scanStart, scanEnd - scanStart);
            scanStart = (scanEnd == std::string::npos) ? scanText.size() : scanEnd + 1;

            if (line.empty()) {
                continue;
            }

            const auto fields = splitEscaped(line, ':');
            if (fields.size() < 3) {
                continue;
            }

            const std::string& ssid = fields[0];
            const std::string& signalRaw = fields[1];
            const std::string& securityRaw = fields[2];

            if (ssid.empty()) {
                continue;
            }

            int signalPercent = 0;
            try {
                signalPercent = std::stoi(signalRaw);
            }
            catch (const std::exception&) {
                continue;
            }

            ScanNetwork network{
                .ssid = ssid,
                .signalDbm = -100 + signalPercent,
                .security = normalizeSecurity(securityRaw),
            };

            auto it = openIndexBySsid.find(ssid);
            if (it == openIndexBySsid.end()) {
                openIndexBySsid.emplace(ssid, scanNetworks.size());
                scanNetworks.push_back(network);
                continue;
            }

            ScanNetwork& existing = scanNetworks[it->second];
            if (network.signalDbm > existing.signalDbm) {
                existing = network;
            }
        }
    }

    if (scanError.has_value() && savedConnections.empty()) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error(scanError.value());
    }

    std::unordered_map<std::string, int> signalBySsid;
    std::unordered_map<std::string, std::string> securityBySsid;
    for (const auto& network : scanNetworks) {
        signalBySsid[network.ssid] = network.signalDbm;
        securityBySsid[network.ssid] = network.security;
    }

    struct SavedEntry {
        WifiNetworkInfo info;
        int64_t timestamp = 0;
    };

    std::vector<SavedEntry> savedEntries;
    savedEntries.reserve(savedConnections.size());
    for (const auto& saved : savedConnections) {
        WifiNetworkInfo info{
            .ssid = saved.ssid,
            .status = saved.active ? WifiNetworkStatus::Connected : WifiNetworkStatus::Saved,
            .signalDbm = std::nullopt,
            .security = saved.security,
            .lastUsedDate = formatDate(saved.timestamp),
            .lastUsedRelative = formatRelative(saved.timestamp),
            .connectionId = saved.connectionId,
        };

        auto signalIt = signalBySsid.find(saved.ssid);
        if (signalIt != signalBySsid.end()) {
            info.signalDbm = signalIt->second;
        }
        auto securityIt = securityBySsid.find(saved.ssid);
        if (securityIt != securityBySsid.end()) {
            info.security = securityIt->second;
        }

        savedEntries.push_back({ info, saved.timestamp });
    }

    std::sort(
        savedEntries.begin(), savedEntries.end(), [](const SavedEntry& a, const SavedEntry& b) {
            if (a.info.status != b.info.status) {
                return a.info.status == WifiNetworkStatus::Connected;
            }
            if (a.timestamp != b.timestamp) {
                return a.timestamp > b.timestamp;
            }
            return a.info.ssid < b.info.ssid;
        });

    std::vector<WifiNetworkInfo> openEntries;
    openEntries.reserve(scanNetworks.size());
    for (const auto& open : scanNetworks) {
        if (open.security != "open") {
            continue;
        }
        if (savedIndexBySsid.find(open.ssid) != savedIndexBySsid.end()) {
            continue;
        }

        WifiNetworkInfo info{
            .ssid = open.ssid,
            .status = WifiNetworkStatus::Open,
            .signalDbm = open.signalDbm,
            .security = open.security,
            .lastUsedDate = std::nullopt,
            .lastUsedRelative = "n/a",
            .connectionId = std::string{},
        };

        openEntries.push_back(info);
    }

    std::sort(
        openEntries.begin(),
        openEntries.end(),
        [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
            const int signalA = a.signalDbm.has_value() ? a.signalDbm.value() : -200;
            const int signalB = b.signalDbm.has_value() ? b.signalDbm.value() : -200;
            if (signalA != signalB) {
                return signalA > signalB;
            }
            return a.ssid < b.ssid;
        });

    std::vector<WifiNetworkInfo> networks;
    networks.reserve(savedEntries.size() + openEntries.size());

    for (const auto& entry : savedEntries) {
        networks.push_back(entry.info);
    }
    for (const auto& entry : openEntries) {
        networks.push_back(entry);
    }

    return Result<std::vector<WifiNetworkInfo>, std::string>::okay(networks);
}

Result<WifiConnectResult, std::string> WifiManager::connect(const WifiNetworkInfo& network) const
{
    if (network.ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }

    if (network.status == WifiNetworkStatus::Connected) {
        return Result<WifiConnectResult, std::string>::okay(
            WifiConnectResult{ .success = true, .ssid = network.ssid });
    }

    Result<std::string, CommandError> output;
    if (!network.connectionId.empty()) {
        output = runCommand({ "nmcli", "connection", "up", "id", network.connectionId });
    }
    else {
        output = runCommand({ "nmcli", "device", "wifi", "connect", network.ssid });
    }

    if (output.isError()) {
        return Result<WifiConnectResult, std::string>::error(output.errorValue().message);
    }

    return Result<WifiConnectResult, std::string>::okay(
        WifiConnectResult{ .success = true, .ssid = network.ssid });
}

Result<WifiConnectResult, std::string> WifiManager::connectBySsid(const std::string& ssid) const
{
    if (ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }

    const auto listResult = listNetworks();
    if (listResult.isError()) {
        return Result<WifiConnectResult, std::string>::error(
            "Network scan failed: " + listResult.errorValue());
    }

    const auto& networks = listResult.value();
    for (const auto& network : networks) {
        if (network.ssid == ssid) {
            return connect(network);
        }
    }

    return Result<WifiConnectResult, std::string>::error(
        "SSID not found in saved or open networks: " + ssid);
}

} // namespace Network
} // namespace DirtSim
