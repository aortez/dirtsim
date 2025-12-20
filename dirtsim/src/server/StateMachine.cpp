#include "StateMachine.h"
#include "Event.h"
#include "EventProcessor.h"
#include "api/PeersGet.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageFull.h"
#include "core/RenderMessageUtils.h"
#include "core/ScenarioConfig.h"
#include "core/SystemMetrics.h"
#include "core/Timers.h"
#include "core/World.h" // Must be first for complete type in variant.
#include "core/WorldData.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "network/CommandDeserializerJson.h"
#include "network/PeerAdvertisement.h"
#include "network/PeerDiscovery.h"
#include "scenarios/Scenario.h"
#include "scenarios/ScenarioRegistry.h"
#include "states/State.h"
#include <cassert>
#include <chrono>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Server {

// =================================================================
// PIMPL IMPLEMENTATION STRUCT
// =================================================================

struct SubscribedClient {
    std::string connectionId;
    RenderFormat renderFormat;
};

struct StateMachine::Impl {
    EventProcessor eventProcessor_;
    ScenarioRegistry scenarioRegistry_;
    SystemMetrics systemMetrics_;
    Timers timers_;
    PeerAdvertisement peerAdvertisement_;
    PeerDiscovery peerDiscovery_;
    State::Any fsmState_{ State::Startup{} };
    Network::WebSocketService* wsService_ = nullptr;
    std::shared_ptr<WorldData> cachedWorldData_;
    mutable std::mutex cachedWorldDataMutex_;

    std::vector<SubscribedClient> subscribedClients_;

    Impl() : scenarioRegistry_(ScenarioRegistry::createDefault()) {}
};

StateMachine::StateMachine() : pImpl()
{
    LOG_INFO(
        State,
        "Server::StateMachine initialized in headless mode in state: {}",
        getCurrentStateName());
    // Note: World will be created by SimRunning state when simulation starts.

    // Start peer discovery for mDNS service browsing.
    if (pImpl->peerDiscovery_.start()) {
        spdlog::info("PeerDiscovery started successfully");
    }
    else {
        spdlog::warn("PeerDiscovery failed to start (Avahi may not be available)");
    }
}

StateMachine::~StateMachine()
{
    pImpl->peerAdvertisement_.stop();
    pImpl->peerDiscovery_.stop();
    LOG_INFO(State, "Server::StateMachine shutting down from state: {}", getCurrentStateName());
}

void StateMachine::startPeerAdvertisement(uint16_t port, const std::string& serviceName)
{
    pImpl->peerAdvertisement_.setServiceName(serviceName);
    pImpl->peerAdvertisement_.setPort(port);
    pImpl->peerAdvertisement_.setRole(PeerRole::Physics);

    if (pImpl->peerAdvertisement_.start()) {
        spdlog::info("PeerAdvertisement started: {} on port {}", serviceName, port);
    }
    else {
        spdlog::warn("PeerAdvertisement failed to start (Avahi may not be available)");
    }
}

// =================================================================
// ACCESSOR IMPLEMENTATIONS
// =================================================================

std::string StateMachine::getCurrentStateName() const
{
    return State::getCurrentStateName(pImpl->fsmState_);
}

EventProcessor& StateMachine::getEventProcessor()
{
    return pImpl->eventProcessor_;
}

const EventProcessor& StateMachine::getEventProcessor() const
{
    return pImpl->eventProcessor_;
}

Network::WebSocketService* StateMachine::getWebSocketService()
{
    return pImpl->wsService_;
}

void StateMachine::setWebSocketService(Network::WebSocketService* service)
{
    pImpl->wsService_ = service;
}

