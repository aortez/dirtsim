#include "core/scenarios/nes/NesGameAdapterRegistry.h"

#include "core/Assert.h"
#include "core/scenarios/nes/NesGameAdapter.h"

namespace DirtSim {

void NesGameAdapterRegistry::registerAdapter(Scenario::EnumType scenarioId, AdapterFactory factory)
{
    DIRTSIM_ASSERT(factory, "NesGameAdapterRegistry: adapter factory must be set");
    factories_[scenarioId] = std::move(factory);
}

std::unique_ptr<NesGameAdapter> NesGameAdapterRegistry::createAdapter(
    Scenario::EnumType scenarioId) const
{
    const auto it = factories_.find(scenarioId);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

NesGameAdapterRegistry NesGameAdapterRegistry::createDefault()
{
    NesGameAdapterRegistry registry;
    registry.registerAdapter(Scenario::EnumType::NesFlappyParatroopa, []() {
        return createNesFlappyParatroopaGameAdapter();
    });
    return registry;
}

} // namespace DirtSim
