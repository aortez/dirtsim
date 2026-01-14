/**
 * @file LightManager_test.cpp
 * @brief Tests for LightManager and LightHandle RAII behavior.
 */

#include "core/LightManager.h"
#include "core/LightTypes.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class LightManagerTest : public ::testing::Test {
protected:
    LightManager manager;

    PointLight makeLight(double x, double y)
    {
        return PointLight{
            .position = Vector2d{ x, y },
            .color = 0xFFFFFFFF,
            .intensity = 1.0f,
            .radius = 10.0f,
            .attenuation = 0.1f,
        };
    }
};

TEST_F(LightManagerTest, AddLightReturnsValidId)
{
    LightId id = manager.addLight(makeLight(5.0, 5.0));

    EXPECT_NE(id, INVALID_LIGHT_ID);
    EXPECT_TRUE(manager.isValid(id));
    EXPECT_EQ(manager.count(), 1);
}

TEST_F(LightManagerTest, AddMultipleLightsReturnsUniqueIds)
{
    LightId id1 = manager.addLight(makeLight(1.0, 1.0));
    LightId id2 = manager.addLight(makeLight(2.0, 2.0));
    LightId id3 = manager.addLight(makeLight(3.0, 3.0));

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
    EXPECT_EQ(manager.count(), 3);
}

TEST_F(LightManagerTest, GetLightReturnsCorrectLight)
{
    LightId id = manager.addLight(makeLight(7.0, 8.0));

    PointLight* light = manager.getLight<PointLight>(id);
    ASSERT_NE(light, nullptr);

    EXPECT_DOUBLE_EQ(light->position.x, 7.0);
    EXPECT_DOUBLE_EQ(light->position.y, 8.0);
}

TEST_F(LightManagerTest, GetLightAllowsModification)
{
    LightId id = manager.addLight(makeLight(0.0, 0.0));

    PointLight* light = manager.getLight<PointLight>(id);
    ASSERT_NE(light, nullptr);
    light->intensity = 0.5f;

    PointLight* updated = manager.getLight<PointLight>(id);
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->intensity, 0.5f);
}

TEST_F(LightManagerTest, RemoveLightMakesIdInvalid)
{
    LightId id = manager.addLight(makeLight(5.0, 5.0));
    EXPECT_TRUE(manager.isValid(id));

    manager.removeLight(id);

    EXPECT_FALSE(manager.isValid(id));
    EXPECT_EQ(manager.count(), 0);
}

TEST_F(LightManagerTest, RemoveInvalidLightIsNoOp)
{
    manager.addLight(makeLight(1.0, 1.0));

    // Should not crash or throw.
    manager.removeLight(INVALID_LIGHT_ID);
    manager.removeLight(LightId{ 999 });

    EXPECT_EQ(manager.count(), 1);
}

TEST_F(LightManagerTest, ClearRemovesAllLights)
{
    manager.addLight(makeLight(1.0, 1.0));
    manager.addLight(makeLight(2.0, 2.0));
    manager.addLight(makeLight(3.0, 3.0));
    EXPECT_EQ(manager.count(), 3);

    manager.clear();

    EXPECT_EQ(manager.count(), 0);
}

TEST_F(LightManagerTest, ForEachLightIteratesAllLights)
{
    manager.addLight(makeLight(1.0, 0.0));
    manager.addLight(makeLight(2.0, 0.0));
    manager.addLight(makeLight(3.0, 0.0));

    double sum = 0.0;
    manager.forEachLight([&sum](LightId /*id*/, const Light& light) {
        if (const auto* point = std::get_if<PointLight>(&light.getVariant())) {
            sum += point->position.x;
        }
    });

    EXPECT_DOUBLE_EQ(sum, 6.0);
}

// ============================================================================
// LightHandle RAII Tests
// ============================================================================

TEST_F(LightManagerTest, CreateLightReturnsValidHandle)
{
    LightHandle handle = manager.createLight(makeLight(5.0, 5.0));

    EXPECT_TRUE(handle.isValid());
    EXPECT_NE(handle.id(), INVALID_LIGHT_ID);
    EXPECT_EQ(manager.count(), 1);
}

TEST_F(LightManagerTest, HandleDestructorRemovesLight)
{
    {
        LightHandle handle = manager.createLight(makeLight(5.0, 5.0));
        EXPECT_EQ(manager.count(), 1);
    }
    // Handle destroyed here.

    EXPECT_EQ(manager.count(), 0);
}

TEST_F(LightManagerTest, HandleIdAccessesLight)
{
    LightHandle handle = manager.createLight(makeLight(7.0, 8.0));

    PointLight* light = manager.getLight<PointLight>(handle.id());
    ASSERT_NE(light, nullptr);

    EXPECT_DOUBLE_EQ(light->position.x, 7.0);
    EXPECT_DOUBLE_EQ(light->position.y, 8.0);
}

TEST_F(LightManagerTest, HandleIdAllowsModification)
{
    LightHandle handle = manager.createLight(makeLight(0.0, 0.0));

    PointLight* light = manager.getLight<PointLight>(handle.id());
    ASSERT_NE(light, nullptr);
    light->intensity = 0.25f;

    PointLight* updated = manager.getLight<PointLight>(handle.id());
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->intensity, 0.25f);
}

TEST_F(LightManagerTest, HandleMoveTransfersOwnership)
{
    LightHandle handle1 = manager.createLight(makeLight(5.0, 5.0));
    LightId id = handle1.id();

    LightHandle handle2 = std::move(handle1);

    EXPECT_FALSE(handle1.isValid());
    EXPECT_TRUE(handle2.isValid());
    EXPECT_EQ(handle2.id(), id);
    EXPECT_EQ(manager.count(), 1);
}

TEST_F(LightManagerTest, HandleMoveAssignmentRemovesOldLight)
{
    LightHandle handle1 = manager.createLight(makeLight(1.0, 1.0));
    LightHandle handle2 = manager.createLight(makeLight(2.0, 2.0));
    EXPECT_EQ(manager.count(), 2);

    LightId id2 = handle2.id();
    handle1 = std::move(handle2);

    // handle1's old light should be removed, handle2's light transferred.
    EXPECT_EQ(manager.count(), 1);
    EXPECT_TRUE(handle1.isValid());
    EXPECT_EQ(handle1.id(), id2);
    EXPECT_FALSE(handle2.isValid());
}

TEST_F(LightManagerTest, HandleReleaseTransfersToManualManagement)
{
    LightHandle handle = manager.createLight(makeLight(5.0, 5.0));
    LightId id = handle.id();

    LightId released_id = handle.release();

    EXPECT_EQ(released_id, id);
    EXPECT_FALSE(handle.isValid());
    // Light should still exist - not auto-removed.
    EXPECT_TRUE(manager.isValid(id));
    EXPECT_EQ(manager.count(), 1);
}

TEST_F(LightManagerTest, DefaultHandleIsInvalid)
{
    LightHandle handle;

    EXPECT_FALSE(handle.isValid());
    EXPECT_EQ(handle.id(), INVALID_LIGHT_ID);
}
