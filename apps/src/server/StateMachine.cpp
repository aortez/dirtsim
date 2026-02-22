#include "StateMachine.h"
#include "Event.h"
#include "EventProcessor.h"
#include "ServerConfig.h"
#include "TrainingResultRepository.h"
#include "UserSettings.h"
#include "api/TrainingBestSnapshotGet.h"
#include "api/TrainingResult.h"
#include "api/TrainingResultDelete.h"
#include "api/TrainingResultGet.h"
#include "api/TrainingResultList.h"
#include "api/TrainingResultSet.h"
#include "api/TrainingStreamConfigSet.h"
#include "api/UserSettingsGet.h"
#include "api/UserSettingsPatch.h"
#include "api/UserSettingsReset.h"
#include "api/UserSettingsSet.h"
#include "api/UserSettingsUpdated.h"
#include "api/WebSocketAccessSet.h"
#include "api/WebUiAccessSet.h"
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
#include "core/network/JsonProtocol.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "network/CommandDeserializerJson.h"
#include "network/HttpServer.h"
#include "states/State.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <unistd.h>

namespace DirtSim {
namespace Server {

// =================================================================
// PIMPL IMPLEMENTATION STRUCT
// =================================================================

struct SubscribedClient {
    std::string connectionId;
    RenderFormat::EnumType renderFormat;
    bool renderEnabled = true;
    int renderEveryN = 1;
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

int getMaxTimezoneIndex()
{
    return static_cast<int>(ClockScenario::TIMEZONES.size()) - 1;
}

constexpr int kStartMenuIdleTimeoutMinMs = 5000;
constexpr int kStartMenuIdleTimeoutMaxMs = 3600000;
constexpr int kGenomeArchiveMaxSizePerBucketMax = 1000;

bool isNesTrainingTarget(const TrainingSpec& spec)
{
    if (spec.organismType == OrganismType::NES_FLAPPY_BIRD
        || spec.scenarioId == Scenario::EnumType::NesFlappyParatroopa) {
        return true;
    }

    for (const auto& population : spec.population) {
        if (population.brainKind == TrainingBrainKind::NesFlappyBird) {
            return true;
        }
    }

    return false;
}

template <typename RecordUpdateFn>
void canonicalizeNesTrainingTarget(UserSettings& settings, RecordUpdateFn&& recordUpdate)
{
    if (!isNesTrainingTarget(settings.trainingSpec)) {
        return;
    }

    if (settings.trainingSpec.organismType != OrganismType::NES_FLAPPY_BIRD) {
        settings.trainingSpec.organismType = OrganismType::NES_FLAPPY_BIRD;
        recordUpdate("trainingSpec.organismType promoted to NES_FLAPPY_BIRD for NES training");
    }

    if (settings.trainingSpec.scenarioId != Scenario::EnumType::NesFlappyParatroopa) {
        settings.trainingSpec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
        recordUpdate("trainingSpec.scenarioId forced to NesFlappyParatroopa for NES training");
    }

    for (size_t index = 0; index < settings.trainingSpec.population.size(); ++index) {
        auto& population = settings.trainingSpec.population[index];
        if (population.brainKind != TrainingBrainKind::NesFlappyBird
            || population.brainVariant.has_value()) {
            population.brainKind = TrainingBrainKind::NesFlappyBird;
            population.brainVariant.reset();
            population.seedGenomes.clear();
            population.randomCount = population.count;
            recordUpdate(
                "trainingSpec population[" + std::to_string(index)
                + "] brainKind migrated to NesFlappyBird");
        }
    }
}

std::filesystem::path getUserSettingsPath(const std::filesystem::path& dataDir)
{
    return dataDir / "user_settings.json";
}

std::filesystem::path getUserSettingsTempPath(const std::filesystem::path& filePath)
{
    const auto pid = static_cast<long long>(::getpid());
    const auto now =
        static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());

