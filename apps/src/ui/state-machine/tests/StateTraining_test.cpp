/**
 * @file StateTraining_test.cpp
 * @brief Unit tests for UI StateTraining.
 *
 * Tests the Training state which displays evolution progress and controls.
 * Written TDD-style - tests first, then implementation.
 */

#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketServiceInterface.h"
#include "server/api/ApiError.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/states/State.h"
#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;

/**
 * @brief Mock WebSocketService for testing command sending.
 *
 * Inherits from WebSocketServiceInterface to provide testable implementation.
 * Tracks sent commands and provides canned success responses.
 */
class MockWebSocketService : public Network::WebSocketServiceInterface {
public:
    MockWebSocketService() = default;

    // Connection methods.
    Result<std::monostate, std::string> connect(
        const std::string& /*url*/, int /*timeoutMs*/ = 5000) override
    {
        connected_ = true;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }
    std::string getUrl() const override { return "ws://mock:8080"; }

    // Server methods (no-ops for UI testing).
    Result<std::monostate, std::string> listen(uint16_t /*port*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    bool isListening() const override { return false; }
    void stopListening() override {}

    // Send methods.
    Result<std::monostate, std::string> sendBinary(const std::vector<std::byte>& /*data*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    Result<std::monostate, std::string> sendToClient(
        const std::string& /*connectionId*/, const std::string& /*message*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    // Callback registration (no-ops).
    void onConnected(ConnectionCallback /*callback*/) override {}
    void onDisconnected(ConnectionCallback /*callback*/) override {}
    void onError(ErrorCallback /*callback*/) override {}
    void onBinary(BinaryCallback /*callback*/) override {}
    void onServerCommand(ServerCommandCallback /*callback*/) override {}

    // Deserializer (no-op).
    void setJsonDeserializer(JsonDeserializer /*deserializer*/) override {}

    // Capture sent commands by name.
    std::vector<std::string> sentCommands_;

    // Override template sendCommand to bypass serialization and return typed responses.
    template <typename Okay, typename Command>
    Result<Result<Okay, ApiError>, std::string> sendCommand(
        const Command& /*cmd*/, int /*timeoutMs*/ = 5000)
    {
        // Capture command name.
        sentCommands_.push_back(Command::apiCommandName());

        // Return properly-typed success response (default-constructed Okay).
        Okay okayValue{};
        auto result = Result<Okay, ApiError>::okay(okayValue);
        return Result<Result<Okay, ApiError>, std::string>::okay(result);
    }

    // Implement sendBinaryAndReceive - returns properly serialized responses by command type.
    Result<Network::MessageEnvelope, std::string> sendBinaryAndReceive(
        const Network::MessageEnvelope& envelope, int /*timeoutMs*/ = 5000) override
    {
        // Capture command name.
        sentCommands_.push_back(envelope.message_type);

        // Create proper serialized response based on command type.
        if (envelope.message_type == "EvolutionStart") {
            Api::EvolutionStart::Okay okayValue{ .started = true };
            auto result = Result<Api::EvolutionStart::Okay, ApiError>::okay(okayValue);
            auto response =
                Network::make_response_envelope(envelope.id, envelope.message_type, result);
            return Result<Network::MessageEnvelope, std::string>::okay(response);
        }
        else if (envelope.message_type == "EvolutionStop") {
            auto result = Result<std::monostate, ApiError>::okay(std::monostate{});
            auto response =
                Network::make_response_envelope(envelope.id, envelope.message_type, result);
            return Result<Network::MessageEnvelope, std::string>::okay(response);
        }
        else if (envelope.message_type == "RenderFormatSet") {
            // RenderFormatSet uses monostate for success response.
            auto result = Result<std::monostate, ApiError>::okay(std::monostate{});
            auto response =
                Network::make_response_envelope(envelope.id, envelope.message_type, result);
            return Result<Network::MessageEnvelope, std::string>::okay(response);
        }
        else {
            // Unknown command - fail test with clear message.
            ADD_FAILURE() << "MockWebSocketService: Unknown command type: "
                          << envelope.message_type;
            return Result<Network::MessageEnvelope, std::string>::error("Unknown command");
        }
    }

private:
    bool connected_ = true;
};

/**
 * @brief Test fixture for Training state tests.
 *
 * Provides a test-mode StateMachine with injected mock WebSocketService.
 */
class StateTrainingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        stateMachine = std::make_unique<StateMachine>(StateMachine::TestMode{});

        // Inject mock WebSocketService (wsService_ is public for testing).
        auto mockWsPtr = std::make_unique<MockWebSocketService>();
        mockWs = mockWsPtr.get(); // Keep raw pointer for test access.
        stateMachine->wsService_ = std::move(mockWsPtr);
    }

    void TearDown() override
    {
        stateMachine.reset(); // Deletes mockWs via unique_ptr.
        mockWs = nullptr;
    }

    std::unique_ptr<StateMachine> stateMachine;
    MockWebSocketService* mockWs = nullptr; // Non-owning pointer for test access.
};

