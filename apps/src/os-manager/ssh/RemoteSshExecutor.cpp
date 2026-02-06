#include "os-manager/ssh/RemoteSshExecutor.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <libssh2.h>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kConnectTimeoutMs = 5000;

struct Libssh2Init {
    bool ready = false;

    Libssh2Init() { ready = (libssh2_init(0) == 0); }
    ~Libssh2Init()
    {
        if (ready) {
            libssh2_exit();
        }
    }
};

struct SocketCloser {
    int fd = -1;

    ~SocketCloser()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

int remainingMs(const std::chrono::steady_clock::time_point& deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

bool waitSocket(int socketFd, LIBSSH2_SESSION* session, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return false;
    }

    pollfd pfd;
    pfd.fd = socketFd;
    pfd.events = 0;
    pfd.revents = 0;

    if (session) {
        const int dir = libssh2_session_block_directions(session);
        if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) {
            pfd.events |= POLLIN;
        }
        if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
            pfd.events |= POLLOUT;
        }
    }

    if (pfd.events == 0) {
        pfd.events = POLLIN | POLLOUT;
    }

    const int rc = poll(&pfd, 1, timeoutMs);
    return rc > 0;
}

std::string base64Encode(const unsigned char* data, size_t length)
{
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

    std::string encoded;
    encoded.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i < length) {
        unsigned char char_array_3[3] = { 0, 0, 0 };
        unsigned char char_array_4[4];

        size_t remaining = length - i;
        size_t bytes = remaining >= 3 ? 3 : remaining;
        for (size_t j = 0; j < bytes; ++j) {
            char_array_3[j] = data[i + j];
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (size_t j = 0; j < bytes + 1; ++j) {
            encoded += base64_chars[char_array_4[j]];
        }

        if (bytes < 3) {
            encoded.append(3 - bytes, '=');
        }

        i += bytes;
    }

    return encoded;
}

std::string stripBase64Padding(std::string value)
{
    while (!value.empty() && value.back() == '=') {
        value.pop_back();
    }
    return value;
}

std::string shellEscapeArg(const std::string& arg)
{
    if (arg.empty()) {
        return "''";
    }

    std::string escaped;
    escaped.reserve(arg.size() + 2);
    escaped.push_back('\'');
    for (const char ch : arg) {
        if (ch == '\'') {
            escaped.append("'\\''");
        }
        else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

std::string buildCommandString(const std::vector<std::string>& argv)
{
    std::string command;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            command.push_back(' ');
        }
        command += shellEscapeArg(argv[i]);
    }
    return command;
}

std::string getLibssh2Error(LIBSSH2_SESSION* session, int rc)
{
    if (!session) {
        return "libssh2 error: " + std::to_string(rc);
    }
    char* message = nullptr;
    int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    if (message && length > 0) {
        return std::string(message, static_cast<size_t>(length));
    }
    return "libssh2 error: " + std::to_string(rc);
}

