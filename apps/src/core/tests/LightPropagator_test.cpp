#include "core/ColorNames.h"
#include "core/LightConfig.h"
#include "core/LightManager.h"
#include "core/LightPropagator.h"
#include "core/LightTypes.h"
#include "core/MaterialColor.h"
#include "core/MaterialType.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class LightPropagatorTest : public ::testing::Test {
protected:
    LightPropagator prop;
    LightConfig config;
    ::Timers timers;

    void SetUp() override
    {
        config = getDefaultLightConfig();
        // Override defaults for deterministic testing.
        config.ambient_intensity = 0.0f;
        config.ambient_color = ColorNames::black();
        config.temporal_persistence = false;
    }
};

TEST_F(LightPropagatorTest, SunlightReachesBottom)
{
    World world(10, 20);
    config.steps_per_frame = 25;

    prop.calculate(world, world.getGrid(), config, timers);

    // Bottom row should be illuminated by sun passing through air.
    const float brightness = ColorNames::brightness(world.getData().colors.at(5, 19));
    EXPECT_GT(brightness, 0.3f) << "Bottom of empty world should be lit, got " << brightness;
}

TEST_F(LightPropagatorTest, WallCreatesShadow)
{
    World world(10, 20);
    WorldData& data = world.getData();

    // Solid wall across row 5.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 5).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    config.steps_per_frame = 25;
    config.sky_intensity = 0.0f;

    prop.calculate(world, world.getGrid(), config, timers);

    // Above wall: bright.
    const float above = ColorNames::brightness(data.colors.at(5, 3));
    EXPECT_GT(above, 0.3f) << "Above wall should be bright, got " << above;

    // Below wall: dark (only scattered light).
    const float below = ColorNames::brightness(data.colors.at(5, 15));
    EXPECT_LT(below, 0.15f) << "Below wall should be dark, got " << below;
}

TEST_F(LightPropagatorTest, SkyDomeFillsLight)
{
    World world(20, 20);
    config.steps_per_frame = 25;
    config.sun_intensity = 0.0f;

    // Without sky.
    config.sky_intensity = 0.0f;
    prop.calculate(world, world.getGrid(), config, timers);
    const float without_sky = ColorNames::brightness(world.getData().colors.at(10, 10));

    // With sky.
    LightPropagator prop2;
    config.sky_intensity = 0.5f;
    prop2.calculate(world, world.getGrid(), config, timers);
    const float with_sky = ColorNames::brightness(world.getData().colors.at(10, 10));

    EXPECT_GT(with_sky, without_sky + 0.05f)
        << "Sky dome should add significant light. Without=" << without_sky << " With=" << with_sky;
}

TEST_F(LightPropagatorTest, MetalReflectsSpecularly)
{
    World world(10, 20);
    WorldData& data = world.getData();

    // Metal floor at row 15.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 15).replaceMaterial(Material::EnumType::Metal, 1.0);
    }
    world.advanceTime(0.0001);

    config.steps_per_frame = 40;
    config.sky_intensity = 0.0f;

    prop.calculate(world, world.getGrid(), config, timers);

    // Above metal should have some reflected light bouncing upward.
    const float above_metal = ColorNames::brightness(data.colors.at(5, 10));
    EXPECT_GT(above_metal, 0.05f) << "Metal reflection should illuminate area above, got "
                                  << above_metal;
}

