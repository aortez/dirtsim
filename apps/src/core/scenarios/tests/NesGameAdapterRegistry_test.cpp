#include "core/scenarios/nes/NesGameAdapter.h"
#include "core/scenarios/nes/NesGameAdapterRegistry.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

class TestNesGameAdapter : public NesGameAdapter {
public:
    explicit TestNesGameAdapter(int* resolveCalls) : resolveCalls_(resolveCalls) {}

    uint8_t resolveControllerMask(const NesGameAdapterControllerInput& input) override
    {
        if (resolveCalls_) {
            ++(*resolveCalls_);
        }
        return input.inferredControllerMask;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        NesGameAdapterFrameOutput output;
        output.rewardDelta = static_cast<double>(input.advancedFrames);
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        DuckSensoryData sensory{};
        sensory.delta_time_seconds = input.deltaTimeSeconds;
        return sensory;
    }

private:
    int* resolveCalls_ = nullptr;
};

} // namespace

TEST(NesGameAdapterRegistryTest, DefaultRegistryRegistersFlappyAdapter)
{
    NesGameAdapterRegistry registry = NesGameAdapterRegistry::createDefault();

    std::unique_ptr<NesGameAdapter> adapter =
        registry.createAdapter(Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_NE(adapter, nullptr);
}

TEST(NesGameAdapterRegistryTest, CreateAdapterReturnsNullForUnregisteredScenario)
{
    NesGameAdapterRegistry registry = NesGameAdapterRegistry::createDefault();

    std::unique_ptr<NesGameAdapter> adapter =
        registry.createAdapter(Scenario::EnumType::TreeGermination);
    EXPECT_EQ(adapter, nullptr);
}

TEST(NesGameAdapterRegistryTest, RegisterAdapterReturnsFreshInstances)
{
    int resolveCalls = 0;
    int factoryCalls = 0;
    NesGameAdapterRegistry registry;
    registry.registerAdapter(Scenario::EnumType::Benchmark, [&resolveCalls, &factoryCalls]() {
        ++factoryCalls;
        return std::make_unique<TestNesGameAdapter>(&resolveCalls);
    });

    std::unique_ptr<NesGameAdapter> first = registry.createAdapter(Scenario::EnumType::Benchmark);
    std::unique_ptr<NesGameAdapter> second = registry.createAdapter(Scenario::EnumType::Benchmark);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get());
    EXPECT_EQ(factoryCalls, 2);

    const uint8_t resolved =
        first->resolveControllerMask(NesGameAdapterControllerInput{ .inferredControllerMask = 7 });
    EXPECT_EQ(resolved, 7u);
    EXPECT_EQ(resolveCalls, 1);
}
