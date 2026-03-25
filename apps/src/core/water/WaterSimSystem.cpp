#include "WaterSimSystem.h"

#include "MacProjectionWaterSim.h"
#include "WaterSim.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"

namespace DirtSim {
namespace {

std::unique_ptr<IWaterSim> createWaterSim(WaterSimMode mode)
{
    switch (mode) {
        case WaterSimMode::LegacyCell:
            return nullptr;
        case WaterSimMode::MacProjection:
            return std::make_unique<MacProjectionWaterSim>();
    }

    return nullptr;
}

} // namespace

WaterSimSystem::~WaterSimSystem() = default;

void WaterSimSystem::syncToSettings(
    const PhysicsSettings& settings, int worldWidth, int worldHeight)
{
    if (settings.water_sim_mode != mode_) {
        setMode(settings.water_sim_mode, worldWidth, worldHeight);
    }
    else {
        resizeIfNeeded(worldWidth, worldHeight);
    }

    if (sim_) {
        sim_->syncToSettings(settings);
    }
}

void WaterSimSystem::advanceTime(World& world, double deltaTimeSeconds)
{
    if (!sim_) {
        return;
    }

    sim_->advanceTime(world, deltaTimeSeconds);
}

void WaterSimSystem::queueGuidedWaterDrain(const GuidedWaterDrain& drain)
{
    if (!sim_) {
        return;
    }

    sim_->queueGuidedWaterDrain(drain);
}

bool WaterSimSystem::tryGetWaterActivityView(WaterActivityView& out) const
{
    if (!sim_) {
        return false;
    }

    return sim_->tryGetWaterActivityView(out);
}

bool WaterSimSystem::tryGetWaterSleepShadowStats(WaterSleepShadowStats& out) const
{
    if (!sim_) {
        return false;
    }

    return sim_->tryGetWaterSleepShadowStats(out);
}

bool WaterSimSystem::tryGetWaterVolumeView(WaterVolumeView& out) const
{
    if (!sim_) {
        return false;
    }

    return sim_->tryGetWaterVolumeView(out);
}

bool WaterSimSystem::tryGetMutableWaterVolumeView(WaterVolumeMutableView& out)
{
    if (!sim_) {
        return false;
    }

    return sim_->tryGetMutableWaterVolumeView(out);
}

void WaterSimSystem::setMode(WaterSimMode mode, int worldWidth, int worldHeight)
{
    mode_ = mode;
    sim_ = createWaterSim(mode_);

    width_ = 0;
    height_ = 0;
    resizeIfNeeded(worldWidth, worldHeight);

    SLOG_INFO("Water sim mode set to {}.", std::string(reflect::enum_name(mode_)));
}

void WaterSimSystem::resizeIfNeeded(int worldWidth, int worldHeight)
{
    if (!sim_) {
        return;
    }

    if (worldWidth == width_ && worldHeight == height_) {
        return;
    }

    width_ = worldWidth;
    height_ = worldHeight;
    sim_->resize(width_, height_);
    sim_->reset();
}

} // namespace DirtSim
