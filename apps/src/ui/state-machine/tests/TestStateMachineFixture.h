#pragma once

#include "tests/MockWebSocketService.h"
#include "ui/ScenarioMetadataManager.h"
#include "ui/UserSettingsManager.h"
#include "ui/state-machine/StateMachine.h"
#include <memory>

namespace DirtSim::Ui::Tests {

using DirtSim::Tests::MockWebSocketService;

struct TestStateMachineFixture {
    TestStateMachineFixture();

    std::unique_ptr<StateMachine> stateMachine;
    UserSettingsManager userSettingsManager;
    ScenarioMetadataManager scenarioMetadataManager;
    MockWebSocketService* mockWebSocketService = nullptr;
};

} // namespace DirtSim::Ui::Tests