    std::filesystem::path tempPath = filePath;
    tempPath += ".tmp.";
    tempPath += std::to_string(pid);
    tempPath += ".";
    tempPath += std::to_string(now);
    return tempPath;
}

bool persistUserSettingsToDisk(
    const std::filesystem::path& filePath, const UserSettings& userSettings)
{
    std::error_code dirErr;
    std::filesystem::create_directories(filePath.parent_path(), dirErr);
    if (dirErr) {
        LOG_ERROR(
            State,
            "Failed to create user settings directory '{}': {}",
            filePath.parent_path().string(),
            dirErr.message());
        return false;
    }

    const std::filesystem::path tempPath = getUserSettingsTempPath(filePath);
    std::ofstream file(tempPath);
    if (!file.is_open()) {
        LOG_ERROR(State, "Failed to open '{}' for writing user settings", tempPath.string());
        return false;
    }

    nlohmann::json json = userSettings;
    file << json.dump(2) << "\n";
    file.flush();
    if (!file.good() || file.fail()) {
        LOG_ERROR(State, "Failed to write user settings to '{}'", tempPath.string());
        std::error_code removeErr;
        std::filesystem::remove(tempPath, removeErr);
        return false;
    }

    file.close();
    if (file.fail()) {
        LOG_ERROR(State, "Failed to close user settings file '{}'", tempPath.string());
        std::error_code removeErr;
        std::filesystem::remove(tempPath, removeErr);
        return false;
    }

    if (::rename(tempPath.c_str(), filePath.c_str()) != 0) {
        const int err = errno;
        LOG_ERROR(
            State,
            "Failed to replace user settings file '{}' via rename '{}': {}",
            filePath.string(),
            tempPath.string(),
            std::strerror(err));
        std::error_code removeErr;
        std::filesystem::remove(tempPath, removeErr);
        return false;
    }

    return true;
}

UserSettings sanitizeUserSettings(
    const UserSettings& input,
    const ScenarioRegistry& registry,
    const GenomeRepository& genomeRepository,
    bool& changed,
    std::vector<std::string>& updates)
{
    UserSettings settings = input;
    changed = false;

    const auto recordUpdate = [&](const std::string& message) {
        updates.push_back(message);
        changed = true;
    };

    if (settings.timezoneIndex < 0) {
        settings.timezoneIndex = 0;
        recordUpdate("timezoneIndex clamped to 0");
    }
    else if (settings.timezoneIndex > getMaxTimezoneIndex()) {
        settings.timezoneIndex = getMaxTimezoneIndex();
        recordUpdate("timezoneIndex clamped to maximum timezone");
    }

    if (settings.volumePercent < 0) {
        settings.volumePercent = 0;
        recordUpdate("volumePercent clamped to 0");
    }
    else if (settings.volumePercent > 100) {
        settings.volumePercent = 100;
        recordUpdate("volumePercent clamped to 100");
    }

    if (!registry.getMetadata(settings.defaultScenario)) {
        settings.defaultScenario = UserSettings{}.defaultScenario;
        recordUpdate("defaultScenario reset to fallback scenario");
    }

    if (settings.startMenuIdleAction > StartMenuIdleAction::TrainingSession) {
        settings.startMenuIdleAction = StartMenuIdleAction::ClockScenario;
        recordUpdate("startMenuIdleAction reset to ClockScenario");
    }

    if (settings.startMenuIdleTimeoutMs < kStartMenuIdleTimeoutMinMs) {
        settings.startMenuIdleTimeoutMs = kStartMenuIdleTimeoutMinMs;
        recordUpdate("startMenuIdleTimeoutMs clamped to minimum timeout");
    }
    else if (settings.startMenuIdleTimeoutMs > kStartMenuIdleTimeoutMaxMs) {
        settings.startMenuIdleTimeoutMs = kStartMenuIdleTimeoutMaxMs;
        recordUpdate("startMenuIdleTimeoutMs clamped to maximum timeout");
    }

    if (settings.trainingResumePolicy > TrainingResumePolicy::WarmFromBest) {
        settings.trainingResumePolicy = TrainingResumePolicy::WarmFromBest;
        recordUpdate("trainingResumePolicy reset to WarmFromBest");
    }

    if (settings.evolutionConfig.targetCpuPercent < 0) {
        settings.evolutionConfig.targetCpuPercent = 0;
        recordUpdate("targetCpuPercent clamped to 0");
    }
    else if (settings.evolutionConfig.targetCpuPercent > 100) {
        settings.evolutionConfig.targetCpuPercent = 100;
        recordUpdate("targetCpuPercent clamped to 100");
    }

    if (settings.evolutionConfig.genomeArchiveMaxSize < 0) {
        settings.evolutionConfig.genomeArchiveMaxSize = 0;
        recordUpdate("genomeArchiveMaxSize clamped to 0");
    }
    else if (settings.evolutionConfig.genomeArchiveMaxSize > kGenomeArchiveMaxSizePerBucketMax) {
        settings.evolutionConfig.genomeArchiveMaxSize = kGenomeArchiveMaxSizePerBucketMax;
        recordUpdate("genomeArchiveMaxSize clamped to 1000");
    }
    if (settings.evolutionConfig.robustFitnessEvaluationCount < 1) {
        settings.evolutionConfig.robustFitnessEvaluationCount = 1;
        recordUpdate("robustFitnessEvaluationCount clamped to 1");
    }
    if (settings.evolutionConfig.warmStartSeedCount < 0) {
        settings.evolutionConfig.warmStartSeedCount = 0;
        recordUpdate("warmStartSeedCount clamped to 0");
    }
    if (settings.evolutionConfig.warmStartSeedPercent < 0.0) {
        settings.evolutionConfig.warmStartSeedPercent = 0.0;
        recordUpdate("warmStartSeedPercent clamped to 0");
    }
    else if (settings.evolutionConfig.warmStartSeedPercent > 100.0) {
        settings.evolutionConfig.warmStartSeedPercent = 100.0;
        recordUpdate("warmStartSeedPercent clamped to 100");
    }
    if (settings.evolutionConfig.warmStartMinRobustEvalCount < 1) {
        settings.evolutionConfig.warmStartMinRobustEvalCount = 1;
        recordUpdate("warmStartMinRobustEvalCount clamped to 1");
    }
    if (settings.evolutionConfig.warmStartNoveltyWeight < 0.0) {
        settings.evolutionConfig.warmStartNoveltyWeight = 0.0;
        recordUpdate("warmStartNoveltyWeight clamped to 0");
    }
    else if (settings.evolutionConfig.warmStartNoveltyWeight > 1.0) {
        settings.evolutionConfig.warmStartNoveltyWeight = 1.0;
        recordUpdate("warmStartNoveltyWeight clamped to 1");
    }
    if (settings.evolutionConfig.warmStartFitnessFloorPercentile < 0.0) {
        settings.evolutionConfig.warmStartFitnessFloorPercentile = 0.0;
        recordUpdate("warmStartFitnessFloorPercentile clamped to 0");
    }
    else if (settings.evolutionConfig.warmStartFitnessFloorPercentile > 100.0) {
        settings.evolutionConfig.warmStartFitnessFloorPercentile = 100.0;
        recordUpdate("warmStartFitnessFloorPercentile clamped to 100");
    }
    if (settings.evolutionConfig.diversityEliteCount < 0) {
        settings.evolutionConfig.diversityEliteCount = 0;
        recordUpdate("diversityEliteCount clamped to 0");
    }
    if (settings.evolutionConfig.diversityEliteFitnessEpsilon < 0.0) {
        settings.evolutionConfig.diversityEliteFitnessEpsilon = 0.0;
        recordUpdate("diversityEliteFitnessEpsilon clamped to 0");
    }

    canonicalizeNesTrainingTarget(settings, recordUpdate);

    for (size_t index = 0; index < settings.trainingSpec.population.size(); ++index) {
        auto& population = settings.trainingSpec.population[index];
        const int originalSeedCount = static_cast<int>(population.seedGenomes.size());
        population.seedGenomes.erase(
            std::remove_if(
                population.seedGenomes.begin(),
                population.seedGenomes.end(),
                [&](const GenomeId& genomeId) {
                    return genomeId.isNil() || !genomeRepository.exists(genomeId);
                }),
            population.seedGenomes.end());

        const int removedSeedCount =
            originalSeedCount - static_cast<int>(population.seedGenomes.size());
        if (removedSeedCount <= 0) {
            continue;
        }

        population.randomCount += removedSeedCount;
        recordUpdate(
            "trainingSpec population[" + std::to_string(index) + "] removed "
            + std::to_string(removedSeedCount) + " missing seed genome(s)");
    }

    return settings;
}

UserSettings loadUserSettingsFromDisk(
    const std::filesystem::path& filePath,
    const ScenarioRegistry& registry,
    const GenomeRepository& genomeRepository)
{
    const UserSettings defaults;

    std::error_code createErr;
    std::filesystem::create_directories(filePath.parent_path(), createErr);
    if (createErr) {
        LOG_WARN(
            State,
            "Failed to create user settings directory '{}': {}",
            filePath.parent_path().string(),
            createErr.message());
    }

    if (!std::filesystem::exists(filePath)) {
        LOG_INFO(State, "User settings file missing, writing defaults to '{}'", filePath.string());
        persistUserSettingsToDisk(filePath, defaults);
        return defaults;
    }

    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_WARN(
                State,
                "Failed to open user settings file '{}', restoring defaults",
                filePath.string());
            persistUserSettingsToDisk(filePath, defaults);
            return defaults;
        }

