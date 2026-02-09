#pragma once

#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace DirtSim {
namespace Client {

struct FunctionalTrainingSummary {
    std::string scenario_id;
    int organism_type = 0;
    int population_size = 0;
    int max_generations = 0;
    int completed_generations = 0;
    double best_fitness = 0.0;
    double average_fitness = 0.0;
    double total_training_seconds = 0.0;
    std::string primary_brain_kind;
    std::optional<std::string> primary_brain_variant;
    int primary_population_count = 0;
    std::string training_session_id;
    int candidate_count = 0;

    nlohmann::json toJson() const;
};

struct FunctionalTestSummary {
    std::string name;
    int64_t duration_ms = 0;
    Result<std::monostate, std::string> result;
    std::optional<std::string> failure_screenshot_path;
    std::optional<FunctionalTrainingSummary> training_summary;

    nlohmann::json toJson() const;
};

class FunctionalTestRunner {
public:
    FunctionalTestSummary runCanExit(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanTrain(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanSetGenerationsAndTrain(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanPlantTreeSeed(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanLoadGenomeFromBrowser(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanOpenTrainingConfigPanel(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanUpdateUserSettings(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanResetUserSettings(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanPersistUserSettingsAcrossRestart(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanUseDefaultScenarioWhenSimRunHasNoScenario(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanApplyClockTimezoneFromUserSettings(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runCanPlaySynthKeys(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
    FunctionalTestSummary runVerifyTraining(
        const std::string& uiAddress,
        const std::string& serverAddress,
        const std::string& osManagerAddress,
        int timeoutMs);
};

} // namespace Client
} // namespace DirtSim
