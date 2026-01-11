#include "WorldCalculatorBase.h"
#include "Cell.h"
#include "World.h"
#include "WorldData.h"

using namespace DirtSim;

const Cell& WorldCalculatorBase::getCellAt(const World& world, uint32_t x, uint32_t y)
{
    return world.getData().at(x, y);
}

bool WorldCalculatorBase::isValidCell(const World& world, int x, int y)
{
    return world.getData().inBounds(x, y);
}