Result<int, ApiError> connectSocket(
    const std::string& host, uint16_t port, const std::chrono::steady_clock::time_point& deadline)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string portStr = std::to_string(port);
    struct addrinfo* result = nullptr;
    const int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0) {
        return Result<int, ApiError>::error(ApiError("Failed to resolve host: " + host));
    }

    SocketCloser socketCloser;
    for (struct addrinfo* addr = result; addr != nullptr; addr = addr->ai_next) {
        if (remainingMs(deadline) <= 0) {
            break;
        }

        socketCloser.fd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (socketCloser.fd < 0) {
            continue;
        }

        const int flags = fcntl(socketCloser.fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(socketCloser.fd, F_SETFL, flags | O_NONBLOCK);
        }

        int connectRc = ::connect(socketCloser.fd, addr->ai_addr, addr->ai_addrlen);
        if (connectRc == 0) {
            break;
        }

        if (errno != EINPROGRESS) {
            ::close(socketCloser.fd);
            socketCloser.fd = -1;
            continue;
        }

        const int waitMs = remainingMs(deadline);
        if (!waitSocket(socketCloser.fd, nullptr, waitMs)) {
            ::close(socketCloser.fd);
            socketCloser.fd = -1;
            continue;
        }

        int socketError = 0;
        socklen_t socketErrorLen = sizeof(socketError);
        if (getsockopt(socketCloser.fd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLen) != 0
            || socketError != 0) {
            ::close(socketCloser.fd);
            socketCloser.fd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo(result);

    if (socketCloser.fd < 0) {
        return Result<int, ApiError>::error(
            ApiError("Failed to connect to " + host + ":" + portStr));
    }

    const int fd = socketCloser.fd;
    socketCloser.fd = -1;
    return Result<int, ApiError>::okay(fd);
}

Result<std::monostate, ApiError> ensureSessionReady(
    LIBSSH2_SESSION* session,
    int socketFd,
    const std::chrono::steady_clock::time_point& deadline,
    const std::string& action)
{
    while (true) {
        const int rc = libssh2_session_handshake(session, socketFd);
        if (rc == 0) {
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            return Result<std::monostate, ApiError>::error(
                ApiError(action + " failed: " + getLibssh2Error(session, rc)));
        }

        const int waitMs = remainingMs(deadline);
        if (!waitSocket(socketFd, session, waitMs)) {
            return Result<std::monostate, ApiError>::error(ApiError(action + " timed out"));
        }
    }
}

void closeChannel(LIBSSH2_SESSION* session, int socketFd, LIBSSH2_CHANNEL* channel)
{
    if (!channel) {
        return;
    }

    for (int i = 0; i < 5; ++i) {
        const int rc = libssh2_channel_close(channel);
        if (rc == 0) {
            break;
        }
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        waitSocket(socketFd, session, 100);
    }

    for (int i = 0; i < 5; ++i) {
        const int rc = libssh2_channel_wait_closed(channel);
        if (rc == 0) {
            break;
        }
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        waitSocket(socketFd, session, 100);
    }

    libssh2_channel_free(channel);
}

} // namespace

RemoteSshExecutor::RemoteSshExecutor(std::filesystem::path keyPath) : keyPath_(keyPath)
{}

