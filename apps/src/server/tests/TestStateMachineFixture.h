#pragma once

#include "server/StateMachine.h"
#include "server/network/PeerDiscovery.h"
#include "tests/MockWebSocketService.h"
#include <filesystem>
#include <memory>
#include <string>

namespace DirtSim::Server::Tests {

using DirtSim::Tests::MockWebSocketService;

class MockPeerDiscovery : public PeerDiscoveryInterface {
public:
    bool start() override;
    void stop() override;
    bool isRunning() const override;

    std::vector<PeerInfo> getPeers() const override;
    void setOnPeersChanged(std::function<void(const std::vector<PeerInfo>&)> callback) override;

    void setPeers(std::vector<PeerInfo> peers);

private:
    bool running_ = false;
    std::vector<PeerInfo> peers_;
    std::function<void(const std::vector<PeerInfo>&)> onPeersChanged_;
};

struct TestStateMachineFixture {
    explicit TestStateMachineFixture(const std::string& dataDirName = "dirtsim-test");
    ~TestStateMachineFixture();

    std::filesystem::path testDataDir;
    std::unique_ptr<StateMachine> stateMachine;
    MockWebSocketService* mockWebSocketService = nullptr;
    MockPeerDiscovery* mockPeerDiscovery = nullptr;
};

} // namespace DirtSim::Server::Tests
