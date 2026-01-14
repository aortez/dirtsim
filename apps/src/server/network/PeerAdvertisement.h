#pragma once

#include "PeerDiscovery.h" // For PeerRole enum.
#include "core/Pimpl.h"

#include <cstdint>
#include <string>

namespace DirtSim {
namespace Server {

// Advertises this service on the local network via mDNS/Avahi.
// Complementary to PeerDiscovery which browses for services.
class PeerAdvertisement {
public:
    PeerAdvertisement();
    ~PeerAdvertisement();

    PeerAdvertisement(const PeerAdvertisement&) = delete;
    PeerAdvertisement& operator=(const PeerAdvertisement&) = delete;

    // Configure the service before starting.
    void setServiceName(const std::string& name);
    void setPort(uint16_t port);
    void setRole(PeerRole role);

    // Start advertising the service on the network.
    // Returns true if successfully started.
    bool start();

    // Stop advertising and clean up.
    void stop();

    // Check if currently advertising.
    bool isRunning() const;

private:
    struct Impl;
    Pimpl<Impl> pImpl_;
};

} // namespace Server
} // namespace DirtSim
