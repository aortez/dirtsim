#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/ScenarioId.h"

#include <memory>
#include <optional>
#include <string>

namespace DirtSim {

class NesGameAdapterRegistry;
class NesTileTokenizer;

class NesTileTokenizerBootstrapper final {
public:
    static constexpr int DefaultBootstrapFrames = 16;

    struct Config {
        int bootstrapFrames = DefaultBootstrapFrames;
    };

    static Result<std::shared_ptr<NesTileTokenizer>, std::string> build(
        Scenario::EnumType scenarioId, const std::optional<ScenarioConfig>& scenarioConfigOverride);
    static Result<std::shared_ptr<NesTileTokenizer>, std::string> build(
        Scenario::EnumType scenarioId,
        const std::optional<ScenarioConfig>& scenarioConfigOverride,
        Config config);
    static Result<std::shared_ptr<NesTileTokenizer>, std::string> build(
        Scenario::EnumType scenarioId,
        const std::optional<ScenarioConfig>& scenarioConfigOverride,
        const NesGameAdapterRegistry& adapterRegistry);
    static Result<std::shared_ptr<NesTileTokenizer>, std::string> build(
        Scenario::EnumType scenarioId,
        const std::optional<ScenarioConfig>& scenarioConfigOverride,
        const NesGameAdapterRegistry& adapterRegistry,
        Config config);
};

} // namespace DirtSim