TEST_F(LightPropagatorTest, EmissiveCellGlows)
{
    World world(20, 20);
    WorldData& data = world.getData();

    // Block sun with wall at top.
    for (int x = 0; x < 20; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    // Emissive seed in center.
    data.at(10, 10).replaceMaterial(Material::EnumType::Seed, 1.0);
    world.advanceTime(0.0001);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 15;

    prop.calculate(world, world.getGrid(), config, timers);

    // Seed cell should be lit.
    const float at_seed = ColorNames::brightness(data.colors.at(10, 10));
    EXPECT_GT(at_seed, 0.02f) << "Seed cell should glow, got " << at_seed;

    // Nearby cell should have some light.
    const float nearby = ColorNames::brightness(data.colors.at(12, 10));
    EXPECT_GT(nearby, 0.005f) << "Nearby cell should receive some glow, got " << nearby;

    // Far corner should be dark.
    const float far_corner = ColorNames::brightness(data.colors.at(19, 19));
    EXPECT_LT(far_corner, 0.01f) << "Far corner should be dark, got " << far_corner;
}

TEST_F(LightPropagatorTest, PointLightInDarkRoom)
{
    World world(20, 20);
    WorldData& data = world.getData();

    // Block sun.
    for (int x = 0; x < 20; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    // Add point light at center.
    PointLight pl;
    pl.position = { 10.0f, 10.0f };
    pl.color = ColorNames::white();
    pl.intensity = 2.0f;
    world.getLightManager().addLight(pl);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 15;

    prop.calculate(world, world.getGrid(), config, timers);

    // Center should be bright.
    const float center = ColorNames::brightness(data.colors.at(10, 10));
    EXPECT_GT(center, 0.1f) << "Point light center should be bright, got " << center;

    // Edge should be dimmer.
    const float edge = ColorNames::brightness(data.colors.at(19, 19));
    EXPECT_LT(edge, center) << "Edge should be dimmer than center";
}

TEST_F(LightPropagatorTest, MaterialTintingPreserved)
{
    World world(10, 20);
    WorldData& data = world.getData();

    // Leaf layer across row 5.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 5).replaceMaterial(Material::EnumType::Leaf, 1.0);
    }
    world.advanceTime(0.0001);

    config.steps_per_frame = 25;
    config.sky_intensity = 0.0f;
    config.sun_color = ColorNames::white();

    prop.calculate(world, world.getGrid(), config, timers);

    // Light below leaves should have green tint (green channel > red channel).
    const auto& color_below = data.colors.at(5, 10);
    EXPECT_GT(color_below.g, color_below.r * 0.8f)
        << "Light through leaves should be greenish. R=" << color_below.r << " G=" << color_below.g;
}

TEST_F(LightPropagatorTest, SpotLightDirectional)
{
    World world(30, 10);
    WorldData& data = world.getData();

    // Block sun.
    for (int x = 0; x < 30; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    // Spot light pointing east at position (5, 5).
    SpotLight sl;
    sl.position = { 5.0f, 5.0f };
    sl.color = ColorNames::white();
    sl.intensity = 2.0f;
    sl.direction = 0.0f;
    sl.arc_width = static_cast<float>(M_PI * 0.5);
    sl.focus = 0.5f;
    world.getLightManager().addLight(sl);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 20;

    prop.calculate(world, world.getGrid(), config, timers);

    // East of light should be bright.
    const float east = ColorNames::brightness(data.colors.at(15, 5));
    // West of light should be darker.
    const float west = ColorNames::brightness(data.colors.at(0, 5));

    EXPECT_GT(east, west + 0.01f) << "East should be brighter than west. East=" << east
                                  << " West=" << west;
}

TEST_F(LightPropagatorTest, SpotLightLightsOffAxisCellsWithinCone)
{
    World world(30, 12);
    WorldData& data = world.getData();

    // Block sun.
    for (int x = 0; x < 30; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    SpotLight sl;
    sl.position = { 5.0f, 6.0f };
    sl.color = ColorNames::white();
    sl.intensity = 2.0f;
    sl.radius = 12.0f;
    sl.attenuation = 0.05f;
    sl.direction = 0.0f;
    sl.arc_width = static_cast<float>(M_PI / 3.0);
    sl.focus = 0.5f;
    world.getLightManager().addLight(sl);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 10;

    prop.calculate(world, world.getGrid(), config, timers);

    const float centerline = ColorNames::brightness(data.colors.at(10, 6));
    const float inside_cone = ColorNames::brightness(data.colors.at(10, 4));
    const float outside_cone = ColorNames::brightness(data.colors.at(10, 1));

    EXPECT_GT(inside_cone, 0.03f) << "Off-axis cell inside the spotlight cone should be lit, got "
                                  << inside_cone;
    EXPECT_GT(inside_cone, outside_cone + 0.02f)
        << "Cell inside the spotlight cone should be brighter than a cell outside it. Inside="
        << inside_cone << " Outside=" << outside_cone;
    EXPECT_GT(centerline, inside_cone)
        << "Centerline should remain brighter than off-axis cells. Center=" << centerline
        << " OffAxis=" << inside_cone;
}

TEST_F(LightPropagatorTest, SpotLightIndirectSpillCanBeTunedPerLight)
{
    auto makeWorld = []() {
        auto world = std::make_unique<World>(20, 12);
        WorldData& data = world->getData();
        for (int x = 0; x < 20; ++x) {
            data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
        }
        data.at(10, 6).replaceMaterial(Material::EnumType::Metal, 1.0);
        world->advanceTime(0.0001);
        return world;
    };

    auto world_with_spill = makeWorld();
    auto world_without_spill = makeWorld();

    SpotLight with_spill;
    with_spill.position = { 5.5f, 6.5f };
    with_spill.color = ColorNames::white();
    with_spill.intensity = 2.0f;
    with_spill.radius = 10.0f;
    with_spill.attenuation = 0.05f;
    with_spill.indirect_strength = 0.25f;
    with_spill.direction = 0.0f;
    with_spill.arc_width = 0.12f;
    with_spill.focus = 0.5f;
    world_with_spill->getLightManager().addLight(with_spill);

    SpotLight without_spill = with_spill;
    without_spill.indirect_strength = 0.0f;
    world_without_spill->getLightManager().addLight(without_spill);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 10;

    LightPropagator prop_with_spill;
    LightPropagator prop_without_spill;
    prop_with_spill.calculate(*world_with_spill, world_with_spill->getGrid(), config, timers);
    prop_without_spill.calculate(
        *world_without_spill, world_without_spill->getGrid(), config, timers);

    const float hit_with_spill =
        ColorNames::brightness(world_with_spill->getData().colors.at(10, 6));
    const float neighbor_with_spill =
        ColorNames::brightness(world_with_spill->getData().colors.at(10, 5));
    const float neighbor_without_spill =
        ColorNames::brightness(world_without_spill->getData().colors.at(10, 5));

    EXPECT_GT(hit_with_spill, 0.05f) << "Impact cell should receive direct spotlight illumination";
    EXPECT_GT(neighbor_with_spill, neighbor_without_spill + 0.01f)
        << "Indirect spill should brighten neighboring cells near the impact point. With="
        << neighbor_with_spill << " Without=" << neighbor_without_spill;
}

TEST_F(LightPropagatorTest, SpotLightIndirectSpillRespectsGlobalScale)
{
    auto makeWorld = []() {
        auto world = std::make_unique<World>(20, 12);
        WorldData& data = world->getData();
        for (int x = 0; x < 20; ++x) {
            data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
        }
        data.at(10, 6).replaceMaterial(Material::EnumType::Metal, 1.0);
        world->advanceTime(0.0001);
        return world;
    };

    auto world_scaled_on = makeWorld();
    auto world_scaled_off = makeWorld();

    SpotLight light;
    light.position = { 5.5f, 6.5f };
    light.color = ColorNames::white();
    light.intensity = 2.0f;
    light.radius = 10.0f;
    light.attenuation = 0.05f;
    light.indirect_strength = 0.25f;
    light.direction = 0.0f;
    light.arc_width = 0.12f;
    light.focus = 0.5f;
    world_scaled_on->getLightManager().addLight(light);
    world_scaled_off->getLightManager().addLight(light);

    LightConfig scaled_on = config;
    scaled_on.sun_intensity = 0.0f;
    scaled_on.sky_intensity = 0.0f;
    scaled_on.steps_per_frame = 10;
    scaled_on.local_light_indirect_scale = 1.0f;

    LightConfig scaled_off = scaled_on;
    scaled_off.local_light_indirect_scale = 0.0f;

    LightPropagator prop_scaled_on;
    LightPropagator prop_scaled_off;
    prop_scaled_on.calculate(*world_scaled_on, world_scaled_on->getGrid(), scaled_on, timers);
    prop_scaled_off.calculate(*world_scaled_off, world_scaled_off->getGrid(), scaled_off, timers);

    const float neighbor_scaled_on =
        ColorNames::brightness(world_scaled_on->getData().colors.at(10, 5));
    const float neighbor_scaled_off =
        ColorNames::brightness(world_scaled_off->getData().colors.at(10, 5));

    EXPECT_GT(neighbor_scaled_on, neighbor_scaled_off + 0.01f)
        << "Global indirect scale should gate local-light spill. On=" << neighbor_scaled_on
        << " Off=" << neighbor_scaled_off;
}

TEST_F(LightPropagatorTest, LightMapStringWorks)
{
    World world(5, 5);
    config.steps_per_frame = 10;

    prop.calculate(world, world.getGrid(), config, timers);

    const std::string map = prop.lightMapString(world);
    EXPECT_FALSE(map.empty()) << "Light map string should not be empty";
    // Should contain newlines for each row.
    const auto newlines = std::count(map.begin(), map.end(), '\n');
    EXPECT_EQ(newlines, 5) << "Should have 5 rows";
}

TEST_F(LightPropagatorTest, EmissiveOverlayWorks)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Block sun.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.steps_per_frame = 10;

    // Without overlay.
    prop.resize(10, 10);
    prop.calculate(world, world.getGrid(), config, timers);
    const float without = ColorNames::brightness(data.colors.at(5, 5));

    // With overlay.
    prop.setEmissive(5, 5, ColorNames::white(), 1.0f);
    prop.calculate(world, world.getGrid(), config, timers);
    const float with = ColorNames::brightness(data.colors.at(5, 5));

    EXPECT_GT(with, without + 0.01f)
        << "Emissive overlay should add light. Without=" << without << " With=" << with;

    // Clear overlay.
    prop.clearEmissive(5, 5);
    prop.calculate(world, world.getGrid(), config, timers);
    const float after_clear = ColorNames::brightness(data.colors.at(5, 5));
    EXPECT_LT(after_clear, with - 0.005f)
        << "After clearing overlay, light should decrease. After=" << after_clear
        << " Before=" << with;
}

TEST_F(LightPropagatorTest, FlatBasicUsesLegacyMaterialColorsAndRenderOverrides)
{
    World world(4, 2);
    WorldData& data = world.getData();

    data.at(1, 0).replaceMaterial(Material::EnumType::Wall, 1.0f);
    data.at(1, 0).render_as = static_cast<int8_t>(Material::EnumType::Metal);
    data.at(2, 0).replaceMaterial(Material::EnumType::Leaf, 1.0f);
    data.at(3, 1).replaceMaterial(Material::EnumType::Water, 1.0f);
    world.advanceTime(0.0001);

    config.mode = LightMode::FlatBasic;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.sun_intensity = 0.0f;

    prop.calculate(world, world.getGrid(), config, timers);

    const LightBuffer& rawLight = prop.getRawLightBuffer();
    const auto packedLegacyColor = [](Material::EnumType material) {
        return ColorNames::toRgba(ColorNames::toRgbF(getLegacyMaterialColor(material)));
    };

    EXPECT_EQ(ColorNames::toRgba(data.colors.at(0, 0)), packedLegacyColor(Material::EnumType::Air));
    EXPECT_EQ(
        ColorNames::toRgba(data.colors.at(1, 0)), packedLegacyColor(Material::EnumType::Metal));
    EXPECT_EQ(
        ColorNames::toRgba(data.colors.at(2, 0)), packedLegacyColor(Material::EnumType::Leaf));
    EXPECT_EQ(
        ColorNames::toRgba(data.colors.at(3, 1)), packedLegacyColor(Material::EnumType::Water));

    EXPECT_EQ(rawLight.at(0, 0), packedLegacyColor(Material::EnumType::Air));
    EXPECT_EQ(rawLight.at(1, 0), packedLegacyColor(Material::EnumType::Metal));
    EXPECT_EQ(rawLight.at(2, 0), packedLegacyColor(Material::EnumType::Leaf));
    EXPECT_EQ(rawLight.at(3, 1), packedLegacyColor(Material::EnumType::Water));
}

TEST_F(LightPropagatorTest, FlatBasicIncludesEmissiveOverlay)
{
    World world(4, 4);
    WorldData& data = world.getData();

    config.mode = LightMode::FlatBasic;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.sun_intensity = 0.0f;

    prop.resize(4, 4);
    prop.calculate(world, world.getGrid(), config, timers);
    const float without = ColorNames::brightness(data.colors.at(2, 2));

    prop.setEmissive(2, 2, ColorNames::white(), 0.5f);
    prop.calculate(world, world.getGrid(), config, timers);
    const float with = ColorNames::brightness(data.colors.at(2, 2));

    EXPECT_GT(with, without + 0.1f)
        << "FlatBasic should include emissive overlay. Without=" << without << " With=" << with;
    EXPECT_GT(ColorNames::brightness(ColorNames::toRgbF(prop.getRawLightBuffer().at(2, 2))), 0.1f);
}

TEST_F(LightPropagatorTest, FlatBasicConsumesAmbientBoostForSingleFrame)
{
    World world(4, 4);
    WorldData& data = world.getData();

    config.mode = LightMode::FlatBasic;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.sun_intensity = 0.0f;

    prop.setAmbientBoost({ 0.25f, 0.25f, 0.25f });
    prop.calculate(world, world.getGrid(), config, timers);
    const float boosted = ColorNames::brightness(data.colors.at(1, 1));

    prop.calculate(world, world.getGrid(), config, timers);
    const float after = ColorNames::brightness(data.colors.at(1, 1));

    EXPECT_GT(boosted, 0.1f) << "FlatBasic should include ambient boost for the current frame";
    EXPECT_LT(after, boosted - 0.1f)
        << "Ambient boost should be consumed after one FlatBasic frame. After=" << after
        << " Boosted=" << boosted;
}

TEST_F(LightPropagatorTest, FlatBasicClearsPropagatedStateBeforeReturningToFastMode)
{
    World world(8, 8);

    config.mode = LightMode::Propagated;
    config.steps_per_frame = 20;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.4f;
    config.sun_intensity = 0.8f;

    prop.calculate(world, world.getGrid(), config, timers);
    EXPECT_GT(ColorNames::brightness(world.getData().colors.at(4, 4)), 0.1f);

    config.mode = LightMode::FlatBasic;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.sun_intensity = 0.0f;

    prop.calculate(world, world.getGrid(), config, timers);
    EXPECT_EQ(ColorNames::toRgba(world.getData().colors.at(4, 4)), ColorNames::black());

    config.mode = LightMode::Fast;
    config.air_fast_path = true;
    config.steps_per_frame = 1;
    config.temporal_decay = 0.995f;
    config.temporal_persistence = false;
    config.ambient_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.sun_intensity = 0.0f;

    prop.calculate(world, world.getGrid(), config, timers);

    EXPECT_EQ(ColorNames::toRgba(world.getData().colors.at(4, 4)), ColorNames::black());
    EXPECT_EQ(prop.getRawLightBuffer().at(4, 4), ColorNames::black());
}
