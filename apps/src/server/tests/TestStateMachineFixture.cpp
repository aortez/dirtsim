#include "server/tests/TestStateMachineFixture.h"
#include "server/api/TrainingResult.h"

namespace DirtSim::Server::Tests {

TestStateMachineFixture::TestStateMachineFixture(const std::string& dataDirName)
{
    testDataDir = std::filesystem::temp_directory_path() / dataDirName;

    auto mockWs = std::make_unique<MockWebSocketService>();

    mockWebSocketService = mockWs.get();

    mockWebSocketService->expectSuccess<Api::TrainingResult>(std::monostate{});

    stateMachine = std::make_unique<StateMachine>(std::move(mockWs), testDataDir);
}

TestStateMachineFixture::~TestStateMachineFixture()
{
    stateMachine.reset();
    std::filesystem::remove_all(testDataDir);
}

} // namespace DirtSim::Server::Tests