        nlohmann::json json;
        file >> json;
        UserSettings parsed = json.get<UserSettings>();

        bool changed = false;
        std::vector<std::string> updates;
        UserSettings sanitized =
            sanitizeUserSettings(parsed, registry, genomeRepository, changed, updates);
        if (changed) {
            for (const auto& update : updates) {
                LOG_WARN(State, "User settings validation: {}", update);
            }
            persistUserSettingsToDisk(filePath, sanitized);
        }

        return sanitized;
    }
    catch (const std::exception& e) {
        LOG_WARN(
            State,
            "Failed to parse user settings '{}': {}. Restoring defaults.",
            filePath.string(),
            e.what());
        persistUserSettingsToDisk(filePath, defaults);
        return defaults;
    }
}

bool isMissingTimestamp(uint64_t timestamp)
{
    return timestamp == 0;
}

bool compareGenomeListEntries(
    const Api::GenomeList::GenomeEntry& left,
    const Api::GenomeList::GenomeEntry& right,
    GenomeSortKey sortKey,
    GenomeSortDirection sortDirection)
{
    const auto leftId = left.id.toString();
    const auto rightId = right.id.toString();
    const auto idLess = leftId < rightId;

    const auto compareValue = [&](const auto& leftValue, const auto& rightValue) {
        if (leftValue == rightValue) {
            return idLess;
        }
        if (sortDirection == GenomeSortDirection::Asc) {
            return leftValue < rightValue;
        }
        return leftValue > rightValue;
    };

    switch (sortKey) {
        case GenomeSortKey::CreatedTimestamp: {
            const bool leftMissing = isMissingTimestamp(left.metadata.createdTimestamp);
            const bool rightMissing = isMissingTimestamp(right.metadata.createdTimestamp);
            if (leftMissing != rightMissing) {
                return !leftMissing;
            }
            return compareValue(left.metadata.createdTimestamp, right.metadata.createdTimestamp);
        }
        case GenomeSortKey::Fitness:
            return compareValue(left.metadata.fitness, right.metadata.fitness);
        case GenomeSortKey::Generation:
            return compareValue(left.metadata.generation, right.metadata.generation);
    }

    return idLess;
}

void sortGenomeListEntries(
    std::vector<Api::GenomeList::GenomeEntry>& entries,
    GenomeSortKey sortKey,
    GenomeSortDirection sortDirection)
{
    std::sort(entries.begin(), entries.end(), [&](const auto& left, const auto& right) {
        return compareGenomeListEntries(left, right, sortKey, sortDirection);
    });
}

} // namespace

struct StateMachine::Impl {
    EventProcessor eventProcessor_;
    std::filesystem::path dataDir_;
    std::unique_ptr<GamepadManager> gamepadManager_;
    GenomeRepository genomeRepository_;
    TrainingResultRepository trainingResultRepository_;
    ScenarioRegistry scenarioRegistry_;
    std::filesystem::path userSettingsPath_;
    UserSettings userSettings_;
    SystemMetrics systemMetrics_;
    Timers timers_;
    std::unique_ptr<HttpServer> httpServer_;
    State::Any fsmState_{ State::PreStartup{} };
    std::unique_ptr<Network::WebSocketServiceInterface> wsServiceOwned_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    uint16_t webSocketPort_ = 8080;
    uint16_t httpPort_ = 8081;
    std::shared_ptr<WorldData> cachedWorldData_;
    mutable std::mutex cachedWorldDataMutex_;
    std::optional<Api::TrainingBestSnapshot> cachedTrainingBestSnapshot_;
    mutable std::mutex cachedTrainingBestSnapshotMutex_;

    std::vector<SubscribedClient> subscribedClients_;
    std::vector<std::string> eventSubscribers_;
    mutable std::mutex trainingResultsMutex_;
    std::vector<std::byte> renderEnvelopeDataScratch_;
    Network::MessageEnvelope renderEnvelopeScratch_;

