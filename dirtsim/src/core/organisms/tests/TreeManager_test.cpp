#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class OrganismManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        world = std::make_unique<World>(10, 10);
        manager = std::make_unique<OrganismManager>();
    }

    std::unique_ptr<World> world;
    std::unique_ptr<OrganismManager> manager;
};

TEST_F(OrganismManagerTest, CreateTreeCreatesOrganism)
{
    OrganismId id = manager->createTree(*world, 5, 5);

    EXPECT_NE(id, INVALID_ORGANISM_ID);
    EXPECT_NE(manager->getTree(id), nullptr);
}
