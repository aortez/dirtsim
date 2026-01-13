#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/Body.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;

// Concrete organism for testing (Organism::Body is abstract).
class TestOrganism : public Organism::Body {
public:
    TestOrganism(OrganismId id) : Organism::Body(id, OrganismType::GOOSE) {}

    Vector2i getAnchorCell() const override { return Vector2i{ 0, 0 }; }
    void setAnchorCell(Vector2i /*pos*/) override {}
    void update(World& /*world*/, double /*deltaTime*/) override {}
};

class OrganismCollisionTest : public ::testing::Test {
protected:
    std::unique_ptr<World> createTestWorld(uint32_t width = 10, uint32_t height = 10)
    {
        auto world = std::make_unique<World>(width, height);

        // Add floor at bottom row.
        for (uint32_t x = 0; x < width; ++x) {
            world->getData().at(x, height - 1).replaceMaterial(Material::EnumType::Wall, 1.0);
        }

        return world;
    }
};

TEST_F(OrganismCollisionTest, NoCollisionWithEmptySpace)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_FALSE(info.blocked);
    EXPECT_TRUE(info.blocked_cells.empty());
}

TEST_F(OrganismCollisionTest, DetectsWallCollision)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    // Add a wall at (5, 5).
    world->getData().at(5, 5).replaceMaterial(Material::EnumType::Wall, 1.0);

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_EQ(info.blocked_cells.size(), 1u);
    EXPECT_EQ(info.blocked_cells[0], Vector2i(5, 5));
}

TEST_F(OrganismCollisionTest, DetectsFloorCollision)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    // Bottom row (y=9) is WALL floor.
    std::vector<Vector2i> target_cells = { { 5, 9 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_EQ(info.blocked_cells.size(), 1u);
}

TEST_F(OrganismCollisionTest, DetectsOutOfBoundsLeft)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    std::vector<Vector2i> target_cells = { { -1, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_GT(info.contact_normal.x, 0.0); // Normal points right (inward).
}

TEST_F(OrganismCollisionTest, DetectsOutOfBoundsRight)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    std::vector<Vector2i> target_cells = { { 10, 5 } }; // World is 10 wide, so x=10 is out.
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_LT(info.contact_normal.x, 0.0); // Normal points left (inward).
}

TEST_F(OrganismCollisionTest, DetectsOutOfBoundsBottom)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    std::vector<Vector2i> target_cells = { { 5, 10 } }; // World is 10 tall, so y=10 is out.
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_LT(info.contact_normal.y, 0.0); // Normal points up (inward).
}

TEST_F(OrganismCollisionTest, DetectsOtherOrganismCollision)
{
    auto world = createTestWorld();

    // Create an obstacle organism at (5, 5).
    OrganismId obstacle_id = world->getOrganismManager().createGoose(*world, 5, 5);

    // Our test organism trying to move into (5, 5).
    TestOrganism org(OrganismId{ 999 }); // Different ID.

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_EQ(info.blocked_cells.size(), 1u);
    (void)obstacle_id;
}

TEST_F(OrganismCollisionTest, NoCollisionWithOwnCells)
{
    auto world = createTestWorld();

    // Create a goose at (5, 5).
    OrganismId goose_id = world->getOrganismManager().createGoose(*world, 5, 5);

    // The goose checking its own position should not collide.
    TestOrganism org(goose_id); // Same ID as the goose.

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_FALSE(info.blocked);
}

TEST_F(OrganismCollisionTest, DetectsDenseDirtCollision)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    // Place dense dirt at (5, 5).
    world->getData().at(5, 5).replaceMaterial(Material::EnumType::Dirt, 0.9);

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
}

TEST_F(OrganismCollisionTest, NoCollisionWithSparseDirt)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    // Place sparse dirt at (5, 5) - below threshold.
    world->getData().at(5, 5).replaceMaterial(Material::EnumType::Dirt, 0.5);

    std::vector<Vector2i> target_cells = { { 5, 5 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_FALSE(info.blocked);
}

TEST_F(OrganismCollisionTest, DetectsMultipleCellCollision)
{
    auto world = createTestWorld();
    TestOrganism org(OrganismId{ 1 });

    // Test a 2-cell organism where one cell hits the floor.
    // Cell at (5, 5) is clear, cell at (5, 9) hits wall floor.
    std::vector<Vector2i> target_cells = { { 5, 5 }, { 5, 9 } };
    CollisionInfo info = org.detectCollisions(target_cells, *world);

    EXPECT_TRUE(info.blocked);
    EXPECT_EQ(info.blocked_cells.size(), 1u);
    EXPECT_EQ(info.blocked_cells[0], Vector2i(5, 9));
}