    explicit Impl(const std::optional<std::filesystem::path>& dataDir)
        : dataDir_(dataDir.value_or(getDefaultDataDir())),
          genomeRepository_(initGenomeRepository(dataDir_)),
          trainingResultRepository_(initTrainingResultRepository(dataDir_)),
          scenarioRegistry_(ScenarioRegistry::createDefault(genomeRepository_)),
          userSettingsPath_(getUserSettingsPath(dataDir_)),
          userSettings_(
              loadUserSettingsFromDisk(userSettingsPath_, scenarioRegistry_, genomeRepository_))
    {
        renderEnvelopeScratch_.id = 0;
        renderEnvelopeScratch_.message_type = "RenderMessage";

        if (userSettings_.evolutionConfig.genomeArchiveMaxSize > 0) {
            const size_t pruned = genomeRepository_.pruneManagedByFitness(
                static_cast<size_t>(userSettings_.evolutionConfig.genomeArchiveMaxSize));
            if (pruned > 0) {
                LOG_INFO(
                    State,
                    "Pruned {} managed genomes on startup (max_per_organism_brain={})",
                    pruned,
                    userSettings_.evolutionConfig.genomeArchiveMaxSize);
            }
        }
        LOG_INFO(State, "User settings file: {}", userSettingsPath_.string());
    }

private:
    static GenomeRepository initGenomeRepository(const std::filesystem::path& dataDir)
    {
        std::filesystem::create_directories(dataDir);
        auto dbPath = dataDir / "genomes.db";
        spdlog::info("GenomeRepository: Using database at {}", dbPath.string());
        return GenomeRepository(dbPath);
    }

    static TrainingResultRepository initTrainingResultRepository(
        const std::filesystem::path& dataDir)
    {
        std::filesystem::create_directories(dataDir);
        auto dbPath = dataDir / "training_results.db";
        spdlog::info("TrainingResultRepository: Using database at {}", dbPath.string());
        return TrainingResultRepository(dbPath);
    }
};

StateMachine::StateMachine(
    std::unique_ptr<Network::WebSocketServiceInterface> webSocketService,
    const std::optional<std::filesystem::path>& dataDir)
    : pImpl(dataDir)
{
    serverConfig = std::make_unique<ServerConfig>();
    serverConfig->dataDir = dataDir;

    pImpl->httpServer_ = std::make_unique<HttpServer>(pImpl->httpPort_);

    if (webSocketService) {
        pImpl->wsServiceOwned_ = std::move(webSocketService);
        pImpl->wsService_ = pImpl->wsServiceOwned_.get();
    }

    LOG_INFO(
        State,
        "Server::StateMachine initialized in headless mode in state: {}",
        getCurrentStateName());
}

StateMachine::StateMachine(const std::optional<std::filesystem::path>& dataDir)
    : StateMachine(nullptr, dataDir)
{}