/**
 * @brief Test that TrainButtonClicked transitions StartMenu to Training.
 */
TEST_F(StateTrainingTest, TrainButtonClickedTransitionsStartMenuToTraining)
{
    // Setup: Create StartMenu state.
    StartMenu startMenuState;

    // Setup: Create TrainButtonClicked event.
    TrainButtonClickedEvent evt;

    // Execute: Send event to StartMenu state.
    State::Any newState = startMenuState.onEvent(evt, *stateMachine);

    // Verify: State transitioned to Training.
    ASSERT_TRUE(std::holds_alternative<Training>(newState.getVariant()))
        << "StartMenu + TrainButtonClicked should transition to Training";
}

/**
 * @brief Test that Exit command in Training transitions to Shutdown.
 */
TEST_F(StateTrainingTest, ExitCommandTransitionsToShutdown)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create Exit command with callback.
    bool callbackInvoked = false;
    UiApi::Exit::Command cmd;
    UiApi::Exit::Cwc cwc(cmd, [&](UiApi::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send Exit command.
    State::Any newState = trainingState.onEvent(cwc, *stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Training + Exit should transition to Shutdown";
    EXPECT_TRUE(callbackInvoked) << "Response callback should be invoked";
}

/**
 * @brief Test that Training state has correct name.
 */
TEST_F(StateTrainingTest, HasCorrectStateName)
{
    Training trainingState;
    EXPECT_STREQ(trainingState.name(), "Training");
}

/**
 * @brief Test that EvolutionProgress event updates Training state.
 */
TEST_F(StateTrainingTest, EvolutionProgressUpdatesState)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create EvolutionProgress event.
    EvolutionProgressReceivedEvent evt;
    evt.progress.generation = 5;
    evt.progress.maxGenerations = 100;
    evt.progress.currentEval = 10;
    evt.progress.populationSize = 50;
    evt.progress.bestFitnessThisGen = 2.5;
    evt.progress.bestFitnessAllTime = 3.0;
    evt.progress.averageFitness = 1.5;

    // Execute: Send event to Training state.
    State::Any result = trainingState.onEvent(evt, *stateMachine);

    // Verify: State did not transition (nullopt means stay).
    EXPECT_FALSE(result.getVariant().index() != 6)
        << "Training + EvolutionProgress should not transition";

    // Verify: Progress was captured.
    EXPECT_EQ(trainingState.progress.generation, 5);
    EXPECT_EQ(trainingState.progress.maxGenerations, 100);
    EXPECT_EQ(trainingState.progress.currentEval, 10);
    EXPECT_EQ(trainingState.progress.populationSize, 50);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessThisGen, 2.5);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessAllTime, 3.0);
    EXPECT_DOUBLE_EQ(trainingState.progress.averageFitness, 1.5);
}

/**
 * @brief Test that ServerDisconnected transitions to Disconnected.
 */
TEST_F(StateTrainingTest, ServerDisconnectedTransitionsToDisconnected)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create disconnect event.
    ServerDisconnectedEvent evt{ "Connection lost" };

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *stateMachine);

    // Verify: State transitioned to Disconnected.
    ASSERT_TRUE(std::holds_alternative<Disconnected>(newState.getVariant()))
        << "Training + ServerDisconnected should transition to Disconnected";
}

/**
 * @brief Test that StartEvolutionButtonClicked sends EvolutionStart command.
 */
TEST_F(StateTrainingTest, StartEvolutionSendsCommand)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create StartEvolutionButtonClicked event with config.
    StartEvolutionButtonClickedEvent evt;
    evt.evolution.populationSize = 10;
    evt.evolution.maxGenerations = 5;
    evt.mutation.rate = 0.1;

    // Execute: Send event to Training state.
    State::Any result = trainingState.onEvent(evt, *stateMachine);

    // Verify: State did not transition (stays in Training).
    EXPECT_TRUE(std::holds_alternative<Training>(result.getVariant()))
        << "Training + StartEvolutionButtonClicked should stay in Training";

    // Verify: EvolutionStart command was sent.
    ASSERT_GE(mockWs->sentCommands_.size(), 1) << "Should send at least EvolutionStart command";
    EXPECT_EQ(mockWs->sentCommands_[0], "EvolutionStart");
}

/**
 * @brief Test that StopButtonClicked sends EvolutionStop and transitions to StartMenu.
 */
TEST_F(StateTrainingTest, StopButtonSendsCommandAndTransitions)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create StopButtonClicked event.
    StopButtonClickedEvent evt;

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *stateMachine);

    // Verify: State transitioned to StartMenu.
    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "Training + StopButtonClicked should transition to StartMenu";

    // Verify: EvolutionStop command was sent.
    ASSERT_EQ(mockWs->sentCommands_.size(), 1) << "Should send EvolutionStop command";
    EXPECT_EQ(mockWs->sentCommands_[0], "EvolutionStop");
}
