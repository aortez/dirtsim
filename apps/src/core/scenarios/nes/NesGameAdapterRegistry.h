#pragma once

#include "core/ScenarioId.h"

#include <functional>
#include <map>
#include <memory>

namespace DirtSim {

class NesGameAdapter;

/**
 * Scenario-to-adapter registry for scenario-driven NES training.
 */
class NesGameAdapterRegistry {
public:
    using AdapterFactory = std::function<std::unique_ptr<NesGameAdapter>()>;

    void registerAdapter(Scenario::EnumType scenarioId, AdapterFactory factory);
    std::unique_ptr<NesGameAdapter> createAdapter(Scenario::EnumType scenarioId) const;

    static NesGameAdapterRegistry createDefault();

private:
    std::map<Scenario::EnumType, AdapterFactory> factories_;
};

} // namespace DirtSim
