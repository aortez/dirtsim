#pragma once

#include "tests/MockWebSocketService.h"
#include "ui/state-machine/StateMachine.h"
#include <memory>

namespace DirtSim::Ui::Tests {

using DirtSim::Tests::MockWebSocketService;

struct TestStateMachineFixture {
    TestStateMachineFixture();

    std::unique_ptr<StateMachine> stateMachine;
    UserSettingsManager userSettingsManager;
    MockWebSocketService* mockWebSocketService = nullptr;
};

} // namespace DirtSim::Ui::Tests
