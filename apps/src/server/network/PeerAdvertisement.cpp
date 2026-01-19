#include "PeerAdvertisement.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {

static constexpr const char* SERVICE_TYPE = "_dirtsim._tcp";

struct PeerAdvertisement::Impl {
    std::atomic<bool> running_{ false };
    std::thread thread_;
    mutable std::mutex mutex_;

    std::string serviceName_ = "dirtsim";
    uint16_t port_ = 8080;
    PeerRole role_ = PeerRole::Physics;

    AvahiSimplePoll* poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;

    // Name collision handling - Avahi may suggest alternatives.
    char* actualName_ = nullptr;

    static void entryGroupCallback(
        AvahiEntryGroup* /*group*/, AvahiEntryGroupState state, void* userdata)
    {
        auto* self = static_cast<Impl*>(userdata);

        switch (state) {
            case AVAHI_ENTRY_GROUP_ESTABLISHED:
                spdlog::info(
                    "PeerAdvertisement: Service '{}' established on port {}",
                    self->actualName_ ? self->actualName_ : self->serviceName_,
                    self->port_);
                break;

            case AVAHI_ENTRY_GROUP_COLLISION: {
                // Name collision - pick an alternative name.
                char* newName = avahi_alternative_service_name(
                    self->actualName_ ? self->actualName_ : self->serviceName_.c_str());
                spdlog::warn("PeerAdvertisement: Name collision, renaming to '{}'", newName);

                if (self->actualName_) {
                    avahi_free(self->actualName_);
                }
                self->actualName_ = newName;

                // Re-register with new name.
                self->createServices();
                break;
            }

            case AVAHI_ENTRY_GROUP_FAILURE:
                spdlog::error(
                    "PeerAdvertisement: Entry group failure: {}",
                    avahi_strerror(avahi_client_errno(self->client_)));
                avahi_simple_poll_quit(self->poll_);
                break;

            case AVAHI_ENTRY_GROUP_UNCOMMITED:
            case AVAHI_ENTRY_GROUP_REGISTERING:
                break;
        }
    }

    static void clientCallback(AvahiClient* client, AvahiClientState state, void* userdata)
    {
        auto* self = static_cast<Impl*>(userdata);

        switch (state) {
            case AVAHI_CLIENT_S_RUNNING:
                // Server is running, register services.
                self->createServices();
                break;

            case AVAHI_CLIENT_FAILURE:
                spdlog::error(
                    "PeerAdvertisement: Client failure: {}",
                    avahi_strerror(avahi_client_errno(client)));
                avahi_simple_poll_quit(self->poll_);
                break;

            case AVAHI_CLIENT_S_COLLISION:
            case AVAHI_CLIENT_S_REGISTERING:
                // Server is registering or collision - reset our services.
                if (self->group_) {
                    avahi_entry_group_reset(self->group_);
                }
                break;

            case AVAHI_CLIENT_CONNECTING:
                break;
        }
    }