StateMachine::~StateMachine()
{
    if (pImpl->httpServer_) {
        pImpl->httpServer_->stop();
    }
    LOG_INFO(State, "Server::StateMachine shutting down from state: {}", getCurrentStateName());
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

Network::WebSocketServiceInterface* StateMachine::getWebSocketService()
{
    return pImpl->wsService_;
}

void StateMachine::setWebSocketService(Network::WebSocketServiceInterface* service)
{
    pImpl->wsService_ = service;
}

void StateMachine::setWebSocketPort(uint16_t port)
{
    pImpl->webSocketPort_ = port;
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
        auto evtIt = std::remove(
            pImpl->eventSubscribers_.begin(), pImpl->eventSubscribers_.end(), connectionId);
        if (evtIt != pImpl->eventSubscribers_.end()) {
            pImpl->eventSubscribers_.erase(evtIt, pImpl->eventSubscribers_.end());
            spdlog::info(
                "StateMachine: Client '{}' disconnected, removed from event subscribers "
                "(remaining={})",
                connectionId,
                pImpl->eventSubscribers_.size());
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
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
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
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }
        DISPATCH_JSON_CMD_WITH_RESP(Api::CellGet);
        DISPATCH_JSON_CMD_EMPTY(Api::CellSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::DiagramGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::EventSubscribe);
        DISPATCH_JSON_CMD_EMPTY(Api::Exit);
        DISPATCH_JSON_CMD_EMPTY(Api::GravitySet);
        DISPATCH_JSON_CMD_EMPTY(Api::NesInputSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::PerfStatsGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::PhysicsSettingsGet);
        DISPATCH_JSON_CMD_EMPTY(Api::PhysicsSettingsSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::RenderFormatGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::RenderFormatSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::RenderStreamConfigSet);
        DISPATCH_JSON_CMD_EMPTY(Api::Reset);
        DISPATCH_JSON_CMD_WITH_RESP(Api::ScenarioConfigSet);
        DISPATCH_JSON_CMD_EMPTY(Api::SeedAdd);
        DISPATCH_JSON_CMD_WITH_RESP(Api::SimRun);
        DISPATCH_JSON_CMD_EMPTY(Api::SimStop);
        DISPATCH_JSON_CMD_EMPTY(Api::SpawnDirtBall);
        DISPATCH_JSON_CMD_WITH_RESP(Api::StateGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::StatusGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::TrainingBestSnapshotGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::TimerStatsGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::UserSettingsGet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::UserSettingsPatch);
        DISPATCH_JSON_CMD_WITH_RESP(Api::UserSettingsReset);
        DISPATCH_JSON_CMD_WITH_RESP(Api::UserSettingsSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::WebSocketAccessSet);
        DISPATCH_JSON_CMD_WITH_RESP(Api::WebUiAccessSet);
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

    service.registerHandler<Api::TrainingBestSnapshotGet::Cwc>(
        [this](Api::TrainingBestSnapshotGet::Cwc cwc) {
            Api::TrainingBestSnapshotGet::Okay response;
            const auto snapshot = getCachedTrainingBestSnapshot();
            if (snapshot.has_value()) {
                response.hasSnapshot = true;
                response.snapshot = *snapshot;
            }
            cwc.sendResponse(Api::TrainingBestSnapshotGet::Response::okay(std::move(response)));
        });

    service.registerHandler<Api::WebSocketAccessSet::Cwc>([this](Api::WebSocketAccessSet::Cwc cwc) {
        using Response = Api::WebSocketAccessSet::Response;

        if (!pImpl->wsService_) {
            cwc.sendResponse(Response::error(ApiError("WebSocket service not available")));
            return;
        }

        if (pImpl->webSocketPort_ == 0) {
            cwc.sendResponse(Response::error(ApiError("WebSocket port not set")));
            return;
        }

        Api::WebSocketAccessSet::Okay okay;
        okay.enabled = cwc.command.enabled;
        cwc.sendResponse(Response::okay(std::move(okay)));

        const std::string bindAddress = cwc.command.enabled ? "0.0.0.0" : "127.0.0.1";
        if (cwc.command.enabled) {
            pImpl->wsService_->setAccessToken(cwc.command.token);
        }
        else {
            pImpl->wsService_->clearAccessToken();
            pImpl->wsService_->closeNonLocalClients();
        }

        pImpl->wsService_->stopListening(false);
        auto listenResult = pImpl->wsService_->listen(pImpl->webSocketPort_, bindAddress);
        if (listenResult.isError()) {
            LOG_ERROR(
                Network,
                "WebSocketAccessSet failed to bind {}:{}: {}",
                bindAddress,
                pImpl->webSocketPort_,
                listenResult.errorValue());
            return;
        }
    });

    service.registerHandler<Api::WebUiAccessSet::Cwc>([this](Api::WebUiAccessSet::Cwc cwc) {
        using Response = Api::WebUiAccessSet::Response;

        Api::WebUiAccessSet::Okay okay;
        okay.enabled = cwc.command.enabled;
        cwc.sendResponse(Response::okay(std::move(okay)));

        if (!pImpl->httpServer_) {
            return;
        }

        if (cwc.command.enabled) {
            const bool started = pImpl->httpServer_->start("0.0.0.0");
            if (!started) {
                LOG_ERROR(Network, "Failed to start HTTP server for /garden");
            }
            return;
        }

        pImpl->httpServer_->stop();
    });

    // RenderFormatGet - return default format (TODO: track per-client).
    service.registerHandler<Api::RenderFormatGet::Cwc>([](Api::RenderFormatGet::Cwc cwc) {
        Api::RenderFormatGet::Okay okay;
        okay.active_format = RenderFormat::EnumType::Basic; // Default for now.
        cwc.sendResponse(Api::RenderFormatGet::Response::okay(std::move(okay)));
    });

    service.registerHandler<Api::TrainingResultList::Cwc>([this](Api::TrainingResultList::Cwc cwc) {
        Result<std::vector<Api::TrainingResultList::Entry>, std::string> listResult;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            listResult = pImpl->trainingResultRepository_.list();
        }
        if (listResult.isError()) {
            cwc.sendResponse(
                Api::TrainingResultList::Response::error(ApiError(listResult.errorValue())));
            return;
        }

        Api::TrainingResultList::Okay response;
        response.results = std::move(listResult).value();
        cwc.sendResponse(Api::TrainingResultList::Response::okay(std::move(response)));
    });

    service.registerHandler<Api::TrainingResultGet::Cwc>([this](Api::TrainingResultGet::Cwc cwc) {
        Result<std::optional<Api::TrainingResult>, std::string> getResult;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            getResult = pImpl->trainingResultRepository_.get(cwc.command.trainingSessionId);
        }

        if (getResult.isError()) {
            cwc.sendResponse(
                Api::TrainingResultGet::Response::error(ApiError(getResult.errorValue())));
            return;
        }
        auto found = std::move(getResult).value();
        if (!found.has_value()) {
            cwc.sendResponse(
                Api::TrainingResultGet::Response::error(ApiError(
                    "TrainingResultGet not found: " + cwc.command.trainingSessionId.toString())));
            return;
        }

        Api::TrainingResultGet::Okay response;
        response.summary = found->summary;
        response.candidates = found->candidates;
        cwc.sendResponse(Api::TrainingResultGet::Response::okay(std::move(response)));
    });

    service.registerHandler<Api::EventSubscribe::Cwc>(
        [this](Api::EventSubscribe::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::RenderFormatSet::Cwc>(
        [this](Api::RenderFormatSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::RenderStreamConfigSet::Cwc>(
        [this](Api::RenderStreamConfigSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingStreamConfigSet::Cwc>(
        [this](Api::TrainingStreamConfigSet::Cwc cwc) { queueEvent(cwc); });

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
    service.registerHandler<Api::NesInputSet::Cwc>(
        [this](Api::NesInputSet::Cwc cwc) { queueEvent(cwc); });
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
    service.registerHandler<Api::UserSettingsGet::Cwc>(
        [this](Api::UserSettingsGet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::UserSettingsPatch::Cwc>(
        [this](Api::UserSettingsPatch::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::UserSettingsReset::Cwc>(
        [this](Api::UserSettingsReset::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::UserSettingsSet::Cwc>(
        [this](Api::UserSettingsSet::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingResultDiscard::Cwc>(
        [this](Api::TrainingResultDiscard::Cwc cwc) { queueEvent(cwc); });
    service.registerHandler<Api::TrainingResultDelete::Cwc>(
        [this](Api::TrainingResultDelete::Cwc cwc) { queueEvent(cwc); });
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

void StateMachine::updateCachedTrainingBestSnapshot(const Api::TrainingBestSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(pImpl->cachedTrainingBestSnapshotMutex_);
    pImpl->cachedTrainingBestSnapshot_ = snapshot;
}

std::optional<Api::TrainingBestSnapshot> StateMachine::getCachedTrainingBestSnapshot() const
{
    std::lock_guard<std::mutex> lock(pImpl->cachedTrainingBestSnapshotMutex_);
    return pImpl->cachedTrainingBestSnapshot_;
}

void StateMachine::clearCachedTrainingBestSnapshot()
{
    std::lock_guard<std::mutex> lock(pImpl->cachedTrainingBestSnapshotMutex_);
    pImpl->cachedTrainingBestSnapshot_.reset();
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

UserSettings& StateMachine::getUserSettings()
{
    return pImpl->userSettings_;
}

const UserSettings& StateMachine::getUserSettings() const
{
    return pImpl->userSettings_;
}

void StateMachine::storeTrainingResult(const Api::TrainingResult& result)
{
    std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
    auto storeResult = pImpl->trainingResultRepository_.store(result);
    if (storeResult.isError()) {
        LOG_ERROR(State, "TrainingResultRepository store failed: {}", storeResult.errorValue());
    }
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

    if (!std::holds_alternative<State::Shutdown>(pImpl->fsmState_.getVariant())) {
        LOG_INFO(
            State,
            "Exit requested while in state {}, transitioning to Shutdown for cleanup",
            getCurrentStateName());
        transitionTo(State::Shutdown{});
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

        sortGenomeListEntries(response.genomes, cwc.command.sortKey, cwc.command.sortDirection);

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
        GenomeMetadata meta = cwc.command.metadata.value_or(
            GenomeMetadata{
                .name = "imported_" + cwc.command.id.toShortString(),
                .fitness = 0.0,
                .robustFitness = 0.0,
                .robustEvalCount = 1,
                .robustFitnessSamples = { 0.0 },
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

    // Handle TrainingResultDelete globally (works in any state).
    if (std::holds_alternative<Api::TrainingResultDelete::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::TrainingResultDelete::Cwc>(event.getVariant());
        Result<bool, std::string> deleteResult;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            deleteResult = pImpl->trainingResultRepository_.remove(cwc.command.trainingSessionId);
        }
        if (deleteResult.isError()) {
            cwc.sendResponse(
                Api::TrainingResultDelete::Response::error(ApiError(deleteResult.errorValue())));
            return;
        }

        Api::TrainingResultDelete::Okay response;
        response.success = deleteResult.value();
        cwc.sendResponse(Api::TrainingResultDelete::Response::okay(std::move(response)));
        return;
    }

    // Handle TrainingResultSet globally (works in any state).
    if (std::holds_alternative<Api::TrainingResultSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::TrainingResultSet::Cwc>(event.getVariant());
        const auto& result = cwc.command.result;

        if (result.summary.trainingSessionId.isNil()) {
            cwc.sendResponse(
                Api::TrainingResultSet::Response::error(
                    ApiError("TrainingResultSet requires trainingSessionId")));
            return;
        }

        bool overwritten = false;
        bool rejected = false;
        std::string failure;
        {
            std::lock_guard<std::mutex> lock(pImpl->trainingResultsMutex_);
            auto existsResult =
                pImpl->trainingResultRepository_.exists(result.summary.trainingSessionId);
            if (existsResult.isError()) {
                failure = existsResult.errorValue();
            }
            else if (existsResult.value()) {
                if (!cwc.command.overwrite) {
                    rejected = true;
                }
                else {
                    overwritten = true;
                }
            }

            if (!rejected && failure.empty()) {
                auto storeResult = pImpl->trainingResultRepository_.store(result);
                if (storeResult.isError()) {
                    failure = storeResult.errorValue();
                }
            }
        }

        if (!failure.empty()) {
            cwc.sendResponse(Api::TrainingResultSet::Response::error(ApiError(failure)));
            return;
        }
        if (rejected) {
            cwc.sendResponse(
                Api::TrainingResultSet::Response::error(
                    ApiError("TrainingResultSet already exists")));
            return;
        }

        Api::TrainingResultSet::Okay response;
        response.stored = true;
        response.overwritten = overwritten;
        cwc.sendResponse(Api::TrainingResultSet::Response::okay(std::move(response)));
        return;
    }

    // Handle EventSubscribe globally (works in any state).
    if (std::holds_alternative<Api::EventSubscribe::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::EventSubscribe::Cwc>(event.getVariant());
        const std::string& connectionId = cwc.command.connectionId;
        assert(!connectionId.empty() && "EventSubscribe: connectionId must be populated!");

        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsEvents(connectionId)) {
            cwc.sendResponse(
                Api::EventSubscribe::Response::error(
                    ApiError{ "Client did not request event updates" }));
            return;
        }

        if (cwc.command.enabled) {
            auto it = std::find(
                pImpl->eventSubscribers_.begin(), pImpl->eventSubscribers_.end(), connectionId);
            if (it == pImpl->eventSubscribers_.end()) {
                pImpl->eventSubscribers_.push_back(connectionId);
            }
        }
        else {
            auto it = std::remove(
                pImpl->eventSubscribers_.begin(), pImpl->eventSubscribers_.end(), connectionId);
            pImpl->eventSubscribers_.erase(it, pImpl->eventSubscribers_.end());
        }

        Api::EventSubscribe::Okay okay;
        okay.subscribed = cwc.command.enabled;
        okay.message =
            cwc.command.enabled ? "Subscribed to event stream" : "Unsubscribed from event stream";
        cwc.sendResponse(Api::EventSubscribe::Response::okay(std::move(okay)));
        return;
    }

    // Handle RenderFormatSet globally (works in any state).
    if (std::holds_alternative<Api::RenderFormatSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::RenderFormatSet::Cwc>(event.getVariant());
        const std::string& connectionId = cwc.command.connectionId;
        assert(!connectionId.empty() && "RenderFormatSet: connectionId must be populated!");

        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsRender(connectionId)) {
            cwc.sendResponse(
                Api::RenderFormatSet::Response::error(
                    ApiError{ "Client did not request render updates" }));
            return;
        }

        // Add or update client subscription.
        auto it = std::find_if(
            pImpl->subscribedClients_.begin(),
            pImpl->subscribedClients_.end(),
            [&connectionId](const SubscribedClient& c) { return c.connectionId == connectionId; });

        bool renderEnabled = true;
        int renderEveryN = 1;
        if (it != pImpl->subscribedClients_.end()) {
            it->renderFormat = cwc.command.format;
            renderEnabled = it->renderEnabled;
            renderEveryN = it->renderEveryN;
        }
        else {
            pImpl->subscribedClients_.push_back({ connectionId, cwc.command.format, true, 1 });
        }

        spdlog::info(
            "StateMachine: Client '{}' subscribed (format={}, render_enabled={}, "
            "render_every_n={}, total={})",
            connectionId,
            cwc.command.format == RenderFormat::EnumType::Basic ? "Basic" : "Debug",
            renderEnabled,
            renderEveryN,
            pImpl->subscribedClients_.size());

        Api::RenderFormatSet::Okay okay;
        okay.active_format = cwc.command.format;
        okay.message = "Subscribed to render messages";
        cwc.sendResponse(Api::RenderFormatSet::Response::okay(std::move(okay)));
        return;
    }

    // Handle RenderStreamConfigSet globally (works in any state).
    if (std::holds_alternative<Api::RenderStreamConfigSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::RenderStreamConfigSet::Cwc>(event.getVariant());
        const std::string& connectionId = cwc.command.connectionId;
        assert(!connectionId.empty() && "RenderStreamConfigSet: connectionId must be populated!");

        if (cwc.command.renderEveryN <= 0) {
            cwc.sendResponse(
                Api::RenderStreamConfigSet::Response::error(
                    ApiError{ "renderEveryN must be >= 1" }));
            return;
        }

        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsRender(connectionId)) {
            cwc.sendResponse(
                Api::RenderStreamConfigSet::Response::error(
                    ApiError{ "Client did not request render updates" }));
            return;
        }

        auto it = std::find_if(
            pImpl->subscribedClients_.begin(),
            pImpl->subscribedClients_.end(),
            [&connectionId](const SubscribedClient& c) { return c.connectionId == connectionId; });
        if (it == pImpl->subscribedClients_.end()) {
            cwc.sendResponse(
                Api::RenderStreamConfigSet::Response::error(
                    ApiError{ "Render stream not active for client" }));
            return;
        }

        it->renderEnabled = cwc.command.renderEnabled;
        it->renderEveryN = std::max(1, cwc.command.renderEveryN);

        spdlog::info(
            "StateMachine: Client '{}' render stream config set (enabled={}, every_n={})",
            connectionId,
            it->renderEnabled,
            it->renderEveryN);

        Api::RenderStreamConfigSet::Okay okay;
        okay.renderEnabled = it->renderEnabled;
        okay.renderEveryN = it->renderEveryN;
        okay.message = "Render stream config updated";
        cwc.sendResponse(Api::RenderStreamConfigSet::Response::okay(std::move(okay)));
        return;
    }

    if (std::holds_alternative<Api::UserSettingsGet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::UserSettingsGet::Cwc>(event.getVariant());
        Api::UserSettingsGet::Okay response{ .settings = pImpl->userSettings_ };
        cwc.sendResponse(Api::UserSettingsGet::Response::okay(std::move(response)));
        return;
    }

    if (std::holds_alternative<Api::UserSettingsPatch::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::UserSettingsPatch::Cwc>(event.getVariant());

        if (cwc.command.isEmpty()) {
            cwc.sendResponse(
                Api::UserSettingsPatch::Response::error(ApiError("No fields provided to patch")));
            return;
        }

        UserSettings patched = pImpl->userSettings_;
        if (cwc.command.timezoneIndex.has_value()) {
            patched.timezoneIndex = *cwc.command.timezoneIndex;
        }
        if (cwc.command.volumePercent.has_value()) {
            patched.volumePercent = *cwc.command.volumePercent;
        }
        if (cwc.command.defaultScenario.has_value()) {
            patched.defaultScenario = *cwc.command.defaultScenario;
        }
        if (cwc.command.startMenuIdleAction.has_value()) {
            patched.startMenuIdleAction = *cwc.command.startMenuIdleAction;
        }
        if (cwc.command.startMenuIdleTimeoutMs.has_value()) {
            patched.startMenuIdleTimeoutMs = *cwc.command.startMenuIdleTimeoutMs;
        }
        if (cwc.command.trainingSpec.has_value()) {
            patched.trainingSpec = *cwc.command.trainingSpec;
        }
        if (cwc.command.evolutionConfig.has_value()) {
            patched.evolutionConfig = *cwc.command.evolutionConfig;
        }
        if (cwc.command.mutationConfig.has_value()) {
            patched.mutationConfig = *cwc.command.mutationConfig;
        }
        if (cwc.command.trainingResumePolicy.has_value()) {
            patched.trainingResumePolicy = *cwc.command.trainingResumePolicy;
        }

        bool changed = false;
        std::vector<std::string> updates;
        const UserSettings sanitized = sanitizeUserSettings(
            patched, pImpl->scenarioRegistry_, pImpl->genomeRepository_, changed, updates);

        if (!persistUserSettingsToDisk(pImpl->userSettingsPath_, sanitized)) {
            cwc.sendResponse(
                Api::UserSettingsPatch::Response::error(
                    ApiError("Failed to persist user settings")));
            return;
        }

        if (changed) {
            for (const auto& update : updates) {
                LOG_WARN(State, "UserSettingsPatch: {}", update);
            }
        }

        pImpl->userSettings_ = sanitized;

        Api::UserSettingsPatch::Okay response{ .settings = pImpl->userSettings_ };
        cwc.sendResponse(Api::UserSettingsPatch::Response::okay(std::move(response)));

        const Api::UserSettingsUpdated updateEvent{ .settings = pImpl->userSettings_ };
        broadcastEventData(
            Api::UserSettingsUpdated::name(), Network::serialize_payload(updateEvent));
        return;
    }

    if (std::holds_alternative<Api::UserSettingsSet::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::UserSettingsSet::Cwc>(event.getVariant());

        bool changed = false;
        std::vector<std::string> updates;
        const UserSettings sanitized = sanitizeUserSettings(
            cwc.command.settings,
            pImpl->scenarioRegistry_,
            pImpl->genomeRepository_,
            changed,
            updates);

        if (!persistUserSettingsToDisk(pImpl->userSettingsPath_, sanitized)) {
            cwc.sendResponse(
                Api::UserSettingsSet::Response::error(ApiError("Failed to persist user settings")));
            return;
        }

        if (changed) {
            for (const auto& update : updates) {
                LOG_WARN(State, "UserSettingsSet: {}", update);
            }
        }

        pImpl->userSettings_ = sanitized;

        Api::UserSettingsSet::Okay response{ .settings = pImpl->userSettings_ };
        cwc.sendResponse(Api::UserSettingsSet::Response::okay(std::move(response)));

        const Api::UserSettingsUpdated updateEvent{ .settings = pImpl->userSettings_ };
        broadcastEventData(
            Api::UserSettingsUpdated::name(), Network::serialize_payload(updateEvent));
        return;
    }

    if (std::holds_alternative<Api::UserSettingsReset::Cwc>(event.getVariant())) {
        const auto& cwc = std::get<Api::UserSettingsReset::Cwc>(event.getVariant());

        const UserSettings defaults;
        if (!persistUserSettingsToDisk(pImpl->userSettingsPath_, defaults)) {
            cwc.sendResponse(
                Api::UserSettingsReset::Response::error(
                    ApiError("Failed to persist user settings")));
            return;
        }

        pImpl->userSettings_ = defaults;
        Api::UserSettingsReset::Okay response{ .settings = pImpl->userSettings_ };
        cwc.sendResponse(Api::UserSettingsReset::Response::okay(std::move(response)));

        const Api::UserSettingsUpdated updateEvent{ .settings = pImpl->userSettings_ };
        broadcastEventData(
            Api::UserSettingsUpdated::name(), Network::serialize_payload(updateEvent));
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
                        spdlog::warn(
                            "Server::StateMachine: State {} does not handle event {}",
                            State::getCurrentStateName(pImpl->fsmState_),
                            getEventName(Event{ evt }));

                        // If this is an API command with sendResponse, send error.
                        if constexpr (requires {
                                          evt.sendResponse(
                                              std::declval<typename std::decay_t<
                                                  decltype(evt)>::Response>());
                                      }) {
                            auto errorMsg = std::string("Command not supported in state: ")
                                + State::getCurrentStateName(pImpl->fsmState_);
                            using EventType = std::decay_t<decltype(evt)>;
                            using ResponseType = typename EventType::Response;
                            evt.sendResponse(ResponseType::error(ApiError(errorMsg)));
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

    const auto shouldSendForClient = [&data](const SubscribedClient& client) {
        if (!client.renderEnabled) {
            return false;
        }
        const int everyN = client.renderEveryN;
        if (everyN <= 1) {
            return true;
        }
        return (data.timestep % everyN) == 0;
    };

    bool hasEligibleClient = false;
    for (const auto& client : pImpl->subscribedClients_) {
        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsRender(client.connectionId)) {
            continue;
        }
        if (shouldSendForClient(client)) {
            hasEligibleClient = true;
            break;
        }
    }
    if (!hasEligibleClient) {
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
        if (!shouldSendForClient(client)) {
            continue;
        }

        RenderMessage msg =
            RenderMessageUtils::packRenderMessage(data, client.renderFormat, organism_grid);

        // Bundle with scenario metadata for transport.
        RenderMessageFull fullMsg;
        fullMsg.render_data = std::move(msg);
        fullMsg.scenario_id = scenario_id;
        fullMsg.scenario_config = scenario_config;

        Network::MessageEnvelope& envelope = pImpl->renderEnvelopeScratch_;
        envelope.id = 0;
        envelope.payload.clear();

        // Serialize RenderMessageFull into reusable envelope payload storage.
        zpp::bits::out payloadOut(envelope.payload);
        payloadOut(fullMsg).or_throw();

        pImpl->renderEnvelopeDataScratch_.clear();
        zpp::bits::out envelopeOut(pImpl->renderEnvelopeDataScratch_);
        envelopeOut(envelope).or_throw();

        auto result =
            pImpl->wsService_->sendToClient(client.connectionId, pImpl->renderEnvelopeDataScratch_);
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
    if (pImpl->eventSubscribers_.empty()) {
        return;
    }

    spdlog::debug(
        "StateMachine: Broadcasting '{}' ({} bytes) to {} clients",
        messageType,
        payload.size(),
        pImpl->eventSubscribers_.size());

    Network::MessageEnvelope envelope{
        .id = 0,
        .message_type = messageType,
        .payload = payload,
    };

    std::vector<std::byte> envelopeData = Network::serialize_envelope(envelope);

    for (const auto& connectionId : pImpl->eventSubscribers_) {
        if (pImpl->wsService_ && !pImpl->wsService_->clientWantsEvents(connectionId)) {
            continue;
        }

        auto result = pImpl->wsService_->sendToClient(connectionId, envelopeData);
        if (result.isError()) {
            spdlog::error(
                "StateMachine: Failed to send '{}' to '{}': {}",
                messageType,
                connectionId,
                result.errorValue());
        }
    }
}

} // namespace Server
} // namespace DirtSim
