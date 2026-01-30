#include "server/tests/TestStateMachineFixture.h"
#include "server/api/TrainingResult.h"

namespace DirtSim::Server::Tests {

bool MockPeerDiscovery::start()
{
    running_ = true;
    return true;
}

void MockPeerDiscovery::stop()
{
    running_ = false;
}

bool MockPeerDiscovery::isRunning() const
{
    return running_;
}

std::vector<PeerInfo> MockPeerDiscovery::getPeers() const
{
    return peers_;
}

void MockPeerDiscovery::setOnPeersChanged(
    std::function<void(const std::vector<PeerInfo>&)> callback)
{
    onPeersChanged_ = std::move(callback);
}

void MockPeerDiscovery::setPeers(std::vector<PeerInfo> peers)
{
    peers_ = std::move(peers);
    if (onPeersChanged_) {
        onPeersChanged_(peers_);
    }
}

TestStateMachineFixture::TestStateMachineFixture(const std::string& dataDirName)
{
    testDataDir = std::filesystem::temp_directory_path() / dataDirName;

    auto mockWs = std::make_unique<MockWebSocketService>();
    auto mockPeer = std::make_unique<MockPeerDiscovery>();

    mockWebSocketService = mockWs.get();
    mockPeerDiscovery = mockPeer.get();

    mockWebSocketService->expectSuccess<Api::TrainingResult>(std::monostate{});

    stateMachine =
        std::make_unique<StateMachine>(std::move(mockWs), std::move(mockPeer), testDataDir);
}

TestStateMachineFixture::~TestStateMachineFixture()
{
    stateMachine.reset();
    std::filesystem::remove_all(testDataDir);
}

} // namespace DirtSim::Server::Tests
