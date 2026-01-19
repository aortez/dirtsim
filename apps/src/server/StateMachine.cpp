#include "StateMachine.h"
#include "Event.h"
#include "EventProcessor.h"
#include "ServerConfig.h"
#include "api/PeersGet.h"
#include "api/TrainingResult.h"
#include "api/TrainingResultGet.h"
#include "api/TrainingResultList.h"
#include "api/TrainingResultSet.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageFull.h"
#include "core/RenderMessageUtils.h"
#include "core/ScenarioConfig.h"
#include "core/StateLifecycle.h"
#include "core/SystemMetrics.h"
#include "core/Timers.h"
#include "core/World.h" // Must be first for complete type in variant.
#include "core/WorldData.h"
#include "core/input/GamepadManager.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "network/CommandDeserializerJson.h"
#include "network/PeerAdvertisement.h"
#include "network/PeerDiscovery.h"
#include "states/State.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
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
    RenderFormat::EnumType renderFormat;
};

namespace {

std::filesystem::path getDefaultDataDir()
{
    const char* home = std::getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    return std::filesystem::path(home) / ".dirtsim";
}

} // namespace

struct StateMachine::Impl {
    EventProcessor eventProcessor_;
    std::unique_ptr<GamepadManager> gamepadManager_;
    GenomeRepository genomeRepository_;
    ScenarioRegistry scenarioRegistry_;
    SystemMetrics systemMetrics_;
    Timers timers_;
    PeerAdvertisement peerAdvertisement_;
    PeerDiscovery peerDiscovery_;
    State::Any fsmState_{ State::PreStartup{} };
    Network::WebSocketService* wsService_ = nullptr;
    std::shared_ptr<WorldData> cachedWorldData_;
    mutable std::mutex cachedWorldDataMutex_;

    std::vector<SubscribedClient> subscribedClients_;
    std::vector<Api::TrainingResult> trainingResults_;
    mutable std::mutex trainingResultsMutex_;

    explicit Impl(const std::optional<std::filesystem::path>& dataDir)
        : genomeRepository_(initGenomeRepository(dataDir)),
          scenarioRegistry_(ScenarioRegistry::createDefault(genomeRepository_))
    {}

private:
    static GenomeRepository initGenomeRepository(
        const std::optional<std::filesystem::path>& dataDir)
    {
        auto dir = dataDir.value_or(getDefaultDataDir());
        std::filesystem::create_directories(dir);
        auto dbPath = dir / "genomes.db";
        spdlog::info("GenomeRepository: Using database at {}", dbPath.string());
        return GenomeRepository(dbPath);
    }
};

