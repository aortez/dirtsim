#include "ui/state-machine/tests/TestStateMachineFixture.h"

namespace DirtSim::Ui::Tests {

TestStateMachineFixture::TestStateMachineFixture()
{
    stateMachine = std::make_unique<StateMachine>(StateMachine::TestMode{}, userSettingsManager);

    auto mockWs = std::make_unique<MockWebSocketService>();
    mockWebSocketService = mockWs.get();
    stateMachine->wsService_ = std::move(mockWs);
}

} // namespace DirtSim::Ui::Tests
