#pragma once

#include "server/StateMachine.h"
#include "tests/MockWebSocketService.h"
#include <filesystem>
#include <memory>
#include <string>

namespace DirtSim::Server::Tests {

using DirtSim::Tests::MockWebSocketService;

struct TestStateMachineFixture {
    explicit TestStateMachineFixture(const std::string& dataDirName = "dirtsim-test");
    ~TestStateMachineFixture();

    std::filesystem::path testDataDir;
    std::unique_ptr<StateMachine> stateMachine;
    MockWebSocketService* mockWebSocketService = nullptr;
};

} // namespace DirtSim::Server::Tests
