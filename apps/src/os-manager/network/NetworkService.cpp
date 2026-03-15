#include "NetworkService.h"
#include "core/LoggingChannels.h"
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <system_error>
#include <thread>

namespace DirtSim {
namespace OsManager {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kPeriodicRefreshInterval = std::chrono::seconds(10);
constexpr auto kSnapshotMaxAge = std::chrono::seconds(4);

std::vector<NetworkService::LocalAddressInfo> collectLocalAddresses()
{
    std::vector<NetworkService::LocalAddressInfo> results;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return results;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || (ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        char addrBuf[INET_ADDRSTRLEN];
        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sa->sin_addr, addrBuf, sizeof(addrBuf)) == nullptr) {
            continue;
        }

        results.push_back({ .name = ifa->ifa_name, .address = addrBuf });
    }

    freeifaddrs(ifaddr);
    return results;
}

} // namespace

struct NetworkService::Impl {
    struct CachedSnapshot {
        Snapshot snapshot;
        Clock::time_point updatedAt = Clock::now();
    };

    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::function<void()>> tasks;
    std::thread workerThread;
    bool running = false;
    bool stopRequested = false;
    std::optional<CachedSnapshot> cachedSnapshot;
    std::string lastRefreshError;
    Clock::time_point lastRefreshAt = Clock::time_point::min();

