#include "core/ColorNames.h"
#include "core/LightConfig.h"

#include <gtest/gtest.h>

using namespace DirtSim;

TEST(LightConfigPresetTest, FastPresetUpdatesManagedFieldsOnly)
{
    LightConfig config = getDefaultLightConfig();
    config.ambient_color = ColorNames::caveAmbient();
    config.ambient_intensity = 0.42f;
    config.sky_color = ColorNames::coolMoonlight();
    config.sky_intensity = 0.17f;
    config.sun_color = ColorNames::torchOrange();
    config.sun_intensity = 0.91f;

    applyLightModePreset(config, LightMode::Fast);

    EXPECT_EQ(config.mode, LightMode::Fast);
    EXPECT_TRUE(config.air_fast_path);
    EXPECT_EQ(config.steps_per_frame, 1);
    EXPECT_FLOAT_EQ(config.temporal_decay, 0.995f);
    EXPECT_TRUE(config.temporal_persistence);
    EXPECT_EQ(config.ambient_color, ColorNames::caveAmbient());
    EXPECT_FLOAT_EQ(config.ambient_intensity, 0.42f);
    EXPECT_EQ(config.sky_color, ColorNames::coolMoonlight());
    EXPECT_FLOAT_EQ(config.sky_intensity, 0.17f);
    EXPECT_EQ(config.sun_color, ColorNames::torchOrange());
    EXPECT_FLOAT_EQ(config.sun_intensity, 0.91f);
}

TEST(LightConfigPresetTest, PropagatedPresetUpdatesManagedFieldsOnly)
{
    LightConfig config = getDefaultLightConfig();
    config.air_fast_path = true;
    config.steps_per_frame = 4;
    config.temporal_decay = 0.75f;
    config.temporal_persistence = true;
    config.ambient_color = ColorNames::nightAmbient();
    config.ambient_intensity = 0.11f;

    applyLightModePreset(config, LightMode::Propagated);

    EXPECT_EQ(config.mode, LightMode::Propagated);
    EXPECT_FALSE(config.air_fast_path);
    EXPECT_EQ(config.steps_per_frame, 15);
    EXPECT_FLOAT_EQ(config.temporal_decay, 0.99f);
    EXPECT_FALSE(config.temporal_persistence);
    EXPECT_EQ(config.ambient_color, ColorNames::nightAmbient());
    EXPECT_FLOAT_EQ(config.ambient_intensity, 0.11f);
}

TEST(LightConfigPresetTest, ManualTuningPersistsUntilNextPresetApplication)
{
    LightConfig config = getDefaultLightConfig();

    applyLightModePreset(config, LightMode::Fast);
    config.steps_per_frame = 7;
    config.temporal_decay = 0.9f;

    EXPECT_EQ(config.mode, LightMode::Fast);
    EXPECT_EQ(config.steps_per_frame, 7);
    EXPECT_FLOAT_EQ(config.temporal_decay, 0.9f);

    applyLightModePreset(config, LightMode::Propagated);

    EXPECT_EQ(config.mode, LightMode::Propagated);
    EXPECT_EQ(config.steps_per_frame, 15);
    EXPECT_FLOAT_EQ(config.temporal_decay, 0.99f);
}