Result<OsApi::RemoteCliRun::Okay, ApiError> RemoteSshExecutor::run(
    const PeerTrustBundle& peer, const std::vector<std::string>& argv, int commandTimeoutMs) const
{
    Libssh2Init init;
    if (!init.ready) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("libssh2 initialization failed"));
    }

    const auto start = std::chrono::steady_clock::now();
    const auto connectDeadline = start + std::chrono::milliseconds(kConnectTimeoutMs);

    auto connectResult = connectSocket(peer.host, peer.ssh_port, connectDeadline);
    if (connectResult.isError()) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(connectResult.errorValue());
    }

    SocketCloser socketCloser;
    socketCloser.fd = connectResult.value();

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Failed to initialize SSH session"));
    }

    libssh2_session_set_blocking(session, 0);
    const int methodRc =
        libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY, "ecdsa-sha2-nistp256");
    if (methodRc != 0) {
        const std::string error = getLibssh2Error(session, methodRc);
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Failed to set SSH host key preference: " + error));
    }

    auto handshakeResult =
        ensureSessionReady(session, socketCloser.fd, connectDeadline, "SSH handshake");
    if (handshakeResult.isError()) {
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(handshakeResult.errorValue());
    }

    const unsigned char* hash = reinterpret_cast<const unsigned char*>(
        libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256));
    if (!hash) {
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Failed to read host key fingerprint"));
    }

    const std::string fingerprint = "SHA256:" + stripBase64Padding(base64Encode(hash, 32));
    if (fingerprint != peer.host_fingerprint_sha256) {
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Host fingerprint mismatch for " + peer.host));
    }

    auto pubKeyPath = keyPath_;
    pubKeyPath += ".pub";

    while (true) {
        const int rc = libssh2_userauth_publickey_fromfile(
            session,
            peer.ssh_user.c_str(),
            pubKeyPath.string().c_str(),
            keyPath_.string().c_str(),
            nullptr);
        if (rc == 0) {
            break;
        }
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH authentication failed: " + getLibssh2Error(session, rc)));
        }

        const int waitMs = remainingMs(connectDeadline);
        if (!waitSocket(socketCloser.fd, session, waitMs)) {
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH authentication timed out"));
        }
    }

    LIBSSH2_CHANNEL* channel = nullptr;
    while (true) {
        channel = libssh2_channel_open_session(session);
        if (channel) {
            break;
        }
        const int rc = libssh2_session_last_errno(session);
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH channel open failed: " + getLibssh2Error(session, rc)));
        }

        const int waitMs = remainingMs(connectDeadline);
        if (!waitSocket(socketCloser.fd, session, waitMs)) {
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH channel open timed out"));
        }
    }

    const int effectiveCommandTimeoutMs = commandTimeoutMs > 0 ? commandTimeoutMs : 1;
    const auto commandStart = std::chrono::steady_clock::now();
    const auto commandDeadline =
        commandStart + std::chrono::milliseconds(effectiveCommandTimeoutMs);

    const std::string command = buildCommandString(argv);
    while (true) {
        const int rc = libssh2_channel_exec(channel, command.c_str());
        if (rc == 0) {
            break;
        }
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            libssh2_channel_free(channel);
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH exec failed: " + getLibssh2Error(session, rc)));
        }

        const int waitMs = remainingMs(commandDeadline);
        if (!waitSocket(socketCloser.fd, session, waitMs)) {
            libssh2_channel_free(channel);
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH exec timed out"));
        }
    }

    std::string stdoutText;
    std::string stderrText;
    bool outputTooLarge = false;
    bool timedOut = false;

    char buffer[4096];
    while (true) {
        if (remainingMs(commandDeadline) <= 0) {
            timedOut = true;
            break;
        }

        bool readData = false;

        while (true) {
            if (remainingMs(commandDeadline) <= 0) {
                timedOut = true;
                break;
            }

            const ssize_t rc = libssh2_channel_read(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                if (stdoutText.size() + static_cast<size_t>(rc) > kMaxStdoutBytes) {
                    outputTooLarge = true;
                    break;
                }
                stdoutText.append(buffer, static_cast<size_t>(rc));
                readData = true;
                continue;
            }
            if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) {
                break;
            }
            libssh2_channel_free(channel);
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("SSH read failed: " + getLibssh2Error(session, static_cast<int>(rc))));
        }

        if (timedOut) {
            break;
        }

        if (outputTooLarge) {
            break;
        }

        while (true) {
            if (remainingMs(commandDeadline) <= 0) {
                timedOut = true;
                break;
            }

            const ssize_t rc = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                if (stderrText.size() + static_cast<size_t>(rc) > kMaxStderrBytes) {
                    outputTooLarge = true;
                    break;
                }
                stderrText.append(buffer, static_cast<size_t>(rc));
                readData = true;
                continue;
            }
            if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) {
                break;
            }
            libssh2_channel_free(channel);
            libssh2_session_free(session);
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(ApiError(
                "SSH read stderr failed: " + getLibssh2Error(session, static_cast<int>(rc))));
        }

        if (timedOut) {
            break;
        }

        if (outputTooLarge) {
            break;
        }

        if (libssh2_channel_eof(channel)) {
            break;
        }

        if (!readData) {
            const int waitMs = remainingMs(commandDeadline);
            if (!waitSocket(socketCloser.fd, session, waitMs)) {
                timedOut = true;
                break;
            }
        }
    }

    if (outputTooLarge) {
        closeChannel(session, socketCloser.fd, channel);
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(ApiError(
            "Remote CLI output exceeded limit (stdout=" + std::to_string(kMaxStdoutBytes)
            + " bytes, stderr=" + std::to_string(kMaxStderrBytes) + " bytes)"));
    }

    if (timedOut) {
        closeChannel(session, socketCloser.fd, channel);
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(ApiError(
            "Remote CLI command timed out after " + std::to_string(effectiveCommandTimeoutMs)
            + "ms"));
    }

    int exitCode = -1;
    exitCode = libssh2_channel_get_exit_status(channel);

    closeChannel(session, socketCloser.fd, channel);
    libssh2_session_disconnect(session, "Normal Shutdown");
    libssh2_session_free(session);

    const auto elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - start)
                                                .count());

    OsApi::RemoteCliRun::Okay okay;
    okay.exit_code = exitCode;
    okay.stdout = stdoutText;
    okay.stderr = stderrText;
    okay.elapsed_ms = elapsedMs;

    return Result<OsApi::RemoteCliRun::Okay, ApiError>::okay(okay);
}

} // namespace OsManager
} // namespace DirtSim
