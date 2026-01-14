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
#include "server/api/RenderFormatSet.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/states/State.h"
#include <gtest/gtest.h>
#include <map>
#include <optional>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;

/**
 * @brief Mock WebSocketService for testing command sending.
 *
 * Type-safe mock that uses Command::OkayType to tie commands to responses.
 * Tests configure expected responses, mock returns them when commands are sent.
 */
class MockWebSocketService : public Network::WebSocketServiceInterface {
public:
    MockWebSocketService() = default;

    // =========================================================================
    // Type-safe response configuration.
    // =========================================================================

    /**
     * @brief Configure success response for a command type.
     * @tparam CommandType The command struct (e.g., Api::EvolutionStart::Command).
     * @param okay The success response value.
     */
    template <typename CommandType>
    void expectSuccess(const typename CommandType::OkayType& okay)
    {
        auto response = Result<typename CommandType::OkayType, ApiError>::okay(okay);
        responses_[std::string(CommandType::name())] =
            Network::make_response_envelope(0, std::string(CommandType::name()), response);
    }

    /**
     * @brief Configure error response for a command type.
     * @tparam CommandType The command struct (e.g., Api::EvolutionStart::Command).
     * @param message The error message.
     */
    template <typename CommandType>
    void expectError(const std::string& message)
    {
        ApiError error;
        error.message = message;
        auto response = Result<typename CommandType::OkayType, ApiError>::error(error);
        responses_[std::string(CommandType::name())] =
            Network::make_response_envelope(0, std::string(CommandType::name()), response);
    }

    /**
     * @brief Get list of commands that were sent.
     */
    const std::vector<std::string>& sentCommands() const { return sentCommands_; }

    /**
     * @brief Clear sent commands (for multi-phase tests).
     */
    void clearSentCommands() { sentCommands_.clear(); }

    // =========================================================================
    // WebSocketServiceInterface implementation.
    // =========================================================================

    Result<std::monostate, std::string> connect(
        const std::string& /*url*/, int /*timeoutMs*/ = 5000) override
    {
        connected_ = true;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }
    std::string getUrl() const override { return "ws://mock:8080"; }

    Result<std::monostate, std::string> listen(uint16_t /*port*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    bool isListening() const override { return false; }
    void stopListening() override {}

    Result<std::monostate, std::string> sendBinary(const std::vector<std::byte>& /*data*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    Result<std::monostate, std::string> sendToClient(
        const std::string& /*connectionId*/, const std::string& /*message*/) override
    {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    void onConnected(ConnectionCallback /*callback*/) override {}
    void onDisconnected(ConnectionCallback /*callback*/) override {}
    void onError(ErrorCallback /*callback*/) override {}
    void onBinary(BinaryCallback /*callback*/) override {}
    void onServerCommand(ServerCommandCallback /*callback*/) override {}
    void setJsonDeserializer(JsonDeserializer /*deserializer*/) override {}

    /**
     * @brief Core mock method - returns configured response for command.
     */
    Result<Network::MessageEnvelope, std::string> sendBinaryAndReceive(
        const Network::MessageEnvelope& envelope, int /*timeoutMs*/ = 5000) override
    {
        sentCommands_.push_back(envelope.message_type);

        auto it = responses_.find(envelope.message_type);
        if (it != responses_.end()) {
            auto response = it->second;
            response.id = envelope.id; // Match correlation ID.
            return Result<Network::MessageEnvelope, std::string>::okay(response);
        }

        // No configured response - fail test with clear message.
        ADD_FAILURE() << "MockWebSocketService: No response configured for: "
                      << envelope.message_type;
        return Result<Network::MessageEnvelope, std::string>::error(
            "No response configured for: " + envelope.message_type);
    }

private:
    bool connected_ = true;
    std::map<std::string, Network::MessageEnvelope> responses_;
    std::vector<std::string> sentCommands_;
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

    // Verify: State did not transition (stays in Training).
    EXPECT_TRUE(std::holds_alternative<Training>(result.getVariant()))
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
    // Setup: Configure expected responses.
    mockWs->expectSuccess<Api::EvolutionStart::Command>({ .started = true });
    mockWs->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });

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
    ASSERT_GE(mockWs->sentCommands().size(), 1) << "Should send at least EvolutionStart command";
    EXPECT_EQ(mockWs->sentCommands()[0], "EvolutionStart");
}

/**
 * @brief Test that StopButtonClicked sends EvolutionStop and transitions to StartMenu.
 */
TEST_F(StateTrainingTest, StopButtonSendsCommandAndTransitions)
{
    // Setup: Configure expected response.
    mockWs->expectSuccess<Api::EvolutionStop::Command>(std::monostate{});

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
    ASSERT_EQ(mockWs->sentCommands().size(), 1) << "Should send EvolutionStop command";
    EXPECT_EQ(mockWs->sentCommands()[0], "EvolutionStop");
}
