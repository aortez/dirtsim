#include "FitnessCalculator.h"
#include "core/Assert.h"
#include "core/organisms/evolution/DuckEvaluator.h"
#include "core/organisms/evolution/GooseEvaluator.h"
#include "core/organisms/evolution/NesEvaluator.h"
#include "core/organisms/evolution/TreeEvaluator.h"

namespace DirtSim {

double computeFitnessForOrganism(const FitnessContext& context)
{
    switch (context.organismType) {
        case OrganismType::DUCK: {
            return DuckEvaluator::evaluate(context);
        }
        case OrganismType::GOOSE: {
            return GooseEvaluator::evaluate(context);
        }
        case OrganismType::NES_FLAPPY_BIRD: {
            return NesEvaluator::evaluate(context);
        }
        case OrganismType::TREE: {
            return TreeEvaluator::evaluate(context);
        }
    }

    DIRTSIM_ASSERT(false, "FitnessCalculator: Unknown OrganismType");
    return 0.0;
}

} // namespace DirtSim