void StateMachine::setupWebSocketService(Network::WebSocketService& service)
{
    spdlog::info("StateMachine: Setting up WebSocketService command handlers...");

    // Store pointer for later access (broadcasting, etc.).
    setWebSocketService(&service);

    // =========================================================================
    // JSON protocol support - inject deserializer and dispatcher.
    // =========================================================================

    // Inject JSON deserializer.
    service.setJsonDeserializer([](const std::string& json) -> std::any {
        CommandDeserializerJson deserializer;
        auto result = deserializer.deserialize(json);
        if (result.isError()) {
            throw std::runtime_error(result.errorValue().message);
        }
        return result.value(); // Return ApiCommand variant wrapped in std::any.
    });

    // Inject JSON command dispatcher.
    service.setJsonCommandDispatcher([this](
                                         std::any cmdAny,
                                         std::shared_ptr<rtc::WebSocket> ws,
                                         uint64_t correlationId,
                                         Network::WebSocketService::HandlerInvoker invokeHandler) {
        // Cast back to ApiCommand variant.
        ApiCommand cmdVariant = std::any_cast<ApiCommand>(cmdAny);
// Visit the variant and dispatch to appropriate handler.
#define DISPATCH_JSON_CMD_WITH_RESP(NamespaceType)                                          \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            nlohmann::json j;                                                               \
            if (resp.isError()) {                                                           \
                j = { { "id", correlationId }, { "error", resp.errorValue().message } };    \
            }                                                                               \
            else {                                                                          \
                j = resp.value().toJson();                                                  \
                j["id"] = correlationId;                                                    \
                j["success"] = true;                                                        \
            }                                                                               \
            ws->send(j.dump());                                                             \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

#define DISPATCH_JSON_CMD_EMPTY(NamespaceType)                                              \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            nlohmann::json j;                                                               \
            if (resp.isError()) {                                                           \
                j = { { "id", correlationId }, { "error", resp.errorValue().message } };    \
            }                                                                               \
            else {                                                                          \
                j = { { "id", correlationId }, { "success", true } };                       \
            }                                                                               \
            ws->send(j.dump());                                                             \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }
        DISPATCH_JSON_CMD_WITH_RESP(Api::CellGet);
        DISPATCH_JSON_CMD_EMPTY(Api::CellSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::DiagramGet);
        DISPATCH_JSON_CMD_EMPTY(Api::Exit);
        DISPATCH_JSON_CMD_EMPTY(Api::GravitySet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::PeersGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::PerfStatsGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::PhysicsSettingsGet);
        DISPATCH_JSON_CMD_EMPTY(Api::PhysicsSettingsSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::RenderFormatGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::RenderFormatSet);
        DISPATCH_JSON_CMD_EMPTY(Api::Reset);
        DISPATCH_JSON_CMD_WITH_RESP(Api::ScenarioConfigSet);
        DISPATCH_JSON_CMD_EMPTY(Api::SeedAdd);
        DISPATCH_JSON_CMD_WITH_RESP(Api::SimRun);
        DISPATCH_JSON_CMD_EMPTY(Api::SpawnDirtBall);
        DISPATCH_JSON_CMD_WITH_RESP(Api::StateGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::StatusGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::TimerStatsGet);
        DISPATCH_JSON_CMD_EMPTY(Api::WorldResize);

#undef DISPATCH_JSON_CMD_WITH_RESP
#undef DISPATCH_JSON_CMD_EMPTY

        spdlog::warn("StateMachine: Unknown JSON command type in variant");
    });

    // =========================================================================
    // Immediate handlers - respond right away without queuing.
    // =========================================================================

    // StateGet - return cached world data.
    service.registerHandler<Api::StateGet::Cwc>([this](Api::StateGet::Cwc cwc) {
        auto cachedPtr = getCachedWorldData();
        if (!cachedPtr) {
            cwc.sendResponse(Api::StateGet::Response::error(ApiError{ "No world data available" }));
            return;
        }

        Api::StateGet::Okay okay;
        okay.worldData = *cachedPtr;
        cwc.sendResponse(Api::StateGet::Response::okay(std::move(okay)));
    });

    // StatusGet - return lightweight status (always includes state, world data if available).
    service.registerHandler<Api::StatusGet::Cwc>([this](Api::StatusGet::Cwc cwc) {
        Api::StatusGet::Okay status;

        // Always include current state machine state.
        status.state = getCurrentStateName();

        // Populate from world data if available (Idle state won't have cached data).
        auto cachedPtr = getCachedWorldData();
        if (cachedPtr) {
            status.timestep = cachedPtr->timestep;
            status.width = cachedPtr->width;
            status.height = cachedPtr->height;
        }

        // Get scenario_id from SimRunning state if active.
        std::visit(
            [&status](auto&& state) {
                using T = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<T, State::SimRunning>) {
                    status.scenario_id = state.scenario_id;
                }
            },
            pImpl->fsmState_.getVariant());

        // System health metrics.
        auto metrics = pImpl->systemMetrics_.get();
        status.cpu_percent = metrics.cpu_percent;
        status.memory_percent = metrics.memory_percent;

        cwc.sendResponse(Api::StatusGet::Response::okay(std::move(status)));
    });

    // PeersGet - return discovered mDNS peers.
    service.registerHandler<Api::PeersGet::Cwc>([this](Api::PeersGet::Cwc cwc) {
        auto peers = pImpl->peerDiscovery_.getPeers();
        Api::PeersGet::Okay response;
        response.peers = std::move(peers);
        cwc.sendResponse(Api::PeersGet::Response::okay(std::move(response)));
    });

    // RenderFormatGet - return default format (TODO: track per-client).
    service.registerHandler<Api::RenderFormatGet::Cwc>([](Api::RenderFormatGet::Cwc cwc) {
        Api::RenderFormatGet::Okay okay;
        okay.active_format = RenderFormat::BASIC; // Default for now.
        cwc.sendResponse(Api::RenderFormatGet::Response::okay(std::move(okay)));
    });

    service.registerHandler<Api::RenderFormatSet::Cwc>(
        [this](Api::RenderFormatSet::Cwc cwc) { queueEvent(cwc); });

    // =========================================================================
    // Queued handlers - queue to state machine for processing.
    // =========================================================================

    // All queued commands follow the same pattern: queue CWC to state machine.
    // State machine routes to current state's onEvent() handler.

    service.registerHandler<Api::CellGet::Cwc>([this](Api::CellGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::CellSet::Cwc>([this](Api::CellSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::DiagramGet::Cwc>(
        [this](Api::DiagramGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::Exit::Cwc>([this](Api::Exit::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerDown::Cwc>(
        [this](Api::FingerDown::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerMove::Cwc>(
        [this](Api::FingerMove::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerUp::Cwc>(
        [this](Api::FingerUp::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::GravitySet::Cwc>(
        [this](Api::GravitySet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::PerfStatsGet::Cwc>(
        [this](Api::PerfStatsGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::PhysicsSettingsGet::Cwc>(
        [this](Api::PhysicsSettingsGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::PhysicsSettingsSet::Cwc>(
        [this](Api::PhysicsSettingsSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::Reset::Cwc>([this](Api::Reset::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::ScenarioConfigSet::Cwc>(
        [this](Api::ScenarioConfigSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::ScenarioListGet::Cwc>(
        [this](Api::ScenarioListGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SeedAdd::Cwc>([this](Api::SeedAdd::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SimRun::Cwc>([this](Api::SimRun::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SpawnDirtBall::Cwc>(
        [this](Api::SpawnDirtBall::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::WorldResize::Cwc>(
        [this](Api::WorldResize::Cwc cwc) { queueEvent(cwc); });

    spdlog::info("StateMachine: WebSocketService handlers registered");
}

void StateMachine::updateCachedWorldData(const WorldData& data)
{
    std::lock_guard<std::mutex> lock(pImpl->cachedWorldDataMutex_);
    pImpl->cachedWorldData_ = std::make_shared<WorldData>(data);
}

std::shared_ptr<WorldData> StateMachine::getCachedWorldData() const
{
    std::lock_guard<std::mutex> lock(pImpl->cachedWorldDataMutex_);
    return pImpl->cachedWorldData_; // Returns shared_ptr (may be nullptr).
}

ScenarioRegistry& StateMachine::getScenarioRegistry()
{
    return pImpl->scenarioRegistry_;
}

const ScenarioRegistry& StateMachine::getScenarioRegistry() const
{
    return pImpl->scenarioRegistry_;
}

Timers& StateMachine::getTimers()
{
    return pImpl->timers_;
}

const Timers& StateMachine::getTimers() const
{
    return pImpl->timers_;
}

PeerDiscovery& StateMachine::getPeerDiscovery()
{
    return pImpl->peerDiscovery_;
}

const PeerDiscovery& StateMachine::getPeerDiscovery() const
{
    return pImpl->peerDiscovery_;
}

void StateMachine::mainLoopRun()
{
    spdlog::info("Starting main event loop");

    // Initialize by sending init complete event.
    queueEvent(InitCompleteEvent{});

    // Main event processing loop.
    while (!shouldExit()) {
        auto loopIterationStart = std::chrono::steady_clock::now();

        // Process events from queue.
        auto eventProcessStart = std::chrono::steady_clock::now();
        pImpl->eventProcessor_.processEventsFromQueue(*this);
        auto eventProcessEnd = std::chrono::steady_clock::now();

        // Tick the simulation if in SimRunning state.
        if (std::holds_alternative<State::SimRunning>(pImpl->fsmState_.getVariant())) {
            auto& simRunning = std::get<State::SimRunning>(pImpl->fsmState_.getVariant());

            // Record frame start time for frame limiting.
            auto frameStart = std::chrono::steady_clock::now();

            // Advance simulation.
            simRunning.tick(*this);

            auto frameEnd = std::chrono::steady_clock::now();

            // Log timing breakdown every 1000 frames.
            static int frameCount = 0;
            static double totalEventProcessMs = 0.0;
            static double totalTickMs = 0.0;
            static double totalSleepMs = 0.0;
            static double totalIterationMs = 0.0;

            double eventProcessMs =
                std::chrono::duration<double, std::milli>(eventProcessEnd - eventProcessStart)
                    .count();
            double tickMs =
                std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

            totalEventProcessMs += eventProcessMs;
            totalTickMs += tickMs;

            // Apply frame rate limiting if configured.
            double sleepMs = 0.0;
            if (simRunning.frameLimit > 0) {
                auto elapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart)
                        .count();

                int remainingMs = simRunning.frameLimit - static_cast<int>(elapsedMs);
                if (remainingMs > 0) {
                    auto sleepStart = std::chrono::steady_clock::now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(remainingMs));
                    auto sleepEnd = std::chrono::steady_clock::now();
                    sleepMs =
                        std::chrono::duration<double, std::milli>(sleepEnd - sleepStart).count();
                    totalSleepMs += sleepMs;
                }
            }

            auto loopIterationEnd = std::chrono::steady_clock::now();
            double iterationMs =
                std::chrono::duration<double, std::milli>(loopIterationEnd - loopIterationStart)
                    .count();
            totalIterationMs += iterationMs;

            frameCount++;
            if (frameCount % 500 == 0) {
                spdlog::info("Main loop timing (avg over {} frames):", frameCount);
                spdlog::info("  Event processing: {:.2f}ms", totalEventProcessMs / frameCount);
                spdlog::info("  Simulation tick: {:.2f}ms", totalTickMs / frameCount);
                spdlog::info("  Sleep: {:.2f}ms", totalSleepMs / frameCount);
                spdlog::info("  Total iteration: {:.2f}ms", totalIterationMs / frameCount);
                spdlog::info(
                    "  Unaccounted: {:.2f}ms",
                    (totalIterationMs - totalEventProcessMs - totalTickMs - totalSleepMs)
                        / frameCount);
            }

            // If frameLimit == 0, no sleep (run as fast as possible).
        }
        else {
            // Small sleep when not running to prevent busy waiting.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    spdlog::info("State machine event loop exiting (shouldExit=true)");

    spdlog::info("Main event loop exiting");
}

void StateMachine::queueEvent(const Event& event)
{
    pImpl->eventProcessor_.enqueueEvent(event);
}

void StateMachine::processEvents()
{
    pImpl->eventProcessor_.processEventsFromQueue(*this);
}

void StateMachine::handleEvent(const Event& event)
{
    LOG_DEBUG(State, "Server::StateMachine: Handling event: {}", getEventName(event));

    // Handle RenderFormatSet globally (works in any state).
    if (std::holds_alternative<Api::RenderFormatSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::RenderFormatSet::Cwc>(event.getVariant());
        const std::string& connectionId = cwc.command.connectionId;
        assert(!connectionId.empty() && "RenderFormatSet: connectionId must be populated!");

        // Add or update client subscription.
        auto it = std::find_if(
            pImpl->subscribedClients_.begin(),
            pImpl->subscribedClients_.end(),
            [&connectionId](const SubscribedClient& c) { return c.connectionId == connectionId; });

        if (it != pImpl->subscribedClients_.end()) {
            it->renderFormat = cwc.command.format;
        }
        else {
            pImpl->subscribedClients_.push_back({ connectionId, cwc.command.format });
        }

        spdlog::info(
            "StateMachine: Client '{}' subscribed (format={}, total={})",
            connectionId,
            cwc.command.format == RenderFormat::BASIC ? "BASIC" : "DEBUG",
            pImpl->subscribedClients_.size());

        Api::RenderFormatSet::Okay okay;
        okay.active_format = cwc.command.format;
        okay.message = "Subscribed to render messages";
        cwc.sendResponse(Api::RenderFormatSet::Response::okay(std::move(okay)));
        return;
    }

    std::visit(
        [this](auto&& evt) {
            std::visit(
                [this, &evt](auto&& state) -> void {
                    using StateType = std::decay_t<decltype(state)>;

                    if constexpr (requires { state.onEvent(evt, *this); }) {
                        auto newState = state.onEvent(evt, *this);
                        if (!std::holds_alternative<StateType>(newState.getVariant())) {
                            transitionTo(std::move(newState));
                        }
                        else {
                            // Same state type - move it back into variant to preserve state.
                            pImpl->fsmState_ = std::move(newState);
                        }
                    }
                    else {
                        // Handle state-independent read-only queries generically.
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(evt)>,
                                          Api::PeersGet::Cwc>) {
                            spdlog::debug(
                                "Server::StateMachine: Handling PeersGet generically (state: {})",
                                State::getCurrentStateName(pImpl->fsmState_));
                            auto peers = pImpl->peerDiscovery_.getPeers();
                            Api::PeersGet::Okay response;
                            response.peers = std::move(peers);
                            evt.sendResponse(Api::PeersGet::Response::okay(std::move(response)));
                        }
                        else {
                            spdlog::warn(
                                "Server::StateMachine: State {} does not handle event {}",
                                State::getCurrentStateName(pImpl->fsmState_),
                                getEventName(Event{ evt }));

                            // If this is an API command with sendResponse, send error.
                            if constexpr (requires {
                                              evt.sendResponse(std::declval<typename std::decay_t<
                                                                   decltype(evt)>::Response>());
                                          }) {
                                auto errorMsg = std::string("Command not supported in state: ")
                                    + State::getCurrentStateName(pImpl->fsmState_);
                                using EventType = std::decay_t<decltype(evt)>;
                                using ResponseType = typename EventType::Response;
                                evt.sendResponse(ResponseType::error(ApiError(errorMsg)));
                            }
                        }
                    }
                },
                pImpl->fsmState_.getVariant());
        },
        event.getVariant());
}

void StateMachine::transitionTo(State::Any newState)
{
    std::string oldStateName = getCurrentStateName();

    // Call onExit for current state.
    std::visit([this](auto& state) { callOnExit(state); }, pImpl->fsmState_.getVariant());

    // Perform transition.
    pImpl->fsmState_ = std::move(newState);

    std::string newStateName = getCurrentStateName();
    LOG_INFO(State, "Server::StateMachine: {} -> {}", oldStateName, newStateName);

    // Call onEnter for new state.
    std::visit([this](auto& state) { callOnEnter(state); }, pImpl->fsmState_.getVariant());
}

// Global event handlers.

State::Any StateMachine::onEvent(const QuitApplicationCommand& /*cmd.*/)
{
    LOG_INFO(State, "Global handler: QuitApplicationCommand received");
    setShouldExit(true);
    return State::Shutdown{};
}

State::Any StateMachine::onEvent(const GetFPSCommand& /*cmd.*/)
{
    // This is an immediate event, should not reach here.
    spdlog::warn("GetFPSCommand reached global handler - should be immediate");
    // Return a default-constructed state of the same type.
    return std::visit(
        [](auto&& state) -> State::Any {
            using T = std::decay_t<decltype(state)>;
            return T{};
        },
        pImpl->fsmState_.getVariant());
}

State::Any StateMachine::onEvent(const GetSimStatsCommand& /*cmd.*/)
{
    // This is an immediate event, should not reach here.
    spdlog::warn("GetSimStatsCommand reached global handler - should be immediate");
    // Return a default-constructed state of the same type.
    return std::visit(
        [](auto&& state) -> State::Any {
            using T = std::decay_t<decltype(state)>;
            return T{};
        },
        pImpl->fsmState_.getVariant());
}

void StateMachine::broadcastRenderMessage(
    const WorldData& data, const std::string& scenario_id, const ScenarioConfig& scenario_config)
{
    if (pImpl->subscribedClients_.empty()) {
        spdlog::debug("StateMachine: broadcastRenderMessage called but no subscribed clients");
        return;
    }

    spdlog::debug(
        "StateMachine: Broadcasting to {} subscribed clients (step {})",
        pImpl->subscribedClients_.size(),
        data.timestep);

    for (const auto& client : pImpl->subscribedClients_) {
        // Pack render message.
        RenderMessage msg = RenderMessageUtils::packRenderMessage(data, client.renderFormat);

        // Bundle with scenario metadata for transport.
        RenderMessageFull fullMsg;
        fullMsg.render_data = std::move(msg);
        fullMsg.scenario_id = scenario_id;
        fullMsg.scenario_config = scenario_config;

        // Serialize RenderMessageFull to payload.
        std::vector<std::byte> payload;
        zpp::bits::out payloadOut(payload);
        payloadOut(fullMsg).or_throw();

        // Wrap in MessageEnvelope for consistent protocol.
        Network::MessageEnvelope envelope{ .id = 0, // No correlation for server pushes.
                                           .message_type = "RenderMessage",
                                           .payload = std::move(payload) };

        std::vector<std::byte> envelopeData = Network::serialize_envelope(envelope);

        auto result = pImpl->wsService_->sendToClient(client.connectionId, envelopeData);
        if (result.isError()) {
            spdlog::error(
                "StateMachine: Failed to send RenderMessage to '{}': {}",
                client.connectionId,
                result.errorValue());
        }
    }
}

} // namespace Server
} // namespace DirtSim
