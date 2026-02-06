#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace OsManager {

struct PeerTrustBundle {
    std::string host;
    std::string ssh_user = "dirtsim";
    uint16_t ssh_port = 22;
    std::string host_fingerprint_sha256;
    std::string client_pubkey;
};

void to_json(nlohmann::json& j, const PeerTrustBundle& bundle);
void from_json(const nlohmann::json& j, PeerTrustBundle& bundle);

} // namespace OsManager
} // namespace DirtSim