    void createServices()
    {
        if (!client_) {
            return;
        }

        // Create entry group if needed.
        if (!group_) {
            group_ = avahi_entry_group_new(client_, entryGroupCallback, this);
            if (!group_) {
                spdlog::error(
                    "PeerAdvertisement: Failed to create entry group: {}",
                    avahi_strerror(avahi_client_errno(client_)));
                avahi_simple_poll_quit(poll_);
                return;
            }
        }

        // If group is empty, add our service.
        if (avahi_entry_group_is_empty(group_)) {
            const char* name = actualName_ ? actualName_ : serviceName_.c_str();

            // Role as TXT record.
            const char* roleStr = "role=unknown";
            if (role_ == PeerRole::Physics) {
                roleStr = "role=physics";
            }
            else if (role_ == PeerRole::Ui) {
                roleStr = "role=ui";
            }

            int ret = avahi_entry_group_add_service(
                group_,
                AVAHI_IF_UNSPEC,
                AVAHI_PROTO_UNSPEC,
                static_cast<AvahiPublishFlags>(0),
                name,
                SERVICE_TYPE,
                nullptr, // domain
                nullptr, // host
                port_,
                roleStr,
                nullptr); // end of TXT records

            if (ret < 0) {
                if (ret == AVAHI_ERR_COLLISION) {
                    // Name collision during add - handle it.
                    char* newName = avahi_alternative_service_name(name);
                    spdlog::warn(
                        "PeerAdvertisement: Name collision during add, renaming to '{}'", newName);
                    if (actualName_) {
                        avahi_free(actualName_);
                    }
                    actualName_ = newName;
                    avahi_entry_group_reset(group_);
                    createServices();
                    return;
                }

                spdlog::error("PeerAdvertisement: Failed to add service: {}", avahi_strerror(ret));
                avahi_simple_poll_quit(poll_);
                return;
            }

            // Commit the entry group.
            ret = avahi_entry_group_commit(group_);
            if (ret < 0) {
                spdlog::error(
                    "PeerAdvertisement: Failed to commit entry group: {}", avahi_strerror(ret));
                avahi_simple_poll_quit(poll_);
                return;
            }

            spdlog::debug(
                "PeerAdvertisement: Registering '{}' as {} on port {}", name, SERVICE_TYPE, port_);
        }
    }

    bool startAvahi()
    {
        poll_ = avahi_simple_poll_new();
        if (!poll_) {
            spdlog::error("PeerAdvertisement: Failed to create Avahi simple poll.");
            return false;
        }

        int error = 0;
        client_ = avahi_client_new(
            avahi_simple_poll_get(poll_),
            static_cast<AvahiClientFlags>(0),
            clientCallback,
            this,
            &error);

        if (!client_) {
            spdlog::error(
                "PeerAdvertisement: Failed to create Avahi client: {}", avahi_strerror(error));
            avahi_simple_poll_free(poll_);
            poll_ = nullptr;
            return false;
        }

        spdlog::info(
            "PeerAdvertisement: Started advertising {} service on port {}", SERVICE_TYPE, port_);
        return true;
    }

    void stopAvahi()
    {
        if (group_) {
            avahi_entry_group_free(group_);
            group_ = nullptr;
        }
        if (client_) {
            avahi_client_free(client_);
            client_ = nullptr;
        }
        if (poll_) {
            avahi_simple_poll_free(poll_);
            poll_ = nullptr;
        }
        if (actualName_) {
            avahi_free(actualName_);
            actualName_ = nullptr;
        }
    }

    void runLoop()
    {
        if (!startAvahi()) {
            running_ = false;
            return;
        }

        while (running_) {
            int result = avahi_simple_poll_iterate(poll_, 100);
            if (result != 0) {
                break;
            }
        }

        stopAvahi();
        spdlog::info("PeerAdvertisement: Stopped.");
    }
};

PeerAdvertisement::PeerAdvertisement() : pImpl_()
{}

PeerAdvertisement::~PeerAdvertisement()
{
    stop();
}

void PeerAdvertisement::setServiceName(const std::string& name)
{
    std::lock_guard<std::mutex> lock(pImpl_->mutex_);
    pImpl_->serviceName_ = name;
}

void PeerAdvertisement::setPort(uint16_t port)
{
    std::lock_guard<std::mutex> lock(pImpl_->mutex_);
    pImpl_->port_ = port;
}

void PeerAdvertisement::setRole(PeerRole role)
{
    std::lock_guard<std::mutex> lock(pImpl_->mutex_);
    pImpl_->role_ = role;
}

bool PeerAdvertisement::start()
{
    if (pImpl_->running_) {
        return true;
    }

    pImpl_->running_ = true;
    pImpl_->thread_ = std::thread([this]() { pImpl_->runLoop(); });
    return true;
}

void PeerAdvertisement::stop()
{
    pImpl_->running_ = false;
    if (pImpl_->poll_) {
        avahi_simple_poll_quit(pImpl_->poll_);
    }
    if (pImpl_->thread_.joinable()) {
        pImpl_->thread_.join();
    }
}

bool PeerAdvertisement::isRunning() const
{
    return pImpl_->running_;
}

} // namespace Server
} // namespace DirtSim
