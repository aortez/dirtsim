#include "os-manager/network/PeerAdvertisement.h"

#include "core/LoggingChannels.h"
#include <atomic>
#include <mutex>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

namespace DirtSim {
namespace OsManager {

static constexpr const char* SERVICE_TYPE = "_dirtsim._tcp";

struct PeerAdvertisement::Impl {
    mutable std::mutex mutex_;

    std::string serviceName_ = "dirtsim";
    uint16_t port_ = 8080;
    PeerRole role_ = PeerRole::Physics;

    AvahiThreadedPoll* poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
    std::atomic<bool> started_{ false };

    // Name collision handling - Avahi may suggest alternatives.
    char* actualName_ = nullptr;

    static void entryGroupCallback(
        AvahiEntryGroup* /*group*/, AvahiEntryGroupState state, void* userdata)
    {
        auto* self = static_cast<Impl*>(userdata);

        switch (state) {
            case AVAHI_ENTRY_GROUP_ESTABLISHED:
                LOG_INFO(
                    Network,
                    "PeerAdvertisement: Service '{}' established on port {}",
                    self->actualName_ ? self->actualName_ : self->serviceName_,
                    self->port_);
                break;

            case AVAHI_ENTRY_GROUP_COLLISION: {
                // Name collision - pick an alternative name.
                std::string serviceName;
                const char* actualName = nullptr;
                {
                    std::lock_guard<std::mutex> lock(self->mutex_);
                    serviceName = self->serviceName_;
                    actualName = self->actualName_;
                }
                char* newName =
                    avahi_alternative_service_name(actualName ? actualName : serviceName.c_str());
                LOG_WARN(Network, "PeerAdvertisement: Name collision, renaming to '{}'", newName);

                {
                    std::lock_guard<std::mutex> lock(self->mutex_);
                    if (self->actualName_) {
                        avahi_free(self->actualName_);
                    }
                    self->actualName_ = newName;
                }

                // Re-register with new name.
                self->createServices();
                break;
            }

            case AVAHI_ENTRY_GROUP_FAILURE:
                LOG_ERROR(
                    Network,
                    "PeerAdvertisement: Entry group failure: {}",
                    avahi_strerror(avahi_client_errno(self->client_)));
                if (self->poll_) {
                    avahi_threaded_poll_quit(self->poll_);
                }
                self->started_.store(false);
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
                LOG_ERROR(
                    Network,
                    "PeerAdvertisement: Client failure: {}",
                    avahi_strerror(avahi_client_errno(client)));
                if (self->poll_) {
                    avahi_threaded_poll_quit(self->poll_);
                }
                self->started_.store(false);
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

        std::string serviceName;
        uint16_t port = 0;
        PeerRole role = PeerRole::Unknown;
        const char* actualName = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            serviceName = serviceName_;
            port = port_;
            role = role_;
            actualName = actualName_;
        }

        // Create entry group if needed.
        if (!group_) {
            group_ = avahi_entry_group_new(client_, entryGroupCallback, this);
            if (!group_) {
                LOG_ERROR(
                    Network,
                    "PeerAdvertisement: Failed to create entry group: {}",
                    avahi_strerror(avahi_client_errno(client_)));
                if (poll_) {
                    avahi_threaded_poll_quit(poll_);
                }
                return;
            }
        }

        // If group is empty, add our service.
        if (avahi_entry_group_is_empty(group_)) {
            const char* name = actualName ? actualName : serviceName.c_str();

            // Role as TXT record.
            const char* roleStr = "role=unknown";
            if (role == PeerRole::Physics) {
                roleStr = "role=physics";
            }
            else if (role == PeerRole::Ui) {
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
                port,
                roleStr,
                nullptr); // end of TXT records

            if (ret < 0) {
                if (ret == AVAHI_ERR_COLLISION) {
                    // Name collision during add - handle it.
                    char* newName = avahi_alternative_service_name(name);
                    LOG_WARN(
                        Network,
                        "PeerAdvertisement: Name collision during add, renaming to '{}'",
                        newName);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (actualName_) {
                            avahi_free(actualName_);
                        }
                        actualName_ = newName;
                    }
                    avahi_entry_group_reset(group_);
                    createServices();
                    return;
                }

                LOG_ERROR(
                    Network, "PeerAdvertisement: Failed to add service: {}", avahi_strerror(ret));
                if (poll_) {
                    avahi_threaded_poll_quit(poll_);
                }
                return;
            }

            // Commit the entry group.
            ret = avahi_entry_group_commit(group_);
            if (ret < 0) {
                LOG_ERROR(
                    Network,
                    "PeerAdvertisement: Failed to commit entry group: {}",
                    avahi_strerror(ret));
                if (poll_) {
                    avahi_threaded_poll_quit(poll_);
                }
                return;
            }

            LOG_DEBUG(
                Network,
                "PeerAdvertisement: Registering '{}' as {} on port {}",
                name,
                SERVICE_TYPE,
                port);
        }
    }

    bool startAvahi()
    {
        poll_ = avahi_threaded_poll_new();
        if (!poll_) {
            LOG_ERROR(Network, "PeerAdvertisement: Failed to create Avahi threaded poll.");
            return false;
        }

        int error = 0;
        client_ = avahi_client_new(
            avahi_threaded_poll_get(poll_),
            static_cast<AvahiClientFlags>(0),
            clientCallback,
            this,
            &error);

        if (!client_) {
            LOG_ERROR(
                Network,
                "PeerAdvertisement: Failed to create Avahi client: {}",
                avahi_strerror(error));
            avahi_threaded_poll_free(poll_);
            poll_ = nullptr;
            return false;
        }

        if (avahi_threaded_poll_start(poll_) < 0) {
            LOG_ERROR(Network, "PeerAdvertisement: Failed to start Avahi threaded poll.");
            avahi_client_free(client_);
            avahi_threaded_poll_free(poll_);
            client_ = nullptr;
            poll_ = nullptr;
            return false;
        }

        LOG_INFO(Network, "PeerAdvertisement: Started advertising {} service", SERVICE_TYPE);
        return true;
    }

    void stopAvahi()
    {
        if (poll_) {
            avahi_threaded_poll_stop(poll_);
        }
        if (group_) {
            avahi_entry_group_free(group_);
            group_ = nullptr;
        }
        if (client_) {
            avahi_client_free(client_);
            client_ = nullptr;
        }
        if (poll_) {
            avahi_threaded_poll_free(poll_);
            poll_ = nullptr;
        }
        if (actualName_) {
            avahi_free(actualName_);
            actualName_ = nullptr;
        }
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
    if (pImpl_->started_.load()) {
        return true;
    }

    pImpl_->started_.store(pImpl_->startAvahi());
    return pImpl_->started_.load();
}

void PeerAdvertisement::stop()
{
    if (!pImpl_->started_.load()) {
        pImpl_->stopAvahi();
        return;
    }

    pImpl_->started_.store(false);
    pImpl_->stopAvahi();
}

bool PeerAdvertisement::isRunning() const
{
    return pImpl_->started_.load();
}

} // namespace OsManager
} // namespace DirtSim
