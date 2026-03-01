#include "ui/state-machine/tests/TestStateMachineFixture.h"

namespace DirtSim::Ui::Tests {

TestStateMachineFixture::TestStateMachineFixture()
{
    stateMachine = std::make_unique<StateMachine>(
        StateMachine::TestMode{}, userSettingsManager, scenarioMetadataManager);

    auto mockWs = std::make_unique<MockWebSocketService>();
    mockWebSocketService = mockWs.get();
    stateMachine->wsService_ = std::move(mockWs);
    userSettingsManager.setWebSocketService(stateMachine->wsService_.get());
}

} // namespace DirtSim::Ui::Tests
