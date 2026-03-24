#include "NexmonChannelController.h"
#include "NexmonChannelProtocol.h"

#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kReceiveTimeoutMs = 1000;
constexpr int kNexmonNetlinkProtocol = 31;
constexpr size_t kReceiveBufferSize = 256;

std::string describeErrno(const std::string& prefix)
{
    return prefix + ": " + std::strerror(errno);
}

} // namespace

NexmonChannelController::~NexmonChannelController()
{
    stop();
}

Result<std::monostate, std::string> NexmonChannelController::start()
{
#ifndef __linux__
    return Result<std::monostate, std::string>::error("Nexmon channel control requires Linux");
#else
    std::lock_guard<std::mutex> lock(mutex_);
    if (txSocketFd_ >= 0 && rxSocketFd_ >= 0) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (txSocketFd_ >= 0) {
        ::close(txSocketFd_);
        txSocketFd_ = -1;
    }
    if (rxSocketFd_ >= 0) {
        ::close(rxSocketFd_);
        rxSocketFd_ = -1;
    }

    const int txSocketFd = ::socket(AF_NETLINK, SOCK_RAW, kNexmonNetlinkProtocol);
    if (txSocketFd < 0) {
        return Result<std::monostate, std::string>::error(
            describeErrno("Failed to create Nexmon send socket"));
    }

    const int rxSocketFd = ::socket(AF_NETLINK, SOCK_RAW, kNexmonNetlinkProtocol);
    if (rxSocketFd < 0) {
        const std::string error = describeErrno("Failed to create Nexmon receive socket");
        ::close(txSocketFd);
        return Result<std::monostate, std::string>::error(error);
    }

    const timeval timeout{
        .tv_sec = kReceiveTimeoutMs / 1000,
        .tv_usec = (kReceiveTimeoutMs % 1000) * 1000,
    };
    if (::setsockopt(rxSocketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        const std::string error = describeErrno("Failed to configure Nexmon receive timeout");
        ::close(txSocketFd);
        ::close(rxSocketFd);
        return Result<std::monostate, std::string>::error(error);
    }

    sockaddr_nl rxAddress{};
    rxAddress.nl_family = AF_NETLINK;
    rxAddress.nl_pid = static_cast<uint32_t>(::getpid());
    if (::bind(rxSocketFd, reinterpret_cast<const sockaddr*>(&rxAddress), sizeof(rxAddress)) != 0) {
        const std::string error = describeErrno("Failed to bind Nexmon receive socket");
        ::close(txSocketFd);
        ::close(rxSocketFd);
        return Result<std::monostate, std::string>::error(error);
    }

    sockaddr_nl txAddress{};
    txAddress.nl_family = AF_NETLINK;
    txAddress.nl_pid = 0;
    if (::connect(txSocketFd, reinterpret_cast<const sockaddr*>(&txAddress), sizeof(txAddress))
        != 0) {
        const std::string error = describeErrno("Failed to connect Nexmon send socket");
        ::close(txSocketFd);
        ::close(rxSocketFd);
        return Result<std::monostate, std::string>::error(error);
    }

    txSocketFd_ = txSocketFd;
    rxSocketFd_ = rxSocketFd;
    return Result<std::monostate, std::string>::okay(std::monostate{});
#endif
}

void NexmonChannelController::stop()
{
#ifdef __linux__
    std::lock_guard<std::mutex> lock(mutex_);
    if (txSocketFd_ >= 0) {
        ::close(txSocketFd_);
        txSocketFd_ = -1;
    }
    if (rxSocketFd_ >= 0) {
        ::close(rxSocketFd_);
        rxSocketFd_ = -1;
    }
#endif
}

Result<std::monostate, std::string> NexmonChannelController::setTuning(const ScannerTuning& tuning)
{
#ifndef __linux__
    static_cast<void>(tuning);
    return Result<std::monostate, std::string>::error("Nexmon channel control requires Linux");
#else
    const auto payloadResult = NexmonChannelProtocol::buildSetChanspecPayload(tuning);
    if (payloadResult.isError()) {
        return Result<std::monostate, std::string>::error(payloadResult.errorValue());
    }

    const auto replyResult = transact(payloadResult.value());
    if (replyResult.isError()) {
        return Result<std::monostate, std::string>::error(replyResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
#endif
}

Result<std::vector<uint8_t>, std::string> NexmonChannelController::transact(
    const std::vector<uint8_t>& payload)
{
#ifndef __linux__
    static_cast<void>(payload);
    return Result<std::vector<uint8_t>, std::string>::error(
        "Nexmon channel control requires Linux");
#else
    std::lock_guard<std::mutex> lock(mutex_);
    if (txSocketFd_ < 0 || rxSocketFd_ < 0) {
        return Result<std::vector<uint8_t>, std::string>::error(
            "Nexmon channel controller is not started");
    }

    std::vector<uint8_t> request(NLMSG_SPACE(payload.size()), 0);
    auto* header = reinterpret_cast<nlmsghdr*>(request.data());
    header->nlmsg_len = NLMSG_SPACE(payload.size());
    header->nlmsg_type = 0;
    header->nlmsg_flags = 0;
    header->nlmsg_seq = 0;
    header->nlmsg_pid = static_cast<uint32_t>(::getpid());
    std::memcpy(NLMSG_DATA(header), payload.data(), payload.size());

    const ssize_t bytesSent = ::sendto(txSocketFd_, request.data(), request.size(), 0, nullptr, 0);
    if (bytesSent < 0) {
        return Result<std::vector<uint8_t>, std::string>::error(
            describeErrno("Failed to send Nexmon netlink request"));
    }
    if (static_cast<size_t>(bytesSent) != request.size()) {
        return Result<std::vector<uint8_t>, std::string>::error(
            "Nexmon netlink request was only partially sent");
    }

    std::vector<uint8_t> response(kReceiveBufferSize, 0);
    const ssize_t bytesReceived =
        ::recvfrom(rxSocketFd_, response.data(), response.size(), 0, nullptr, nullptr);
    if (bytesReceived < 0) {
        return Result<std::vector<uint8_t>, std::string>::error(
            describeErrno("Failed to receive Nexmon netlink reply"));
    }
    if (bytesReceived < static_cast<ssize_t>(sizeof(nlmsghdr))) {
        return Result<std::vector<uint8_t>, std::string>::error(
            "Nexmon netlink reply is too short");
    }

    const auto* responseHeader = reinterpret_cast<const nlmsghdr*>(response.data());
    if (responseHeader->nlmsg_len < NLMSG_HDRLEN
        || responseHeader->nlmsg_len > static_cast<uint32_t>(bytesReceived)) {
        return Result<std::vector<uint8_t>, std::string>::error(
            "Nexmon netlink reply length is invalid");
    }
    if (responseHeader->nlmsg_type != NLMSG_DONE) {
        return Result<std::vector<uint8_t>, std::string>::error(
            "Unexpected Nexmon netlink reply type " + std::to_string(responseHeader->nlmsg_type));
    }

    const size_t payloadSize = responseHeader->nlmsg_len - NLMSG_HDRLEN;
    const auto* responsePayload = static_cast<const uint8_t*>(NLMSG_DATA(responseHeader));
    return Result<std::vector<uint8_t>, std::string>::okay(
        std::vector<uint8_t>(responsePayload, responsePayload + payloadSize));
#endif
}

} // namespace OsManager
} // namespace DirtSim