StateMachine::StateMachine(const std::optional<std::filesystem::path>& dataDir) : pImpl(dataDir)
{
    serverConfig = std::make_unique<ServerConfig>();
    serverConfig->dataDir = dataDir;

    LOG_INFO(
        State,
        "Server::StateMachine initialized in headless mode in state: {}",
        getCurrentStateName());

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

    // Register for client disconnect notifications to clean up subscriber list.
    service.onClientDisconnect([this](const std::string& connectionId) {
        auto it = std::remove_if(
            pImpl->subscribedClients_.begin(),
            pImpl->subscribedClients_.end(),
            [&connectionId](const SubscribedClient& c) { return c.connectionId == connectionId; });
        if (it != pImpl->subscribedClients_.end()) {
            pImpl->subscribedClients_.erase(it, pImpl->subscribedClients_.end());
            spdlog::info(
                "StateMachine: Client '{}' disconnected, removed from subscribers (remaining={})",
                connectionId,
                pImpl->subscribedClients_.size());
        }
    });

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
        DISPATCH_JSON_CMD_EMPTY(Api::SimStop);
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

        // Get state-specific fields.
        std::visit(
            [&status](auto&& state) {
                using T = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<T, State::SimRunning>) {
                    status.scenario_id = state.scenario_id;
                }
                else if constexpr (std::is_same_v<T, State::Error>) {
                    status.error_message = state.error_message;
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
        okay.active_format = RenderFormat::EnumType::Basic; // Default for now.
        cwc.sendResponse(Api::RenderFormatGet::Response::okay(std::move(okay)));
    });

    service.registerHandler<Api::TrainingResultList::Cwc>([this](Api::TrainingResultList::Cwc cwc) {
        std::vector<Api::TrainingResult> snapshot;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            snapshot = pImpl->trainingResults_;
        }

        Api::TrainingResultList::Okay response;
        response.results.reserve(snapshot.size());
        for (const auto& result : snapshot) {
            Api::TrainingResultList::Entry entry;
            entry.summary = result.summary;
            entry.candidateCount = static_cast<int>(result.candidates.size());
            response.results.push_back(std::move(entry));
        }

        cwc.sendResponse(Api::TrainingResultList::Response::okay(std::move(response)));
    });

    service.registerHandler<Api::TrainingResultGet::Cwc>([this](Api::TrainingResultGet::Cwc cwc) {
        std::optional<Api::TrainingResult> found;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            for (auto it = pImpl->trainingResults_.rbegin(); it != pImpl->trainingResults_.rend();
                 ++it) {
                if (it->summary.trainingSessionId == cwc.command.trainingSessionId) {
                    found = *it;
                    break;
                }
            }
        }

        if (!found.has_value()) {
            cwc.sendResponse(Api::TrainingResultGet::Response::error(ApiError(
                "TrainingResultGet not found: " + cwc.command.trainingSessionId.toString())));
            return;
        }

        Api::TrainingResultGet::Okay response;
        response.summary = found->summary;
        response.candidates = found->candidates;
        cwc.sendResponse(Api::TrainingResultGet::Response::okay(std::move(response)));
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
    service.registerHandler<Api::ClockEventTrigger::Cwc>(
        [this](Api::ClockEventTrigger::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::DiagramGet::Cwc>(
        [this](Api::DiagramGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::EvolutionStart::Cwc>(
        [this](Api::EvolutionStart::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::EvolutionStop::Cwc>(
        [this](Api::EvolutionStop::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::Exit::Cwc>([this](Api::Exit::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerDown::Cwc>(
        [this](Api::FingerDown::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerMove::Cwc>(
        [this](Api::FingerMove::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::FingerUp::Cwc>(
        [this](Api::FingerUp::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::GenomeDelete::Cwc>(
        [this](Api::GenomeDelete::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::GenomeGet::Cwc>(
        [this](Api::GenomeGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::GenomeList::Cwc>(
        [this](Api::GenomeList::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::GenomeSet::Cwc>(
        [this](Api::GenomeSet::Cwc cwc) { queueEvent(cwc); });
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
    service.registerHandler<Api::ScenarioSwitch::Cwc>(
        [this](Api::ScenarioSwitch::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SeedAdd::Cwc>([this](Api::SeedAdd::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SimRun::Cwc>([this](Api::SimRun::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SimStop::Cwc>([this](Api::SimStop::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::SpawnDirtBall::Cwc>(
        [this](Api::SpawnDirtBall::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TimerStatsGet::Cwc>(
        [this](Api::TimerStatsGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingResultDiscard::Cwc>(
        [this](Api::TrainingResultDiscard::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingResultSave::Cwc>(
        [this](Api::TrainingResultSave::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingResultSet::Cwc>(
        [this](Api::TrainingResultSet::Cwc cwc) { queueEvent(cwc); });
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

GamepadManager& StateMachine::getGamepadManager()
{
    // Lazy initialization if not yet created.
    if (!pImpl->gamepadManager_) {
        pImpl->gamepadManager_ = std::make_unique<GamepadManager>();
    }
    return *pImpl->gamepadManager_;
}

const GamepadManager& StateMachine::getGamepadManager() const
{
    // Note: const version assumes already initialized.
    assert(pImpl->gamepadManager_ && "GamepadManager accessed before initialization");
    return *pImpl->gamepadManager_;
}

GenomeRepository& StateMachine::getGenomeRepository()
{
    return pImpl->genomeRepository_;
}

const GenomeRepository& StateMachine::getGenomeRepository() const
{
    return pImpl->genomeRepository_;
}

void StateMachine::storeTrainingResult(const Api::TrainingResult& result)
{
    std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
    auto it = std::find_if(
        pImpl->trainingResults_.begin(),
        pImpl->trainingResults_.end(),
        [&result](const Api::TrainingResult& existing) {
            return existing.summary.trainingSessionId == result.summary.trainingSessionId;
        });

    if (it != pImpl->trainingResults_.end()) {
        *it = result;
        return;
    }

    pImpl->trainingResults_.push_back(result);
}

void StateMachine::mainLoopRun()
{
    // Initialize GamepadManager now that server is listening.
    // This avoids 1.5s SDL initialization delay blocking server startup.
    if (!pImpl->gamepadManager_) {
        pImpl->gamepadManager_ = std::make_unique<GamepadManager>();
    }

    spdlog::info("Starting main event loop");

    // Enter Startup state through the normal framework path.
    transitionTo(State::Startup{});

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

            // Log timing breakdown every 10 seconds.
            static int frameCount = 0;
            static double totalEventProcessMs = 0.0;
            static double totalTickMs = 0.0;
            static double totalSleepMs = 0.0;
            static double totalIterationMs = 0.0;
            static auto lastTimingLog = std::chrono::steady_clock::now();

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

                    // Break sleep into 5ms chunks to allow quick exit on signal.
                    constexpr int SLEEP_CHUNK_MS = 5;
                    while (remainingMs > 0 && !shouldExit()) {
                        int sleepNow = std::min(remainingMs, SLEEP_CHUNK_MS);
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleepNow));
                        remainingMs -= sleepNow;
                    }

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
            if (loopIterationEnd - lastTimingLog >= std::chrono::seconds(10)) {
                lastTimingLog = loopIterationEnd;
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
        else if (std::holds_alternative<State::Evolution>(pImpl->fsmState_.getVariant())) {
            // Tick evolution state (evaluates one organism per tick).
            auto& evolution = std::get<State::Evolution>(pImpl->fsmState_.getVariant());
            if (auto nextState = evolution.tick(*this)) {
                transitionTo(std::move(*nextState));
            }
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

    // Handle ScenarioListGet globally (read-only, works in any state).
    if (std::holds_alternative<Api::ScenarioListGet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::ScenarioListGet::Cwc>(event.getVariant());
        auto& registry = getScenarioRegistry();
        auto scenarioIds = registry.getScenarioIds();

        Api::ScenarioListGet::Okay response;
        response.scenarios.reserve(scenarioIds.size());

        for (const auto& id : scenarioIds) {
            const ScenarioMetadata* metadata = registry.getMetadata(id);
            if (metadata) {
                response.scenarios.push_back(
                    Api::ScenarioListGet::ScenarioInfo{ .id = id,
                                                        .name = metadata->name,
                                                        .description = metadata->description,
                                                        .category = metadata->category });
            }
        }

        LOG_DEBUG(State, "ScenarioListGet returning {} scenarios", response.scenarios.size());
        cwc.sendResponse(Api::ScenarioListGet::Response::okay(std::move(response)));
        return;
    }

    // Handle GenomeGet globally (read-only, works in any state).
    if (std::holds_alternative<Api::GenomeGet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::GenomeGet::Cwc>(event.getVariant());
        auto& repo = getGenomeRepository();

        Api::GenomeGet::Okay response;

        if (auto genome = repo.get(cwc.command.id)) {
            response.found = true;
            response.id = cwc.command.id;
            response.weights = genome->weights;

            if (auto meta = repo.getMetadata(cwc.command.id)) {
                response.metadata = *meta;
            }
        }
        else {
            response.found = false;
        }

        cwc.sendResponse(Api::GenomeGet::Response::okay(std::move(response)));
        return;
    }

    // Handle GenomeList globally (read-only, works in any state).
    if (std::holds_alternative<Api::GenomeList::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::GenomeList::Cwc>(event.getVariant());
        auto& repo = getGenomeRepository();

        Api::GenomeList::Okay response;
        for (const auto& [id, meta] : repo.list()) {
            response.genomes.push_back(Api::GenomeList::GenomeEntry{ .id = id, .metadata = meta });
        }

        cwc.sendResponse(Api::GenomeList::Response::okay(std::move(response)));
        return;
    }

    // Handle GenomeSet globally (works in any state).
    if (std::holds_alternative<Api::GenomeSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::GenomeSet::Cwc>(event.getVariant());
        auto& repo = getGenomeRepository();

        // Check if genome already exists.
        bool overwritten = repo.exists(cwc.command.id);

        // Build genome from weights.
        Genome genome;
        genome.weights = cwc.command.weights;

        // Use provided metadata or create default.
        GenomeMetadata meta = cwc.command.metadata.value_or(GenomeMetadata{
            .name = "imported_" + cwc.command.id.toShortString(),
            .fitness = 0.0,
            .generation = 0,
            .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
            .organismType = std::nullopt,
            .brainKind = std::nullopt,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        });

        repo.store(cwc.command.id, genome, meta);

        LOG_INFO(
            State,
            "GenomeSet: Stored genome {} ({} weights, overwritten={})",
            cwc.command.id.toShortString(),
            genome.weights.size(),
            overwritten);

        Api::GenomeSet::Okay response;
        response.success = true;
        response.overwritten = overwritten;
        cwc.sendResponse(Api::GenomeSet::Response::okay(std::move(response)));
        return;
    }

    // Handle GenomeDelete globally (works in any state).
    if (std::holds_alternative<Api::GenomeDelete::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::GenomeDelete::Cwc>(event.getVariant());
        auto& repo = getGenomeRepository();

        bool existed = repo.exists(cwc.command.id);
        if (existed) {
            repo.remove(cwc.command.id);
            LOG_INFO(State, "GenomeDelete: Deleted genome {}", cwc.command.id.toShortString());
        }
        else {
            LOG_INFO(State, "GenomeDelete: Genome {} not found", cwc.command.id.toShortString());
        }

        Api::GenomeDelete::Okay response;
        response.success = existed;
        cwc.sendResponse(Api::GenomeDelete::Response::okay(std::move(response)));
        return;
    }

    // Handle TrainingResultSet globally (works in any state).
    if (std::holds_alternative<Api::TrainingResultSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::TrainingResultSet::Cwc>(event.getVariant());
        const auto& result = cwc.command.result;

        if (result.summary.trainingSessionId.isNil()) {
            cwc.sendResponse(Api::TrainingResultSet::Response::error(
                ApiError("TrainingResultSet requires trainingSessionId")));
            return;
        }

        bool overwritten = false;
        bool rejected = false;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            auto it = std::find_if(
                pImpl->trainingResults_.begin(),
                pImpl->trainingResults_.end(),
                [&result](const Api::TrainingResult& existing) {
                    return existing.summary.trainingSessionId == result.summary.trainingSessionId;
                });

            if (it != pImpl->trainingResults_.end()) {
                if (!cwc.command.overwrite) {
                    rejected = true;
                }
                else {
                    *it = result;
                    overwritten = true;
                }
            }
            else {
                pImpl->trainingResults_.push_back(result);
            }
        }

        if (rejected) {
            cwc.sendResponse(Api::TrainingResultSet::Response::error(
                ApiError("TrainingResultSet already exists")));
            return;
        }

        Api::TrainingResultSet::Okay response;
        response.stored = true;
        response.overwritten = overwritten;
        cwc.sendResponse(Api::TrainingResultSet::Response::okay(std::move(response)));
        return;
    }

    // Handle RenderFormatSet globally (works in any state).
    if (std::holds_alternative<Api::RenderFormatSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::RenderFormatSet::Cwc>(event.getVariant());
        const std::string& connectionId = cwc.command.connectionId;
        assert(!connectionId.empty() && "RenderFormatSet: connectionId must be populated!");

        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsRender(connectionId)) {
            cwc.sendResponse(Api::RenderFormatSet::Response::error(
                ApiError{ "Client did not request render updates" }));
            return;
        }

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
            cwc.command.format == RenderFormat::EnumType::Basic ? "Basic" : "Debug",
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

    invokeOnExit(pImpl->fsmState_, *this);

    auto expectedIndex = newState.getVariant().index();
    pImpl->fsmState_ = std::move(newState);

    std::string newStateName = getCurrentStateName();
    LOG_INFO(State, "Server::StateMachine: {} -> {}", oldStateName, newStateName);

    pImpl->fsmState_ = invokeOnEnter(std::move(pImpl->fsmState_), *this);

    // Chain transition if onEnter redirected to a different state.
    if (pImpl->fsmState_.getVariant().index() != expectedIndex) {
        transitionTo(std::move(pImpl->fsmState_));
    }
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
    const WorldData& data,
    const std::vector<OrganismId>& organism_grid,
    Scenario::EnumType scenario_id,
    const ScenarioConfig& scenario_config)
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
        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsRender(client.connectionId)) {
            continue;
        }

        RenderMessage msg =
            RenderMessageUtils::packRenderMessage(data, client.renderFormat, organism_grid);

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

void StateMachine::broadcastCommand(const std::string& messageType)
{
    broadcastEventData(messageType, {});
}

void StateMachine::broadcastEventData(
    const std::string& messageType, const std::vector<std::byte>& payload)
{
    if (pImpl->subscribedClients_.empty()) {
        return;
    }

    spdlog::debug(
        "StateMachine: Broadcasting '{}' ({} bytes) to {} clients",
        messageType,
        payload.size(),
        pImpl->subscribedClients_.size());

    Network::MessageEnvelope envelope{
        .id = 0,
        .message_type = messageType,
        .payload = payload,
    };

    std::vector<std::byte> envelopeData = Network::serialize_envelope(envelope);

    for (const auto& client : pImpl->subscribedClients_) {
        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsEvents(client.connectionId)) {
            continue;
        }

        auto result = pImpl->wsService_->sendToClient(client.connectionId, envelopeData);
        if (result.isError()) {
            spdlog::error(
                "StateMachine: Failed to send '{}' to '{}': {}",
                messageType,
                client.connectionId,
                result.errorValue());
        }
    }
}

} // namespace Server
} // namespace DirtSim