    Result<std::monostate, std::string> start()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (running) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }

        stopRequested = false;
        try {
            workerThread = std::thread([this]() { workerLoop(); });
        }
        catch (const std::system_error& e) {
            return Result<std::monostate, std::string>::error(e.what());
        }

        running = true;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    void stop()
    {
        std::thread threadToJoin;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!running) {
                return;
            }

            stopRequested = true;
            condition.notify_all();
            threadToJoin = std::move(workerThread);
            running = false;
        }

        if (threadToJoin.joinable()) {
            threadToJoin.join();
        }

        cachedSnapshot.reset();
        lastRefreshError.clear();
        lastRefreshAt = Clock::time_point::min();
    }

    Result<Snapshot, std::string> getSnapshot(bool forceRefresh)
    {
        auto startResult = start();
        if (startResult.isError()) {
            return Result<Snapshot, std::string>::error(startResult.errorValue());
        }

        auto promise = std::make_shared<std::promise<Result<Snapshot, std::string>>>();
        auto future = promise->get_future();
        enqueueTask([this, forceRefresh, promise]() {
            if (forceRefresh || snapshotIsStale()) {
                refreshSnapshot();
            }

            if (cachedSnapshot.has_value()) {
                promise->set_value(Result<Snapshot, std::string>::okay(cachedSnapshot->snapshot));
                return;
            }

            const std::string error =
                lastRefreshError.empty() ? "Network snapshot unavailable" : lastRefreshError;
            promise->set_value(Result<Snapshot, std::string>::error(error));
        });
        return future.get();
    }

    Result<Network::WifiConnectResult, std::string> connectBySsid(
        const std::string& ssid, const std::optional<std::string>& password)
    {
        auto startResult = start();
        if (startResult.isError()) {
            return Result<Network::WifiConnectResult, std::string>::error(startResult.errorValue());
        }

        auto promise =
            std::make_shared<std::promise<Result<Network::WifiConnectResult, std::string>>>();
        auto future = promise->get_future();
        enqueueTask([this, ssid, password, promise]() {
            Network::WifiManager wifiManager;
            const auto result = wifiManager.connectBySsid(ssid, password);
            if (!result.isError()) {
                refreshSnapshot();
            }
            promise->set_value(result);
        });
        return future.get();
    }

    Result<Network::WifiDisconnectResult, std::string> disconnect(
        const std::optional<std::string>& ssid)
    {
        auto startResult = start();
        if (startResult.isError()) {
            return Result<Network::WifiDisconnectResult, std::string>::error(
                startResult.errorValue());
        }

        auto promise =
            std::make_shared<std::promise<Result<Network::WifiDisconnectResult, std::string>>>();
        auto future = promise->get_future();
        enqueueTask([this, ssid, promise]() {
            Network::WifiManager wifiManager;
            const auto result = wifiManager.disconnect(ssid);
            if (!result.isError()) {
                refreshSnapshot();
            }
            promise->set_value(result);
        });
        return future.get();
    }

    Result<Network::WifiForgetResult, std::string> forget(const std::string& ssid)
    {
        auto startResult = start();
        if (startResult.isError()) {
            return Result<Network::WifiForgetResult, std::string>::error(startResult.errorValue());
        }

        auto promise =
            std::make_shared<std::promise<Result<Network::WifiForgetResult, std::string>>>();
        auto future = promise->get_future();
        enqueueTask([this, ssid, promise]() {
            Network::WifiManager wifiManager;
            const auto result = wifiManager.forget(ssid);
            if (!result.isError()) {
                refreshSnapshot();
            }
            promise->set_value(result);
        });
        return future.get();
    }

    void enqueueTask(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        }
        condition.notify_all();
    }

    bool snapshotIsStale() const
    {
        if (!cachedSnapshot.has_value()) {
            return true;
        }

        return (Clock::now() - cachedSnapshot->updatedAt) >= kSnapshotMaxAge;
    }

    void refreshSnapshot()
    {
        Network::WifiManager wifiManager;
        const auto statusResult = wifiManager.getStatus();
        const auto networksResult = wifiManager.listNetworks();
        if (statusResult.isError()) {
            lastRefreshError = statusResult.errorValue();
            LOG_WARN(Network, "NetworkService refresh failed: {}", lastRefreshError);
            return;
        }
        if (networksResult.isError()) {
            lastRefreshError = networksResult.errorValue();
            LOG_WARN(Network, "NetworkService refresh failed: {}", lastRefreshError);
            return;
        }

        Snapshot snapshot{
            .status = statusResult.value(),
            .networks = networksResult.value(),
            .localAddresses = collectLocalAddresses(),
        };
        cachedSnapshot =
            CachedSnapshot{ .snapshot = std::move(snapshot), .updatedAt = Clock::now() };
        lastRefreshError.clear();
        lastRefreshAt = cachedSnapshot->updatedAt;
        LOG_INFO(
            Network,
            "NetworkService snapshot refreshed ({} networks).",
            cachedSnapshot->snapshot.networks.size());
    }

    void workerLoop()
    {
        refreshSnapshot();

        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                const auto waitUntil = lastRefreshAt == Clock::time_point::min()
                    ? Clock::now() + kPeriodicRefreshInterval
                    : lastRefreshAt + kPeriodicRefreshInterval;
                condition.wait_until(
                    lock, waitUntil, [this]() { return stopRequested || !tasks.empty(); });

                if (stopRequested) {
                    return;
                }

                if (!tasks.empty()) {
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
            }

            if (task) {
                task();
            }

            if (lastRefreshAt == Clock::time_point::min()
                || (Clock::now() - lastRefreshAt) >= kPeriodicRefreshInterval) {
                refreshSnapshot();
            }
        }
    }
};

NetworkService::NetworkService() : pImpl_()
{}

NetworkService::~NetworkService()
{
    pImpl_->stop();
}

Result<std::monostate, std::string> NetworkService::start()
{
    return pImpl_->start();
}

void NetworkService::stop()
{
    pImpl_->stop();
}

Result<NetworkService::Snapshot, std::string> NetworkService::getSnapshot(bool forceRefresh)
{
    return pImpl_->getSnapshot(forceRefresh);
}

Result<Network::WifiConnectResult, std::string> NetworkService::connectBySsid(
    const std::string& ssid, const std::optional<std::string>& password)
{
    return pImpl_->connectBySsid(ssid, password);
}

Result<Network::WifiDisconnectResult, std::string> NetworkService::disconnect(
    const std::optional<std::string>& ssid)
{
    return pImpl_->disconnect(ssid);
}

Result<Network::WifiForgetResult, std::string> NetworkService::forget(const std::string& ssid)
{
    return pImpl_->forget(ssid);
}

} // namespace OsManager
} // namespace DirtSim
